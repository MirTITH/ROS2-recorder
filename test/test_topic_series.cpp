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
