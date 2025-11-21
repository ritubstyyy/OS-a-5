// File: simple-multithreader.h
#ifndef SIMPLE_MULTITHREADER_H
#define SIMPLE_MULTITHREADER_H

// SimpleMultithreader - header-only implementation using Pthreads and C++11 lambdas.
// Usage: include this header in a C++ file compiled with -std=c++11 and -pthread.

#include <pthread.h>
#include <functional>
#include <memory>
#include <vector>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <climits>

namespace simple_mt {

inline long long now_ms() {
    using namespace std::chrono;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Internal struct passed to each pthread
template<typename LambdaType, typename IndexArg>
struct ThreadArg {
    std::shared_ptr<LambdaType> lambda;
    IndexArg start_idx;
    IndexArg end_idx;
    int low1;
    int low2;
    int width2;

    ThreadArg(std::shared_ptr<LambdaType> l, IndexArg s, IndexArg e)
        : lambda(l), start_idx(s), end_idx(e), low1(0), low2(0), width2(0) {}
};

inline void *thread_entry_1d(void *arg_void) {
    auto *arg = static_cast<ThreadArg<std::function<void(int)>, int>*>(arg_void);
    if (!arg) return nullptr;
    try {
        auto lambda = arg->lambda;
        for (int idx = arg->start_idx; idx < arg->end_idx; ++idx) {
            (*lambda)(idx);
        }
    } catch (...) {
        // swallow exceptions across pthread boundary
    }
    delete arg;
    return nullptr;
}

inline void *thread_entry_2d(void *arg_void) {
    auto *arg = static_cast<ThreadArg<std::function<void(int,int)>, int>*>(arg_void);
    if (!arg) return nullptr;
    try {
        auto lambda = arg->lambda;
        int start = arg->start_idx;
        int end = arg->end_idx;
        int low1 = arg->low1;
        int low2 = arg->low2;
        int width2 = arg->width2;
        for (int flat = start; flat < end; ++flat) {
            int i = flat / width2 + low1;
            int j = flat % width2 + low2;
            (*lambda)(i, j);
        }
    } catch (...) {
    }
    delete arg;
    return nullptr;
}

// Helper to split range [low, high) into `numPieces` contiguous pieces.
inline std::vector<std::pair<int,int>> split_range(int low, int high, int numPieces) {
    std::vector<std::pair<int,int>> parts;
    parts.reserve(numPieces > 0 ? numPieces : 0);
    if (numPieces <= 0) return parts;
    if (low >= high) {
        for (int i = 0; i < numPieces; ++i) parts.emplace_back(low, low);
        return parts;
    }
    int total = high - low;
    int base = total / numPieces;
    int rem = total % numPieces;
    int cursor = low;
    for (int t = 0; t < numPieces; ++t) {
        int add = base + (t < rem ? 1 : 0);
        int s = cursor;
        int e = cursor + add;
        parts.emplace_back(s, e);
        cursor = e;
    }
    return parts;
}

// Public API: 1D parallel_for
inline void parallel_for(int low, int high, std::function<void(int)> &&lambda, int numThreads) {
    if (numThreads <= 0) numThreads = 1;
    if (low >= high) return;

    long long t0 = now_ms();

    int create_count = numThreads - 1;
    auto parts = split_range(low, high, numThreads);
    if ((int)parts.size() != numThreads) {
        // fallback: create single piece covering full range
        parts = split_range(low, high, 1);
        create_count = 0;
        numThreads = 1;
    }

    auto shared_lambda = std::make_shared<std::function<void(int)>>(std::move(lambda));
    std::vector<pthread_t> tids;
    tids.reserve(std::max(0, create_count));

    for (int t = 0; t < create_count; ++t) {
        auto *p = new ThreadArg<std::function<void(int)>, int>(shared_lambda, parts[t].first, parts[t].second);
        pthread_t tid;
        int rc = pthread_create(&tid, nullptr, thread_entry_1d, p);
        if (rc != 0) {
            delete p;
            for (pthread_t &pt : tids) pthread_join(pt, nullptr);
            throw std::runtime_error("pthread_create failed in parallel_for (1D)");
        }
        tids.push_back(tid);
    }

    // main thread does last part
    int main_part_idx = create_count;
    {
        int s = parts[main_part_idx].first;
        int e = parts[main_part_idx].second;
        for (int idx = s; idx < e; ++idx) {
            (*shared_lambda)(idx);
        }
    }

    for (pthread_t &pt : tids) {
        pthread_join(pt, nullptr);
    }

    long long t1 = now_ms();
    std::cout << "[SimpleMultithreader] parallel_for(1D) time = " << (t1 - t0) << " ms\n";
}

// Public API: 2D parallel_for
inline void parallel_for(int low1, int high1, int low2, int high2,
                         std::function<void(int,int)> &&lambda, int numThreads) {
    if (numThreads <= 0) numThreads = 1;
    if (low1 >= high1 || low2 >= high2) return;

    long long rows = static_cast<long long>(high1 - low1);
    long long cols = static_cast<long long>(high2 - low2);
    long long total = rows * cols;
    if (total <= 0) return;
    if (total > INT_MAX) {
        throw std::runtime_error("2D range too large to handle (exceeds INT_MAX flattened size).");
    }

    long long t0 = now_ms();

    int total_int = static_cast<int>(total);
    auto parts_flat = split_range(0, total_int, numThreads);
    if ((int)parts_flat.size() != numThreads) {
        parts_flat = split_range(0, total_int, 1);
        numThreads = 1;
    }

    auto shared_lambda = std::make_shared<std::function<void(int,int)>>(std::move(lambda));
    int create_count = numThreads - 1;
    std::vector<pthread_t> tids;
    tids.reserve(std::max(0, create_count));

    for (int t = 0; t < create_count; ++t) {
        auto *p = new ThreadArg<std::function<void(int,int)>, int>(shared_lambda, parts_flat[t].first, parts_flat[t].second);
        p->low1 = low1;
        p->low2 = low2;
        p->width2 = static_cast<int>(cols);
        pthread_t tid;
        int rc = pthread_create(&tid, nullptr, thread_entry_2d, p);
        if (rc != 0) {
            delete p;
            for (pthread_t &pt : tids) pthread_join(pt, nullptr);
            throw std::runtime_error("pthread_create failed in parallel_for (2D)");
        }
        tids.push_back(tid);
    }

    // main thread does its portion
    int main_idx = create_count;
    {
        int s = parts_flat[main_idx].first;
        int e = parts_flat[main_idx].second;
        int cols_int = static_cast<int>(cols);
        for (int flat = s; flat < e; ++flat) {
            int i = flat / cols_int + low1;
            int j = flat % cols_int + low2;
            (*shared_lambda)(i, j);
        }
    }

    for (pthread_t &pt : tids) {
        pthread_join(pt, nullptr);
    }

    long long t1 = now_ms();
    std::cout << "[SimpleMultithreader] parallel_for(2D) time = " << (t1 - t0) << " ms\n";
}

} // namespace simple_mt

// Provide unqualified names expected by assignment (no namespace in sample code)
inline void parallel_for(int low, int high, std::function<void(int)> &&lambda, int numThreads) {
    simple_mt::parallel_for(low, high, std::move(lambda), numThreads);
}

inline void parallel_for(int low1, int high1, int low2, int high2,
                         std::function<void(int,int)> &&lambda, int numThreads) {
    simple_mt::parallel_for(low1, high1, low2, high2, std::move(lambda), numThreads);
}

#endif // SIMPLE_MULTITHREADER_H
