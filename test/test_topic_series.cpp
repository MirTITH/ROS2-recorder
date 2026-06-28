#include <gtest/gtest.h>

#include <vector>

#include "data_recorder/topic_series.hpp"

using data_recorder::SeriesPoint;
using data_recorder::decimate;

TEST(Decimate, KeepsAllWhenUnderBudget)
{
  std::vector<SeriesPoint> in{{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}};
  const auto out = decimate(in, 10);
  EXPECT_EQ(out, in);
}

TEST(Decimate, ReducesToAtMostBudgetAndStaysTimeSorted)
{
  std::vector<SeriesPoint> in;
  for (int i = 0; i < 1000; ++i) {
    in.push_back({static_cast<double>(i), static_cast<double>(i % 7)});
  }
  const auto out = decimate(in, 100);
  EXPECT_LE(out.size(), 100u);
  EXPECT_GT(out.size(), 0u);
  for (std::size_t i = 1; i < out.size(); ++i) {
    EXPECT_LE(out[i - 1].first, out[i].first);  // 时间升序
  }
}

TEST(Decimate, PreservesSpike)
{
  std::vector<SeriesPoint> in;
  for (int i = 0; i < 1000; ++i) { in.push_back({static_cast<double>(i), 0.0}); }
  in[500].second = 999.0;  // 单点尖峰
  const auto out = decimate(in, 50);
  bool found = false;
  for (const auto & p : out) {
    if (p.second == 999.0) { found = true; }
  }
  EXPECT_TRUE(found) << "min/max 抽稀应保留尖峰";
}

TEST(Decimate, HandlesEmptyInput)
{
  EXPECT_TRUE(decimate({}, 10).empty());
}

TEST(Decimate, PassthroughAtExactBudget)
{
  std::vector<SeriesPoint> in{{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}};
  EXPECT_EQ(decimate(in, 3), in);  // size == budget: must not decimate
}

TEST(TopicSeries, RingBufferDropsOldest)
{
  data_recorder::TopicSeries ts(/*max_points=*/3);
  for (int i = 0; i < 5; ++i) {
    ts.add_sample("k", static_cast<double>(i), static_cast<double>(i));
  }
  const auto snap = ts.snapshot(/*budget=*/100);
  ASSERT_EQ(snap.size(), 1u);
  EXPECT_EQ(snap[0].key, "k");
  ASSERT_EQ(snap[0].points.size(), 3u);          // 仅留最近 3 个
  EXPECT_DOUBLE_EQ(snap[0].points.front().first, 2.0);  // 最旧 0,1 被丢
  EXPECT_DOUBLE_EQ(snap[0].points.back().first, 4.0);
}

TEST(TopicSeries, MessageTimesUniformDownsample)
{
  data_recorder::TopicSeries ts(/*max_points=*/1000);
  for (int i = 0; i < 100; ++i) {
    ts.add_message_time(static_cast<double>(i));
  }
  const auto few = ts.message_times(/*budget=*/10);
  EXPECT_LE(few.size(), 10u);
  EXPECT_GT(few.size(), 0u);
  for (std::size_t i = 1; i < few.size(); ++i) { EXPECT_LT(few[i - 1], few[i]); }
  EXPECT_DOUBLE_EQ(few.front(), 0.0);
  EXPECT_DOUBLE_EQ(few.back(), 99.0);

  const auto all = ts.message_times(/*budget=*/1000);
  EXPECT_EQ(all.size(), 100u);  // 不超预算则全返回
}

TEST(TopicSeries, SnapshotSortedByKey)
{
  data_recorder::TopicSeries ts;
  ts.add_sample("b", 0.0, 0.0);
  ts.add_sample("a", 0.0, 0.0);
  const auto snap = ts.snapshot(100);
  ASSERT_EQ(snap.size(), 2u);
  EXPECT_EQ(snap[0].key, "a");  // std::map 保证 key 升序
  EXPECT_EQ(snap[1].key, "b");
}

TEST(TopicSeries, MessageTimeRingDropsOldest)
{
  data_recorder::TopicSeries ts(/*max_points=*/3);
  for (int i = 0; i < 5; ++i) {
    ts.add_message_time(static_cast<double>(i));
  }
  const auto all = ts.message_times(/*budget=*/100);
  ASSERT_EQ(all.size(), 3u);
  EXPECT_DOUBLE_EQ(all.front(), 2.0);
  EXPECT_DOUBLE_EQ(all.back(), 4.0);
}

TEST(TopicSeries, ClearResets)
{
  data_recorder::TopicSeries ts;
  ts.add_message_time(1.0);
  ts.add_sample("k", 1.0, 0.0);
  ts.clear();
  EXPECT_TRUE(ts.message_times(100).empty());
  EXPECT_TRUE(ts.snapshot(100).empty());
}
