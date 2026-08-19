#include "data_recorder/player.hpp"

#include <QImage>

#include <rmw/rmw.h>
#include <rosbag2_cpp/converter_options.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/serialized_bag_message.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <rosbag2_transport/qos.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "data_recorder/path_utils.hpp"
#include "data_recorder/video_clip_reader.hpp"

namespace fs = std::filesystem;

namespace data_recorder
{
namespace
{

std::string normalized_topic(std::string topic)
{
  if (topic.empty()) { return topic; }
  if (topic.front() != '/') { topic.insert(topic.begin(), '/'); }
  while (topic.size() > 1 && topic.back() == '/') { topic.pop_back(); }
  return topic;
}

rclcpp::QoS publisher_qos_for_topic(const rosbag2_storage::TopicMetadata & topic)
{
  using rosbag2_transport::Rosbag2QoS;
  if (topic.offered_qos_profiles.empty()) { return Rosbag2QoS{}; }

  try {
    const auto profiles = YAML::Load(topic.offered_qos_profiles)
      .as<std::vector<Rosbag2QoS>>();
    return Rosbag2QoS::adapt_offer_to_recorded_offers(topic.name, profiles);
  } catch (const std::exception &) {
    return Rosbag2QoS{};
  }
}

}  // namespace

PlayerNode::PlayerNode(const rclcpp::NodeOptions & options)
: Node("player", options)
{
  session_directory_ = declare_parameter<std::string>("session_dir", "");
  rate_ = declare_parameter<double>("rate", 1.0);
  loop_ = declare_parameter<bool>("loop", false);
  paused_ = declare_parameter<bool>("start_paused", false);
  topic_prefix_ = normalized_topic(declare_parameter<std::string>("topic_prefix", ""));
  topic_filter_ = declare_parameter<std::vector<std::string>>(
    "topics", std::vector<std::string>{});
  publish_clock_enabled_ = declare_parameter<bool>("publish_clock", false);
  image_frame_id_ = declare_parameter<std::string>("image_frame_id", "");

  if (session_directory_.empty()) {
    throw std::invalid_argument("parameter 'session_dir' must name a recording session directory");
  }
  if (!std::isfinite(rate_) || rate_ <= 0.0) {
    throw std::invalid_argument("parameter 'rate' must be finite and greater than zero");
  }
  for (auto & topic : topic_filter_) { topic = normalized_topic(std::move(topic)); }

  std::error_code ec;
  const fs::path absolute_directory = fs::absolute(session_directory_, ec);
  if (!ec) { session_directory_ = absolute_directory.lexically_normal().string(); }

  if (publish_clock_enabled_) {
    clock_publisher_ = create_publisher<rosgraph_msgs::msg::Clock>("/clock", rclcpp::ClockQoS());
  }

  load_recording();
  build_event_queue();
  create_control_services();

  anchor_wall_ = std::chrono::steady_clock::now();
  RCLCPP_INFO(
    get_logger(),
    "Playing '%s': %zu bag messages, %zu video streams, %zu total events, rate %.3gx%s",
    session_directory_.c_str(), bag_messages_.size(), video_clips_.size(), events_.size(), rate_,
    paused_ ? " (paused)" : "");
  playback_thread_ = std::thread(&PlayerNode::playback_loop, this);
}

PlayerNode::~PlayerNode()
{
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    stop_requested_ = true;
  }
  state_cv_.notify_all();
  if (playback_thread_.joinable()) { playback_thread_.join(); }
}

bool PlayerNode::finished() const
{
  std::lock_guard<std::mutex> lock(state_mutex_);
  return finished_;
}

void PlayerNode::load_recording()
{
  const fs::path session_path(session_directory_);
  if (!fs::is_directory(session_path)) {
    throw std::runtime_error("session directory does not exist: " + session_directory_);
  }
  if (!fs::is_regular_file(session_path / "session.yaml")) {
    throw std::runtime_error("session.yaml is missing from: " + session_directory_);
  }

  try {
    const YAML::Node root = YAML::LoadFile((session_path / "session.yaml").string());
    if (root["recorded_at"]) {
      recorded_epoch_ns_ = root["recorded_at"]["ros_time_ns"].as<int64_t>(0);
    }
  } catch (const YAML::Exception & error) {
    throw std::runtime_error(std::string("could not read session.yaml: ") + error.what());
  }

  const fs::path bag_directory = session_path / "rosbag";
  if (fs::is_directory(bag_directory)) {
    load_bag(bag_directory.string());
  } else {
    RCLCPP_WARN(get_logger(), "No rosbag directory found in '%s'", session_directory_.c_str());
  }
  load_videos(session_directory_);
}

