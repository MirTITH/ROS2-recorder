#include "data_recorder/recorder_engine.hpp"

#include <QImage>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sstream>

#include <rclcpp/serialization.hpp>

#include "data_recorder/live_bridge.hpp"
#include "data_recorder/path_utils.hpp"
#include "data_recorder/recording_time.hpp"
#include "data_recorder/rosbag2_compat.hpp"
#include "data_recorder/session_manager.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace data_recorder
{

namespace
{
constexpr std::size_t kLiveCurveBufferMaxPoints = std::numeric_limits<std::size_t>::max();

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

rclcpp::QoS subscription_qos(const QosConfig & config)
{
  rclcpp::QoS qos = config.history == QosHistory::KeepAll ?
    rclcpp::QoS(rclcpp::KeepAll()) :
    rclcpp::QoS(rclcpp::KeepLast(config.depth));

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

std::string offered_qos_for_node(rclcpp::Node & node, const std::string & topic)
{
  const auto endpoints = node.get_publishers_info_by_topic(topic);
  if (endpoints.empty()) { return ""; }
  std::vector<rclcpp::QoS> profiles;
  for (const auto & ep : endpoints) {
    profiles.emplace_back(ep.qos_profile());
  }
  return rosbag2_compat::serialize_qos_profiles(profiles);
}
}  // namespace

RecorderEngine::RecorderEngine(
  rclcpp::Node::SharedPtr node, ConfigData config,
  LiveBridge * bridge, SessionManager * session_manager)
: node_(node), config_(std::move(config)), bridge_(bridge), session_manager_(session_manager)
{
  setup_subscriptions();
  register_builtin_extractors(extractor_registry_);

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

  // 发现竞态补订：构造时（spin 前、node 建好仅数毫秒）DDS 发现远未收敛，很多发布者还没被
  // 看到。setup_subscriptions 把订不上的话题放入 pending_topics_，这里周期性补订；持续运行
  // 还能捕获录制开始后才启动/重启的发布者。
  resubscribe_timer_ = node_->create_wall_timer(500ms, [this]() { try_subscribe_pending(); });

  live_edge_timer_ = node_->create_wall_timer(33ms, [this]() {
    std::lock_guard<std::mutex> lock(session_mutex_);
    if (!active_session_ || !active_session_->accepting.load()) { return; }
    const double seconds = relative_seconds(steady_now_ns(), active_session_->start_steady_ns);
    live_edge_seconds_.store(seconds);
    if (bridge_) { bridge_->set_live_edge(seconds); }
  });

  // 数值曲线快照推送（5 Hz）：把每 topic 的折叠点 + （仅展开 topic 的）抽稀曲线打成 QVariant
  // 交 LiveBridge。背压：上一帧未被 GUI 消费前不推下一帧，防事件堆积致 UI 渐冻。
  curves_timer_ = node_->create_wall_timer(200ms, [this]() {
    if (!bridge_) { return; }
    if (curves_in_flight_.load()) { return; }  // 背压：GUI 还没消费上一帧

    QVariantList topics;
    QVariantList new_types;
    {
      std::lock_guard<std::mutex> lock(series_mutex_);
      for (auto & [topic, type] : topic_types_) {
        if (announced_types_.insert(topic).second) {
          QVariantMap tm;
          tm.insert("topicKey", QString::fromStdString(topic));
          tm.insert("rosType", QString::fromStdString(type));
          new_types.push_back(tm);
        }
      }
      // 折叠点抽稀到 ~屏幕像素量级；series 仅对展开 topic 发，亦受预算约束。
      topics = build_curve_payload(
        series_, expanded_topics_, /*dot_budget=*/600, /*series_budget=*/600);
    }
    if (!new_types.isEmpty()) { bridge_->push_topic_types(new_types); }
    if (!topics.isEmpty()) {
      // 仅当 push 实际被接受（非回放抑制）才武装背压，否则标志会永久卡住。
      if (bridge_->push_curves(topics)) { curves_in_flight_.store(true); }
    }
  });
}

RecorderEngine::~RecorderEngine()
{
  // Controller/main 正常退出时显式停录；这里只用已同步的内存快照兜底。
  try { stop_session(); }
  catch (const std::exception & error) {
    std::cerr << "[RecorderEngine] 退出收尾失败: " << error.what() << "\n";
  } catch (...) {
    std::cerr << "[RecorderEngine] 退出收尾失败\n";
  }
}

std::string RecorderEngine::offered_qos_for(const std::string & topic) const
{
  return offered_qos_for_node(*node_, topic);
}

void RecorderEngine::setup_subscriptions()
{
  for (const auto & topic : config_.topics) {
    {
      std::lock_guard<std::mutex> lock(rate_mutex_);
      rate_monitors_.emplace(topic.topic_name, TopicRateMonitor(1.0));
    }
    if (topic.backend_name == "video") {
      // qos_explicit 的话题不依赖发布者是否已被发现，可立即订阅；否则可能要等 try_subscribe_pending 补订。
      if (!subscribe_video_topic(topic)) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_video_topics_.insert(topic.topic_name);
      }
    } else {
      // 此刻（spin 前）发布者多半还没被发现而订不上；放入 pending，由 resubscribe_timer_ 补订。
      if (!subscribe_rosbag_topic(topic.topic_name)) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_topics_.insert(topic.topic_name);
      }
    }
  }
}

