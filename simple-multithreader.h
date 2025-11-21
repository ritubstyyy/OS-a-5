#ifndef SIMPLE_MULTITHREADER_H
#define SIMPLE_MULTITHREADER_H

// simple-multithreader.h
// Header-only Pthreads-based simple parallel_for (C++11 lambda support).
// Compile with: g++ -std=c++11 -pthread ...

#include <pthread.h>
#include <functional>
#include <memory>
#include <vector>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <climits>

// Internal helper: millisecond timestamp
inline long long sm_now_ms() {
    using namespace std::chrono;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Thread argument struct template (used for 1D and 2D)
template<typename LambdaType>
struct SM_ThreadArg {
    std::shared_ptr<LambdaType> lambda;
    int start_idx;
    int end_idx;
    int low1;
    int low2;
    int width2;
    SM_ThreadArg(std::shared_ptr<LambdaType> l, int s, int e)
        : lambda(l), start_idx(s), end_idx(e), low1(0), low2(0), width2(0) {}
};

// pthread entry for 1D
inline void *sm_thread_entry_1d(void *arg_void) {
    auto *arg = static_cast<SM_ThreadArg<std::function<void(int)> >*>(arg_void);
    if (!arg) return nullptr;
    try {
        auto f = arg->lambda;
        for (int i = arg->start_idx; i < arg->end_idx; ++i) (*f)(i);
    } catch (...) { /* do not propagate */ }
    delete arg;
    return nullptr;
}

// pthread entry for 2D (flattened)
inline void *sm_thread_entry_2d(void *arg_void) {
    auto *arg = static_cast<SM_ThreadArg<std::function<void(int,int)> >*>(arg_void);
    if (!arg) return nullptr;
    try {
        auto f = arg->lambda;
        int s = arg->start_idx;
        int e = arg->end_idx;
        int low1 = arg->low1;
        int low2 = arg->low2;
        int w = arg->width2;
        for (int flat = s; flat < e; ++flat) {
            int i = flat / w + low1;
            int j = flat % w + low2;
            (*f)(i, j);
        }
    } catch (...) {}
    delete arg;
    return nullptr;
}

// split [low, high) into num pieces (contiguous)
inline std::vector<std::pair<int,int>> sm_split_range(int low, int high, int numPieces) {
    std::vector<std::pair<int,int>> parts;
    if (numPieces <= 0) return parts;
    if (low >= high) {
        for (int k = 0; k < numPieces; ++k) parts.emplace_back(low, low);
        return parts;
    }
    int total = high - low;
    int base = total / numPieces;
    int rem = total % numPieces;
    int cur = low;
    for (int t = 0; t < numPieces; ++t) {
        int add = base + (t < rem ? 1 : 0);
        int s = cur;
        int e = cur + add;
        parts.emplace_back(s, e);
        cur = e;
    }
    return parts;
}

// Public API - 1D
inline void parallel_for(int low, int high, std::function<void(int)> &&lambda, int numThreads) {
    if (numThreads <= 0) numThreads = 1;
    if (low >= high) return;

    long long t0 = sm_now_ms();

    auto parts = sm_split_range(low, high, numThreads);
    if ((int)parts.size() != numThreads) { parts = sm_split_range(low, high, 1); numThreads = 1; }

    auto shared_lambda = std::make_shared<std::function<void(int)>>(std::move(lambda));
    int to_create = numThreads - 1;
    std::vector<pthread_t> tids;
    tids.reserve(std::max(0, to_create));

    for (int t = 0; t < to_create; ++t) {
        auto *arg = new SM_ThreadArg<std::function<void(int)>>(shared_lambda, parts[t].first, parts[t].second);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, sm_thread_entry_1d, arg) != 0) {
            delete arg;
            for (pthread_t &pt : tids) pthread_join(pt, nullptr);
            throw std::runtime_error("pthread_create failed (1D)");
        }
        tids.push_back(tid);
    }

    // main thread does last piece
    int main_idx = to_create;
    for (int i = parts[main_idx].first; i < parts[main_idx].second; ++i) (*shared_lambda)(i);

    for (pthread_t &pt : tids) pthread_join(pt, nullptr);

    long long t1 = sm_now_ms();
    std::cout << "[SimpleMultithreader] parallel_for(1D) time = " << (t1 - t0) << " ms\n";
}

// Public API - 2D (low1..high1, low2..high2)
inline void parallel_for(int low1, int high1, int low2, int high2,
                         std::function<void(int,int)> &&lambda, int numThreads) {
    if (numThreads <= 0) numThreads = 1;
    if (low1 >= high1 || low2 >= high2) return;

    long long rows = static_cast<long long>(high1 - low1);
    long long cols = static_cast<long long>(high2 - low2);
    long long total = rows * cols;
    if (total <= 0) return;
    if (total > INT_MAX) throw std::runtime_error("2D range too large");

    long long t0 = sm_now_ms();

    int total_int = static_cast<int>(total);
    auto parts = sm_split_range(0, total_int, numThreads);
    if ((int)parts.size() != numThreads) { parts = sm_split_range(0, total_int, 1); numThreads = 1; }

    auto shared_lambda = std::make_shared<std::function<void(int,int)>>(std::move(lambda));
    int to_create = numThreads - 1;
    std::vector<pthread_t> tids;
    tids.reserve(std::max(0, to_create));

    for (int t = 0; t < to_create; ++t) {
        auto *arg = new SM_ThreadArg<std::function<void(int,int)>>(shared_lambda, parts[t].first, parts[t].second);
        arg->low1 = low1;
        arg->low2 = low2;
        arg->width2 = static_cast<int>(cols);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, sm_thread_entry_2d, arg) != 0) {
            delete arg;
            for (pthread_t &pt : tids) pthread_join(pt, nullptr);
            throw std::runtime_error("pthread_create failed (2D)");
        }
        tids.push_back(tid);
    }

    // main thread piece
    int main_idx = to_create;
    int w = static_cast<int>(cols);
    for (int flat = parts[main_idx].first; flat < parts[main_idx].second; ++flat) {
        int i = flat / w + low1;
        int j = flat % w + low2;
        (*shared_lambda)(i, j);
    }

    for (pthread_t &pt : tids) pthread_join(pt, nullptr);

    long long t1 = sm_now_ms();
    std::cout << "[SimpleMultithreader] parallel_for(2D) time = " << (t1 - t0) << " ms\n";
}

#endif // SIMPLE_MULTITHREADER_H
