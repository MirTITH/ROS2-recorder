#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "data_recorder/config_model.hpp"
#include "data_recorder/recorder_types.hpp"
#include "data_recorder/rosbag_writer.hpp"
#include "data_recorder/topic_rate_monitor.hpp"
#include "data_recorder/topic_series.hpp"
#include "data_recorder/value_extractor.hpp"
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
  // 订阅一个 rosbag 话题；发布者尚未被发现则返回 false（留待 try_subscribe_pending 补订）。
  bool subscribe_rosbag_topic(const std::string & topic_name);
  // 周期性尝试补订 pending_topics_ 里还没订上的话题（spin 线程上的定时器调用）。
  void try_subscribe_pending();
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
  // 构造时发布者未被发现而未订上的 rosbag 话题；resubscribe_timer_ 持续补订。
  // pending_mutex_ 同时保护 pending_topics_ 与 subscriptions_ 的追加（构造线程 vs spin 线程）。
  std::set<std::string> pending_topics_;
  std::mutex pending_mutex_;
  std::map<std::string, TopicRateMonitor> rate_monitors_;
  std::mutex rate_mutex_;
  std::map<std::string, std::pair<int, int>> image_dims_;  // topic -> (w,h)
  std::mutex dims_mutex_;

  // 数值曲线核心：registry 判可绘制 + 每 topic 时间序列缓冲。
  ValueExtractorRegistry extractor_registry_;
  std::map<std::string, TopicSeries> series_;        // topic -> 缓冲
  std::map<std::string, std::string> topic_types_;   // topic -> ROS 类型（首见记录）
  std::set<std::string> announced_types_;            // 已推过类型的 topic（防重复）
  std::mutex series_mutex_;                           // 保护以上四者（spin 写，timer 读）

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
  // steady_clock 起点存为 atomic ns（live_edge_timer_ 在 ROS 线程读，start_session 在 GUI 线程写）。
  std::atomic<int64_t> record_start_steady_ns_{0};
  double record_start_unix_{0.0};
  int64_t record_start_ros_ns_{0};
  // shared_ptr：on_rosbag_message 可在 session_mutex_ 外持本地副本完成阻塞 push，
  // 即使 stop_session 已 reset 成员，本地副本仍保活 queue/writer 直到 push 返回。
  std::shared_ptr<RosbagWriter> rosbag_writer_;
  std::shared_ptr<WriterQueue<std::function<void()>>> rosbag_queue_;
  std::map<std::string, std::shared_ptr<VideoSink>> video_sinks_;
  // 本会话已在 writer 上 add_topic 过的话题（session_mutex_ 保护）。start_session 预登记开录时
  // 已有发布者的话题；录制中补订的话题在首帧时懒登记，靠此集合防重复 create_topic。
  std::set<std::string> registered_topics_;

  // live edge / stats 定时器
  rclcpp::TimerBase::SharedPtr stats_timer_;
  rclcpp::TimerBase::SharedPtr live_edge_timer_;
  // 数值曲线快照推送定时器（~5 Hz）。
  rclcpp::TimerBase::SharedPtr curves_timer_;
  // 发现竞态补订定时器（持续运行；pending 为空时近乎 no-op）。
  rclcpp::TimerBase::SharedPtr resubscribe_timer_;
  std::atomic<double> live_edge_seconds_{0.0};
};

}  // namespace data_recorder
