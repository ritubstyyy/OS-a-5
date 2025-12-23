# SimpleMultithreader (C++11, Pthreads)

Overview
SimpleMultithreader is a header-only C++11 library that provides simple parallel_for APIs for 1D and 2D workloads using POSIX threads.
Each parallel_for call creates exactly the specified number of threads, executes the work in parallel, prints execution time, and terminates all threads before returning.

This project was developed as Assignment 5.

APIs Provided

parallel_for(int low, int high, lambda, numThreads)
Executes a 1D loop in parallel over the range [low, high).

parallel_for(int low1, int high1, int low2, int high2, lambda, numThreads)
Executes a 2D loop in parallel by flattening the 2D space into a single iteration range.

Both APIs accept C++11 lambdas and use static work partitioning.

Design Highlights

Header-only implementation contained entirely in simple-multithreader.h
No thread pool; threads are created and destroyed per parallel_for call
Main thread participates in computation along with pthreads
Static contiguous chunking for balanced workload distribution
2D iteration space is flattened and mapped back to (i, j) indices
Execution time is measured using std::chrono and printed per call

Implementation Details

Lambdas are shared safely across threads using std::shared_ptr
Each pthread receives a heap-allocated argument struct defining its work range
Separate pthread entry functions are used for 1D and 2D execution
Exceptions inside threads are caught and not propagated across thread boundaries
Input validation for thread count and iteration ranges is performed
pthread_create failures are handled with proper cleanup and error reporting

Performance and Correctness

Static chunking minimizes overhead and works well for uniform workloads
Thread creation cost is acceptable for coarse-grained tasks
No shared mutable state is accessed during execution
Memory safety is ensured by strict partitioning of iteration ranges

Build and Run

Build on Linux or WSL using g++ with pthread support.

Example commands:
g++ -std=c++11 -pthread vector.cpp -o vector_test
g++ -std=c++11 -pthread matrix.cpp -o matrix_test

Run examples:
./vector_test 4 48000000
./matrix_test 4 1024

Files Included

simple-multithreader.h
vector.cpp
matrix.cpp
Makefile

Contributors

Ritu Basumatary
Arja Kaur Anand

Work was divided equally between both contributors, covering design, implementation, testing, and documentation.
