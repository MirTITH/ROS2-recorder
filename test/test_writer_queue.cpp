#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
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
    queue.push(i);
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
  queue.push(0);
  while (!parked.load()) { std::this_thread::sleep_for(1ms); }

  // 此刻队列空、worker 卡在 sink。推入 1..10：capacity=4，丢最旧，最终队列留 7,8,9,10。
  for (int i = 1; i <= 10; ++i) {
    queue.push(i);
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
    queue.push(i);  // 满时阻塞，绝不丢
  }
  queue.stop();

  EXPECT_EQ(consumed.size(), 20u);
  EXPECT_EQ(queue.dropped_count(), 0u);
}
