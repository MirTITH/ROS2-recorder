#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QCoreApplication>

#include <rclcpp/rclcpp.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>
#include <yaml-cpp/yaml.h>

#include "data_recorder/app_controller.hpp"
#include "data_recorder/live_bridge.hpp"
#include "data_recorder/recorder_engine.hpp"
#include "data_recorder/session_manager.hpp"

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace
{

class RecordingLifecycleTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
    root_ = fs::temp_directory_path() / (
      "data_recorder_lifecycle_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root_);
  }

  void TearDown() override
  {
    rclcpp::shutdown();
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  data_recorder::ConfigData config() const
  {
    data_recorder::ConfigData config;
    config.output_dir = (root_ / "recordings").string();
    config.topics.push_back({
      "/dr_lifecycle_text", "rosbag", 0,
      data_recorder::TopicUiCategory::NumericTrack, false, {}, {}, false});
    return config;
  }

  fs::path root_;
};

data_recorder::ConfigData config_with_topics(
  const fs::path & output,
  const std::vector<data_recorder::TopicEntry> & topics)
{
  data_recorder::ConfigData config;
  config.output_dir = output.string();
  config.topics = topics;
  return config;
}

sensor_msgs::msg::Image test_image()
{
  sensor_msgs::msg::Image image;
  image.width = 16;
  image.height = 12;
  image.encoding = "bgr8";
  image.step = image.width * 3;
  image.data.assign(static_cast<std::size_t>(image.step * image.height), 7);
  return image;
}

bool wait_for_recording_error(
  data_recorder::RecorderEngine & engine, rclcpp::executors::SingleThreadedExecutor & executor)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some(20ms);
    if (!engine.recording_error().empty()) { return true; }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

}  // namespace

TEST_F(RecordingLifecycleTest, StartOnFileReturnsFailureWithoutThrowing)
{
  const fs::path output_file = root_ / "recordings_file";
  std::ofstream(output_file) << "not a directory";
  auto config_data = config();
  config_data.output_dir = output_file.string();
  auto node = std::make_shared<rclcpp::Node>("dr_lifecycle_bad_output_node");
  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager manager;
  data_recorder::RecorderEngine engine(node, config_data, &bridge, &manager);

  data_recorder::SessionStartResult result;
  EXPECT_NO_THROW(result = engine.start_session());
  EXPECT_FALSE(result.success());
  EXPECT_TRUE(result.session_id.empty());
  EXPECT_FALSE(result.error.empty());
  EXPECT_TRUE(fs::is_regular_file(output_file));
}

TEST_F(RecordingLifecycleTest, StopIsIdempotentAndDurationIsFrozen)
{
  auto config_data = config();
  auto node = std::make_shared<rclcpp::Node>("dr_lifecycle_stop_node");
  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager manager;
  data_recorder::RecorderEngine engine(node, config_data, &bridge, &manager);

  ASSERT_TRUE(engine.start_session().success());
  std::this_thread::sleep_for(30ms);
  const auto first = engine.stop_session();
  ASSERT_FALSE(first.record.session_id.empty());
  EXPECT_FALSE(engine.is_recording());
  EXPECT_GT(first.record.duration_seconds, 0.0);
  std::this_thread::sleep_for(100ms);
  const auto second = engine.stop_session();
  EXPECT_EQ(second.record.session_id, first.record.session_id);
  EXPECT_EQ(second.record.duration_seconds, first.record.duration_seconds);
  EXPECT_EQ(second.metadata_saved, first.metadata_saved);
  EXPECT_EQ(second.errors, first.errors);
}

TEST_F(RecordingLifecycleTest, ControllerShutdownPreservesTagsAndCompletedAnnotations)
{
  int argc = 1;
  char app_name[] = "test_recording_lifecycle";
  char * argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  auto config_data = config();
  config_data.tags = {{"成功", "#2f9e44"}};
  config_data.event_markers = {{"p", "完成", "point", "#1763c9"}};
  auto node = std::make_shared<rclcpp::Node>("dr_lifecycle_controller_node");
  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager manager;
  data_recorder::RecorderEngine engine(node, config_data, &bridge, &manager);
  {
    data_recorder::AppController controller(config_data, &bridge, &engine, &manager);
    controller.toggleRecording();
    ASSERT_TRUE(controller.recording());
    controller.toggleTag(0);
    ASSERT_TRUE(controller.triggerMarkerShortcut("p"));
    controller.shutdownRecording();
    controller.shutdownRecording();
    EXPECT_FALSE(controller.recording());
  }

  const auto sessions = manager.scan(config_data.output_dir);
  ASSERT_EQ(sessions.size(), 1u);
  ASSERT_EQ(sessions.front().tags.size(), 1u);
  EXPECT_EQ(sessions.front().tags.front().name, "成功");
  ASSERT_EQ(sessions.front().annotations.size(), 1u);
  EXPECT_EQ(sessions.front().annotations.front().kind, "point");
}

