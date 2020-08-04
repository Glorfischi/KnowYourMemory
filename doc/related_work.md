## Towards Zero Copy Dataflows using RDMA

In this paper they introduce a prototype on top of tensorflow that replaces the default gRPC transport with an RDMA based transport. 

They introduce a drop-in replacement for tensorflows memory allocator which returns memory regions that re registered for RDMA usage.

They then use RDMA reads with invalidate, to transfer the buffer. I suspect they use invalidate so that the sender does not have
to deregister the MR after use?


*Bairen Yi, Jiacheng Xia, Li Chen, and Kai Chen. 2017. Towards Zero Copy Dataflows using RDMA. In Proceedings of the SIGCOMM Posters and Demos (SIGCOMM Posters and Demos ’17). Association for Computing Machinery, New York, NY, USA, 28–30. DOI:https://doi.org/10.1145/3123878.3131975*


## Rethinking Message Brokers on RDMA and NVM

They design a message broker, conceptually similar to Apache Kafka. The main difference is that the partitions are directly accessed using RDMA read and writes. 

The partition logs are stored on NVM drives, which are directly byte addressable and can be accessed using RDMA. A producer or client first communicates with the broker to get the location of the partition and then will then directly access it on the storage node.

The design was not implemented and it is unclear what performance improvements could be achieved.

*Hendrik Makait. 2020. Rethinking Message Brokers on RDMA and NVM. In Proceedings of the 2020 ACM SIGMOD International Conference on Management of Data (SIGMOD ’20). Association for Computing Machinery, New York, NY, USA, 2833–2835. DOI:https://doi.org/10.1145/3318464.3384403*

## RFP: When RPC is Faster than Server-Bypass with RDMA

This paper introduces a RDMA connection paradigm called *Remote Fetching Paradigm(RFP)* as well as outlines an interesting observation about RDMAs asymmetric performance characteristics.

Measurement have shown that issuing a RDMA out-bound operation (RDMA read / write) is up to 5 times slower than handling in-bound operations (i.e. someone reading or writing to your memory) for messages below 2 KB. This makes sense as for in-bound operations the CPU an OS is not involved and for out-bound operations it needs to issue it. For larger message sizes, the bandwidth becomes the bottleneck.

Based on this observation they designed RFP so that the server will (usually) only have to handle in-bound RDMA operations while keeping the traditional and well understood RPC interface. For each connection the server will setup one (or more?) request and response buffer. The client will then use RDMA write to write the request to the request buffer and will poll the response buffer using RMDA read. The server will process the request and write the response into the corresponding response buffer.

I have some questions on their RFP implementation and how this will work for multithreaded workloads which was not explained in depth. The server side could be handled multithreaded quite easily by multiple threads polling the set of receive buffers. This would however probably need some kind of mutex. Client side it seems we need a connection per thread, as the request buffer setup seems inherently sequential. The limitation of one single buffer also prevents the client from buffering multiple requests and needs to wait for a response for each request. Some kind of ringbuffer might enable this.


*Maomeng Su, Mingxing Zhang, Kang Chen, Zhenyu Guo, and Yongwei Wu. 2017. RFP: When RPC is Faster than Server-Bypass with RDMA. In Proceedings of the Twelfth European Conference on Computer Systems (EuroSys ’17). Association for Computing Machinery, New York, NY, USA, 1–15. DOI:https://doi.org/10.1145/3064176.3064189*


## Using RDMA efficiently for key-value services

This paper presents *HERD*, another key-value store using RDMA verbs. They makes some unconventional design decisions based on measurements.

* *Writes are faster then Reads*. For one writes have lower latency than reads, as long as they are unsignaled (do not generate a completion event) and small (up to 64 bytes) the latency of WRITEs is about half that of READs. READs also achieve higher inbound throughput. This is explained as writes require less state maintenance at the receivers RNIC as WRITEs do not expect responses. 

* *UD messages scale better* Outbound WRITEs (or any outbound verbs for RC/UC connections for that matter) scale poorly. This is caused by the limited amount of RNIC on-chip memory. This means if we have a lot of active QPs we will cause a lot of cache misses, severely degrading performance. Inbound WRITEs however do scale well, because very little state needs to maintained at the inbound RNIC, so it can support a much larger number of active QPs

* *We need multiple READs* most implementations of key-value stores using RDMA use READs to perform GET requests. This however usually requires multiple READs to find and access the correct value. This causes multiple round trips. They argue that sending requests and handling them through the servers CPU will result in lower latency.

With these observations in mind the paper came up with the following design for HERD. Let `N_c` be the number of clients, `N_s`be the number of server cores, and `W` be the maximum number of outstanding requests per client.

* *Request* Clients write their request to a region of memory on the server. The memory region is split into 1 KB slots, the maximum size of a HERD request. Each server process owns a chunk of the memory, which is further split into per-client chunks which contain `W` slots. The server and clients agree on this memory setup on initialization. Each server process will round robin over its per-client chunks to notice new incoming requests. This means we only need `N_c` QPs and server processes do not create any connections for receiving requests.

* *Response* To respond to requests HERD uses SENDs over UD. That means each server process only needs on UD QP to respond to requests. This avoids outbound WRITES, which do not scale well.

**Questions** The request design looks rather fixed and it seems like we would need to know the exact number of clients on start which might not be feasible in practice. It would be interesting to extend this.



*Anuj Kalia, Michael Kaminsky, and David G. Andersen. 2014. Using RDMA efficiently for key-value services. SIGCOMM Comput. Commun. Rev. 44, 4 (October 2014), 295–306. DOI:https://doi.org/10.1145/2740070.2626299*


*Anuj Kalia, Michael Kaminsky, and David G. Andersen. 2014. Using RDMA efficiently for key-value services. In Proceedings of the 2014 ACM conference on SIGCOMM (SIGCOMM ’14). Association for Computing Machinery, New York, NY, USA, 295–306. DOI:https://doi.org/10.1145/2619239.2626299*

## Scalable RDMA RPC on Reliable Connection with Efficient Resource Sharing

This paper presents *ScaleRPC*, an RPC system that addresses the scalability problem of RCs. They point out that the main scalability problem of reliable connections stem from limited RNIC and CPU caches, resulting in cache thrashing with a large amount of active QPs. 

In general *ScaleRPC* uses RDMA WRITEs to send both requests and responses.

*ScaleRPC* addresses scaling problem with temporal slicing. Clients are grouped into *connection groups* and they can only send request in there appropriate time slice. This reduces the amount of active connections. Each *connection group* also uses the same memory region to write requests to, further reducing the amount of mapped memory in order to fit in the CPUs last level cache. 

*ScaleRPC* seems to scale to at least 400 clients.

*Youmin Chen, Youyou Lu, and Jiwu Shu. 2019. Scalable RDMA RPC on Reliable Connection with Efficient Resource Sharing. In Proceedings of the Fourteenth EuroSys Conference 2019 (EuroSys ’19). Association for Computing Machinery, New York, NY, USA, Article 19, 1–14. DOI:https://doi.org/10.1145/3302424.3303968*

