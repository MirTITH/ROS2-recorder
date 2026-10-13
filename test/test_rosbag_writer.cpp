#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>

#include <pluginlib/class_loader.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/storage_interfaces/read_write_interface.hpp>
#include <std_msgs/msg/string.hpp>
#include <yaml-cpp/yaml.h>

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
    ASSERT_TRUE(writer.is_open()) << writer.error_message();
    writer.add_topic("/chatter", "std_msgs/msg/String", serialized_qos);
    for (int i = 0; i < 5; ++i) {
      std_msgs::msg::String msg;
      msg.data = "hello " + std::to_string(i);
      rclcpp::SerializedMessage serialized;
      serializer.serialize_message(&msg, &serialized);
      writer.write("/chatter", serialized, /*time_stamp_ns=*/1000LL + i);
    }
    EXPECT_TRUE(writer.close()) << writer.error_message();
    EXPECT_TRUE(writer.close());
    EXPECT_TRUE(writer.error_message().empty());
  }

  const auto root = YAML::LoadFile((fs::path(bag_dir) / "metadata.yaml").string());
  ASSERT_TRUE(root["rosbag2_bagfile_information"]);
  EXPECT_EQ(root["rosbag2_bagfile_information"]["message_count"].as<uint64_t>(), 5u);

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

TEST(RosbagWriter, ExplicitMcapMetadataRemainsReadableAfterCloseAndDestruction)
{
  pluginlib::ClassLoader<rosbag2_storage::storage_interfaces::ReadWriteInterface> loader(
    "rosbag2_storage", "rosbag2_storage::storage_interfaces::ReadWriteInterface");
  const auto plugins = loader.getDeclaredClasses();
  if (std::find(plugins.begin(), plugins.end(), "mcap") == plugins.end()) {
    GTEST_SKIP() << "MCAP storage plugin is not installed";
  }
  const fs::path tmp = fs::temp_directory_path() / "dr_rosbag_test_explicit_mcap";
  fs::remove_all(tmp);
  rclcpp::Serialization<std_msgs::msg::String> serializer;
  {
    data_recorder::RosbagWriter writer(tmp.string(), "mcap");
    ASSERT_TRUE(writer.is_open()) << writer.error_message();
    ASSERT_EQ(writer.storage_id(), "mcap");
    writer.add_topic("/chatter", "std_msgs/msg/String", "");
    for (int i = 0; i < 5; ++i) {
      std_msgs::msg::String message;
      message.data = "explicit MCAP " + std::to_string(i);
      rclcpp::SerializedMessage serialized;
      serializer.serialize_message(&message, &serialized);
      writer.write("/chatter", serialized, 1000LL + i);
    }
    ASSERT_TRUE(writer.close()) << writer.error_message();
    ASSERT_TRUE(writer.close()) << writer.error_message();
    const auto root = YAML::LoadFile((tmp / "metadata.yaml").string());
    ASSERT_TRUE(root["rosbag2_bagfile_information"]);
    EXPECT_EQ(root["rosbag2_bagfile_information"]["storage_identifier"].as<std::string>(), "mcap");
    EXPECT_EQ(root["rosbag2_bagfile_information"]["message_count"].as<uint64_t>(), 5u);
  }
  const auto root = YAML::LoadFile((tmp / "metadata.yaml").string());
  ASSERT_TRUE(root["rosbag2_bagfile_information"]);
  {
    rosbag2_cpp::Reader reader;
    ASSERT_NO_THROW(reader.open(tmp.string()));
    int count = 0;
    while (reader.has_next()) {
      const auto bag_message = reader.read_next();
      EXPECT_EQ(bag_message->topic_name, "/chatter");
      rclcpp::SerializedMessage serialized(*bag_message->serialized_data);
      std_msgs::msg::String message;
      serializer.deserialize_message(&serialized, &message);
      EXPECT_EQ(message.data, "explicit MCAP " + std::to_string(count));
      ++count;
    }
    EXPECT_EQ(count, 5);
  }
  fs::remove_all(tmp);
}

