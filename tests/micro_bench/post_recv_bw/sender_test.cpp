#include <iostream>       // std::cout
#include <infiniband/verbs.h>
#include <thread>
#include "cxxopts.hpp"

const uint32_t UD_HEADER = 40;

struct ibv_qp  * create_rc_qp(struct ibv_cq *cq,struct ibv_pd *pd, uint32_t max_outstanding_sends, uint8_t ibport){
  return NULL;
}

struct ibv_qp  * create_ud_qp(struct ibv_cq *cq,struct ibv_pd *pd, uint32_t max_outstanding_sends, uint8_t ibport){
// define QP initial attributes, needed for the next step when creating the QP (https://linux.die.net/man/3/ibv_create_qp)
      struct ibv_qp_init_attr init_attr;
      memset(&init_attr,0,sizeof init_attr);
      init_attr.send_cq = cq;  // use a single CQ for send and recv
      init_attr.recv_cq = cq;  // see above 
      init_attr.cap.max_send_wr  = max_outstanding_sends; // the maximum number of send requests can be submitted to the QP at the same time. Polling CQ helps to remove requests from the device.
      init_attr.cap.max_recv_wr  = 0; // the maximum number of receives we can post to the QP. I use 0 to show that the connection will not receive anything.  
      init_attr.cap.max_send_sge = 1; // is not interesting for you. Must be 1 for you.
      init_attr.cap.max_recv_sge = 1; // is not interesting for you. Must be 1 for you.
      init_attr.qp_type = IBV_QPT_UD;
      

      struct ibv_qp *  qp = ibv_create_qp(pd, &init_attr); // NOte that we use PD here! it means that QP will belong to this PD.
      if (!qp)  {
        fprintf(stderr, "Couldn't create QP\n");
        return NULL;
      }

      {
        // move QP to INIT state
        struct ibv_qp_attr attr;
        memset(&attr,0,sizeof attr);
        attr.qp_state        = IBV_QPS_INIT;
        attr.pkey_index      = 0; // for you always 0
        attr.port_num        = ibport;
        attr.qkey            = 0x11111111; // can be any constant
 
        if (ibv_modify_qp(qp, &attr,
              IBV_QP_STATE              |
              IBV_QP_PKEY_INDEX         |
              IBV_QP_PORT               |
              IBV_QP_QKEY)) {
          fprintf(stderr, "Failed to modify QP to INIT\n");
          return NULL;
        }
      }

      {
        struct ibv_qp_attr attr;
        memset(&attr,0,sizeof attr);
        attr.qp_state   = IBV_QPS_RTR;      

        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
          fprintf(stderr, "Failed to modify QP to RTR\n");
          return NULL;
        }

      }
      
 
      uint32_t my_psn = 0; // first message will have PSN (packet serial number) equal to 0; It will help to debug. Note PSN is 24 bits
   
  // move QP to RTS state
      {
        struct ibv_qp_attr attr;
        memset(&attr,0,sizeof attr);
        attr.qp_state     = IBV_QPS_RTS;
        attr.sq_psn     = my_psn;

        if (ibv_modify_qp(qp, &attr,
              IBV_QP_STATE              |
              IBV_QP_SQ_PSN)) {
          fprintf(stderr, "Failed to modify QP to RTS\n");
          return NULL;
        }
      }

      return qp;
}

void workload_worker(uint64_t meesages_to_send,uint32_t max_outstanding_sends, uint32_t batch_send, struct ibv_qp *qp, struct ibv_send_wr *wrs ){
  uint64_t sent = 0;
  uint32_t can_send = max_outstanding_sends; 
  struct ibv_wc wc[16]; // what is this? why 16?

// while we have messages to send
  while (sent < meesages_to_send){
    // we can only send if the device has free send slots, and we have free send buffers
    if(can_send >= batch_send){
      can_send-=batch_send;
 
      struct ibv_send_wr *bad_wr;
      int ret = ibv_post_send(qp, wrs, &bad_wr);
      if (ret) {
        fprintf(stderr, "Couldn't post request\n");
        return;
      }
    }

    int ne = ibv_poll_cq(qp->send_cq, 16, wc); // 16 is the length of the array wc. it means we will get at most 16 events at a time // what is the wc? why 16?
    if(ne<0){
      fprintf(stderr, "Error on ibv_poll_cq\n");
      return;
    }
    // we loop the completions
    for(int i = 0; i < ne; i++){
      sent+=batch_send;
      can_send+=batch_send;
    }
  }

}



