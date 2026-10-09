#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>

#include "data_recorder/config_model.hpp"
#include "data_recorder/live_bridge.hpp"
#include "data_recorder/path_utils.hpp"
#include "data_recorder/recorder_engine.hpp"
#include "data_recorder/session_manager.hpp"

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace
{

bool spin_until(
  rclcpp::executors::SingleThreadedExecutor & executor,
  const std::function<bool()> & condition, std::chrono::seconds timeout = 5s)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some(20ms);
    if (condition()) { return true; }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

sensor_msgs::msg::Image make_test_image(uint8_t fill)
{
  sensor_msgs::msg::Image image;
  image.header.stamp.sec = 123;
  image.header.stamp.nanosec = 456789000u + fill;
  image.header.frame_id = "camera_frame";
  image.width = 64;
  image.height = 48;
  image.encoding = "bgr8";
  image.is_bigendian = 1;
  image.step = image.width * 3 + 4;  // Include padding to verify the original stride and bytes.
  image.data.resize(image.step * image.height);
  for (std::size_t i = 0; i < image.data.size(); ++i) {
    image.data[i] = static_cast<uint8_t>(fill + i % 17);
  }
  return image;
}

class RecorderEngineImageBackendTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
    test_dir_ = fs::temp_directory_path() / (
      "dr_image_backend_test_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(test_dir_);
  }

  void TearDown() override
  {
    // Test-local engines and nodes are destroyed first, including after an ASSERT early return.
    rclcpp::shutdown();
    std::error_code ec;
    fs::remove_all(test_dir_, ec);
  }

  fs::path test_dir_;
};

}  // namespace