bool RecorderEngine::subscribe_video_topic(const TopicEntry & topic)
{
  rclcpp::QoS qos = subscription_qos(topic.qos);
  if (!topic.qos_explicit) {
    // 未显式配置 QoS：和 rosbag2 一致,跟随真实发布者。没发布者就订不上，留给补订。
    const auto eps = node_->get_publishers_info_by_topic(topic.topic_name);
    if (eps.empty()) { return false; }
    qos = rosbag2_compat::Rosbag2QoS::adapt_request_to_offers(topic.topic_name, eps);
  }
  const std::string topic_name = topic.topic_name;
  auto sub = node_->create_subscription<sensor_msgs::msg::Image>(
    topic_name, qos,
    [this, topic_name](sensor_msgs::msg::Image::ConstSharedPtr msg) {
      on_image_message(topic_name, msg);
    });
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    subscriptions_.push_back(sub);
  }
  return true;
}

bool RecorderEngine::subscribe_rosbag_topic(const std::string & topic_name)
{
  // 类型在订阅时可能未知；用发布者通告的类型。没有发布者就订不上，返回 false 等补订。
  const auto eps = node_->get_publishers_info_by_topic(topic_name);
  if (eps.empty()) { return false; }
  const std::string type = eps.front().topic_type();
  if (type.empty()) { return false; }
  // 使用 rosbag2 的适配规则兼容同一话题的全部发布者，而不是只取发现列表的第一个端点。
  // 保留较深的 KeepLast(100) 队列；TRANSIENT_LOCAL 会让 /tf_static 的历史样本被回放。
  auto qos = rosbag2_compat::Rosbag2QoS::adapt_request_to_offers(topic_name, eps);
  qos.keep_last(100);
  const bool transient_local =
    qos.get_rmw_qos_profile().durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  auto sub = node_->create_generic_subscription(
    topic_name, type, qos,
    [this, topic_name, type, transient_local](std::shared_ptr<rclcpp::SerializedMessage> msg) {
      on_rosbag_message(topic_name, type, transient_local, msg);
    });
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    subscriptions_.push_back(sub);
  }
  return true;
}

void RecorderEngine::try_subscribe_pending()
{
  std::vector<std::string> todo;
  std::vector<std::string> video_todo;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    todo.assign(pending_topics_.begin(), pending_topics_.end());
    video_todo.assign(pending_video_topics_.begin(), pending_video_topics_.end());
  }
  for (const auto & topic_name : todo) {
    if (subscribe_rosbag_topic(topic_name)) {
      {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_topics_.erase(topic_name);
      }
      std::cerr << "[RecorderEngine] 补订成功: " << topic_name << "\n";
    }
  }
  for (const auto & topic_name : video_todo) {
    const auto entry = std::find_if(
      config_.topics.begin(), config_.topics.end(),
      [&topic_name](const TopicEntry & t) { return t.topic_name == topic_name; });
    if (entry != config_.topics.end() && subscribe_video_topic(*entry)) {
      {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_video_topics_.erase(topic_name);
      }
      std::cerr << "[RecorderEngine] 补订成功: " << topic_name << "\n";
    }
  }
}

