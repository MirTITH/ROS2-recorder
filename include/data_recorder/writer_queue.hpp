#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace data_recorder
{

enum class OverflowPolicy
{
  Block,       // 满时阻塞生产者（rosbag：宁慢不丢）
  DropOldest,  // 满时丢最旧并计数（video：保流畅）
};

// 单生产者→单消费者有界队列 + 自带 worker 线程。
template<typename T>
class WriterQueue
{
public:
  WriterQueue(std::size_t capacity, OverflowPolicy policy, std::function<void(T)> sink)
  : capacity_(capacity), policy_(policy), sink_(std::move(sink))
  {
    worker_ = std::thread([this]() { run(); });
  }

  ~WriterQueue() noexcept
  {
    stop();
  }

  WriterQueue(const WriterQueue &) = delete;
  WriterQueue & operator=(const WriterQueue &) = delete;

  bool push(T item)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopping_ || failed_) {
      return false;
    }

    // A zero-capacity queue cannot accept an item.  Treat it as closed rather
    // than waiting forever (or looping forever for DropOldest).
    if (capacity_ == 0) {
      return false;
    }

    if (policy_ == OverflowPolicy::Block) {
      space_cv_.wait(lock, [this]() {
        return queue_.size() < capacity_ || stopping_ || failed_;
      });
      if (stopping_ || failed_) {
        return false;
      }
    } else {  // DropOldest
      while (queue_.size() >= capacity_) {
        if (stopping_ || failed_) {
          return false;
        }
        queue_.pop_front();
        ++dropped_;
      }
    }
    queue_.push_back(std::move(item));
    item_cv_.notify_one();
    return true;
  }

  // Stop accepting new items, then let the worker drain the queue.
  void request_stop() noexcept
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    item_cv_.notify_all();
    space_cv_.notify_all();
  }

  // Wait for the worker to exit.  This is deliberately separate from
  // request_stop() so callers can first close all producers and then join.
  void join() noexcept
  {
    // Check the dedicated worker's identity before taking join_mutex_.  The
    // owner can already hold that mutex while waiting for this worker, so
    // checking only after locking would make a worker's stop() deadlock.
    // TLS also avoids reading std::thread while another thread is joining it.
    if (worker_queue_ == this) {
      // Self-destruction is unsupported; an external owner must join later.
      return;
    }
    std::lock_guard<std::mutex> join_lock(join_mutex_);
    if (worker_.joinable()) {
      try {
        worker_.join();
      } catch (...) {
        // Destruction and stop are noexcept.  A valid std::thread should make
        // join() succeed; if the platform reports an error, leave cleanup to
        // the owning thread rather than propagating from a callback/destructor.
      }
    }
  }

  // Stop accepting items and block until the worker has drained and exited.
  void stop() noexcept
  {
    request_stop();
    join();
  }

  std::size_t dropped_count() const
  {
    return dropped_.load();
  }

  std::exception_ptr error() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
  }

private:
  void run()
  {
    worker_queue_ = this;
    try {
      for (;;) {
        T item;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          item_cv_.wait(lock, [this]() { return !queue_.empty() || stopping_; });
          if (queue_.empty()) {
            // On a normal stop, exit only after the queue is drained.  A
            // failed worker clears the queue and exits immediately.
            if (stopping_) {
              return;
            }
            continue;
          }
          item = std::move(queue_.front());
          queue_.pop_front();
          space_cv_.notify_one();
        }
        sink_(std::move(item));  // 锁外执行，避免阻塞生产者
      }
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!failed_) {
          error_ = std::current_exception();
        }
        failed_ = true;
        stopping_ = true;
        queue_.clear();
      }
      // Wake blocked producers so they can observe failed_/stopping_ and
      // reject their item instead of waiting for a worker that has exited.
      item_cv_.notify_all();
      space_cv_.notify_all();
    }
  }

  std::size_t capacity_;
  OverflowPolicy policy_;
  std::function<void(T)> sink_;

  mutable std::mutex mutex_;
  std::condition_variable item_cv_;
  std::condition_variable space_cv_;
  std::deque<T> queue_;
  std::atomic<std::size_t> dropped_{0};
  bool stopping_{false};
  bool failed_{false};
  std::exception_ptr error_;
  std::mutex join_mutex_;
  std::thread worker_;
  inline static thread_local const WriterQueue * worker_queue_{nullptr};
};

}  // namespace data_recorder
