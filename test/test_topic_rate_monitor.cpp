#include <gtest/gtest.h>

#include "data_recorder/topic_rate_monitor.hpp"

TEST(TopicRateMonitor, EstimatesRateFromArrivalTimes)
{
  data_recorder::TopicRateMonitor monitor(/*window_seconds=*/1.0);
  // 10 个事件，间隔 0.1s（精确 10Hz），t 从 0.0 到 0.9s（纳秒）。
  for (int i = 0; i < 10; ++i) {
    monitor.record(static_cast<int64_t>(i) * 100'000'000LL);
  }
  EXPECT_DOUBLE_EQ(monitor.hz(), 10.0);  // 9 个间隔 / 0.9s = 精确 10Hz
}

TEST(TopicRateMonitor, ZeroBeforeTwoSamples)
{
  data_recorder::TopicRateMonitor monitor(1.0);
  EXPECT_DOUBLE_EQ(monitor.hz(), 0.0);
  monitor.record(0);
  EXPECT_DOUBLE_EQ(monitor.hz(), 0.0);  // 单样本无法估计
}

TEST(TopicRateMonitor, DropsSamplesOutsideWindow)
{
  data_recorder::TopicRateMonitor monitor(1.0);
  // 老样本（应被丢弃）
  monitor.record(0);
  monitor.record(100'000'000LL);
  // 新样本，远在窗口外（5s 后），间隔 0.05s（20Hz）
  for (int i = 0; i < 20; ++i) {
    monitor.record(5'000'000'000LL + static_cast<int64_t>(i) * 50'000'000LL);
  }
  EXPECT_DOUBLE_EQ(monitor.hz(), 20.0);
}

TEST(TopicRateMonitor, ZeroSpanFromDuplicateStampsYieldsZero)
{
  data_recorder::TopicRateMonitor monitor(1.0);
  monitor.record(5'000'000'000LL);
  monitor.record(5'000'000'000LL);  // 同一时刻两条 → span=0
  EXPECT_DOUBLE_EQ(monitor.hz(), 0.0);
}
