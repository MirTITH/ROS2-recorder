#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

#include "data_recorder/writer_queue.hpp"

using namespace std::chrono_literals;

TEST(WriterQueue, ProcessesAllItemsInOrder)
{
  std::vector<int> consumed;
  data_recorder::WriterQueue<int> queue(
    /*capacity=*/100,
    data_recorder::OverflowPolicy::Block,
    [&consumed](int value) { consumed.push_back(value); });

  for (int i = 0; i < 50; ++i) {
    ASSERT_TRUE(queue.push(i));
  }
  queue.stop();  // 阻塞直到排空 + worker 退出

  ASSERT_EQ(consumed.size(), 50u);
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(consumed[i], i);
  }
}

TEST(WriterQueue, DropOldestPolicyDropsOldestAndCountsExactly)
{
  std::atomic<bool> gate{true};
  std::atomic<bool> parked{false};  // sink signals it has taken item 0 and is blocking
  std::vector<int> consumed;
  data_recorder::WriterQueue<int> queue(
    /*capacity=*/4,
    data_recorder::OverflowPolicy::DropOldest,
    [&](int value) {
      parked.store(true);                 // 第一次进入 sink 即标记已驻留
      while (gate.load()) { std::this_thread::sleep_for(1ms); }
      consumed.push_back(value);
    });

  // 推入 item 0，等 worker 真正取走并卡在 gate（确定性，不靠调度顺序）。
  ASSERT_TRUE(queue.push(0));
  while (!parked.load()) { std::this_thread::sleep_for(1ms); }

  // 此刻队列空、worker 卡在 sink。推入 1..10：capacity=4，丢最旧，最终队列留 7,8,9,10。
  for (int i = 1; i <= 10; ++i) {
    ASSERT_TRUE(queue.push(i));
  }

  gate.store(false);  // 放行 sink
  queue.stop();       // 排空 7,8,9,10

  // 丢最旧 => 幸存顺序恰为 [0, 7, 8, 9, 10]
  ASSERT_EQ(consumed.size(), 5u);
  EXPECT_EQ(consumed, (std::vector<int>{0, 7, 8, 9, 10}));
  EXPECT_EQ(queue.dropped_count(), 6u);  // 1,2,3,4,5,6 被丢
}

TEST(WriterQueue, BlockPolicyNeverDrops)
{
  std::vector<int> consumed;
  data_recorder::WriterQueue<int> queue(
    /*capacity=*/2,
    data_recorder::OverflowPolicy::Block,
    [&](int value) {
      std::this_thread::sleep_for(1ms);
      consumed.push_back(value);
    });

  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(queue.push(i));  // 满时阻塞，绝不丢
  }
  queue.stop();

  EXPECT_EQ(consumed.size(), 20u);
  EXPECT_EQ(queue.dropped_count(), 0u);
}

TEST(WriterQueue, StopRejectsNewItemsForBothPoliciesAndIsIdempotent)
{
  for (const auto policy : {data_recorder::OverflowPolicy::Block,
                            data_recorder::OverflowPolicy::DropOldest}) {
    data_recorder::WriterQueue<int> queue(
      /*capacity=*/2, policy, [](int) {});

    queue.request_stop();
    EXPECT_FALSE(queue.push(1));
    queue.stop();
    queue.stop();
    queue.request_stop();
    EXPECT_FALSE(queue.push(2));
  }
}

TEST(WriterQueue, StopWakesBlockedBlockProducer)
{
  std::atomic<bool> sink_entered{false};
  std::atomic<bool> release_sink{false};
  std::atomic<bool> push_done{false};
  std::promise<void> producer_started;
  auto producer_started_future = producer_started.get_future();

  data_recorder::WriterQueue<int> queue(
    /*capacity=*/1,
    data_recorder::OverflowPolicy::Block,
    [&](int) {
      sink_entered.store(true);
      while (!release_sink.load()) {
        std::this_thread::sleep_for(1ms);
      }
    });

  ASSERT_TRUE(queue.push(1));
  while (!sink_entered.load()) {
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_TRUE(queue.push(2));  // Fill the bounded queue while sink is parked.

  std::thread producer([&]() {
    producer_started.set_value();
    push_done.store(queue.push(3));
  });
  producer_started_future.wait();
  std::this_thread::sleep_for(10ms);
  EXPECT_FALSE(push_done.load());

  queue.request_stop();
  producer.join();
  EXPECT_FALSE(push_done.load());

  release_sink.store(true);
  queue.join();
}

TEST(WriterQueue, SinkFailureClearsQueueWakesProducerAndIsReported)
{
  std::atomic<bool> sink_entered{false};
  std::atomic<bool> release_sink{false};
  std::atomic<bool> push_done{false};
  std::promise<void> producer_started;
  auto producer_started_future = producer_started.get_future();

  data_recorder::WriterQueue<int> queue(
    /*capacity=*/1,
    data_recorder::OverflowPolicy::Block,
    [&](int) {
      sink_entered.store(true);
      while (!release_sink.load()) {
        std::this_thread::sleep_for(1ms);
      }
      throw std::runtime_error("sink failed");
    });

  ASSERT_TRUE(queue.push(1));
  while (!sink_entered.load()) {
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_TRUE(queue.push(2));

  std::thread producer([&]() {
    producer_started.set_value();
    push_done.store(queue.push(3));
  });
  producer_started_future.wait();
  std::this_thread::sleep_for(10ms);
  EXPECT_FALSE(push_done.load());

  release_sink.store(true);
  producer.join();
  EXPECT_FALSE(push_done.load());
  queue.stop();

  const auto failure = queue.error();
  ASSERT_TRUE(failure);
  try {
    std::rethrow_exception(failure);
  } catch (const std::runtime_error & error) {
    EXPECT_STREQ(error.what(), "sink failed");
  } catch (...) {
    FAIL() << "unexpected exception type";
  }
}

TEST(WriterQueue, WorkerCanRequestStopWhileOwnerIsJoining)
{
  std::promise<void> sink_entered;
  auto sink_entered_future = sink_entered.get_future();
  std::promise<void> release_sink;
  auto release_sink_future = release_sink.get_future();
  std::promise<void> owner_joining;
  auto owner_joining_future = owner_joining.get_future();
  std::promise<void> owner_joined;
  auto owner_joined_future = owner_joined.get_future();
  data_recorder::WriterQueue<int> * worker_queue = nullptr;

  data_recorder::WriterQueue<int> queue(
    /*capacity=*/1,
    data_recorder::OverflowPolicy::Block,
    [&](int) {
      sink_entered.set_value();
      release_sink_future.wait();
      // This must close admission and return without contending with the
      // owner that is already waiting for this same worker to exit.
      worker_queue->stop();
    });
  worker_queue = &queue;
  ASSERT_TRUE(queue.push(1));
  sink_entered_future.wait();

  std::thread owner([&]() {
    queue.request_stop();
    owner_joining.set_value();
    queue.join();
    owner_joined.set_value();
  });
  owner_joining_future.wait();
  // With the worker parked, the owner's join cannot complete.  Give it time
  // to enter join() before releasing the worker's self-stop.
  EXPECT_EQ(owner_joined_future.wait_for(20ms), std::future_status::timeout);
  release_sink.set_value();
  EXPECT_EQ(owner_joined_future.wait_for(1s), std::future_status::ready);
  owner.join();

  EXPECT_FALSE(queue.push(2));
  queue.stop();
}
