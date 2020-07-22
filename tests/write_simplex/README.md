## WriteSimplexConnection

The `WriteSimplexConnection` is a one-to-one, one directional connection type using a ring buffer.

### Protocol

#### On Send

* Checks if we received any acknowledgments and update the head if so. We also decrease the number of 
outstanding acks and repost the receive buffer. If we reached the maximum number of outstanding acks,
this step will block until we receive an ack to prevent RNR errors

* Write the message to either the tail of the buffer if there is enough space at the end of the buffer or 
wrap around to the front of the buffer and write it to offset 0. This wastes space in the ring buffer, but
allows us to write it with a single `sge`. We perform the write with a `WriteWithImmidate`, also sending 
the length of the written message. 

* We update the tail accordingly and increase the number of outstanding acks.


#### On Receive 

* We poll the receive queue. We will receive the length of the next message. 

* We repost the receive buffer

* We then make the same check as when sending to get the location of the written message and return it.


#### On Free

* We update the head and send it back to the sender as acknowledgment using an inline send.


### Test Setup

You can build the test binary using make.

    make

On the server we then run

    /bin/writesimplex -s -i 172.17.5.101 -n 1024

On the client we run

    /bin/writesimplex -i 172.17.5.101 -n 1024

This will send 1024 messages from the client to the server and measure the latency of the send, and 
receive and free calls. The message size is fixed to 2 KiB.
