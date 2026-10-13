#include "data_recorder/rosbag_writer.hpp"

#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_cpp/writers/sequential_writer.hpp>
#include <rosbag2_storage/default_storage_id.hpp>
#include <rosbag2_storage/metadata_io.hpp>
#include <rosbag2_storage/serialized_bag_message.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <rosbag2_storage/topic_metadata.hpp>
#include <rosbag2_storage/storage_interfaces/read_write_interface.hpp>

#include <pluginlib/class_loader.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "data_recorder/rosbag2_compat.hpp"

namespace data_recorder
{

struct RosbagMetadataWriteStatus
{
  bool failed{false};
  std::string error;
};

namespace
{
// Humble 的默认 MetadataIo 不检查输出流状态。通过 SequentialWriter 的注入接口，
// 在库实际提交 metadata 时检查普通文件写入；失败只记录，不向 close/析构抛异常。
class CheckedMetadataIo : public rosbag2_storage::MetadataIo
{
public:
  explicit CheckedMetadataIo(std::shared_ptr<RosbagMetadataWriteStatus> status)
  : status_(std::move(status)) {}

  void write_metadata(
    const std::string & uri, const rosbag2_storage::BagMetadata & metadata) noexcept override
  {
    if (status_->failed) { return; }
    try {
      // serialize_metadata 只返回 BagMetadata 字段；write_metadata 的文件格式另有外层键。
      // 保持 rosbag2 Reader 使用的原有 schema，不能直接把序列化字符串作为整个文件。
      YAML::Node root;
      root["rosbag2_bagfile_information"] = YAML::Load(serialize_metadata(metadata));
      const auto contents = YAML::Dump(root);
      const auto path = std::filesystem::path(uri) / metadata_filename;
      std::ofstream output;
      output.exceptions(std::ofstream::failbit | std::ofstream::badbit);
      output.open(path, std::ios::binary | std::ios::trunc);
      output << contents;
      output.flush();
      output.close();
    } catch (const std::exception & error) {
      fail(uri, error.what());
    } catch (...) {
      fail(uri, "未知异常");
    }
  }

private:
  void fail(const std::string & uri, const char * detail) noexcept
  {
    status_->failed = true;
    try {
      status_->error = uri + "/" + metadata_filename + ": " + detail;
    } catch (...) {
      // 记录错误本身也可能因分配失败而抛异常；failed 仍保证结果不会是假成功。
    }
  }

  std::shared_ptr<RosbagMetadataWriteStatus> status_;
};

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
: bag_dir_(bag_dir)
{
  try {
    resolved_storage_id_ = resolve_storage(storage_id);
    metadata_status_ = std::make_shared<RosbagMetadataWriteStatus>();
    auto sequential_writer = std::make_unique<rosbag2_cpp::writers::SequentialWriter>(
      std::make_unique<rosbag2_storage::StorageFactory>(),
      std::make_shared<rosbag2_cpp::SerializationFormatConverterFactory>(),
      std::make_unique<CheckedMetadataIo>(metadata_status_));
    writer_ = std::make_unique<rosbag2_cpp::Writer>(std::move(sequential_writer));
    rosbag2_storage::StorageOptions storage_options;
    storage_options.uri = bag_dir;
    storage_options.storage_id = resolved_storage_id_;
    writer_->open(storage_options);
    open_ = true;
  } catch (const std::exception & e) {
    record_error("打开", e.what());
  } catch (...) {
    record_error("打开", "未知异常");
  }
}

RosbagWriter::~RosbagWriter() noexcept
{
  try {
    close();
  } catch (...) {
    // 析构只负责兜底；正常保存结果由显式 close() 的调用者处理。
  }
}

void RosbagWriter::record_error(const std::string & operation, const std::string & detail)
{
  if (error_message_.empty()) {
    error_message_ = "rosbag " + operation + "失败: " + bag_dir_ + ": " + detail;
    std::cerr << "[RosbagWriter] " << error_message_ << "\n";
  }
}

void RosbagWriter::require_open()
{
  if (!open_ && error_message_.empty()) {
    record_error("写入", "writer 已关闭");
  }
  if (!error_message_.empty()) {
    throw std::runtime_error(error_message_);
  }
}

void RosbagWriter::add_topic(
  const std::string & name, const std::string & type, const std::string & offered_qos_profiles)
{
  require_open();
  try {
    rosbag2_storage::TopicMetadata metadata;
    metadata.name = name;
    metadata.type = type;
    metadata.serialization_format = "cdr";
    rosbag2_compat::set_topic_qos_profiles(metadata, offered_qos_profiles);
    writer_->create_topic(metadata);
  } catch (const std::exception & e) {
    record_error("注册话题 " + name, e.what());
    throw;
  } catch (...) {
    record_error("注册话题 " + name, "未知异常");
    throw;
  }
}

void RosbagWriter::write(
  const std::string & topic, const rclcpp::SerializedMessage & msg, int64_t time_stamp_ns)
{
  require_open();
  try {
    auto bag_msg = std::make_shared<rosbag2_storage::SerializedBagMessage>();
    bag_msg->topic_name = topic;
    rosbag2_compat::set_message_timestamp(*bag_msg, time_stamp_ns);

    const auto & rcl_msg = msg.get_rcl_serialized_message();
    bag_msg->serialized_data = std::shared_ptr<rcutils_uint8_array_t>(
      new rcutils_uint8_array_t,
      [](rcutils_uint8_array_t * arr) {
        const rcutils_ret_t ret = rcutils_uint8_array_fini(arr);
        (void)ret;
        delete arr;
      });
    *bag_msg->serialized_data = rcutils_get_zero_initialized_uint8_array();
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    const rcutils_ret_t init_ret =
      rcutils_uint8_array_init(bag_msg->serialized_data.get(), rcl_msg.buffer_length, &allocator);
    if (init_ret != RCUTILS_RET_OK) {
      throw std::runtime_error(
              "failed to allocate rosbag message buffer: rcutils error " +
              std::to_string(init_ret));
    }
    std::memcpy(bag_msg->serialized_data->buffer, rcl_msg.buffer, rcl_msg.buffer_length);
    bag_msg->serialized_data->buffer_length = rcl_msg.buffer_length;

    writer_->write(bag_msg);
  } catch (const std::exception & e) {
    record_error("写入话题 " + topic, e.what());
    throw;
  } catch (...) {
    record_error("写入话题 " + topic, "未知异常");
    throw;
  }
}

bool RosbagWriter::close()
{
  if (writer_ && open_) {
    // 在调用前关闭准入；即使 close 抛异常，重复收尾也不会掩盖首次失败。
    open_ = false;
    try {
      writer_->close();
    } catch (const std::exception & e) {
      record_error("关闭", e.what());
    } catch (...) {
      record_error("关闭", "未知异常");
    }
  }
  writer_.reset();
  if (metadata_status_ && metadata_status_->failed) {
    record_error("保存 metadata.yaml", metadata_status_->error.empty() ?
      "无法保存 bag 元数据" : metadata_status_->error);
  }
  open_ = false;
  return error_message_.empty();
}

}  // namespace data_recorder
