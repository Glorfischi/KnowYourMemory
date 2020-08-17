## Week 7: Benchmarking
2020-08-19

### Finishing Atomic Connection

With the updated RXE in debian testing that supports getting the number of written bytes from the `wc` I was able to get the
atomic connect working, although only using softRoCE.

### Latency testing

I started to write benchmarks for the SendReceive Connection. I first cleaned up the current latency measurement and plotted 
the resulting data using gnuplot. I found that especially the tail latency was very unpredictable. The 95th percentile ranges
from less then 100 us to up to 5000 us running the same benchmark. I'm unsure what is causing this.

### Throughput testing

..

## Week 6: Atomic Connection and move to actual hardware
2020-08-12 70ea73bd3c0f69f67bca05dc4d8fe0d5dc23c782

I started implementing the `write_atomic` connection, which is a ring buffer connection that shares the ring buffer for receiving
from multiple sender. The sender initially increases the tail pointer atomically by the length of its message using `fetch and add`,
effectively reserving the buffer space. A rough overview of the protocol can be found in the 
connections [README](../tests/write_atomic/README.md)

I also ran the existing code on the physical boadcom card, which lead to some minor bugs, but on the whole they worked.


## Week 5: Physical Card and Related Work
2020-08-05 ea911fa7471cc6f8641fec4c56ea60c7446438a5

Most of this week as spend trying to set up the new physical broadcom NIC to work.

I read quite a lot of papers getting a better grasp of the current state of research. I summarized some of them 
[here](./related_work.md)

Getting the broadcom card to work was quite a hassle. In the end I had to recompile my kernel to include `bnxt_re`
driver, which is not included in the base debian kernel. I also head to move the NIC to another PCIe slot after peaking 
at around `8 Gb/s` in the initial slot.

I also the discovered to use of `private_data` when setting up the connection using `rdma_cma`. This allows us to exchange 
arbitrary data on connection setup, greatly simplifying the setup.

## Week 4: Two sided writes
2020-07-29 c08855e56dcfe2b6fec8aeba0775d50d4e585fa7

I implemented the `write_duplex` connection, which is a ring buffer connection that uses reads from the sending side to
update the head position, this means the receiver does not have to make any outbound calls, which enables this protocol to 
send and receive data using a single QP. A rough overview of the protocol can be found in the 
connections [README](../tests/write_duplex/README.md)


## Week 3: Circular Buffer
2020-07-22 196e00423d9d339dc2d0b4eb2c8d3afdbd0a1dc9

I realized that we should focus on benchmark implementations and some kind of working connections instead of library design.
I split the code base into the `src` directory, containing common library code, and connection implementations in the `tests/`
directory. The latter can be at times quite hacky.

### Write Simplex

I implemented the `write_simplex` connection, which is a ring buffer connection that uses sends at the receiving side to
update the head position, which makes it inherently one sided. A rough overview of the protocol can be found in the 
connections [README](../tests/write_simplex/README.md)

### Magic Buffer

When writing to the ring buffer through RDMA we cannot simply wrap around the end of the buffer. In the `write_simplex` 
connection we "solved" this by skipping to the start of the buffer as soon as there is not enough space at the end of it.
This potentially wastes a lot of space in the ring buffer. We could improve this by using two writes (or two SGEs) but 
we used a so called "magic buffer".

The idea of the magic buffer is to map the same physical memory region two times, adjacent to each other. This means if we
write over the end of the first one we write to the start of the buffer and we do not have to waste space or split our 
write request into two. I used [smcho-kr/magic-ring-buffer](https://github.com/smcho-kr/magic-ring-buffer) as a reference 
for our implementation.

Using this magic buffer I extended the `write_simplex` connection and got 
[write\_simplex\_magic](../tests/write_simplex_magic/README.md)



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



