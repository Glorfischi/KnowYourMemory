#include <iostream>       // std::cout
#include <infiniband/verbs.h>
#include <chrono>
#include <math.h>
#include <cassert>
#include "cxxopts.hpp"

#define MIN(a,b) (((a)<(b))?(a):(b))
const uint32_t UD_HEADER = 40;


struct ibv_qp  * create_rc_qp(struct ibv_cq *cq,struct ibv_pd *pd, uint32_t max_recv_size, uint8_t ibport, struct ibv_srq *srq){
 return NULL;
}

struct ibv_qp  * create_ud_qp(struct ibv_cq *cq,struct ibv_pd *pd, uint32_t max_recv_size, uint8_t ibport, struct ibv_srq *srq){
      struct ibv_qp_init_attr init_attr;
      memset(&init_attr,0,sizeof init_attr);
      init_attr.send_cq = cq;  // use a single CQ for send and recv
      init_attr.recv_cq = cq;  // see above 
      init_attr.srq = srq;
      init_attr.cap.max_send_wr  = 0; // the maximum number of send requests can be submitted to the QP at the same time. Polling CQ helps to remove requests from the device.
      init_attr.cap.max_recv_wr  = max_recv_size; // the maximum number of receives we can post to the QP.  It means no more than max_recv_size ibv_post_recv at a time
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

      return qp;
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
      ("CQ", "use experimental CQ with single threaded flag")
      ("RC", "use reliable connection (IS NOT IMPLEMENTED)")
      ("SRQ", "use rshared receive queue)")
      ("g,gid-idx", "local port gid index", cxxopts::value<uint32_t>()->default_value("1"), "N") // default value =1 for RoCE v2
      ("recv-size", "The IB maximum receive size", cxxopts::value<uint32_t>()->default_value(std::to_string(256)), "N") // this is the size of the queue request on the device
      ("recv-batch-size", "Batching of receive requests", cxxopts::value<uint32_t>()->default_value(std::to_string(16)), "N")
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
    uint32_t gididx = allparams["gid-idx"].as<uint32_t>();
    uint32_t max_recv_size = allparams["recv-size"].as<uint32_t>(); // Please. see where it is usedv ? what is this? how many receive requests I can send to the device
    uint32_t batch_post_recv = allparams["recv-batch-size"].as<uint32_t>(); // Please. see where it is used

    if(batch_post_recv > max_recv_size){
      perror("receive batch size MUST be larger or equal to max recv size");
      return 1;
    }
     
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
 
// find devices
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
 
    struct ibv_cq_init_attr_ex attr_ex = {
      .cqe = 4096,
      .cq_context = NULL,
      .channel = NULL,
      .comp_vector = 0,
      .wc_flags = IBV_WC_STANDARD_FLAGS,
      .comp_mask = 0,
      .flags = IBV_CREATE_CQ_ATTR_SINGLE_THREADED
    };
 
    struct ibv_cq   * cq = allparams.count("CQ") ? ibv_cq_ex_to_cq(ibv_create_cq_ex(context, &attr_ex)) : ibv_create_cq(context, 4096, NULL, NULL, 0); // max_recv_size*2 is the size of the queue. It is a hard bound. Overflows must be avoided.
    if(allparams.count("CQ")){
      printf("Using experimental CQ\n");
    }
    if (!cq) {
      fprintf(stderr, "Couldn't create CQ\n");
      return 1;
    }

    // now we can create the connection.
    struct ibv_qp *qp = NULL;
    struct ibv_srq *srq = NULL; 

    if (allparams.count("SRQ")){  
      struct ibv_srq_init_attr srq_init_attr;
      memset(&srq_init_attr,0,sizeof(srq_init_attr));
      srq_init_attr.attr.max_wr = max_recv_size;
      srq_init_attr.attr.max_sge = 1;
      srq = ibv_create_srq(pd, &srq_init_attr);
    }

