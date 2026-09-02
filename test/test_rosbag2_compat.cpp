#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <rclcpp/qos.hpp>

#include "data_recorder/rosbag2_compat.hpp"

namespace
{

struct HumbleTopicMetadata
{
  std::string offered_qos_profiles;
};

struct JazzyTopicMetadata
{
  std::vector<rclcpp::QoS> offered_qos_profiles;
};

struct HumbleBagMessage
{
  int64_t time_stamp{0};
};

struct JazzyBagMessage
{
  int64_t recv_timestamp{0};
  int64_t send_timestamp{0};
};

void expect_test_qos(const data_recorder::rosbag2_compat::Rosbag2QoS & qos)
{
  const auto & profile = qos.get_rmw_qos_profile();
  EXPECT_EQ(profile.depth, 7u);
  EXPECT_EQ(profile.reliability, RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
  EXPECT_EQ(profile.durability, RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
}

}  // namespace

TEST(Rosbag2Compat, SupportsHumbleAndJazzyTopicMetadataShapes)
{
  const auto qos = rclcpp::QoS(rclcpp::KeepLast(7)).best_effort().transient_local();
  const auto serialized = data_recorder::rosbag2_compat::serialize_qos_profiles({qos});

  HumbleTopicMetadata humble;
  data_recorder::rosbag2_compat::set_topic_qos_profiles(humble, serialized);
  EXPECT_EQ(humble.offered_qos_profiles, serialized);
  const auto humble_profiles = data_recorder::rosbag2_compat::topic_qos_profiles(humble);
  ASSERT_EQ(humble_profiles.size(), 1u);
  expect_test_qos(humble_profiles.front());

  JazzyTopicMetadata jazzy;
  data_recorder::rosbag2_compat::set_topic_qos_profiles(jazzy, serialized);
  ASSERT_EQ(jazzy.offered_qos_profiles.size(), 1u);
  const auto jazzy_profiles = data_recorder::rosbag2_compat::topic_qos_profiles(jazzy);
  ASSERT_EQ(jazzy_profiles.size(), 1u);
  expect_test_qos(jazzy_profiles.front());
}

TEST(Rosbag2Compat, SupportsHumbleAndJazzyTimestampShapes)
{
  constexpr int64_t kTimestamp = 123456789;

  HumbleBagMessage humble;
  data_recorder::rosbag2_compat::set_message_timestamp(humble, kTimestamp);
  EXPECT_EQ(humble.time_stamp, kTimestamp);
  EXPECT_EQ(data_recorder::rosbag2_compat::message_timestamp(humble), kTimestamp);

  JazzyBagMessage jazzy;
  data_recorder::rosbag2_compat::set_message_timestamp(jazzy, kTimestamp);
  EXPECT_EQ(jazzy.recv_timestamp, kTimestamp);
  EXPECT_EQ(jazzy.send_timestamp, kTimestamp);
  EXPECT_EQ(data_recorder::rosbag2_compat::message_timestamp(jazzy), kTimestamp);
}

TEST(Rosbag2Compat, SupportsInstalledRosbag2Types)
{
  constexpr int64_t kTimestamp = 987654321;
  const auto qos = rclcpp::QoS(rclcpp::KeepLast(7)).best_effort().transient_local();
  const auto serialized = data_recorder::rosbag2_compat::serialize_qos_profiles({qos});

  rosbag2_storage::TopicMetadata metadata;
  data_recorder::rosbag2_compat::set_topic_qos_profiles(metadata, serialized);
  const auto profiles = data_recorder::rosbag2_compat::topic_qos_profiles(metadata);
  ASSERT_EQ(profiles.size(), 1u);
  expect_test_qos(profiles.front());

  rosbag2_storage::SerializedBagMessage message;
  data_recorder::rosbag2_compat::set_message_timestamp(message, kTimestamp);
  EXPECT_EQ(data_recorder::rosbag2_compat::message_timestamp(message), kTimestamp);
}