void PlayerNode::load_bag(const std::string & bag_directory)
{
  rosbag2_storage::StorageOptions storage_options;
  storage_options.uri = bag_directory;
  storage_options.storage_id = "";

  bag_reader_ = std::make_unique<rosbag2_cpp::Reader>();
  bag_reader_->open(
    storage_options,
    rosbag2_cpp::ConverterOptions{"", rmw_get_serialization_format()});

  const auto topic_metadata = bag_reader_->get_all_topics_and_types();
  for (const auto & topic : topic_metadata) {
    if (!topic_selected(topic.name)) { continue; }
    try {
      bag_publishers_.emplace(
        topic.name,
        create_generic_publisher(
          output_topic(topic.name), topic.type, publisher_qos_for_topic(topic)));
    } catch (const std::exception & error) {
      RCLCPP_WARN(
        get_logger(), "Skipping bag topic '%s' (%s): %s", topic.name.c_str(),
        topic.type.c_str(), error.what());
    }
  }

  while (bag_reader_->has_next()) {
    auto message = bag_reader_->read_next();
    if (!message || bag_publishers_.find(message->topic_name) == bag_publishers_.end()) {
      continue;
    }
    bag_messages_.push_back({std::move(message)});
  }
}

void PlayerNode::load_videos(const std::string & session_directory)
{
  const fs::path session_path(session_directory);
  YAML::Node root;
  try {
    root = YAML::LoadFile((session_path / "session.yaml").string());
  } catch (const YAML::Exception & error) {
    throw std::runtime_error(std::string("could not read session.yaml: ") + error.what());
  }

  if (!root["topics"] || !root["topics"].IsSequence()) { return; }
  const fs::path video_directory = session_path / "video";
  std::unordered_set<std::string> loaded_topics;

  for (const auto & item : root["topics"]) {
    if (item["backend"].as<std::string>("rosbag") != "video") { continue; }
    const std::string topic = normalized_topic(item["name"].as<std::string>(""));
    if (topic.empty() || !topic_selected(topic) || !loaded_topics.insert(topic).second) { continue; }

    const std::string base = file_base_for_topic(topic);
    auto reader = std::make_unique<VideoClipReader>();
    if (!reader->open(
        (video_directory / (base + ".mp4")).string(),
        (video_directory / (base + ".csv")).string()))
    {
      RCLCPP_WARN(get_logger(), "Skipping video topic '%s': MP4 or CSV could not be opened", topic.c_str());
      continue;
    }

    try {
      VideoClip clip;
      clip.topic = topic;
      clip.reader = std::move(reader);
      clip.publisher = create_publisher<sensor_msgs::msg::Image>(
        output_topic(topic), rclcpp::SensorDataQoS());
      video_clips_.push_back(std::move(clip));
    } catch (const std::exception & error) {
      RCLCPP_WARN(get_logger(), "Skipping video topic '%s': %s", topic.c_str(), error.what());
    }
  }
}

void PlayerNode::build_event_queue()
{
  std::uint64_t order = 0;
  events_.reserve(bag_messages_.size());
  for (std::size_t i = 0; i < bag_messages_.size(); ++i) {
    events_.push_back({
      Event::Kind::Bag, static_cast<int64_t>(bag_messages_[i].message->time_stamp), i, 0, order++});
  }
  for (std::size_t clip_index = 0; clip_index < video_clips_.size(); ++clip_index) {
    const auto frame_count = video_clips_[clip_index].reader->frame_count();
    events_.reserve(events_.size() + frame_count);
    for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
      events_.push_back({
        Event::Kind::Video,
        video_clips_[clip_index].reader->frame_stamp_ns(frame_index),
        clip_index, frame_index, order++});
    }
  }

  if (events_.empty()) {
    throw std::runtime_error("the selected recording contains no playable events");
  }
  std::stable_sort(
    events_.begin(), events_.end(),
    [](const Event & lhs, const Event & rhs) {
      if (lhs.stamp_ns != rhs.stamp_ns) { return lhs.stamp_ns < rhs.stamp_ns; }
      return lhs.order < rhs.order;
    });
  epoch_ns_ = events_.front().stamp_ns;
  if (recorded_epoch_ns_ > 0) {
    // 视频采集线程可能在会话起点前已经拿到第一帧；选更早的有效时间，
    // 使这些帧不会被截掉，同时仍以 session.yaml 的时间作为通常情况下的基准。
    epoch_ns_ = std::min(epoch_ns_, recorded_epoch_ns_);
  }
  duration_ns_ = std::max<int64_t>(0, events_.back().stamp_ns - epoch_ns_);
}

