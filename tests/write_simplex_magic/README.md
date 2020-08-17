## Magic WriteSimplexConnection

The "Magic" `WriteSimplexConnection` is a one-to-one, one directional connection type using a ring buffer, 
and is a one to one replacement to the standard `WriteSimplexConnection` using a magic buffer trick to not waste space
when wrapping around the buffer.

### Protocol

#### On Send

* Checks if we received any acknowledgments and update the head if so. We also decrease the number of 
outstanding acks and repost the receive buffer. If we reached the maximum number of outstanding acks,
this step will block until we receive an ack to prevent RNR errors

* If there is enough space, we write the message to the buffer. If we write over the end of the buffer, the "magic" 
properties will make it wrap around to the beginning of the buffer. We perform the write with a `WriteWithImmidate`,
also sending the length of the written message. 

* We update the tail accordingly and increase the number of outstanding acks.


#### On Receive 

* We poll the receive queue. We will receive the length of the next message. 

* We repost the receive buffer

* We then make the same check as when sending to get the location of the written message and return it.


#### On Free

* We update the head and send it back to the sender as acknowledgment using an inline send.


### The Magic
The magic of the buffer is that we map the same physical address twice adjacent to each other. That means writing over
the end of the first buffer will write to the beginning of the second, which in turn is the same as the first.

Here is an example trying to explain the concept. On the left side we have the virtual address space. We mapped the same 
physical buffer on the write side back to back. We wrote `abcdefg` at the end of the first buffer mapping. (Note in turn
it was also written to the end of the second mapping.

    virtual                                    physical 
    ----------------------------------         ------------------
    |         abcdefg|        abcdefg|         |         abcdefg|
    ----------------------------------         ------------------
                    ^

If we keep on writing we get the same wrapping around we would like from a standard circular buffer

    virtual                                    physical 
    ----------------------------------         ------------------
    |hijk     abcdefg|hijk    abcdefg|         |hijk     abcdefg|
    ----------------------------------         ------------------
                         ^





### Test Setup

You can build the test binary using make.

    make

On the server we then run

    ./bin/writesimplex -s -i 172.17.5.101 -n 1024

On the client we run

    ./bin/writesimplex -i 172.17.5.101 -n 1024

This will send 1024 messages from the client to the server and measure the latency of the send, and 
receive and free calls. The message size is fixed to 2 KiB.
