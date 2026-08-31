#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <std_msgs/msg/string.hpp>

#include "data_recorder/config_model.hpp"
#include "data_recorder/recorder_engine.hpp"
#include "data_recorder/session_manager.hpp"

using namespace std::chrono_literals;
namespace fs = std::filesystem;

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