// ParseResult class: used for the parsing of line arguments
cxxopts::ParseResult
parse(int argc, char* argv[])
{
    cxxopts::Options options(argv[0], "Sender of UD test");
    options
      .positional_help("[optional args]")
      .show_positional_help();

  try
  {
 
    options.add_options()
      ("i,ib-port", "IB port", cxxopts::value<uint8_t>()->default_value("1"), "N")
      ("ib-dev", "device", cxxopts::value<std::string>(), "DEVNAME")
      ("t,threads", "number", cxxopts::value<uint32_t>()->default_value(std::to_string(1)), "threads")
      ("RC", "use reliable connection (IS NOT IMPLEMENTED)")
      ("qpn", "Destination QPN", cxxopts::value<uint32_t>(), "N")
      ("gid1", "First part of the dest. GID", cxxopts::value<uint64_t>(), "N")
      ("gid2", "Second part of the dest. GID", cxxopts::value<uint64_t>(), "N")
      ("g,gid-idx", "local port gid index for sending. note that 0-Roce1-IP6, 1-Roce2-IP6, 2-Roce1-IP4, 3-Roce2-IP4", cxxopts::value<uint32_t>()->default_value("1"), "N")
      ("send-size", "The IB maximum send size", cxxopts::value<uint32_t>()->default_value(std::to_string(512)), "N") // this is the size of the queue request on the device
      ("batch-send", "The IB maximum send size", cxxopts::value<uint32_t>()->default_value(std::to_string(32)), "N") // this is the size of the queue request on the device
      ("num-messages", "The number of messages to send", cxxopts::value<uint64_t>()->default_value(std::to_string(1024*1024)), "N")
      ("help", "Print help")
     ;

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
      std::cout << options.help({""}) << std::endl;
      exit(0);
    }
 
    return result;

  }
  catch (const cxxopts::OptionException& e)
  {
    std::cout << "error parsing options: " << e.what() << std::endl;
    std::cout << options.help({""}) << std::endl;
    exit(1);
  }
}

int main(int argc, char* argv[]){
// parse the arguments and creates a dictionary which maps arguments to values
    auto allparams = parse(argc,argv);

    uint32_t threads = allparams["threads"].as<uint32_t>();  
    uint32_t batch_send = allparams["batch-send"].as<uint32_t>(); // Please. see where it is used
    uint32_t max_outstanding_sends = allparams["send-size"].as<uint32_t>(); // Please. see where it is used
    uint64_t meesages_to_send = allparams["num-messages"].as<uint64_t>(); // Please. see where it is used
  
// get the list of IBV devices
// https://linux.die.net/man/3/ibv_get_device_list
// returns a NULL-terminated array of RDMA devices currently available.
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
      perror("Failed to get IB devices list");
      return 1;
    }

    struct ibv_device *ib_dev = NULL;
    uint8_t ibport = allparams["ib-port"].as<uint8_t>(); // get the port number
 
// look if we find the device
    if(allparams.count("ib-dev")){
      const char *ib_devname = allparams["ib-dev"].as<std::string>().c_str();
      for (int i = 0; dev_list[i]; ++i){
        if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname)){
          ib_dev = dev_list[i];
        }
      }
      
      if (!ib_dev) {
        fprintf(stderr, "IB device %s not found\n", ib_devname);
        return 1;
      }
    } else {
      ib_dev = dev_list[0];
      if (!ib_dev) {
        fprintf(stderr, "No IB devices found\n");
        return 1;
      }
    }
 
    struct ibv_context  *context = ibv_open_device(ib_dev);
    if (!context) {
      fprintf(stderr, "Couldn't get context for %s\n", ibv_get_device_name(ib_dev));
      return 1;
    }

  
    struct ibv_pd *pd = ibv_alloc_pd(context);
    if (!pd) {
      fprintf(stderr, "Couldn't allocate PD\n");
      return 1;
    }


    // now we can create the connection.
    std::vector<struct ibv_qp*> qps(threads,NULL); 
    for(uint32_t i = 0; i<threads; i++){
      struct ibv_cq   *cq = ibv_create_cq(context, max_outstanding_sends*2, NULL, NULL, 0); // 16 is the size of the queue. It is a hard bound. Overflows must be avoided.
      if (!cq) {
        fprintf(stderr, "Couldn't create CQ\n");
        return 1;
      }

      struct ibv_qp   *qp = NULL;
      if (allparams.count("RC")){
        qp = create_rc_qp(cq,pd,max_outstanding_sends,ibport);
      } else {
        qp = create_ud_qp(cq,pd,max_outstanding_sends,ibport);
      }
      if(qp == NULL){
        return 1;
      }
      qps[i] = qp;
    }

 
 
    struct ibv_ah *ah = NULL; // it is the address handler we need to send data. It will contain info about the path to destination.

    uint32_t destqpn = 0; 