TEST_F(RecorderEngineImageBackendTest, RosbagImagesKeepPreviewAndOriginalMessagesInMixedSession)
{
  const std::string raw_topic = "/dr_test_rosbag_camera/image_raw";
  const std::string video_topic = "/dr_test_video_camera/image_raw";
  const auto config_path = test_dir_ / "config.yaml";
  {
    std::ofstream out(config_path);
    out << "output_dir: " << (test_dir_ / "recordings").string() << "\n"
        << "groups:\n"
        << "  - topics: [" << raw_topic << "]\n"
        << "    backend: rosbag\n"
        << "  - topics: [" << video_topic << "]\n"
        << "    backend: video\n";
  }
  const auto config = data_recorder::ConfigModel().load_from_file(config_path.string());
  ASSERT_EQ(config.topics.size(), 2u);
  ASSERT_EQ(config.topics[0].backend_name, "rosbag");
  ASSERT_EQ(config.topics[0].ui_category, data_recorder::TopicUiCategory::CameraPreview);

  auto recorder_node = std::make_shared<rclcpp::Node>("dr_test_image_backend_recorder");
  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager session_manager;
  data_recorder::RecorderEngine engine(recorder_node, config, &bridge, &session_manager);

  // Publishers appear after construction to exercise discovery and pending subscription routing.
  auto pub_node = std::make_shared<rclcpp::Node>("dr_test_image_backend_publisher");
  auto raw_pub = pub_node->create_publisher<sensor_msgs::msg::Image>(raw_topic, 10);
  auto video_pub = pub_node->create_publisher<sensor_msgs::msg::Image>(video_topic, 10);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(recorder_node);
  executor.add_node(pub_node);
  ASSERT_TRUE(spin_until(executor, [&]() {
    return raw_pub->get_subscription_count() > 0 && video_pub->get_subscription_count() > 0;
  }));

  const auto raw_key = QString::fromStdString(raw_topic);
  const auto video_key = QString::fromStdString(video_topic);
  raw_pub->publish(make_test_image(5));
  ASSERT_TRUE(spin_until(executor, [&]() { return bridge.latest_frame(raw_key) != nullptr; }))
    << "A rosbag image must remain available for camera preview before recording";
  EXPECT_EQ(bridge.latest_frame(raw_key)->width(), 64);
  EXPECT_EQ(bridge.latest_frame(raw_key)->height(), 48);

  ASSERT_FALSE(engine.start_session().empty());
  std::vector<sensor_msgs::msg::Image> expected_images;
  for (const uint8_t fill : {uint8_t{30}, uint8_t{60}, uint8_t{90}}) {
    auto image = make_test_image(fill);
    expected_images.push_back(image);
    raw_pub->publish(image);
    video_pub->publish(image);
    ASSERT_TRUE(spin_until(executor, [&]() {
      const auto raw_preview = bridge.latest_frame(raw_key);
      const auto video_preview = bridge.latest_frame(video_key);
      return raw_preview && video_preview &&
             raw_preview->constBits()[0] == fill && video_preview->constBits()[0] == fill;
    })) << "Both image backends must update their camera previews while recording";
  }
  const auto record = engine.stop_session({}, {});
  ASSERT_FALSE(record.directory.empty());
  ASSERT_EQ(record.topics.size(), 2u);
  EXPECT_EQ(record.topics[0].name, raw_topic);
  EXPECT_EQ(record.topics[0].backend, "rosbag");
  EXPECT_EQ(record.topics[1].name, video_topic);
  EXPECT_EQ(record.topics[1].backend, "video");

  const auto session_yaml = YAML::LoadFile((fs::path(record.directory) / "session.yaml").string());
  ASSERT_EQ(session_yaml["topics"].size(), 2u);
  EXPECT_EQ(session_yaml["topics"][0]["backend"].as<std::string>(), "rosbag");
  EXPECT_EQ(session_yaml["topics"][1]["backend"].as<std::string>(), "video");

  rosbag2_cpp::Reader reader;
  reader.open((fs::path(record.directory) / "rosbag").string());
  const auto bag_topics = reader.get_all_topics_and_types();
  ASSERT_EQ(bag_topics.size(), 1u) << "Only the configured rosbag topic belongs in the bag";
  EXPECT_EQ(bag_topics[0].name, raw_topic);
  EXPECT_EQ(bag_topics[0].type, "sensor_msgs/msg/Image");
  rclcpp::Serialization<sensor_msgs::msg::Image> serializer;
  std::size_t message_count = 0;
  while (reader.has_next()) {
    const auto bag_message = reader.read_next();
    EXPECT_EQ(bag_message->topic_name, raw_topic);
    ASSERT_LT(message_count, expected_images.size());
    rclcpp::SerializedMessage serialized(*bag_message->serialized_data);
    sensor_msgs::msg::Image image;
    serializer.deserialize_message(&serialized, &image);
    // ROS message equality covers header, encoding, dimensions, endian flag, stride and pixel bytes.
    EXPECT_EQ(image, expected_images[message_count]);
    ++message_count;
  }
  EXPECT_EQ(message_count, expected_images.size());

  const fs::path video_dir = fs::path(record.directory) / "video";
  ASSERT_TRUE(fs::exists(video_dir));
  const auto video_base = data_recorder::file_base_for_topic(video_topic);
  const auto mp4 = video_dir / (video_base + ".mp4");
  const auto csv = video_dir / (video_base + ".csv");
  ASSERT_TRUE(fs::exists(mp4));
  EXPECT_GT(fs::file_size(mp4), 0u);
  ASSERT_TRUE(fs::exists(csv));
  std::ifstream csv_stream(csv);
  std::string line;
  std::size_t csv_lines = 0;
  while (std::getline(csv_stream, line)) { ++csv_lines; }
  EXPECT_EQ(csv_lines, expected_images.size() + 1);  // Header plus one row per encoded frame.
  for (const auto & entry : fs::directory_iterator(video_dir)) {
    EXPECT_TRUE(entry.path() == mp4 || entry.path() == csv)
      << "A rosbag image must not produce video or CSV files: " << entry.path();
  }
}

