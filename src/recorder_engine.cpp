#include "data_recorder/recorder_engine.hpp"

#include <QImage>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <rclcpp/serialization.hpp>
#include <rosbag2_transport/qos.hpp>
#include <yaml-cpp/yaml.h>

#include "data_recorder/live_bridge.hpp"
#include "data_recorder/path_utils.hpp"
#include "data_recorder/session_manager.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace data_recorder
{

namespace
{
std::string file_name_for_topic(const std::string & topic)
{
  return file_base_for_topic(topic);
}

std::string timestamp_now()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
  return os.str();
}
}  // namespace

RecorderEngine::RecorderEngine(
  rclcpp::Node::SharedPtr node, ConfigData config,
  LiveBridge * bridge, SessionManager * session_manager)
: node_(node), config_(std::move(config)), bridge_(bridge), session_manager_(session_manager)
{
  setup_subscriptions();

  stats_timer_ = node_->create_wall_timer(500ms, [this]() {
    std::vector<TopicStats> stats;
    {
      std::lock_guard<std::mutex> lock(rate_mutex_);
      for (auto & [topic, monitor] : rate_monitors_) {
        TopicStats s;
        s.topic_key = topic;
        s.hz = monitor.hz();
        {
          std::lock_guard<std::mutex> dlock(dims_mutex_);
          auto d = image_dims_.find(topic);
          if (d != image_dims_.end()) { s.width = d->second.first; s.height = d->second.second; }
        }
        stats.push_back(s);
      }
    }
    if (bridge_) { bridge_->push_stats(stats); }
  });

  record_start_steady_ns_.store(
    std::chrono::steady_clock::now().time_since_epoch().count());
  live_edge_timer_ = node_->create_wall_timer(33ms, [this]() {
    const int64_t now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
    const int64_t elapsed_ns = now_ns - record_start_steady_ns_.load();
    const double seconds = static_cast<double>(elapsed_ns) / 1e9;
    live_edge_seconds_.store(seconds);
    if (bridge_) { bridge_->set_live_edge(seconds); }
  });
}

RecorderEngine::~RecorderEngine()
{
  if (recording_.load()) {
    stop_session({}, {});
  }
}

std::string RecorderEngine::offered_qos_for(const std::string & topic) const
{
  const auto endpoints = node_->get_publishers_info_by_topic(topic);
  if (endpoints.empty()) { return ""; }
  std::vector<rosbag2_transport::Rosbag2QoS> profiles;
  for (const auto & ep : endpoints) {
    profiles.emplace_back(ep.qos_profile());
  }
  YAML::Node node;
  node = profiles;
  return YAML::Dump(node);
}

void RecorderEngine::setup_subscriptions()
{
  for (const auto & topic : config_.topics) {
    {
      std::lock_guard<std::mutex> lock(rate_mutex_);
      rate_monitors_.emplace(topic.topic_name, TopicRateMonitor(1.0));
    }
    if (topic.backend_name == "video" ||
      topic.ui_category == TopicUiCategory::CameraPreview)
    {
      const std::string topic_name = topic.topic_name;
      auto sub = node_->create_subscription<sensor_msgs::msg::Image>(
        topic_name, rclcpp::SensorDataQoS(),
        [this, topic_name](sensor_msgs::msg::Image::ConstSharedPtr msg) {
          on_image_message(topic_name, msg);
        });
      subscriptions_.push_back(sub);
    } else {
      const std::string topic_name = topic.topic_name;
      // 类型在订阅时可能未知；用发布者通告的类型。
      std::string type;
      const auto eps = node_->get_publishers_info_by_topic(topic_name);
      if (!eps.empty()) { type = eps.front().topic_type(); }
      // 暂无发布者，跳过（spec v1：简单处理）。注意：启动后才出现发布者的话题不会被订阅（v1 限制，非 bug）。
      if (type.empty()) { continue; }
      // 适配发布者 QoS，否则 BEST_EFFORT 话题订阅不上（RELIABLE 订阅匹配不到 BEST_EFFORT 发布者）。
      // 与 offered_qos_for() 记录的 QoS 保持一致；保留较深的 KeepLast(100) 队列深度。
      auto qos = rclcpp::QoS(rclcpp::KeepLast(100));
      if (!eps.empty()) {
        const auto pub_qos = eps.front().qos_profile();
        qos.reliability(pub_qos.reliability());
        qos.durability(pub_qos.durability());
      }
      auto sub = node_->create_generic_subscription(
        topic_name, type, qos,
        [this, topic_name, type](std::shared_ptr<rclcpp::SerializedMessage> msg) {
          on_rosbag_message(topic_name, type, msg);
        });
      subscriptions_.push_back(sub);
    }
  }
}