TEST_F(RecordingLifecycleTest, ControllerDestructorStopsAndPreservesClosedRange)
{
  int argc = 1;
  char app_name[] = "test_recording_lifecycle";
  char * argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  auto config_data = config();
  config_data.tags = {{"力控", "#7c4dff"}};
  config_data.event_markers = {{"r", "区间", "range", "#1763c9"}};
  auto node = std::make_shared<rclcpp::Node>("dr_lifecycle_destructor_node");
  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager manager;
  data_recorder::RecorderEngine engine(node, config_data, &bridge, &manager);
  {
    data_recorder::AppController controller(config_data, &bridge, &engine, &manager);
    controller.toggleRecording();
    ASSERT_TRUE(controller.recording());
    controller.toggleTag(0);
    ASSERT_TRUE(controller.eventMarkerModel()->toggleRange(0, 0.1));
    ASSERT_TRUE(controller.eventMarkerModel()->toggleRange(0, 0.2));
  }

  const auto sessions = manager.scan(config_data.output_dir);
  ASSERT_EQ(sessions.size(), 1u);
  ASSERT_EQ(sessions.front().tags.size(), 1u);
  EXPECT_EQ(sessions.front().tags.front().name, "力控");
  ASSERT_EQ(sessions.front().annotations.size(), 1u);
  EXPECT_EQ(sessions.front().annotations.front().kind, "range");
  EXPECT_GT(sessions.front().annotations.front().end, sessions.front().annotations.front().t);
}

TEST_F(RecordingLifecycleTest, ControllerStopAllowsNewSessionAndShutdownPreventsRestart)
{
  int argc = 1;
  char app_name[] = "test_recording_lifecycle";
  char * argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  auto config_data = config();
  config_data.tags = {{"完成", "#2f9e44"}};
  auto node = std::make_shared<rclcpp::Node>("dr_lifecycle_continuous_node");
  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager manager;
  data_recorder::RecorderEngine engine(node, config_data, &bridge, &manager);
  data_recorder::AppController controller(config_data, &bridge, &engine, &manager);
  controller.toggleRecording();
  ASSERT_TRUE(controller.recording());
  controller.toggleTag(0);
  controller.stopRecording();
  controller.stopRecording();
  EXPECT_FALSE(controller.recording());
  const auto first_sessions = manager.scan(config_data.output_dir);
  ASSERT_EQ(first_sessions.size(), 1u);
  ASSERT_EQ(first_sessions.front().tags.size(), 1u);

  controller.toggleRecording();
  ASSERT_TRUE(controller.recording());
  controller.stopRecording();
  controller.shutdownRecording();
  controller.shutdownRecording();
  EXPECT_FALSE(controller.canRecord());
  controller.toggleRecording();
  EXPECT_FALSE(controller.recording());
  EXPECT_FALSE(engine.is_recording());
  const auto sessions = manager.scan(config_data.output_dir);
  ASSERT_EQ(sessions.size(), 2u);
  EXPECT_NE(sessions[0].session_id, sessions[1].session_id);
  const auto old_session = std::find_if(
    sessions.begin(), sessions.end(), [&first_sessions](const auto & session) {
      return session.session_id == first_sessions.front().session_id;
    });
  ASSERT_NE(old_session, sessions.end());
  ASSERT_EQ(old_session->tags.size(), 1u);
  EXPECT_EQ(old_session->tags.front().name, "完成");
}

