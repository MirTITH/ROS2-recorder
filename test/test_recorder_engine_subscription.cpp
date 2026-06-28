#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include "data_recorder/config_model.hpp"
#include "data_recorder/recorder_engine.hpp"
#include "data_recorder/session_manager.hpp"

using namespace std::chrono_literals;
namespace fs = std::filesystem;

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