    if (allparams.count("RC")){
      qp = create_rc_qp(cq,pd,max_recv_size,ibport,srq);
    } else {
      qp = create_ud_qp(cq,pd,max_recv_size,ibport,srq);
    }
    if(qp == NULL){
      return 1;
    }

    {
      union ibv_gid   gid; // address of the destination.
      printf("Using gid-idx: %u \n",gididx);
      if(ibv_query_gid(context, ibport, gididx, &gid)){
          fprintf(stderr, "Failed to query GID\n");
          return 1;
      }
      printf("QPN: %u\n",qp->qp_num);
      printf("Gid1: %llu\nGid2: %llu\n",gid.global.subnet_prefix, gid.global.interface_id);
    }


    int size = 64; // should be at least UD_HEADER

 
    // create memory to send the data from. 
    // Note, we need UD_HEADER extra bytes for UD rooting info. 
    char *buf = (char*)aligned_alloc(4096,size*max_recv_size);  // it is allocated aligned )
    if (!buf) {
      fprintf(stderr, "Couldn't allocate memory\n");
      return 1;
    }
    
    struct ibv_mr *mr = ibv_reg_mr(pd, buf, size*max_recv_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE); 
    if (!mr) {
      fprintf(stderr, "Couldn't register MR\n");
      return 1;
    }

  struct ibv_sge* sges = (struct ibv_sge *)malloc(max_recv_size*sizeof(struct ibv_sge));
  struct ibv_recv_wr * wrs = (struct ibv_recv_wr *)malloc(max_recv_size*sizeof(struct ibv_recv_wr));
 
  for(uint32_t i =0; i<max_recv_size; i++){
    sges[i].addr = (uint64_t)(void*)(buf + size*i);  // The address of the buffer to read from (or write to)  
    sges[i].length = size; // The length of the buffer in bytes, i.e. size of the data to receive (or to send)
    sges[i].lkey = mr->lkey; // The Local key of the Memory Region that this memory buffer was registered with

    wrs[i].wr_id      = i;
    wrs[i].sg_list    = &sges[i]; 
    wrs[i].num_sge    = 1; 
    wrs[i].next       = &wrs[i+1]; // chain to the next
  }
  wrs[max_recv_size-1].next  = NULL; // the last must be null


  // submit as many buffers we can for receive
  for(uint32_t i = 0; i < max_recv_size; i++){
    // let's submit the request
 
    struct ibv_recv_wr wr;
    wr.wr_id      = 0; 
    wr.sg_list    = &sges[i];
    wr.num_sge    = 1; 
    wr.next       = NULL; 

    struct ibv_recv_wr *bad_wr;
    int ret = srq ? ibv_post_srq_recv(srq, &wr, &bad_wr) : ibv_post_recv(qp, &wr, &bad_wr);
    if (ret) {
      fprintf(stderr, "Couldn't post receive.\n");
      return 1;
    }
  }
  

  struct ibv_wc wc[1024];
  uint32_t can_post = 0; // how many we can post receives
  uint32_t recv = 0; // number of messages received

  const uint32_t N = 1024;
  std::vector<uint32_t> data(N,0);
  std::vector<uint64_t> times(N,0);
 
