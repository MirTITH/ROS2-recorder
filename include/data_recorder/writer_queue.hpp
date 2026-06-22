#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
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

  ~WriterQueue()
  {
    stop();
  }

  WriterQueue(const WriterQueue &) = delete;
  WriterQueue & operator=(const WriterQueue &) = delete;

  void push(T item)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (policy_ == OverflowPolicy::Block) {
      space_cv_.wait(lock, [this]() { return queue_.size() < capacity_ || stopping_; });
      if (stopping_) {
        return;
      }
    } else {  // DropOldest
      while (queue_.size() >= capacity_) {
        queue_.pop_front();
        ++dropped_;
      }
    }
    queue_.push_back(std::move(item));
    item_cv_.notify_one();
  }

  // 阻塞直到队列排空且 worker 退出。
  void stop()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      stopping_ = true;
    }
    item_cv_.notify_all();
    space_cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  std::size_t dropped_count() const
  {
    return dropped_.load();
  }

private:
  void run()
  {
    for (;;) {
      T item;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        item_cv_.wait(lock, [this]() { return !queue_.empty() || stopping_; });
        if (queue_.empty()) {
          // 仅当 stopping_ 且已排空才退出。
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
  }

  std::size_t capacity_;
  OverflowPolicy policy_;
  std::function<void(T)> sink_;

  std::mutex mutex_;
  std::condition_variable item_cv_;
  std::condition_variable space_cv_;
  std::deque<T> queue_;
  std::atomic<std::size_t> dropped_{0};
  bool stopping_{false};
  std::thread worker_;
};

}  // namespace data_recorder
