#include "threadforge/thread_pool.hpp"

#include <algorithm>
#include <stdexcept>

namespace tf {

ThreadPool::ThreadPool(unsigned threads, std::size_t max_queue) : max_queue_(max_queue) {
    threads = std::max(1u, threads);
    workers_.reserve(threads);
    for (unsigned i = 0; i < threads; ++i) workers_.emplace_back([this] { worker_loop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
    }
    job_cv_.notify_all();
    space_cv_.notify_all();
    for (auto& t : workers_) t.join();  // workers drain the remaining queue first
}

void ThreadPool::enqueue(std::function<void()> job) {
    {
        std::unique_lock<std::mutex> lk(m_);
        space_cv_.wait(lk, [this] { return stop_ || max_queue_ == 0 || jobs_.size() < max_queue_; });
        if (stop_) throw std::runtime_error("ThreadPool is stopping");
        jobs_.push(std::move(job));
    }
    job_cv_.notify_one();
}

void ThreadPool::wait_idle() {
    std::unique_lock<std::mutex> lk(m_);
    idle_cv_.wait(lk, [this] { return jobs_.empty() && active_ == 0; });
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(m_);
            job_cv_.wait(lk, [this] { return stop_ || !jobs_.empty(); });
            if (jobs_.empty()) return;  // stop_ is set and nothing left to do
            job = std::move(jobs_.front());
            jobs_.pop();
            ++active_;
        }
        space_cv_.notify_one();
        job();  // exceptions are captured by the packaged_task
        {
            std::lock_guard<std::mutex> lk(m_);
            --active_;
            if (jobs_.empty() && active_ == 0) idle_cv_.notify_all();
        }
    }
}

}  // namespace tf