TEST_F(RecordingLifecycleTest, ControllerStartsWithTruncatedMetadataAndStillSavesNewSession)
{
  int argc = 1;
  char app_name[] = "test_recording_lifecycle";
  char * argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  auto config_data = config();
  config_data.tags = {{"完成", "#2f9e44"}};
  const fs::path damaged_directory = fs::path(config_data.output_dir) / "interrupted";
  fs::create_directories(damaged_directory);
  // This legal YAML prefix can remain after an interrupted direct write.
  std::ofstream(damaged_directory / "session.yaml") << "session: interrupted\ntopics:\n  - name";
  const auto damaged = YAML::LoadFile((damaged_directory / "session.yaml").string());
  ASSERT_TRUE(damaged["topics"][0].IsScalar());

  auto node = std::make_shared<rclcpp::Node>("dr_lifecycle_truncated_start_node");
  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager manager;
  EXPECT_THROW(manager.scan(config_data.output_dir), YAML::Exception);
  data_recorder::RecorderEngine engine(node, config_data, &bridge, &manager);
  std::unique_ptr<data_recorder::AppController> controller;
  EXPECT_NO_THROW(controller = std::make_unique<data_recorder::AppController>(
      config_data, &bridge, &engine, &manager));
  ASSERT_NE(controller, nullptr);
  EXPECT_TRUE(controller->statusText().contains(QStringLiteral("历史列表扫描失败")));
  EXPECT_TRUE(controller->statusText().contains(QString::fromStdString(config_data.output_dir)));
  EXPECT_EQ(controller->recordingSessionModel()->rowCount(), 0);

  controller->toggleRecording();
  ASSERT_TRUE(controller->recording());
  controller->toggleTag(0);
  EXPECT_NO_THROW(controller->stopRecording());
  EXPECT_FALSE(controller->recording());
  EXPECT_TRUE(controller->statusText().contains(QStringLiteral("历史列表扫描失败")));
  const auto saved = engine.stop_session();
  ASSERT_TRUE(saved.success());
  ASSERT_NE(saved.record.directory, damaged_directory.string());
  const auto metadata = YAML::LoadFile((fs::path(saved.record.directory) / "session.yaml").string());
  ASSERT_EQ(metadata["tags"].size(), 1u);
  EXPECT_EQ(metadata["tags"][0]["name"].as<std::string>(), "完成");
  EXPECT_TRUE(YAML::LoadFile((damaged_directory / "session.yaml").string())["topics"][0].IsScalar());
}

TEST_F(RecordingLifecycleTest, FailedHistoryScanAfterStopKeepsPreviousList)
{
  int argc = 1;
  char app_name[] = "test_recording_lifecycle";
  char * argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  auto config_data = config();
  data_recorder::SessionManager manager;
  fs::create_directories(config_data.output_dir);
  data_recorder::SessionRecord old_session;
  old_session.session_id = "previous";
  old_session.directory = manager.create_session_directory(config_data.output_dir, "previous");
  ASSERT_TRUE(manager.write_session_yaml(old_session));
  auto node = std::make_shared<rclcpp::Node>("dr_lifecycle_truncated_stop_node");
  data_recorder::LiveBridge bridge;
  data_recorder::RecorderEngine engine(node, config_data, &bridge, &manager);
  data_recorder::AppController controller(config_data, &bridge, &engine, &manager);
  ASSERT_EQ(controller.recordingSessionModel()->rowCount(), 1);

  const fs::path damaged_directory = fs::path(config_data.output_dir) / "interrupted";
  fs::create_directory(damaged_directory);
  std::ofstream(damaged_directory / "session.yaml") << "session: interrupted\ntopics:\n  - name";
  controller.toggleRecording();
  ASSERT_TRUE(controller.recording());
  EXPECT_NO_THROW(controller.stopRecording());
  EXPECT_FALSE(controller.recording());
  EXPECT_TRUE(controller.statusText().contains(QStringLiteral("历史列表扫描失败")));
  EXPECT_EQ(controller.recordingSessionModel()->rowCount(), 1);
  const auto model_index = controller.recordingSessionModel()->index(0, 0);
  EXPECT_EQ(controller.recordingSessionModel()->data(
      model_index, data_recorder::RecordingSessionModel::FolderNameRole).toString(),
    QStringLiteral("previous"));
  const auto saved = engine.stop_session();
  ASSERT_TRUE(saved.success());
  EXPECT_TRUE(fs::is_regular_file(fs::path(saved.record.directory) / "session.yaml"));
}