  for(int post_recv=batch_post_recv; post_recv<=max_recv_size/2; post_recv*=2){
    if(post_recv > 1) wrs[post_recv/2-1].next = &wrs[post_recv/2]; // fix prev null
    wrs[post_recv-1].next  = NULL; // the last must be null
    float last_mean = 0.0;

    for(int poll_size=1; poll_size<=1024; poll_size*=2){
      printf("Test. Post size: %u Poll size: %u.\n", post_recv,poll_size);
      uint32_t measurements = 0; 
      auto t1 = std::chrono::high_resolution_clock::now();
      while(true){
        int ne = ibv_poll_cq(cq, poll_size, wc); 
        if(ne<0){
          fprintf(stderr, "Error on ibv_poll_cq\n");
          return 1; 
        } else {
          recv+=ne;
          can_post+=ne;
        }

        int post_loop = can_post/post_recv;
        can_post %= post_recv;
        if(srq){
          for(int i=0; i< post_loop; i++){
            struct ibv_recv_wr *bad_wr;
            int ret =  ibv_post_srq_recv(srq, wrs, &bad_wr); // post all requests. once I received all, i push once the request....
            if (ret) {
              fprintf(stderr, "Couldn't post receive.\n");
              return 1;
            }
          }
        } else {
          for(int i=0; i< post_loop; i++){
            struct ibv_recv_wr *bad_wr;
            int ret = ibv_post_recv(qp, wrs, &bad_wr); // post all requests. once I received all, i push once the request....
            if (ret) {
              fprintf(stderr, "Couldn't post receive.\n");
              return 1;
            }
          }
        }


        if(recv>(1024*16)){
          data[measurements] = recv;
          auto t2 = std::chrono::high_resolution_clock::now();
          times[measurements] = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
          t1 = t2;
          recv = 0;
          measurements++;
          if(measurements == N){
            break;
          }
        }
      }

      float mean = 0.0;
      float mean2 = 0.0;
      for(uint64_t i = 0; i<data.size(); i++){
        float t = (data[i]*1000.0)/times[i];
        mean += t; 
        mean2 += t*t; 
     //   printf("%.2f ",t);
      }

      mean /= data.size();
      mean2 /= data.size();
      printf("mean: %.2f std: %.2f Mreq/sec\n",mean, sqrtf(mean2 - mean*mean));
      if(last_mean < mean){
        last_mean = mean; 
      } else {
        break;
      }
    }
  }


  const uint32_t NN = 256*1024;
  times.resize(NN);

  printf("\n\n\nTest2\n");

  int poll_size = 256;
  for(int post_recv=batch_post_recv; post_recv<=max_recv_size/2; post_recv*=2){
      if(post_recv > 1) wrs[post_recv/2-1].next = &wrs[post_recv/2]; // fix prev null
      wrs[post_recv-1].next  = NULL; // the last must be null
      printf("Test. Post size: %u \n", post_recv);
      assert(max_recv_size > 128/post_recv && "Increase max_recv_size"); 
 
      uint32_t measurements=0;
      while(true){
        int ne = ibv_poll_cq(cq, poll_size, wc); 
        if(ne<0){
          fprintf(stderr, "Error on ibv_poll_cq\n");
          return 1; 
        } else {
          recv+=ne;
          can_post+=ne;
        }
        int post_loop = can_post/post_recv;
        if(post_loop > 128/post_recv){ // I want at least 128 posts to measure time with smaller error
          can_post %= post_recv;
          auto t1 = std::chrono::high_resolution_clock::now();
          if(srq){
            for(int i=0; i < post_loop; i++){
              struct ibv_recv_wr *bad_wr;
              int ret = ibv_post_srq_recv(srq, wrs, &bad_wr); // post all requests. once I received all, i push once the request....
              if (ret) {
                fprintf(stderr, "Couldn't post receive.\n");
                return 1;
              }
            }
          } else {
            for(int i=0; i < post_loop; i++){
              struct ibv_recv_wr *bad_wr;
              int ret = ibv_post_recv(qp, wrs, &bad_wr); // post all requests. once I received all, i push once the request....
              if (ret) {
                fprintf(stderr, "Couldn't post receive.\n");
                return 1;
              }
            }
          }

          auto t2 = std::chrono::high_resolution_clock::now();
          times[measurements] = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count()/post_loop;
          measurements++;
          if(measurements == NN){
            break;
          }
        }
      }

      float mean = 0.0;
      float mean2 = 0.0;

      for(uint64_t i = 0; i<times.size(); i++){
        float t = ((float)times[i])/(post_recv);
        mean += t; 
        mean2 += t*t; 
      }

      mean /= times.size();
      mean2 /= times.size();
      printf("mean: %.2f std: %.2f Nanosec/req\n",mean, sqrtf(mean2 - mean*mean));
    }

}
 
 
 
