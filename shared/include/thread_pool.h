#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <queue>
#include <vector>
#include <atomic>

// Lightweight thread pool — submit work, optionally drain on shutdown.
// No futures, no exceptions, just fire-and-forget tasks.
// Used on server for chunk generation and on client for mesh building.
class ThreadPool {
public:
    // nThreads=0 → use hardware_concurrency-1 (leave one core for main)
    explicit ThreadPool(int nThreads = 0) {
        int n = nThreads > 0 ? nThreads
              : std::max(1, (int)std::thread::hardware_concurrency() - 1);
        _workers.reserve(n);
        for (int i = 0; i < n; i++)
            _workers.emplace_back([this]{ workerLoop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard lk(_mu);
            _stop = true;
        }
        _cv.notify_all();
        for (auto& t : _workers) t.join();
    }

    // Submit a task. Cheap — just a queue push.
    void submit(std::function<void()> fn) {
        {
            std::lock_guard lk(_mu);
            _queue.push(std::move(fn));
        }
        _cv.notify_one();
    }

    int pending() const {
        std::lock_guard lk(_mu);
        return (int)_queue.size();
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> fn;
            {
                std::unique_lock lk(_mu);
                _cv.wait(lk, [this]{ return _stop || !_queue.empty(); });
                if (_stop && _queue.empty()) return;
                fn = std::move(_queue.front());
                _queue.pop();
            }
            fn();
        }
    }

    mutable std::mutex      _mu;
    std::condition_variable _cv;
    std::queue<std::function<void()>> _queue;
    std::vector<std::thread> _workers;
    bool _stop = false;
};
