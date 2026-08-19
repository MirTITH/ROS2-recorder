#pragma once

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rosbag2_interfaces/srv/get_rate.hpp>
#include <rosbag2_interfaces/srv/is_paused.hpp>
#include <rosbag2_interfaces/srv/pause.hpp>
#include <rosbag2_interfaces/srv/resume.hpp>
#include <rosbag2_interfaces/srv/set_rate.hpp>
#include <rosbag2_interfaces/srv/toggle_paused.hpp>
#include <rosgraph_msgs/msg/clock.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rosbag2_storage
{
struct SerializedBagMessage;
}

namespace rosbag2_cpp
{
class Reader;
}

namespace data_recorder
{

class VideoClipReader;

// 播放一个 data_recorder 会话目录中的 rosbag 和视频。
// 调度线程使用录制消息的绝对 ROS 时间戳，所有输出共享同一播放时钟。
class PlayerNode : public rclcpp::Node
{
public:
  explicit PlayerNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~PlayerNode() override;

  PlayerNode(const PlayerNode &) = delete;
  PlayerNode & operator=(const PlayerNode &) = delete;

  bool finished() const;

private:
  struct BagMessage
  {
    std::shared_ptr<rosbag2_storage::SerializedBagMessage> message;
  };

  struct VideoClip
  {
    std::string topic;
    std::unique_ptr<VideoClipReader> reader;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher;
  };

  struct Event
  {
    enum class Kind { Bag, Video };
    Kind kind{Kind::Bag};
    int64_t stamp_ns{0};
    std::size_t source_index{0};
    std::size_t frame_index{0};
    std::uint64_t order{0};
  };

  void load_recording();
  void load_bag(const std::string & bag_directory);
  void load_videos(const std::string & session_directory);
  void build_event_queue();

  void create_control_services();
  void playback_loop();
  void publish_event(const Event & event);
  void publish_clock(int64_t stamp_ns);

  void update_position_locked(std::chrono::steady_clock::time_point now);
  void reset_position_locked(std::chrono::steady_clock::time_point now);
  void pause_locked(std::chrono::steady_clock::time_point now);
  void resume_locked(std::chrono::steady_clock::time_point now);

  bool topic_selected(const std::string & topic) const;
  std::string output_topic(const std::string & topic) const;

  std::string session_directory_;
  std::string topic_prefix_;
  std::vector<std::string> topic_filter_;
  std::string image_frame_id_;
  bool loop_{false};
  bool publish_clock_enabled_{false};

  int64_t epoch_ns_{0};
  int64_t recorded_epoch_ns_{0};
  int64_t duration_ns_{0};
  // Reader 必须比它返回的 SerializedBagMessage 活得更久；部分存储插件的释放逻辑
  // 位于插件动态库中，提前销毁 Reader 会卸载该库。
  std::unique_ptr<rosbag2_cpp::Reader> bag_reader_;
  std::vector<BagMessage> bag_messages_;
  std::vector<VideoClip> video_clips_;
  std::vector<Event> events_;
  std::unordered_map<std::string, rclcpp::GenericPublisher::SharedPtr> bag_publishers_;
  rclcpp::Publisher<rosgraph_msgs::msg::Clock>::SharedPtr clock_publisher_;

  rclcpp::Service<rosbag2_interfaces::srv::Pause>::SharedPtr pause_service_;
  rclcpp::Service<rosbag2_interfaces::srv::Resume>::SharedPtr resume_service_;
  rclcpp::Service<rosbag2_interfaces::srv::TogglePaused>::SharedPtr toggle_paused_service_;
  rclcpp::Service<rosbag2_interfaces::srv::IsPaused>::SharedPtr is_paused_service_;
  rclcpp::Service<rosbag2_interfaces::srv::GetRate>::SharedPtr get_rate_service_;
  rclcpp::Service<rosbag2_interfaces::srv::SetRate>::SharedPtr set_rate_service_;

  mutable std::mutex state_mutex_;
  std::condition_variable state_cv_;
  bool paused_{false};
  bool stop_requested_{false};
  bool finished_{false};
  double rate_{1.0};
  std::size_t next_event_{0};
  int64_t position_ns_{0};
  int64_t anchor_position_ns_{0};
  std::chrono::steady_clock::time_point anchor_wall_{};
  std::thread playback_thread_;
};

}  // namespace data_recorder
