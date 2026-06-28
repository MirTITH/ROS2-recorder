#include <gtest/gtest.h>

#include <memory>

#include "data_recorder/value_extractor.hpp"

namespace
{
// 测试用假 extractor：忽略输入，返回一条固定样本。
class FakeExtractor : public data_recorder::ValueExtractor
{
public:
  std::vector<data_recorder::ExtractedSample> extract(
    const rclcpp::SerializedMessage &) const override
  {
    return {{"fake", 42.0}};
  }
};
}  // namespace

TEST(ValueExtractorRegistry, HasAndGet)
{
  data_recorder::ValueExtractorRegistry reg;
  EXPECT_FALSE(reg.has("some/Type"));
  EXPECT_EQ(reg.get("some/Type"), nullptr);

  reg.register_extractor("some/Type", std::make_unique<FakeExtractor>());
  EXPECT_TRUE(reg.has("some/Type"));
  ASSERT_NE(reg.get("some/Type"), nullptr);

  rclcpp::SerializedMessage dummy;
  const auto samples = reg.get("some/Type")->extract(dummy);
  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0].series_key, "fake");
  EXPECT_DOUBLE_EQ(samples[0].value, 42.0);
}

TEST(ValueExtractorRegistry, RegisterBuiltinExtractorsDoesNotCrash)
{
  data_recorder::ValueExtractorRegistry reg;
  EXPECT_NO_FATAL_FAILURE(data_recorder::register_builtin_extractors(reg));
}
