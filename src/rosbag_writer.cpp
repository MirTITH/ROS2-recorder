#include "data_recorder/rosbag_writer.hpp"

#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_cpp/writers/sequential_writer.hpp>
#include <rosbag2_storage/default_storage_id.hpp>
#include <rosbag2_storage/serialized_bag_message.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <rosbag2_storage/topic_metadata.hpp>
#include <rosbag2_storage/storage_interfaces/read_write_interface.hpp>

#include <pluginlib/class_loader.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace data_recorder
{

namespace
{
// mcap 若已注册则优先，否则回退默认存储。
std::string resolve_storage(const std::string & requested)
{
  if (!requested.empty()) {
    return requested;
  }
  std::vector<std::string> writers;
  try {
    // 经 pluginlib 列出已声明的可读写存储插件（与 StorageFactory 内部同源）。
    pluginlib::ClassLoader<rosbag2_storage::storage_interfaces::ReadWriteInterface> loader(
      "rosbag2_storage",
      "rosbag2_storage::storage_interfaces::ReadWriteInterface");
    writers = loader.getDeclaredClasses();
  } catch (const std::exception & e) {
    std::cerr << "[RosbagWriter] 枚举存储插件失败: " << e.what() << "\n";
  }
  if (std::find(writers.begin(), writers.end(), "mcap") != writers.end()) {
    return "mcap";
  }
  // get_default_storage_id 必然已安装
  return rosbag2_storage::get_default_storage_id();
}
}  // namespace

RosbagWriter::RosbagWriter(const std::string & bag_dir, const std::string & storage_id)
{
  resolved_storage_id_ = resolve_storage(storage_id);
  try {
    writer_ = std::make_unique<rosbag2_cpp::Writer>();
    rosbag2_storage::StorageOptions storage_options;
    storage_options.uri = bag_dir;
    storage_options.storage_id = resolved_storage_id_;
    writer_->open(storage_options);
    open_ = true;
  } catch (const std::exception & e) {
    std::cerr << "[RosbagWriter] 打开失败: " << e.what() << "\n";
    open_ = false;
  }
}

RosbagWriter::~RosbagWriter()
{
  close();
}

void RosbagWriter::add_topic(
  const std::string & name, const std::string & type, const std::string & offered_qos_profiles)
{
  if (!open_) { return; }
  rosbag2_storage::TopicMetadata metadata;
  metadata.name = name;
  metadata.type = type;
  metadata.serialization_format = "cdr";
  metadata.offered_qos_profiles = offered_qos_profiles;
  writer_->create_topic(metadata);
}

void RosbagWriter::write(
  const std::string & topic, const rclcpp::SerializedMessage & msg, int64_t time_stamp_ns)
{
  if (!open_) { return; }
  auto bag_msg = std::make_shared<rosbag2_storage::SerializedBagMessage>();
  bag_msg->topic_name = topic;
  bag_msg->time_stamp = time_stamp_ns;

  const auto & rcl_msg = msg.get_rcl_serialized_message();
  bag_msg->serialized_data = std::shared_ptr<rcutils_uint8_array_t>(
    new rcutils_uint8_array_t,
    [](rcutils_uint8_array_t * arr) {
      rcutils_uint8_array_fini(arr);
      delete arr;
    });
  *bag_msg->serialized_data = rcutils_get_zero_initialized_uint8_array();
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rcutils_uint8_array_init(bag_msg->serialized_data.get(), rcl_msg.buffer_length, &allocator);
  std::memcpy(bag_msg->serialized_data->buffer, rcl_msg.buffer, rcl_msg.buffer_length);
  bag_msg->serialized_data->buffer_length = rcl_msg.buffer_length;

  writer_->write(bag_msg);
}

void RosbagWriter::close()
{
  if (writer_ && open_) {
    writer_.reset();  // 析构写 metadata.yaml
  }
  open_ = false;
}

}  // namespace data_recorder