TEST_F(RecordingLifecycleTest, BadVideoCodecReportsWorkerErrorAndKeepsRosbagAndMetadata)
{
  const std::string text_topic = "/dr_lifecycle_mixed_text";
  const std::string image_topic = "/dr_lifecycle_bad_codec_image";
  data_recorder::TopicEntry bag_topic{
    text_topic, "rosbag", 0, data_recorder::TopicUiCategory::NumericTrack, false, {}, {}, false};
  data_recorder::TopicEntry video_topic{
    image_topic, "video", 0, data_recorder::TopicUiCategory::CameraPreview, false, {}, {}, false};
  video_topic.params["codec"] = "codec-that-does-not-exist";
  auto config_data = config_with_topics(root_ / "mixed", {bag_topic, video_topic});
  config_data.tags = {{"异常仍保留", "#e03131"}};

  auto node = std::make_shared<rclcpp::Node>("dr_lifecycle_bad_codec_node");
  auto pub_node = std::make_shared<rclcpp::Node>("dr_lifecycle_bad_codec_pub");
  auto text_pub = pub_node->create_publisher<std_msgs::msg::String>(text_topic, 10);
  auto image_pub = pub_node->create_publisher<sensor_msgs::msg::Image>(image_topic, 10);
  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager manager;
  data_recorder::RecorderEngine engine(node, config_data, &bridge, &manager);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(pub_node);
  const auto discovery_deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < discovery_deadline &&
    (text_pub->get_subscription_count() == 0 || image_pub->get_subscription_count() == 0))
  {
    executor.spin_some(20ms);
    std::this_thread::sleep_for(10ms);
  }
  ASSERT_GT(text_pub->get_subscription_count(), 0u);
  ASSERT_GT(image_pub->get_subscription_count(), 0u);
  ASSERT_TRUE(engine.start_session().success());
  data_recorder::AnnotationRecord annotation;
  annotation.name = "错误点";
  annotation.kind = "point";
  annotation.t = 0.1;
  engine.update_session_metadata({annotation}, {{"异常仍保留", "#e03131"}});
  std_msgs::msg::String text;
  text.data = "bag survives";
  text_pub->publish(text);
  // Deliver healthy data before provoking the independent video worker failure.
  for (int i = 0; i < 10; ++i) {
    executor.spin_some(20ms);
    std::this_thread::sleep_for(10ms);
  }
  image_pub->publish(test_image());
  ASSERT_TRUE(wait_for_recording_error(engine, executor));
  const std::string worker_error = engine.recording_error();
  EXPECT_NE(worker_error.find("codec"), std::string::npos);

  const auto result = engine.stop_session();
  EXPECT_FALSE(result.success());
  EXPECT_TRUE(result.metadata_saved);
  ASSERT_FALSE(result.record.directory.empty());
  ASSERT_EQ(result.record.tags.size(), 1u);
  ASSERT_EQ(result.record.annotations.size(), 1u);
  EXPECT_FALSE(result.errors.empty());
  EXPECT_TRUE(fs::exists(fs::path(result.record.directory) / "rosbag"));
  rosbag2_cpp::Reader reader;
  ASSERT_NO_THROW(reader.open((fs::path(result.record.directory) / "rosbag").string()));
  ASSERT_TRUE(reader.has_next());
  EXPECT_EQ(reader.read_next()->topic_name, text_topic);
  EXPECT_TRUE(fs::exists(fs::path(result.record.directory) / "session.yaml"));
  const auto sessions = manager.scan(config_data.output_dir);
  ASSERT_EQ(sessions.size(), 1u);
  ASSERT_EQ(sessions.front().tags.size(), 1u);
  EXPECT_EQ(sessions.front().tags.front().name, "异常仍保留");
  ASSERT_EQ(sessions.front().annotations.size(), 1u);
  EXPECT_EQ(sessions.front().annotations.front().name, "错误点");
}

TEST_F(RecordingLifecycleTest, MetadataDirectoryFailureIsReportedAndNextSessionUsesNewDirectory)
{
  auto config_data = config();
  auto node = std::make_shared<rclcpp::Node>("dr_lifecycle_metadata_failure_node");
  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager manager;
  data_recorder::RecorderEngine engine(node, config_data, &bridge, &manager);

  const auto first_start = engine.start_session();
  ASSERT_TRUE(first_start.success());
  const fs::path first_directory = fs::path(config_data.output_dir) / first_start.session_id;
  fs::create_directory(first_directory / "session.yaml");
  engine.update_session_metadata({}, {{"保留", "#2f9e44"}});
  const auto failed = engine.stop_session();
  EXPECT_FALSE(failed.metadata_saved);
  EXPECT_FALSE(failed.errors.empty());
  EXPECT_FALSE(failed.success());
  ASSERT_EQ(failed.record.tags.size(), 1u);
  EXPECT_EQ(failed.record.tags.front().name, "保留");
  EXPECT_TRUE(fs::is_directory(first_directory / "session.yaml"));

  const auto second_start = engine.start_session();
  ASSERT_TRUE(second_start.success());
  EXPECT_NE(second_start.session_id, first_start.session_id);
  const auto second = engine.stop_session();
  EXPECT_TRUE(second.success());
  EXPECT_NE(second.record.directory, first_directory.string());
  EXPECT_TRUE(fs::is_directory(first_directory / "session.yaml"));
}

