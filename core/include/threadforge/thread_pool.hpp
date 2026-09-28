#pragma once
// Fixed-size thread pool with an optional bounded queue (back-pressure).
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace tf {

class ThreadPool {
public:
    // max_queue == 0 -> unbounded. Otherwise submit() blocks while the queue is full.
    explicit ThreadPool(unsigned threads, std::size_t max_queue = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <class F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>>> {
        using R = std::invoke_result_t<std::decay_t<F>>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> fut = task->get_future();
        enqueue([task] { (*task)(); });
        return fut;
    }

    // Blocks until the queue is empty and no worker is running a job.
    void wait_idle();

    std::size_t thread_count() const noexcept { return workers_.size(); }

private:
    void enqueue(std::function<void()> job);
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> jobs_;
    std::mutex m_;
    std::condition_variable job_cv_;    // workers wait for jobs
    std::condition_variable space_cv_;  // producers wait for queue space
    std::condition_variable idle_cv_;   // wait_idle() waits here
    std::size_t max_queue_;
    std::size_t active_ = 0;
    bool stop_ = false;
};

}  // namespace tf