TEST(RosbagWriter, OpenFailureRetainsReasonAndRejectsWrites)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_rosbag_test_open_failure";
  fs::remove_all(tmp);
  {
    data_recorder::RosbagWriter writer(tmp.string(), "nonexistent_test_storage_plugin");
    EXPECT_FALSE(writer.is_open());
    ASSERT_FALSE(writer.error_message().empty());
    const auto error = writer.error_message();
    EXPECT_THROW(writer.add_topic("/chatter", "std_msgs/msg/String", ""), std::runtime_error);
    rclcpp::SerializedMessage message;
    EXPECT_THROW(writer.write("/chatter", message, 0), std::runtime_error);
    EXPECT_FALSE(writer.close());
    EXPECT_FALSE(writer.close());
    EXPECT_EQ(writer.error_message(), error);
  }
  fs::remove_all(tmp);
}

TEST(RosbagWriter, WriteFailureIsReportedAndRetainedAtClose)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_rosbag_test_write_failure";
  fs::remove_all(tmp);
  {
    data_recorder::RosbagWriter writer(tmp.string(), "");
    ASSERT_TRUE(writer.is_open()) << writer.error_message();
    std_msgs::msg::String message;
    message.data = "unregistered topic";
    rclcpp::Serialization<std_msgs::msg::String> serializer;
    rclcpp::SerializedMessage serialized;
    serializer.serialize_message(&message, &serialized);
    EXPECT_THROW(writer.write("/not_registered", serialized, 0), std::exception);
    ASSERT_FALSE(writer.error_message().empty());
    const auto error = writer.error_message();
    EXPECT_FALSE(writer.close());
    EXPECT_FALSE(writer.close());
    EXPECT_EQ(writer.error_message(), error);
  }
  fs::remove_all(tmp);
}

TEST(RosbagWriter, ClosedWriterDoesNotSilentlyAcceptData)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_rosbag_test_closed";
  fs::remove_all(tmp);
  {
    data_recorder::RosbagWriter writer(tmp.string(), "");
    ASSERT_TRUE(writer.is_open()) << writer.error_message();
    ASSERT_TRUE(writer.close());
    rclcpp::SerializedMessage message;
    EXPECT_THROW(writer.write("/chatter", message, 0), std::runtime_error);
    EXPECT_FALSE(writer.error_message().empty());
    EXPECT_FALSE(writer.close());
  }
  fs::remove_all(tmp);
}

TEST(RosbagWriter, MetadataTargetDirectoryReportsFailureWithoutThrowing)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_rosbag_test_metadata_directory";
  fs::remove_all(tmp);
  {
    data_recorder::RosbagWriter writer(tmp.string(), "");
    ASSERT_TRUE(writer.is_open()) << writer.error_message();
    writer.add_topic("/chatter", "std_msgs/msg/String", "");
    fs::create_directory(tmp / "metadata.yaml");
    bool saved = true;
    EXPECT_NO_THROW(saved = writer.close());
    EXPECT_FALSE(saved);
    EXPECT_FALSE(writer.is_open());
    ASSERT_FALSE(writer.error_message().empty());
    EXPECT_NE(writer.error_message().find("metadata.yaml"), std::string::npos);
    const auto error = writer.error_message();
    EXPECT_FALSE(writer.close());
    EXPECT_EQ(writer.error_message(), error);
    EXPECT_TRUE(fs::is_directory(tmp / "metadata.yaml"));
  }  // 库析构不得因 metadata 写入失败而 terminate。
  fs::remove_all(tmp);
}

TEST(RosbagWriter, BufferedMetadataDiskFailureIsReportedWithoutThrowing)
{
  if (!fs::exists("/dev/full")) { GTEST_SKIP() << "requires /dev/full"; }
  const fs::path tmp = fs::temp_directory_path() / "dr_rosbag_test_metadata_disk_full";
  fs::remove_all(tmp);
  {
    data_recorder::RosbagWriter writer(tmp.string(), "");
    ASSERT_TRUE(writer.is_open()) << writer.error_message();
    writer.add_topic("/chatter", "std_msgs/msg/String", "");
    fs::create_symlink("/dev/full", tmp / "metadata.yaml");
    bool saved = true;
    EXPECT_NO_THROW(saved = writer.close());
    EXPECT_FALSE(saved);
    EXPECT_FALSE(writer.is_open());
    ASSERT_FALSE(writer.error_message().empty());
    EXPECT_NE(writer.error_message().find("metadata.yaml"), std::string::npos);
    const auto error = writer.error_message();
    EXPECT_FALSE(writer.close());
    EXPECT_EQ(writer.error_message(), error);
    EXPECT_TRUE(fs::is_symlink(tmp / "metadata.yaml"));
  }
  fs::remove_all(tmp);
}
