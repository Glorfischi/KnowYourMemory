# KnowYourMemory

A collection of benchmarks to understand RDMA connection design. This is part of a Master Thesis.

### Timeline

| Connection          |  Implemented   | Performance Model | single-threaded bench | multi-threaded bench | N:1 | 1:N | N:N |
|:-------------------:|:--------------:|:-----------------:|:---------------------:|:--------------------:|:---:|:---:|:---:|
| Send Receive        | x              |                   | x                     | ~                    |     |     |     |
| Buffered Write      | x              |                   | x                     | ~                    |     |     |     |
| Shared Buffer Write | x              |                   | ~                     |                      |     |     |     |
| Unbuffere Write     |                |                   |                       |                      |     |     |     |
| Buffered Read       |                |                   |                       |                      |     |     |     |
| Shared Buffer Read  |                |                   |                       |                      |     |     |     |
| Unbuffered Read     | x              |                   | x                     |                      |     |     |     |
|                     |                |                   |                       |                      |     |     |     |

### Project Structure
    .
    ├── doc               # Documentation, Plots and Thesis
    ├── external          # Some external, useful C++ libraries
    ├── README.md         # You are here!
    ├── src               # Common library
    ├── tests             # Directory containing all benchmarks and tests
    └── tools             # Collection of tools that helps with the project


#### Library
In the `src` is all common library code. Eventually it should contain multiple more polished connection implementations that 
have proved to work well in benchmarks. Code in this directory should work reasonably well, but keep in mind that this 
is only a research project.


### Requirements