TEST(RecorderEngineSubscription, ExplicitQosAppliesRegardlessOfPublisher)
{
  rclcpp::init(0, nullptr);

  data_recorder::ConfigData config;
  config.output_dir = (fs::temp_directory_path() / "dr_camera_qos_test").string();

  // qos_explicit=true：即使订阅时还没有发布者，也应立即按配置 QoS 建立订阅（不进 pending）。
  data_recorder::TopicEntry custom_entry;
  custom_entry.topic_name = "/dr_test_camera_custom_qos";
  custom_entry.backend_name = "video";
  custom_entry.ui_category = data_recorder::TopicUiCategory::CameraPreview;
  custom_entry.qos.history = data_recorder::QosHistory::KeepAll;
  custom_entry.qos.reliability = data_recorder::QosReliability::BestEffort;
  custom_entry.qos.durability = data_recorder::QosDurability::TransientLocal;
  custom_entry.qos_explicit = true;
  config.topics.push_back(custom_entry);

  auto node = std::make_shared<rclcpp::Node>("dr_test_camera_qos_node");
  data_recorder::SessionManager session_manager;
  data_recorder::RecorderEngine engine(node, config, /*bridge=*/nullptr, &session_manager);

  const auto custom_endpoints =
    node->get_subscriptions_info_by_topic(custom_entry.topic_name);
  ASSERT_EQ(custom_endpoints.size(), 1u);
  const auto & custom_qos = custom_endpoints.front().qos_profile();
  EXPECT_EQ(custom_qos.reliability(), rclcpp::ReliabilityPolicy::BestEffort);
  EXPECT_EQ(custom_qos.durability(), rclcpp::DurabilityPolicy::TransientLocal);

  rclcpp::shutdown();
}

// 回归测试：video 话题默认（非 qos_explicit）应像 rosbag 话题一样跟随真实发布者的 QoS，
// 而不是套用固定的 config 默认值。构造时发布者还不存在 → 走 pending，由 resubscribe_timer_ 补订。
TEST(RecorderEngineSubscription, DefaultVideoQosMatchesPublisherAfterDiscovery)
{
  rclcpp::init(0, nullptr);

  const std::string topic = "/dr_test_camera_default_qos";

  data_recorder::ConfigData config;
  config.output_dir = (fs::temp_directory_path() / "dr_camera_qos_default_test").string();
  data_recorder::TopicEntry default_entry;
  default_entry.topic_name = topic;
  default_entry.backend_name = "video";
  default_entry.ui_category = data_recorder::TopicUiCategory::CameraPreview;
  config.topics.push_back(default_entry);

  auto node = std::make_shared<rclcpp::Node>("dr_test_camera_default_qos_node");
  data_recorder::SessionManager session_manager;
  data_recorder::RecorderEngine engine(node, config, /*bridge=*/nullptr, &session_manager);

  // 构造之后才创建发布者（确定性复现竞态），QoS 为 BestEffort/TransientLocal。
  auto pub_node = std::make_shared<rclcpp::Node>("dr_test_camera_pub_node");
  auto pub = pub_node->create_publisher<sensor_msgs::msg::Image>(
    topic, rclcpp::QoS(1).best_effort().transient_local());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(pub_node);

  // 自旋至多 ~5s，等 resubscribe_timer_（500ms 周期）补订。
  bool subscribed = false;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some(50ms);
    if (pub->get_subscription_count() > 0) {
      subscribed = true;
      break;
    }
    std::this_thread::sleep_for(50ms);
  }
  ASSERT_TRUE(subscribed) << "RecorderEngine 未补订默认 QoS 的 video 话题";

  const auto endpoints = node->get_subscriptions_info_by_topic(topic);
  ASSERT_EQ(endpoints.size(), 1u);
  const auto & qos = endpoints.front().qos_profile();
  EXPECT_EQ(qos.reliability(), rclcpp::ReliabilityPolicy::BestEffort);
  EXPECT_EQ(qos.durability(), rclcpp::DurabilityPolicy::TransientLocal);

  executor.remove_node(node);
  executor.remove_node(pub_node);
  rclcpp::shutdown();
}

