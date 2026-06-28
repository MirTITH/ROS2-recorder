#include <gtest/gtest.h>

#include "data_recorder/recording_time.hpp"

TEST(RecordingTime, RelativeSecondsClampBeforeStart)
{
  EXPECT_DOUBLE_EQ(data_recorder::relative_seconds(900, 1000), 0.0);
  EXPECT_DOUBLE_EQ(data_recorder::relative_seconds(1000, 1000), 0.0);
}

TEST(RecordingTime, RelativeSecondsUsesNanosecondPrecision)
{
  EXPECT_DOUBLE_EQ(data_recorder::relative_seconds(1'000'000'001, 1'000'000'000), 0.000000001);
  EXPECT_DOUBLE_EQ(data_recorder::relative_seconds(1'033'000'000, 1'000'000'000), 0.033);
}