void RecorderEngine::on_rosbag_message(
  const std::string & topic, const std::string & type,
  bool transient_local, std::shared_ptr<rclcpp::SerializedMessage> msg)
{
  auto initial_session = accepting_session();
  const int64_t now_ns = node_->now().nanoseconds();
  const int64_t now_steady_ns = steady_now_ns();
  {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    auto it = rate_monitors_.find(topic);
    if (it != rate_monitors_.end()) { it->second.record(now_ns); }
  }

  // 类型发现独立于录制状态，便于 UI 在开录前就知道哪些 topic 可展开。
  {
    std::lock_guard<std::mutex> lock(series_mutex_);
    topic_types_.emplace(topic, type);
  }

  // 相机预览独立于存储后端。反序列化仅用于预览/尺寸统计，写 bag 仍使用原始序列化消息。
  if (type == "sensor_msgs/msg/Image") {
    sensor_msgs::msg::Image image;
    rclcpp::Serialization<sensor_msgs::msg::Image> serializer;
    serializer.deserialize_message(msg.get(), &image);
    update_image_preview(topic, image);
  }

  // /tf_static 等 TRANSIENT_LOCAL 话题通常只发布一次。长期订阅会在开录前就消费 DDS
  // 回放的历史样本，因此缓存最近的消息，供每个新会话写入。
  // 在缓存锁内选择当前会话：若新会话已读取缓存，当前样本由回调补写；否则它会在
  // 开录时从缓存写入。持久状态可以跨会话补写，普通消息仍只使用回调开始时的会话。
  auto session = transient_local ? std::shared_ptr<ActiveSession>() : initial_session;
  if (transient_local) {
    std::lock_guard<std::mutex> lock(transient_cache_mutex_);
    auto & cached_messages = transient_cache_[topic];
    constexpr std::size_t kTransientCacheDepth = 100;
    if (cached_messages.size() == kTransientCacheDepth) {
      cached_messages.pop_front();
    }
    cached_messages.push_back({type, std::make_shared<rclcpp::SerializedMessage>(*msg)});
    session = accepting_session();
  }

  if (!session || !session->accepting.load()) { return; }

  // 数值曲线：录制中记折叠点；可绘制类型则反序列化提取标量。用每条消息到达时的
  // steady-clock 相对时间做 x 轴，避免高频 topic 共用 33ms live-edge timer 时间而画出竖线尖峰。
  // 这里不能使用 TopicSeries 默认 20000 点环形缓冲，否则高频 topic 在几分钟内就会只剩尾部。
  const double t_seconds = relative_seconds(now_steady_ns, session->start_steady_ns);
  {
    std::lock_guard<std::mutex> lock(series_mutex_);
    if (!session->accepting.load()) { return; }
    auto [buffer_it, inserted] = series_.try_emplace(topic, kLiveCurveBufferMaxPoints);
    (void)inserted;
    auto & buffer = buffer_it->second;
    buffer.add_message_time(t_seconds);
    const ValueExtractor * extractor = extractor_registry_.get(type);
    if (extractor != nullptr) {
      for (const auto & sample : extractor->extract(*msg)) {
        buffer.add_sample(sample.series_key, t_seconds, sample.value);
      }
    }
  }

  if (!session->accepting.load()) { return; }
  const auto writer = session->rosbag_writer;
  const auto queue = session->rosbag_queue;
  const auto serialized = std::make_shared<rclcpp::SerializedMessage>(*msg);
  const int64_t bag_stamp_ns = transient_local ? std::max(now_ns, session->record.ros_time_ns) : now_ns;
  // 任务只保留本会话的 writer 和登记集合。不会在停录后查找下一会话的资源。
  auto * registered = &session->registered_topics;
  queue->push([writer, registered, node = node_, topic, type, serialized, bag_stamp_ns]() {
    // 只有首次登记才查询 ROS graph 并序列化 QoS，避免占用每条消息的订阅回调时间。
    if (registered->insert(topic).second) {
      writer->add_topic(topic, type, offered_qos_for_node(*node, topic));
    }
    writer->write(topic, *serialized, bag_stamp_ns);
  });
}

