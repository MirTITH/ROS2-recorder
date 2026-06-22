#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "data_recorder/config_model.hpp"
#include "data_recorder/recorder_types.hpp"
#include "data_recorder/rosbag_writer.hpp"
#include "data_recorder/topic_rate_monitor.hpp"
#include "data_recorder/video_recorder.hpp"
#include "data_recorder/writer_queue.hpp"

namespace data_recorder
{

class LiveBridge;
class SessionManager;

// 录制引擎：持全部订阅、会话生命周期、扇出。运行在 ROS spin 线程。
class RecorderEngine
{
public:
  RecorderEngine(
    rclcpp::Node::SharedPtr node, ConfigData config,
    LiveBridge * bridge, SessionManager * session_manager);
  ~RecorderEngine();

  // 由 GUI 线程调（通过 AppController），内部用锁保护会话状态。
  // 返回 session_id（成功）或空串（失败，如 output_dir 不可写）。
  std::string start_session();
  // 写 annotations/tags 快照到 session.yaml；返回完成的 SessionRecord。
  SessionRecord stop_session(
    const std::vector<AnnotationRecord> & annotations,
    const std::vector<TagRecord> & tags);

  bool is_recording() const { return recording_.load(); }
  double live_edge_seconds() const;

private:
  void setup_subscriptions();
  void on_rosbag_message(
    const std::string & topic, const std::string & type,
    std::shared_ptr<rclcpp::SerializedMessage> msg);
  void on_image_message(
    const std::string & topic, sensor_msgs::msg::Image::ConstSharedPtr msg);
  std::string offered_qos_for(const std::string & topic) const;

  rclcpp::Node::SharedPtr node_;
  ConfigData config_;
  LiveBridge * bridge_;
  SessionManager * session_manager_;

  std::vector<rclcpp::SubscriptionBase::SharedPtr> subscriptions_;
  std::map<std::string, TopicRateMonitor> rate_monitors_;
  std::mutex rate_mutex_;
  std::map<std::string, std::pair<int, int>> image_dims_;  // topic -> (w,h)
  std::mutex dims_mutex_;

  // 一路相机的视频 sink：队列 + 懒建 recorder（首帧定尺寸）。
  struct VideoSink
  {
    std::unique_ptr<VideoRecorder> recorder;
    std::unique_ptr<WriterQueue<ImageFrame>> queue;
    std::string video_path;
    std::string csv_path;
    VideoParams params;
  };

  // 会话态（GUI start/stop 与 ROS 回调共享）
  std::mutex session_mutex_;
  std::atomic<bool> recording_{false};
  std::string session_dir_;
  std::string session_id_;
  std::chrono::steady_clock::time_point record_start_steady_;
  double record_start_unix_{0.0};
  int64_t record_start_ros_ns_{0};
  std::unique_ptr<RosbagWriter> rosbag_writer_;
  std::unique_ptr<WriterQueue<std::function<void()>>> rosbag_queue_;
  std::map<std::string, std::shared_ptr<VideoSink>> video_sinks_;

  // live edge / stats 定时器
  rclcpp::TimerBase::SharedPtr stats_timer_;
  rclcpp::TimerBase::SharedPtr live_edge_timer_;
  std::atomic<double> live_edge_seconds_{0.0};
};

}  // namespace data_recorder
