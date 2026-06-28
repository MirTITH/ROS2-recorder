#include <gtest/gtest.h>

#include <memory>

#include <rclcpp/serialization.hpp>
#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <trajectory_msgs/msg/joint_trajectory.hpp>

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

namespace
{
template<class T>
rclcpp::SerializedMessage serialize(const T & msg)
{
  rclcpp::Serialization<T> ser;
  rclcpp::SerializedMessage out;
  ser.serialize_message(&msg, &out);
  return out;
}

double value_of(const std::vector<data_recorder::ExtractedSample> & s, const std::string & key)
{
  for (const auto & e : s) {
    if (e.series_key == key) { return e.value; }
  }
  ADD_FAILURE() << "series_key not found: " << key;
  return 0.0;
}
}  // namespace

TEST(BuiltinExtractors, JointStatePairsByName)
{
  data_recorder::ValueExtractorRegistry reg;
  data_recorder::register_builtin_extractors(reg);
  ASSERT_TRUE(reg.has("sensor_msgs/msg/JointState"));

  sensor_msgs::msg::JointState msg;
  msg.name = {"a", "b"};
  msg.position = {1.0, 2.0};
  msg.velocity = {0.5};  // 只有 a 有速度
  const auto out = reg.get("sensor_msgs/msg/JointState")->extract(serialize(msg));

  EXPECT_DOUBLE_EQ(value_of(out, "pos/a"), 1.0);
  EXPECT_DOUBLE_EQ(value_of(out, "pos/b"), 2.0);
  EXPECT_DOUBLE_EQ(value_of(out, "vel/a"), 0.5);
  for (const auto & e : out) {
    EXPECT_NE(e.series_key, "vel/b");
    EXPECT_NE(e.series_key, "eff/a");
  }
}

TEST(BuiltinExtractors, WrenchStampedSixAxes)
{
  data_recorder::ValueExtractorRegistry reg;
  data_recorder::register_builtin_extractors(reg);
  geometry_msgs::msg::WrenchStamped msg;
  msg.wrench.force.x = 1.0; msg.wrench.force.y = 2.0; msg.wrench.force.z = 3.0;
  msg.wrench.torque.x = 4.0; msg.wrench.torque.y = 5.0; msg.wrench.torque.z = 6.0;
  const auto out = reg.get("geometry_msgs/msg/WrenchStamped")->extract(serialize(msg));
  ASSERT_EQ(out.size(), 6u);
  EXPECT_DOUBLE_EQ(value_of(out, "force.x"), 1.0);
  EXPECT_DOUBLE_EQ(value_of(out, "force.z"), 3.0);
  EXPECT_DOUBLE_EQ(value_of(out, "torque.y"), 5.0);
}

TEST(BuiltinExtractors, JointTrajectoryUsesLastPoint)
{
  data_recorder::ValueExtractorRegistry reg;
  data_recorder::register_builtin_extractors(reg);
  trajectory_msgs::msg::JointTrajectory msg;
  msg.joint_names = {"a", "b"};
  trajectory_msgs::msg::JointTrajectoryPoint p0, p1;
  p0.positions = {0.0, 0.0};
  p1.positions = {7.0, 8.0};
  msg.points = {p0, p1};
  const auto out = reg.get("trajectory_msgs/msg/JointTrajectory")->extract(serialize(msg));
  EXPECT_DOUBLE_EQ(value_of(out, "cmd/a"), 7.0);
  EXPECT_DOUBLE_EQ(value_of(out, "cmd/b"), 8.0);
}

TEST(BuiltinExtractors, JointTrajectoryEmptyPointsYieldsNothing)
{
  data_recorder::ValueExtractorRegistry reg;
  data_recorder::register_builtin_extractors(reg);
  trajectory_msgs::msg::JointTrajectory msg;
  msg.joint_names = {"a"};
  const auto out = reg.get("trajectory_msgs/msg/JointTrajectory")->extract(serialize(msg));
  EXPECT_TRUE(out.empty());
}