void RecorderEngine::on_image_message(
  const std::string & topic, sensor_msgs::msg::Image::ConstSharedPtr msg)
{
  const auto session = accepting_session();
  const int64_t header_ns =
    static_cast<int64_t>(msg->header.stamp.sec) * 1'000'000'000LL + msg->header.stamp.nanosec;
  const int64_t now_ns = node_->now().nanoseconds();

  {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    auto it = rate_monitors_.find(topic);
    if (it != rate_monitors_.end()) { it->second.record(now_ns); }
  }
  update_image_preview(topic, *msg);

  if (!session || !session->accepting.load()) { return; }
  auto sit = session->video_sinks.find(topic);
  if (sit != session->video_sinks.end() && sit->second && sit->second->queue) {
    ImageFrame frame;
    frame.width = static_cast<int>(msg->width);
    frame.height = static_cast<int>(msg->height);
    frame.step = static_cast<int>(msg->step);
    frame.encoding = msg->encoding;
    frame.is_bigendian = msg->is_bigendian;
    frame.recv_stamp_ns = now_ns;
    frame.header_stamp_ns = header_ns;
    frame.frame_id = msg->header.frame_id;
    frame.data = msg->data;  // 拷贝
    sit->second->queue->push(std::move(frame));
  }
}

void RecorderEngine::update_image_preview(
  const std::string & topic, const sensor_msgs::msg::Image & image)
{
  {
    std::lock_guard<std::mutex> lock(dims_mutex_);
    image_dims_[topic] = {static_cast<int>(image.width), static_cast<int>(image.height)};
  }

  // 预览：转 QImage（bgr8 用 Format_BGR888；其余转 RGB888），不选择录制后端。
  if (bridge_ && (image.encoding == "bgr8" || image.encoding == "rgb8" || image.encoding == "mono8")) {
    QImage::Format fmt = image.encoding == "bgr8" ? QImage::Format_BGR888
      : image.encoding == "mono8" ? QImage::Format_Grayscale8
      : QImage::Format_RGB888;
    QImage img(image.data.data(), image.width, image.height, image.step, fmt);
    // 深拷贝，脱离消息生命周期。
    bridge_->push_frame(QString::fromStdString(topic),
      std::make_shared<QImage>(img.copy()));
  }
}

std::shared_ptr<RecorderEngine::ActiveSession> RecorderEngine::accepting_session() const
{
  std::lock_guard<std::mutex> lock(session_mutex_);
  return active_session_ && active_session_->accepting.load() ? active_session_ : nullptr;
}

void RecorderEngine::update_session_metadata(
  const std::vector<AnnotationRecord> & annotations, const std::vector<TagRecord> & tags)
{
  const auto session = accepting_session();
  if (!session) { return; }
  std::lock_guard<std::mutex> lock(session->metadata_mutex);
  if (!session->accepting.load()) { return; }
  session->record.annotations = annotations;
  session->record.tags = tags;
}

namespace
{
std::string exception_message(const std::exception_ptr & error)
{
  if (!error) { return {}; }
  try { std::rethrow_exception(error); }
  catch (const std::exception & e) { return e.what(); }
  catch (...) { return "未知写入异常"; }
}
}  // namespace

std::string RecorderEngine::recording_error() const
{
  const auto session = accepting_session();
  if (!session) { return {}; }
  if (auto error = session->rosbag_queue->error()) {
    return "rosbag: " + exception_message(error);
  }
  for (const auto & [topic, sink] : session->video_sinks) {
    if (auto error = sink->queue->error()) {
      return topic + ": " + exception_message(error);
    }
  }
  return {};
}