void PlayerNode::create_control_services()
{
  using Pause = rosbag2_interfaces::srv::Pause;
  pause_service_ = create_service<Pause>(
    "~/pause",
    [this](Pause::Request::ConstSharedPtr, Pause::Response::SharedPtr) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        pause_locked(std::chrono::steady_clock::now());
      }
      state_cv_.notify_all();
    });

  using Resume = rosbag2_interfaces::srv::Resume;
  resume_service_ = create_service<Resume>(
    "~/resume",
    [this](Resume::Request::ConstSharedPtr, Resume::Response::SharedPtr) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        resume_locked(std::chrono::steady_clock::now());
      }
      state_cv_.notify_all();
    });

  using TogglePaused = rosbag2_interfaces::srv::TogglePaused;
  toggle_paused_service_ = create_service<TogglePaused>(
    "~/toggle_paused",
    [this](TogglePaused::Request::ConstSharedPtr, TogglePaused::Response::SharedPtr) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (paused_) { resume_locked(now); } else { pause_locked(now); }
      }
      state_cv_.notify_all();
    });

  using IsPaused = rosbag2_interfaces::srv::IsPaused;
  is_paused_service_ = create_service<IsPaused>(
    "~/is_paused",
    [this](IsPaused::Request::ConstSharedPtr, IsPaused::Response::SharedPtr response) {
      std::lock_guard<std::mutex> lock(state_mutex_);
      response->paused = paused_;
    });

  using GetRate = rosbag2_interfaces::srv::GetRate;
  get_rate_service_ = create_service<GetRate>(
    "~/get_rate",
    [this](GetRate::Request::ConstSharedPtr, GetRate::Response::SharedPtr response) {
      std::lock_guard<std::mutex> lock(state_mutex_);
      response->rate = rate_;
    });

  using SetRate = rosbag2_interfaces::srv::SetRate;
  set_rate_service_ = create_service<SetRate>(
    "~/set_rate",
    [this](SetRate::Request::ConstSharedPtr request, SetRate::Response::SharedPtr response) {
      if (!std::isfinite(request->rate) || request->rate <= 0.0) {
        response->success = false;
        return;
      }
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const auto now = std::chrono::steady_clock::now();
        update_position_locked(now);
        rate_ = request->rate;
        anchor_position_ns_ = position_ns_;
        anchor_wall_ = now;
        response->success = true;
      }
      state_cv_.notify_all();
    });
}

void PlayerNode::playback_loop()
{
  std::unique_lock<std::mutex> lock(state_mutex_);
  while (!stop_requested_ && rclcpp::ok()) {
    if (paused_) {
      state_cv_.wait(lock, [this]() {return stop_requested_ || !paused_;});
      continue;
    }

    const auto now = std::chrono::steady_clock::now();
    update_position_locked(now);

    if (next_event_ >= events_.size()) {
      if (loop_) {
        reset_position_locked(now);
        continue;
      }
      finished_ = true;
      RCLCPP_INFO(get_logger(), "Playback finished");
      break;
    }

    const int64_t due_ns = std::max<int64_t>(0, events_[next_event_].stamp_ns - epoch_ns_);
    if (position_ns_ < due_ns) {
      const double wait_seconds = static_cast<double>(due_ns - position_ns_) / 1.0e9 / rate_;
      state_cv_.wait_for(lock, std::chrono::duration<double>(wait_seconds));
      continue;
    }

    const Event event = events_[next_event_++];
    lock.unlock();
    publish_event(event);
    lock.lock();
  }
}