void RecorderEngine::on_rosbag_message(
  const std::string & topic, const std::string & /*type*/,
  std::shared_ptr<rclcpp::SerializedMessage> msg)
{
  const int64_t now_ns = node_->now().nanoseconds();
  {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    auto it = rate_monitors_.find(topic);
    if (it != rate_monitors_.end()) { it->second.record(now_ns); }
  }
  if (!recording_.load()) { return; }

  // 在锁内只取 queue/writer 的 shared_ptr 本地副本，随即释放 session_mutex_，
  // 再做可能阻塞的 push（Block 背压满时会等 space_cv_）。这样即便 writer 卡住，
  // stop_session 仍能拿到 session_mutex_ 调 rosbag_queue_->stop() 解除阻塞。
  // 本地副本保活 queue/writer 直到本次 push 返回；任务闭包另持 writer 副本，
  // 保证 worker 线程执行 write 时 writer 仍存活。
  std::shared_ptr<WriterQueue<std::function<void()>>> queue;
  std::shared_ptr<RosbagWriter> writer;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    queue = rosbag_queue_;
    writer = rosbag_writer_;
  }
  if (queue && writer) {
    // 拷贝序列化消息进闭包（生命周期安全），入队到 writer 线程。
    auto serialized = std::make_shared<rclcpp::SerializedMessage>(*msg);
    const std::string topic_copy = topic;
    queue->push([writer, topic_copy, serialized, now_ns]() {
      writer->write(topic_copy, *serialized, now_ns);
    });
  }
}

void RecorderEngine::on_image_message(
  const std::string & topic, sensor_msgs::msg::Image::ConstSharedPtr msg)
{
  const int64_t header_ns =
    static_cast<int64_t>(msg->header.stamp.sec) * 1'000'000'000LL + msg->header.stamp.nanosec;
  const int64_t now_ns = node_->now().nanoseconds();
  const int64_t stamp_ns = header_ns != 0 ? header_ns : now_ns;

  {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    auto it = rate_monitors_.find(topic);
    if (it != rate_monitors_.end()) { it->second.record(now_ns); }
  }
  {
    std::lock_guard<std::mutex> lock(dims_mutex_);
    image_dims_[topic] = {static_cast<int>(msg->width), static_cast<int>(msg->height)};
  }

  // 预览：转 QImage（bgr8 用 Format_BGR888；其余转 RGB888）
  if (bridge_ && (msg->encoding == "bgr8" || msg->encoding == "rgb8" || msg->encoding == "mono8")) {
    QImage::Format fmt = msg->encoding == "bgr8" ? QImage::Format_BGR888
      : msg->encoding == "mono8" ? QImage::Format_Grayscale8
      : QImage::Format_RGB888;
    QImage img(msg->data.data(), msg->width, msg->height, msg->step, fmt);
    // 深拷贝，脱离 msg 生命周期。
    bridge_->push_frame(QString::fromStdString(topic),
      std::make_shared<QImage>(img.copy()));
  }

  if (!recording_.load()) { return; }
  std::lock_guard<std::mutex> lock(session_mutex_);
  auto sit = video_sinks_.find(topic);
  if (sit != video_sinks_.end() && sit->second && sit->second->queue) {
    ImageFrame frame;
    frame.width = static_cast<int>(msg->width);
    frame.height = static_cast<int>(msg->height);
    frame.step = static_cast<int>(msg->step);
    frame.encoding = msg->encoding;
    frame.ros_stamp_ns = stamp_ns;
    frame.data = msg->data;  // 拷贝
    sit->second->queue->push(std::move(frame));
  }
}