SessionStartResult RecorderEngine::start_session()
{
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  if (const auto session = accepting_session()) { return {session->record.session_id, {}}; }

  std::shared_ptr<ActiveSession> session;
  bool directory_created = false;
  try {
    session = std::make_shared<ActiveSession>();
    if (!session_manager_) { throw std::runtime_error("缺少会话管理器"); }
    const fs::path output_dir = fs::absolute(config_.output_dir);
    std::error_code ec;
    fs::create_directories(output_dir, ec);
    if (ec) { throw fs::filesystem_error("无法创建输出目录", output_dir, ec); }
    if (!fs::is_directory(output_dir)) {
      throw std::runtime_error("输出路径不是目录: " + output_dir.string());
    }

    // 独占新目录；同一秒内重复录制也不会复用上一会话。
    const std::string base_id = timestamp_now();
    for (unsigned suffix = 0; ; ++suffix) {
      session->record.session_id = base_id + (suffix ? "_" + std::to_string(suffix) : "");
      const fs::path candidate = output_dir / session->record.session_id;
      if (fs::exists(candidate)) { continue; }
      try {
        session->record.directory = session_manager_->create_session_directory(
          output_dir.string(), session->record.session_id);
        break;
      } catch (const fs::filesystem_error & e) {
        if (e.code() == std::errc::file_exists) { continue; }
        throw;
      }
    }
    directory_created = true;
    session->record.recorder_version = DATA_RECORDER_VERSION;
    session->rosbag_writer = std::make_shared<RosbagWriter>(
      (fs::path(session->record.directory) / "rosbag").string(), "");
    if (!session->rosbag_writer->is_open()) {
      throw std::runtime_error(session->rosbag_writer->error_message());
    }
    for (const auto & topic : config_.topics) {
      const bool video = topic.backend_name == "video";
      const auto offered_qos = offered_qos_for(topic.topic_name);
      session->record.topics.push_back({topic.topic_name, video ? "video" : "rosbag",
        video ? offered_qos : ""});
      if (!video) {
        const auto endpoints = node_->get_publishers_info_by_topic(topic.topic_name);
        if (!endpoints.empty() && !endpoints.front().topic_type().empty()) {
          session->rosbag_writer->add_topic(
            topic.topic_name, endpoints.front().topic_type(), offered_qos);
          session->registered_topics.insert(topic.topic_name);
        }
      }
    }
    session->rosbag_queue = std::make_shared<WriterQueue<std::function<void()>>>(
      2000, OverflowPolicy::Block, [](std::function<void()> task) { task(); });

    const fs::path video_dir = fs::path(session->record.directory) / "video";
    fs::create_directory(video_dir);
    for (const auto & topic : config_.topics) {
      if (topic.backend_name != "video") { continue; }
      auto sink = std::make_shared<VideoSink>();
      auto & params = sink->params;
      auto pit = topic.params.find("codec"); if (pit != topic.params.end()) params.codec = pit->second;
      pit = topic.params.find("preset"); if (pit != topic.params.end()) params.preset = pit->second;
      pit = topic.params.find("crf");
      if (pit != topic.params.end()) {
        try {
          size_t consumed = 0;
          const int crf = std::stoi(pit->second, &consumed);
          if (consumed == pit->second.size()) { params.crf = crf; }
          else { std::cerr << "[RecorderEngine] crf 无效，使用默认值\n"; }
        } catch (const std::exception &) {
          std::cerr << "[RecorderEngine] crf 无效，使用默认值\n";
        }
      }
      pit = topic.params.find("pix_fmt"); if (pit != topic.params.end()) params.pix_fmt = pit->second;
      pit = topic.params.find("container"); if (pit != topic.params.end()) params.container = pit->second;
      const auto base = file_name_for_topic(topic.topic_name);
      sink->video_path = (video_dir / (base + "." + params.container)).string();
      sink->csv_path = (video_dir / (base + ".csv")).string();
      auto * sink_raw = sink.get();
      sink->queue = std::make_unique<WriterQueue<ImageFrame>>(
        90, OverflowPolicy::DropOldest, [sink_raw](ImageFrame frame) {
          if (!sink_raw->recorder) {
            sink_raw->recorder = std::make_unique<VideoRecorder>(
              sink_raw->video_path, sink_raw->csv_path, frame.width, frame.height, sink_raw->params);
            if (!sink_raw->recorder->is_open()) {
              throw std::runtime_error(sink_raw->video_path + ": " +
                sink_raw->recorder->error_message());
            }
          }
          if (!sink_raw->recorder->encode(frame) &&
            !sink_raw->recorder->error_message().empty())
          {
            throw std::runtime_error(sink_raw->video_path + ": " +
              sink_raw->recorder->error_message());
          }
        });
      session->video_sinks.emplace(topic.topic_name, std::move(sink));
    }

    // 在缓存锁内写入已收到的 TRANSIENT_LOCAL 历史样本，然后才允许回调投递。
    std::lock_guard<std::mutex> cache_lock(transient_cache_mutex_);
    session->start_steady_ns = steady_now_ns();
    session->record.unix_time = std::chrono::duration<double>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    session->record.ros_time_ns = node_->now().nanoseconds();
    for (const auto & [topic, cached_messages] : transient_cache_) {
      for (const auto & cached : cached_messages) {
        if (session->registered_topics.insert(topic).second) {
          session->rosbag_writer->add_topic(topic, cached.type, offered_qos_for(topic));
        }
        session->rosbag_writer->write(topic, *cached.message, session->record.ros_time_ns);
      }
    }
    {
      std::lock_guard<std::mutex> series_lock(series_mutex_);
      series_.clear();
      topic_types_.clear();
      announced_types_.clear();
    }
    {
      std::lock_guard<std::mutex> lock(session_mutex_);
      live_edge_seconds_.store(0.0);
      session->accepting.store(true);
      active_session_ = session;
      recording_.store(true);
    }
    last_finish_ = {};
    return {session->record.session_id, {}};
  } catch (...) {
    const auto error = exception_message(std::current_exception());
    if (!session) { return {{}, error}; }
    // 局部会话从未发布给回调。停止全部已建队列后再释放 writers。
    if (session->rosbag_queue) { session->rosbag_queue->request_stop(); }
    for (const auto & [topic, sink] : session->video_sinks) { sink->queue->request_stop(); }
    if (session->rosbag_queue) { session->rosbag_queue->join(); }
    for (const auto & [topic, sink] : session->video_sinks) { sink->queue->join(); }
    if (session->rosbag_writer) {
      try { session->rosbag_writer->close(); } catch (...) {}
    }
    // 只删除本次新建、仍为空的目录；部分初始化产物保留，绝不触碰旧会话。
    std::error_code cleanup_error;
    if (directory_created && fs::is_empty(session->record.directory, cleanup_error)) {
      fs::remove(session->record.directory, cleanup_error);
    }
    std::cerr << "[RecorderEngine] 启动录制失败: " << error << "\n";
    return {{}, error};
  }
}