// get as input the QPN of the receiver
    if(allparams.count("qpn")){
      destqpn = allparams["qpn"].as<uint32_t>();
    } else {
      std::cout << "I need destination qpn: \n";
      std::cin >> destqpn;
    }


    {
      struct ibv_ah_attr ah_attr;
      memset(&ah_attr,0,sizeof ah_attr);
      ah_attr.is_global = 1; // must be 1 for roce
      ah_attr.dlid = 0;      // must be 0 for roce
      ah_attr.sl  = 0; // it is a service level. I am not sure what it gives for us, but can be 0.
      ah_attr.src_path_bits = 0; // I do not know. what is it. must be zero
      ah_attr.port_num      = ibport; 
      ah_attr.grh.hop_limit = 1; 

// get as input the GID of the receiver
      union ibv_gid dgid; // address of the destination.

      if(allparams.count("gid1")){
        dgid.global.subnet_prefix = allparams["gid1"].as<uint64_t>();
      } else {
        std::cout << "I need the first part of GID: \n";
        std::cin >> dgid.global.subnet_prefix;
      }

      if(allparams.count("gid2")){
        dgid.global.interface_id = allparams["gid2"].as<uint64_t>();
      } else {
        std::cout << "I need the second part of GID: \n";
        std::cin >> dgid.global.interface_id;
      }

      // address of the remote side
      ah_attr.grh.dgid = dgid;
      ah_attr.grh.sgid_index = allparams["gid-idx"].as<uint32_t>(); //  it sets ip and roce versions

 
      ah = ibv_create_ah(pd, &ah_attr);
      if (!ah) {
        fprintf(stderr, "Failed to create AH\n");
        return 1;
      }
  };




  struct ibv_send_wr * wrs = (struct ibv_send_wr *)malloc(batch_send*sizeof(struct ibv_send_wr));
 
  for(uint32_t i =0; i<batch_send; i++){
    wrs[i].wr_id      = 0;  // I will fill it in later (64 bits which will be in the completion event. A 64 bits value associated with this WR. If a Work Completion will be generated when this Work Request ends, it will contain this value)
    wrs[i].sg_list    = NULL; // Scatter/Gather array. It specifies the buffers where data will be written in
    wrs[i].num_sge    = 0; // Size of the sg_list array. This number can be less or equal to the number of scatter/gather entries that the Queue Pair was created to support in the Receive Queue
    wrs[i].next       = &wrs[i+1]; // chain to the next; // we can batch requests. So it can point to the next send request. Pointer to the next WR in the linked list.
    wrs[i].opcode     = IBV_WR_SEND; // The operation that this WR will perform. This value controls the way that data will be sent, the direction of the data flow and the used attributes in the WR
    wrs[i].send_flags = 0 ; 

    wrs[i].wr.ud.ah   = ah; // path to destination. Address handle (AH) that describes how to send the packet. This AH must be valid until any posted Work Requests that uses it isn't considered outstanding anymore. Relevant only for UD QP 
    wrs[i].wr.ud.remote_qpn  = destqpn; // connection ID of the dest. QP number of the destination QP.
    wrs[i].wr.ud.remote_qkey = 0x11111111; // sanity check

  }
  wrs[batch_send-1].send_flags = IBV_SEND_SIGNALED;
  wrs[batch_send-1].next  = NULL; // the last must be null

  
  std::vector<std::thread> workers;
  for(int i = 0; i < (int)threads; i++){
      workers.push_back(std::thread(workload_worker, meesages_to_send, max_outstanding_sends,  batch_send,  qps[i], wrs));
  }    
 
  for (auto& th : workers) th.join();



 
}
 
