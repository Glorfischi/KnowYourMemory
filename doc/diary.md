## Week 2: Impoving my C++
2020-07-15 f70c4cf159d056010e95a963e77d4bf2cf5d1bd5

I got a little sidetracked this week. My goal was to implement acknowledgements for the ReadConnetion as well as implement
the first iteration of a circular buffer based WriteConnetion. In the end I actually read up on CPP features and thought about
error handling, and in turn basically refactored everything again.

### Learning C++

I did try to think about error handling last week, but in the end my approach was very much insufficient and I decided to 
revisit it. First I tried to look at the way other, similar libraries did this. I had to realize that there does not seem 
to a single to do this. Some use exceptions (https://github.com/zeromq/cppzmq), others return error code, while others 
simply assert that no error has occurred (https://github.com/claudebarthels/infinity). It seems error handling is not as 
important for CPP developers as it should be.

**Exceptions** Exceptions seemed to be the first and logical solution. It is the built in solution. I was already a bit
skeptical about exceptions. I'm many a Golang developer, which consciously decided not to include exceptions. Further reading up
on CPP exceptions I decided not to use them. They are easy to get wrong and make the control flow more complicated. Google's 
CPP style guide suggests not to use them. Hard to merge with C code.

**Return Code** The other way to do error handling is to return them. This however usually makes the function signature
uglier as we always have to return an error code in addition to the actual return type. In the end I decided to copy the way 
google does this their cloud api library (https://github.com/googleapis/google-cloud-cpp/blob/master/google/cloud/status_or.h).
I return a Status object with an error code as well as a message and if the function should return something we return a 
StatusOr object which contains either a non ok status or the value we wanted to return.

**Smart Pointers** It is easy to leak memory in C/CPP. CPP11 introduced smart pointers to simplify freeing allocated 
objects. It comes with some performance penalty and cannot be used for raw memory regions on the heap, which we need to handle.
But I decided to use smart pointers for inter object dependencies and in the user facing interface.

### Endpoint Class

I realized that directly calling the *ibv* C library is a bad idea in the long run and that we can reduce code duplication 
for the connection setup. This means I introduced a generic *Endpoint* class that wraps the *ibv* library and contains the 
queue pair as well as a generic *Listener* class and a generic *Dial* function.

It also wraps the integer return types in a *Status*.


### ReadConnection. Acks and how to interleave requests

The idea we had last week was to introduce a memory region on the sender with one *uint64\_t* for every sent request. The 
receiver will then have to set the corresponding value to a non zero number to signal that he is done reading from the buffer
and it can be reused. I realized that this does not quite fit in the current understanding of the connection.

**Serial implementation** For now the *send* implementation is inherently serial. We send a message and immediately 
wait for it to complete. This means there is no point in having an array for multiple outstanding requests. So for now I 
implemented a acknowledgement buffer with single integer on the sender which is updated by a *rdma\_write* request by the 
receiver as soon as he is done.

**Way to concurrency**  To enable concurrency in our *Sender* interface, we will need to do two things. First we will
need to give each *SendRegion* a unique identifier. We the need to split the *Send* method into *Send* and *Commit*. The 
send will immediately return and the *Commit* will only return if the completion event for this region arrived in the 
completion queue and the *SendRegion* can be reused. 

## Proceeding with the Project 

In the interest of faster progress we decided to not focus on building a robust library, but on multiple fairly self contained
benchmarks. We will still have a common core library and potentially moving connection implementations into the core library
further down the line. This means from now in in the connection implementation we will not focus on error handling and 
robustness.


## Week 1: Inital CPP Design

2020-07-08 d7ac527afe2f6cbbb529c0c300b8966dc72debc5

I implement three connection types this week. The standard *SendReceiveConnection*, a *SharedReceiveConnection* and a 
*ReadConnection*. I tested these connection types through their unified interface with a very simple send and receive loop.

### Interface

I tried to have a single unified interface for all 3 connection types. This lead to the following first iteration of 
a Sender and Receiver interface. Each connection implements both of them.


    class Sender {
      public:
        virtual SendRegion GetMemoryRegion(size_t size);
        virtual void Free(SendRegion);
        virtual int Send(SendRegion);
    };

    class Receiver {
      public:
        virtual ReceiveRegion Receive();
        virtual void Free(ReceiveRegion);
    };



### SendReceiveConnection

For the SendReceiveConnection we simply use the *IBV_SEND* method. This means the receiver needs to have receive buffers
posted at all times. This means on creation the receiver class posts *N* buffers of size *max_transmission_size*. When 
*Receive()* is called we return a region containing the address of the buffer we received the message to as well as some
identifier. As soon as the user read the data, she has to *Free* it which will repost the buffer. To identify which buffer
the message was received to, we use the *wc_id* when posting the receive buffer to identify it when we receive a completion
event.

A sender simply allocates and registers a new memory region and calls *IBV_SEND*. To improve performance we could implement 
a more sophisticated allocator.

**Reader Not Ready** If no receive buffer is posted when we want to send a message we get a RNR error. If we get 
such an error the Queue Pair moves into an error state, which means we will loose connection. We can tell the NIC to 
retry the send one to seven or infinite times.

### SharedReceive

The SharedReceiveConnection is very similar. The main difference is that multiple connections share a single recieve queue.
This means we only need to post receive buffer to a single queue.

**Completion Queues** It is worth to note that for the standard setup by the RDMA connection manager each QP has 
an own completion queue and completion events will only be sent to the correct completion queue. We might need to check 
whether this inflicts some kind of performance penalty.

### ReadConnection

The ReadCconnection works a bit differently and is probably best suited for larger messages. The idea is that the sender
simply sends the *lkey* and address of the to be sent message and the receiver uses an RDMA read call to read it from the
senders memory.