// 回归测试：发布者在 RecorderEngine 构造“之后”才出现，也应被补订。
// 复现启动发现竞态——旧代码在构造时查不到发布者就 continue 永久跳过，发布者永远订不上。
TEST(RecorderEngineSubscription, SubscribesToPublisherThatAppearsAfterConstruction)
{
  rclcpp::init(0, nullptr);

  const std::string topic = "/dr_test_late";

  data_recorder::ConfigData config;
  config.output_dir = (fs::temp_directory_path() / "dr_sub_test").string();
  data_recorder::TopicEntry entry;
  entry.topic_name = topic;
  entry.backend_name = "rosbag";
  entry.ui_category = data_recorder::TopicUiCategory::NumericTrack;
  config.topics.push_back(entry);

  auto node = std::make_shared<rclcpp::Node>("dr_test_engine_node");
  data_recorder::SessionManager session_manager;
  // 构造时该话题还没有发布者 → 进 pending（旧代码会永久跳过）。
  data_recorder::RecorderEngine engine(node, config, /*bridge=*/nullptr, &session_manager);

  // 构造之后才创建发布者（确定性复现竞态）。
  auto pub_node = std::make_shared<rclcpp::Node>("dr_test_pub_node");
  auto pub = pub_node->create_publisher<std_msgs::msg::String>(topic, 10);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(pub_node);

  // 自旋至多 ~5s，等 resubscribe_timer_（500ms 周期）补订；补订成功后发布者会看到 1 个订阅者。
  bool subscribed = false;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some(50ms);
    if (pub->get_subscription_count() > 0) {
      subscribed = true;
      break;
    }
    std::this_thread::sleep_for(50ms);
  }

  EXPECT_TRUE(subscribed)
    << "RecorderEngine 未补订构造后才出现的发布者（启动发现竞态未修复）";

  executor.remove_node(node);
  executor.remove_node(pub_node);
  rclcpp::shutdown();
  std::error_code ec;
  fs::remove_all(config.output_dir, ec);
}

// 回归测试：TRANSIENT_LOCAL 历史样本在开录前已被长期订阅消费，开录后即使发布者不再
// publish，也必须把缓存的样本写入 bag。/tf_static 正是这种行为。
TEST(RecorderEngineSubscription, RecordsTransientLocalSampleConsumedBeforeSession)
{
  rclcpp::init(0, nullptr);

  const std::string topic = "/dr_test_transient_local";
  const fs::path output_dir = fs::temp_directory_path() / "dr_transient_sub_test";
  std::error_code ec;
  fs::remove_all(output_dir, ec);

  data_recorder::ConfigData config;
  config.output_dir = output_dir.string();
  data_recorder::TopicEntry entry;
  entry.topic_name = topic;
  entry.backend_name = "rosbag";
  entry.ui_category = data_recorder::TopicUiCategory::NumericTrack;
  config.topics.push_back(entry);

  auto pub_node = std::make_shared<rclcpp::Node>("dr_test_transient_pub_node");
  auto pub = pub_node->create_publisher<std_msgs::msg::String>(
    topic, rclcpp::QoS(1).reliable().transient_local());
  std_msgs::msg::String latched;
  latched.data = "published before recording";
  pub->publish(latched);

  auto recorder_node = std::make_shared<rclcpp::Node>("dr_test_transient_engine_node");
  data_recorder::SessionManager session_manager;
  data_recorder::RecorderEngine engine(
    recorder_node, config, /*bridge=*/nullptr, &session_manager);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(recorder_node);
  executor.add_node(pub_node);

  // 等订阅建立并充分自旋，确保历史样本已在 recording=false 时送达并被消费。
  const auto discovery_deadline = std::chrono::steady_clock::now() + 5s;
  while (pub->get_subscription_count() == 0 &&
    std::chrono::steady_clock::now() < discovery_deadline)
  {
    executor.spin_some(50ms);
    std::this_thread::sleep_for(20ms);
  }
  ASSERT_GT(pub->get_subscription_count(), 0u);
  const auto delivery_deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < delivery_deadline) {
    executor.spin_some(50ms);
    std::this_thread::sleep_for(10ms);
  }

  const std::string session_id = engine.start_session();
  ASSERT_FALSE(session_id.empty());
  // 开录后故意不再 publish。
  const auto record_deadline = std::chrono::steady_clock::now() + 300ms;
  while (std::chrono::steady_clock::now() < record_deadline) {
    executor.spin_some(50ms);
  }
  const auto record = engine.stop_session({}, {});
  ASSERT_FALSE(record.directory.empty());

  rosbag2_cpp::Reader reader;
  reader.open((fs::path(record.directory) / "rosbag").string());
  int message_count = 0;
  while (reader.has_next()) {
    const auto bag_message = reader.read_next();
    if (bag_message->topic_name == topic) {
      ++message_count;
    }
  }
  EXPECT_EQ(message_count, 1)
    << "开录前消费的 TRANSIENT_LOCAL 历史样本没有写入 rosbag";

  executor.remove_node(recorder_node);
  executor.remove_node(pub_node);
  rclcpp::shutdown();
  fs::remove_all(output_dir, ec);
}
