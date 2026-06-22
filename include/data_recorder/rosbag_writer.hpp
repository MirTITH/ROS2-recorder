#pragma once

#include <memory>
#include <string>

#include <rclcpp/serialized_message.hpp>

namespace rosbag2_cpp { class Writer; }

namespace data_recorder
{

// 包 rosbag2_cpp::Writer：默认存储（mcap 若可用，否则回退）；产物可被 ros2 bag 读取。
// 非线程安全——由单个 worker 线程使用。
class RosbagWriter
{
public:
  // storage_id 空 → 用 rosbag2_storage::get_default_storage_id()，但优先 mcap（若已注册）。
  RosbagWriter(const std::string & bag_dir, const std::string & storage_id);
  ~RosbagWriter();

  RosbagWriter(const RosbagWriter &) = delete;
  RosbagWriter & operator=(const RosbagWriter &) = delete;

  bool is_open() const { return open_; }
  const std::string & storage_id() const { return resolved_storage_id_; }

  void add_topic(
    const std::string & name, const std::string & type,
    const std::string & offered_qos_profiles);

  void write(
    const std::string & topic, const rclcpp::SerializedMessage & msg, int64_t time_stamp_ns);

  void close();

private:
  std::unique_ptr<rosbag2_cpp::Writer> writer_;
  std::string resolved_storage_id_;
  bool open_{false};
};

}  // namespace data_recorder