void PlayerNode::publish_event(const Event & event)
{
  try {
    if (event.kind == Event::Kind::Bag) {
      if (event.source_index >= bag_messages_.size()) { return; }
      const auto & bag_message = bag_messages_[event.source_index].message;
      if (!bag_message || !bag_message->serialized_data) { return; }
      const auto publisher = bag_publishers_.find(bag_message->topic_name);
      if (publisher != bag_publishers_.end()) {
        publisher->second->publish(rclcpp::SerializedMessage(*bag_message->serialized_data));
      }
    } else {
      if (event.source_index >= video_clips_.size()) { return; }
      auto & clip = video_clips_[event.source_index];
      QImage image = clip.reader->frameAtIndex(event.frame_index);
      if (!image.isNull()) {
        if (image.format() != QImage::Format_RGB888) {
          image = image.convertToFormat(QImage::Format_RGB888);
        }
        sensor_msgs::msg::Image message;
        message.header.stamp = rclcpp::Time(event.stamp_ns);
        message.header.frame_id = image_frame_id_;
        message.height = static_cast<std::uint32_t>(image.height());
        message.width = static_cast<std::uint32_t>(image.width());
        message.encoding = "rgb8";
        message.is_bigendian = false;
        message.step = message.width * 3U;
        message.data.resize(static_cast<std::size_t>(message.step) * message.height);
        for (std::uint32_t row = 0; row < message.height; ++row) {
          std::memcpy(
            message.data.data() + static_cast<std::size_t>(row) * message.step,
            image.constScanLine(static_cast<int>(row)), message.step);
        }
        if (clip.publisher) { clip.publisher->publish(message); }
      }
    }
  } catch (const std::exception & error) {
    RCLCPP_WARN(get_logger(), "Could not publish event: %s", error.what());
  }
  publish_clock(event.stamp_ns);
}

void PlayerNode::publish_clock(int64_t stamp_ns)
{
  if (!clock_publisher_) { return; }
  try {
    rosgraph_msgs::msg::Clock message;
    message.clock = rclcpp::Time(stamp_ns);
    clock_publisher_->publish(message);
  } catch (const std::exception &) {
    // Context shutdown can race with the final worker iteration (for example on Ctrl-C).
  }
}

void PlayerNode::update_position_locked(std::chrono::steady_clock::time_point now)
{
  if (paused_) { return; }
  const double elapsed_seconds = std::chrono::duration<double>(now - anchor_wall_).count();
  const double scaled_ns = elapsed_seconds * rate_ * 1.0e9;
  const int64_t elapsed_ns = scaled_ns >= static_cast<double>(std::numeric_limits<int64_t>::max()) ?
    std::numeric_limits<int64_t>::max() : static_cast<int64_t>(scaled_ns);
  if (elapsed_ns >= duration_ns_ - std::min(duration_ns_, anchor_position_ns_)) {
    position_ns_ = duration_ns_;
  } else {
    position_ns_ = anchor_position_ns_ + elapsed_ns;
  }
}

void PlayerNode::reset_position_locked(std::chrono::steady_clock::time_point now)
{
  next_event_ = 0;
  position_ns_ = 0;
  anchor_position_ns_ = 0;
  anchor_wall_ = now;
  finished_ = false;
}

void PlayerNode::pause_locked(std::chrono::steady_clock::time_point now)
{
  if (paused_) { return; }
  update_position_locked(now);
  paused_ = true;
  anchor_position_ns_ = position_ns_;
  anchor_wall_ = now;
}

void PlayerNode::resume_locked(std::chrono::steady_clock::time_point now)
{
  if (!paused_) { return; }
  paused_ = false;
  anchor_position_ns_ = position_ns_;
  anchor_wall_ = now;
}

bool PlayerNode::topic_selected(const std::string & topic) const
{
  if (topic_filter_.empty()) { return true; }
  const std::string normalized = normalized_topic(topic);
  return std::find(topic_filter_.begin(), topic_filter_.end(), normalized) != topic_filter_.end();
}

std::string PlayerNode::output_topic(const std::string & topic) const
{
  const std::string normalized = normalized_topic(topic);
  if (topic_prefix_.empty() || topic_prefix_ == "/") { return normalized; }
  return topic_prefix_ + normalized;
}

}  // namespace data_recorder
