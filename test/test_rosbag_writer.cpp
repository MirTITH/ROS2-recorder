#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <std_msgs/msg/string.hpp>

#include "data_recorder/rosbag_writer.hpp"

namespace fs = std::filesystem;

TEST(RosbagWriter, WritesMessagesReadableByRosbag2Reader)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_rosbag_test";
  fs::remove_all(tmp);
  const std::string bag_dir = (tmp / "rosbag").string();

  // 序列化几条 std_msgs/String
  rclcpp::Serialization<std_msgs::msg::String> serializer;
  {
    data_recorder::RosbagWriter writer(bag_dir, /*storage_id=*/"");  // 空=默认存储
    writer.add_topic("/chatter", "std_msgs/msg/String", /*offered_qos_profiles=*/"");
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
  int count = 0;
  while (reader.has_next()) {
    auto bag_msg = reader.read_next();
    EXPECT_EQ(bag_msg->topic_name, "/chatter");
    ++count;
  }
  EXPECT_EQ(count, 5);

  fs::remove_all(tmp);
}
