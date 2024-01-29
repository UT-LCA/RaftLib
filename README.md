Blocking-Less Queuing (BLQ) Runtime
===================================

Blocking-Less Queuing (BLQ) is a runtime framework capable of finding the proper strategies at or before queue blocking.
BLQ collects a set of solutions, including yielding, advanced dynamic queue buffer resizing, and resource-aware task scheduling.
The programming interface of BLQ is simple and intuitive:
just use C++ streams operator to connect compute kernels as a data-flow graph, 
then you get all the optimizations (e.g., parallel execution, userspace threading, buffer management) the runtime provides!

### Example

Define a compute kernel, Filter, that filters away non-zero values:
```c++
class Filter : public raft::Kernel {
 public:
  Filter() : raft::Kernel() {
    add_input<int>("0"_port); // add an input
    add_output<int>("0"_port); // add an output
  }
  virtual raft::kstatus::value_t compute(raft::StreamingData &dataIn, raft::StreamingData &bufOut) {
    int val;
    dataIn.pop<int>(val); // pop message
    if (0 != val) { bufOut.push(val); } // filter away zero values
    return raft::kstatus::proceed; // proceed to next task
  }
  virtual bool pop( raft::Task *task, bool dryrun ) { return task->pop( "0"_port, dryrun ); }
  virtual bool allocate( raft::Task *task, bool dryrun ) { return task->allocate( "0"_port, dryrun ); }
};
```

Define a compute kernel, Generator, that generates values:
```c++
class Generator : public raft::Kernel {
 int n;
 public:
  Generator(int count) : raft::Kernel(), n(count) {
    add_output<int>("0"_port); // add an output
  }
  virtual raft::kstatus::value_t compute(raft::StreamingData &dataIn, raft::StreamingData &bufOut) {
    bufOut.push(rand() % 2);
    if (0 >= n--) {
      return raft::kstatus::stop;
    }
    return raft::kstatus::proceed;
  }
  virtual bool pop( raft::Task *task, bool dryrun ) { return true; }
  virtual bool allocate( raft::Task *task, bool dryrun ) { return task->allocate( "0"_port, dryrun ); }
};
```

Define a compute kernel, Print, that print values:
```c++
class Print : public raft::Kernel {
 public:
  Print() : raft::Kernel() {
    add_input<int>("0"_port); // add an input
  }
  virtual raft::kstatus::value_t compute(raft::StreamingData &dataIn, raft::StreamingData &bufOut) {
    int val;
    dataIn.pop<int>(val); // pop message
    std::cout << val << std::endl;
    return raft::kstatus::proceed;
  }
  virtual bool pop( raft::Task *task, bool dryrun ) { return task->pop( "0"_port, dryrun ); }
  virtual bool allocate( raft::Task *task, bool dryrun ) { return true; }
};
```

Connect compute kernels together to form a graph, and execute 
```c++
#include <raft>

int main() {
  Filter f;
  Generator g(10);
  Print p;
  raft::DAG dag;
  dag += g >> f >> p;
  dag.exe<raft::RuntimeFIFO>();
  return 0;
}
```

### Build

#### Depedencies
* Compiler: c++17 capable -> GNU GCC 8.0+, clang 5.0+
* cmake
* pkg-config

#### Instructions

Make a build directory (for the instructions below, we'll 
write [build]).

To use the [QThreads User space HPC threading library](http://www.cs.sandia.gov/qthreads/) 
you will need to use the version with the RaftLib org and follow the RaftLib specific readme. 
This QThread version has patches for hwloc2.x applied and fixes for test cases.
To assist cmake to find the QThread library install, you can set the environment variable
`QTHREAD_PATH=<qthread install path>`, or
`QTHREAD_LIB=<qthread library path> QTHREAD_INC=<qthread header path>`.
If QThread is found and compiled, the corresponding compiler flags and include path, linking
path would be set in the pkg-config file for RaftLib.

Similarly, to use the [libut threading library](https://github.com/UT-LCA/libut)
you can set the environment variable
`UT_PATH=<libut install path>`, or
`UT_LIB=<libut library path> UT_INC=<libut header path>`
to assist cmake to find the libut library install.
If libut is found, the corresponding compiler flags and include path, linking path would
be set in the pkg-config file for RaftLib.

Building the examples, and tests can be disabled using:
```bash
-DBUILD_EXAMPLES=false
-DBUILD_TESTS=false
```

To build:

```bash
mkdir [build]
cd [build]
cmake ..
make && make test
sudo make install
```
NOTE: The default prefix in the makefile is: 
```
PREFIX ?= /usr/local
```

### Citation
If you use this framework for something that gets published, please cite it as:
```bibtex
@inproceedings{CC2024BLQ,
author = {Wu, Qinzhe and Li, Ruihao and Beard, Jonathan C and John, Lizy K},
title = {BLQ: Light-Weight Locality-Aware Runtime for Blocking-Less Queuing},
url = {https://doi.org/10.1145/3640537.3641568},
doi = {10.1145/3640537.3641568},
year = {2024},
series = {CC 2024}
}
```
