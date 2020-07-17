# KnowYourMemory

A collection of benchmarks to understand RDMA connection design. This is part of a Master Thesis.

### Project Structure
    .
    ├── doc
    │   └── diary.md      # Development diary with updates for each week
    ├── external          # Some external, useful C++ libraries
    │   └── cxxopts.hpp
    ├── README.md         # You are here!
    ├── src               # Common library
    │   ├── conn.hpp        # Common interface shared by (most) connections
    │   ├── endpoint.hpp    # C++ wrapper around ibv, including simplified connection setup
    │   ├── error.hpp       # Error handling code, including custom status structure
    │   ├── mm.hpp          # Interface for rdma memory region allocators
    │   └── mm              # Allocator implementations
    │   
    ├── tests             # Directory containing all benchmarks and tests
    │   ├── send_rcv        # Simple send receive connection implementation
    │   ├── shared_recv     # Send Receive connection with shared receive queue
    │   ├── remote_read     # Connection that reads from remote posed buffer
    │   ├── write_simplex   # Oneway one-to-one circular buffer connection  
    │   └── write_duplex    # Duplex one-to-one circular buffer connection
    │   
    └── tools             # Collection of tools that helps with the project

#### Benchmarks
There are multiple benchmarks ..

#### Library
In the `src` is all common library code. Eventually it should contain multiple more polished connection implementations that 
have proved to work well in benchmarks. Code in this directory should work reasonably well, but keep in mind that this 
is only a research project.


### Requirements