SessionFinishResult RecorderEngine::stop_session()
{
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  std::shared_ptr<ActiveSession> session;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session = std::move(active_session_);
    if (!session) { return last_finish_; }
    session->accepting.store(false);
    recording_.store(false);
    // 保存时长在切断准入时冻结，不计排队、flush 或 YAML 写入耗时。
    session->record.duration_seconds = relative_seconds(steady_now_ns(), session->start_steady_ns);
    live_edge_seconds_.store(session->record.duration_seconds);
  }
  SessionFinishResult result;
  {
    std::lock_guard<std::mutex> lock(session->metadata_mutex);
    result.record = session->record;
  }
  // 全部队列先关闭入口，解除 Block 生产者，再在 session_mutex_ 外 join。
  session->rosbag_queue->request_stop();
  for (const auto & [topic, sink] : session->video_sinks) { sink->queue->request_stop(); }
  session->rosbag_queue->join();
  for (const auto & [topic, sink] : session->video_sinks) { sink->queue->join(); }

  if (auto error = session->rosbag_queue->error()) {
    result.errors.push_back("rosbag 写入失败: " + exception_message(error));
  }
  for (const auto & [topic, sink] : session->video_sinks) {
    if (auto error = sink->queue->error()) {
      result.errors.push_back(topic + " 写入失败: " + exception_message(error));
    }
    try {
      if (sink->recorder && !sink->recorder->close()) {
        result.errors.push_back(topic + " 收尾失败: " + sink->recorder->error_message());
      }
    } catch (...) {
      result.errors.push_back(topic + " 收尾异常: " + exception_message(std::current_exception()));
    }
  }
  try {
    if (!session->rosbag_writer->close()) {
      result.errors.push_back("rosbag 收尾失败: " + session->rosbag_writer->error_message());
    }
  } catch (...) {
    result.errors.push_back("rosbag 收尾异常: " + exception_message(std::current_exception()));
  }
  std::string metadata_error;
  try {
    result.metadata_saved = session_manager_->write_session_yaml(result.record, &metadata_error);
  } catch (...) {
    metadata_error = "session.yaml 保存异常: " + exception_message(std::current_exception());
  }
  if (!result.metadata_saved) { result.errors.push_back(metadata_error); }
  for (const auto & error : result.errors) { std::cerr << "[RecorderEngine] " << error << "\n"; }
  last_finish_ = result;
  return result;
}

double RecorderEngine::live_edge_seconds() const
{
  return live_edge_seconds_.load();
}

void RecorderEngine::set_expanded_topics(const std::set<std::string> & topics)
{
  std::lock_guard<std::mutex> lock(series_mutex_);
  expanded_topics_ = topics;
}

void RecorderEngine::notify_curves_consumed()
{
  curves_in_flight_.store(false);
}

}  // namespace data_recorder