std::string RecorderEngine::start_session()
{
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (recording_.load()) { return session_id_; }

  const std::string output_dir = fs::absolute(config_.output_dir).string();
  std::error_code ec;
  fs::create_directories(output_dir, ec);

  session_id_ = timestamp_now();
  session_dir_ = session_manager_->create_session_directory(output_dir, session_id_);

  // rosbag writer + 队列（阻塞背压）
  rosbag_writer_ = std::make_shared<RosbagWriter>(
    (fs::path(session_dir_) / "rosbag").string(), /*storage_id=*/"");
  if (!rosbag_writer_->is_open()) {
    std::cerr << "[RecorderEngine] rosbag writer 打开失败，取消录制\n";
    rosbag_writer_.reset();
    return "";
  }
  for (const auto & topic : config_.topics) {
    if (topic.backend_name != "video" && topic.ui_category != TopicUiCategory::CameraPreview) {
      const auto eps = node_->get_publishers_info_by_topic(topic.topic_name);
      std::string type = eps.empty() ? "" : eps.front().topic_type();
      if (!type.empty()) {
        rosbag_writer_->add_topic(topic.topic_name, type, offered_qos_for(topic.topic_name));
      }
    }
  }
  rosbag_queue_ = std::make_shared<WriterQueue<std::function<void()>>>(
    2000, OverflowPolicy::Block, [](std::function<void()> task) { task(); });

  // video sinks（丢最旧背压）。recorder 在首帧到达时懒建（尺寸首帧才知）。
  const fs::path video_dir = fs::path(session_dir_) / "video";
  fs::create_directories(video_dir, ec);
  for (const auto & topic : config_.topics) {
    if (topic.backend_name == "video" || topic.ui_category == TopicUiCategory::CameraPreview) {
      const std::string key = topic.topic_name;
      const std::string base = file_name_for_topic(topic.topic_name);
      VideoParams params;
      auto pit = topic.params.find("codec"); if (pit != topic.params.end()) params.codec = pit->second;
      pit = topic.params.find("preset"); if (pit != topic.params.end()) params.preset = pit->second;
      pit = topic.params.find("crf");
      if (pit != topic.params.end()) {
        // crf 来自用户可改的 YAML 标量，无数值校验；非法值不应让 start_session 抛出。
        try {
          size_t consumed = 0;
          const int crf = std::stoi(pit->second, &consumed);
          if (consumed == pit->second.size()) {
            params.crf = crf;
          } else {
            std::cerr << "[RecorderEngine] crf 含非数字字符（\"" << pit->second
                      << "\"），用默认值 " << params.crf << "\n";
          }
        } catch (const std::exception & e) {
          std::cerr << "[RecorderEngine] crf 解析失败（\"" << pit->second
                    << "\"）：" << e.what() << "，用默认值 " << params.crf << "\n";
        }
      }
      pit = topic.params.find("pix_fmt"); if (pit != topic.params.end()) params.pix_fmt = pit->second;
      pit = topic.params.find("container"); if (pit != topic.params.end()) params.container = pit->second;

      auto sink = std::make_shared<VideoSink>();
      sink->video_path = (video_dir / (base + "." + params.container)).string();
      sink->csv_path = (video_dir / (base + ".csv")).string();
      sink->params = params;
      auto * sink_raw = sink.get();  // 队列 worker 线程在 sink 生命周期内运行
      sink->queue = std::make_unique<WriterQueue<ImageFrame>>(
        90, OverflowPolicy::DropOldest,
        [sink_raw](ImageFrame frame) {
          if (!sink_raw->recorder) {
            sink_raw->recorder = std::make_unique<VideoRecorder>(
              sink_raw->video_path, sink_raw->csv_path, frame.width, frame.height, sink_raw->params);
          }
          sink_raw->recorder->encode(frame);
        });
      video_sinks_[key] = sink;
    }
  }

  record_start_steady_ns_.store(
    std::chrono::steady_clock::now().time_since_epoch().count());
  record_start_unix_ = std::chrono::duration<double>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  record_start_ros_ns_ = node_->now().nanoseconds();
  live_edge_seconds_.store(0.0);

  recording_.store(true);
  return session_id_;
}

SessionRecord RecorderEngine::stop_session(
  const std::vector<AnnotationRecord> & annotations, const std::vector<TagRecord> & tags)
{
  std::lock_guard<std::mutex> lock(session_mutex_);
  SessionRecord record;
  if (!recording_.load()) { return record; }
  recording_.store(false);

  // 停 video sinks：排空帧队列 → recorder flush + 写 trailer/CSV
  for (auto & [key, sink] : video_sinks_) {
    if (sink->queue) { sink->queue->stop(); }
    if (sink->recorder) { sink->recorder->close(); }
  }
  video_sinks_.clear();

  // 停 rosbag 队列再关 writer
  if (rosbag_queue_) { rosbag_queue_->stop(); rosbag_queue_.reset(); }
  if (rosbag_writer_) { rosbag_writer_->close(); rosbag_writer_.reset(); }

  record.session_id = session_id_;
  record.directory = session_dir_;
  record.unix_time = record_start_unix_;
  record.ros_time_ns = record_start_ros_ns_;
  record.duration_seconds = live_edge_seconds_.load();
  for (const auto & t : config_.topics) {
    record.topics.push_back({t.topic_name,
      (t.backend_name == "video" || t.ui_category == TopicUiCategory::CameraPreview) ? "video" : "rosbag"});
  }
  record.tags = tags;
  record.annotations = annotations;

  session_manager_->write_session_yaml(record);
  return record;
}

double RecorderEngine::live_edge_seconds() const
{
  return live_edge_seconds_.load();
}

}  // namespace data_recorder
