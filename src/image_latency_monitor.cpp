#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "data_recorder/config_model.hpp"

namespace data_recorder
{

namespace
{

rclcpp::QoS subscription_qos(const QosConfig & config)
{
  rclcpp::QoS qos = config.history == QosHistory::KeepAll ?
    rclcpp::QoS(rclcpp::KeepAll()) : rclcpp::QoS(rclcpp::KeepLast(config.depth));

  switch (config.reliability) {
    case QosReliability::Reliable:
      qos.reliable();
      break;
    case QosReliability::BestEffort:
      qos.best_effort();
      break;
    case QosReliability::SystemDefault:
      qos.reliability(RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT);
      break;
  }

  switch (config.durability) {
    case QosDurability::Volatile:
      qos.durability_volatile();
      break;
    case QosDurability::TransientLocal:
      qos.transient_local();
      break;
    case QosDurability::SystemDefault:
      qos.durability(RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT);
      break;
  }
  return qos;
}

struct Statistics
{
  void add(double value)
  {
    latest = value;
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
    ++count;
    const double delta = value - mean;
    mean += delta / static_cast<double>(count);
    squared_difference_sum += delta * (value - mean);
  }

  double standard_deviation() const
  {
    return count > 1 ?
      std::sqrt(squared_difference_sum / static_cast<double>(count - 1)) : 0.0;
  }

  std::uint64_t count{0};
  double latest{0.0};
  double mean{0.0};
  double squared_difference_sum{0.0};
  double minimum{std::numeric_limits<double>::infinity()};
  double maximum{-std::numeric_limits<double>::infinity()};
};

struct TopicStatistics
{
  Statistics interval;
  Statistics total;
  std::uint64_t received{0};
  std::uint64_t zero_stamps{0};
};

void print_usage(const char * program_name)
{
  std::cerr
    << "Usage:\n"
    << "  ros2 run data_recorder image_latency_monitor --ros-args\n"
    << "    -p config_file:=/path/to/recorder_config.yaml\n\n"
    << "The node monitors every image topic selected by the recorder configuration.\n"
    << "Optional parameter: report_period_sec (default: 1.0)\n\n"
    << "Program: " << program_name << std::endl;
}

}  // namespace

class ImageLatencyMonitor : public rclcpp::Node
{
public:
  explicit ImageLatencyMonitor(const ConfigData & config)
  : Node("image_latency_monitor")
  {
    const double report_period_sec = declare_parameter<double>("report_period_sec", 1.0);
    if (!std::isfinite(report_period_sec) || report_period_sec <= 0.0) {
      throw std::invalid_argument("report_period_sec must be a finite positive number");
    }

    for (const auto & topic : config.topics) {
      if (topic.ui_category != TopicUiCategory::CameraPreview) {
        continue;
      }
      if (statistics_.find(topic.topic_name) != statistics_.end()) {
        throw std::invalid_argument("duplicate image topic in config: " + topic.topic_name);
      }

      statistics_.emplace(topic.topic_name, TopicStatistics{});
      const auto topic_name = topic.topic_name;
      subscriptions_.push_back(create_subscription<sensor_msgs::msg::Image>(
        topic_name, subscription_qos(topic.qos),
        [this, topic_name](sensor_msgs::msg::Image::ConstSharedPtr message) {
          receive(topic_name, *message);
        }));
      RCLCPP_INFO(get_logger(), "Monitoring %s", topic_name.c_str());
    }

    if (subscriptions_.empty()) {
      throw std::invalid_argument("config contains no image topics");
    }

    report_timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(report_period_sec)),
      [this]() {report(false);});
  }

  void report(bool final)
  {
    for (auto & [topic, values] : statistics_) {
      if (values.interval.count == 0) {
        if (final && values.total.count > 0) {
          const auto & total = values.total;
          RCLCPP_INFO(
            get_logger(),
            "%s: latency_ms mean=%.3f stddev=%.3f min=%.3f max=%.3f "
            "total_frames=%lu zero_stamp=%lu [final]",
            topic.c_str(), total.mean, total.standard_deviation(), total.minimum, total.maximum,
            static_cast<unsigned long>(total.count), static_cast<unsigned long>(values.zero_stamps));
        } else if (final || values.received == 0) {
          RCLCPP_WARN(
            get_logger(), "%s: no valid stamped frames (received=%lu, zero_stamp=%lu)",
            topic.c_str(), static_cast<unsigned long>(values.received),
            static_cast<unsigned long>(values.zero_stamps));
        }
        continue;
      }

      const auto & window = values.interval;
      const auto & total = values.total;
      RCLCPP_INFO(
        get_logger(),
        "%s: latency_ms latest=%.3f mean=%.3f stddev=%.3f min=%.3f max=%.3f "
        "frames=%lu | total_mean=%.3f total_frames=%lu zero_stamp=%lu%s",
        topic.c_str(), window.latest, window.mean, window.standard_deviation(), window.minimum,
        window.maximum, static_cast<unsigned long>(window.count), total.mean,
        static_cast<unsigned long>(total.count), static_cast<unsigned long>(values.zero_stamps),
        final ? " [final]" : "");
      values.interval = Statistics{};
    }
  }

private:
  void receive(const std::string & topic, const sensor_msgs::msg::Image & message)
  {
    // Take the receive time at callback entry, before inspecting or copying image data.
    const std::int64_t receive_ns = now().nanoseconds();
    auto & values = statistics_.at(topic);
    ++values.received;

    const std::int64_t header_ns =
      static_cast<std::int64_t>(message.header.stamp.sec) * 1'000'000'000LL +
      static_cast<std::int64_t>(message.header.stamp.nanosec);
    if (header_ns == 0) {
      ++values.zero_stamps;
      return;
    }

    const double latency_ms = static_cast<double>(receive_ns - header_ns) / 1'000'000.0;
    values.interval.add(latency_ms);
    values.total.add(latency_ms);
  }

  std::map<std::string, TopicStatistics> statistics_;
  std::vector<rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr> subscriptions_;
  rclcpp::TimerBase::SharedPtr report_timer_;
};

}  // namespace data_recorder

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto parameter_node = std::make_shared<rclcpp::Node>("image_latency_monitor_config");
  parameter_node->declare_parameter<std::string>("config_file", "");
  const auto config_path = parameter_node->get_parameter("config_file").as_string();
  if (config_path.empty()) {
    data_recorder::print_usage(argv[0]);
    rclcpp::shutdown();
    return EXIT_FAILURE;
  }

  try {
    const auto config = data_recorder::ConfigModel().load_from_file(config_path);
    auto monitor = std::make_shared<data_recorder::ImageLatencyMonitor>(config);
    rclcpp::spin(monitor);
    monitor->report(true);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(rclcpp::get_logger("image_latency_monitor"), "%s", error.what());
    rclcpp::shutdown();
    return EXIT_FAILURE;
  }

  rclcpp::shutdown();
  return EXIT_SUCCESS;
}
