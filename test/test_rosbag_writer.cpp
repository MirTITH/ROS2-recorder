#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <std_msgs/msg/string.hpp>

#include "data_recorder/rosbag2_compat.hpp"
#include "data_recorder/rosbag_writer.hpp"

namespace fs = std::filesystem;

TEST(RosbagWriter, WritesMessagesReadableByRosbag2Reader)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_rosbag_test";
  fs::remove_all(tmp);
  const std::string bag_dir = (tmp / "rosbag").string();

  // 序列化几条 std_msgs/String
  rclcpp::Serialization<std_msgs::msg::String> serializer;
  const auto offered_qos = rclcpp::QoS(rclcpp::KeepLast(7)).best_effort().transient_local();
  const auto serialized_qos =
    data_recorder::rosbag2_compat::serialize_qos_profiles({offered_qos});
  {
    data_recorder::RosbagWriter writer(bag_dir, /*storage_id=*/"");  // 空=默认存储
    writer.add_topic("/chatter", "std_msgs/msg/String", serialized_qos);
    for (int i = 0; i < 5; ++i) {
      std_msgs::msg::String msg;
      msg.data = "hello " + std::to_string(i);
      rclcpp::SerializedMessage serialized;
      serializer.serialize_message(&msg, &serialized);
      writer.write("/chatter", serialized, /*time_stamp_ns=*/1000LL + i);
    }
    writer.close();
  }

  // 用 rosbag2 Reader 读回
  rosbag2_cpp::Reader reader;
  reader.open(bag_dir);
  const auto topics = reader.get_all_topics_and_types();
  ASSERT_EQ(topics.size(), 1u);
  const auto persisted_profiles =
    data_recorder::rosbag2_compat::topic_qos_profiles(topics.front());
  ASSERT_EQ(persisted_profiles.size(), 1u);
  const auto & persisted_qos = persisted_profiles.front();
  EXPECT_EQ(persisted_qos.get_rmw_qos_profile().depth, 7u);
  EXPECT_EQ(
    persisted_qos.get_rmw_qos_profile().reliability,
    RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
  EXPECT_EQ(
    persisted_qos.get_rmw_qos_profile().durability,
    RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);

  int count = 0;
  while (reader.has_next()) {
    auto bag_msg = reader.read_next();
    EXPECT_EQ(bag_msg->topic_name, "/chatter");
    ++count;
  }
  EXPECT_EQ(count, 5);

  fs::remove_all(tmp);
}
