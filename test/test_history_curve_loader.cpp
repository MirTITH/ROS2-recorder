#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>

#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

#include "data_recorder/history_curve_loader.hpp"
#include "data_recorder/rosbag_writer.hpp"

namespace fs = std::filesystem;

namespace
{
// 在 <tmp>/rosbag 写若干 JointState（a,b 两关节，position 递增）。返回 session_dir(<tmp>)。
std::string write_joint_state_bag(const fs::path & tmp)
{
  fs::remove_all(tmp);
  const std::string bag_dir = (tmp / "rosbag").string();
  rclcpp::Serialization<sensor_msgs::msg::JointState> ser;
  data_recorder::RosbagWriter writer(bag_dir, /*storage_id=*/"");
  writer.add_topic("/joint_states", "sensor_msgs/msg/JointState", "");
  for (int i = 0; i < 5; ++i) {
    sensor_msgs::msg::JointState msg;
    msg.name = {"a", "b"};
    msg.position = {static_cast<double>(i), static_cast<double>(i) * 2.0};
    rclcpp::SerializedMessage serialized;
    ser.serialize_message(&msg, &serialized);
    writer.write("/joint_states", serialized, /*time_stamp_ns=*/1'000'000LL * (i + 1));
  }
  writer.close();
  return tmp.string();
}

QVariantMap find_topic(const QVariantList & topics, const QString & key)
{
  for (const auto & v : topics) {
    const auto m = v.toMap();
    if (m.value("topicKey").toString() == key) { return m; }
  }
  return {};
}

QVariantMap find_series(const QVariantList & series, const QString & key)
{
  for (const auto & v : series) {
    const auto m = v.toMap();
    if (m.value("key").toString() == key) { return m; }
  }
  return {};
}
}  // namespace

TEST(HistoryCurveLoader, ScanTimestampsEmitsDotsAndTypes)
{
  const auto tmp = fs::temp_directory_path() / "dr_history_scan_test";
  const std::string session_dir = write_joint_state_bag(tmp);

  data_recorder::HistoryCurveLoader loader;
  QSignalSpy curves_spy(&loader, &data_recorder::HistoryCurveLoader::curvesReady);
  QSignalSpy types_spy(&loader, &data_recorder::HistoryCurveLoader::topicTypesReady);

  loader.scanTimestamps(QString::fromStdString(session_dir), {"/joint_states"});

  ASSERT_EQ(types_spy.count(), 1);
  const auto types = types_spy.at(0).at(0).toList();
  const auto tm = find_topic(types, "/joint_states");  // 复用：topicKey 字段相同
  EXPECT_EQ(tm.value("rosType").toString().toStdString(), "sensor_msgs/msg/JointState");

  ASSERT_EQ(curves_spy.count(), 1);
  const auto topics = curves_spy.at(0).at(0).toList();
  const auto topic = find_topic(topics, "/joint_states");
  ASSERT_FALSE(topic.isEmpty());
  const auto dots = topic.value("messageDots").toList();
  EXPECT_EQ(dots.size(), 5);
  EXPECT_TRUE(topic.value("series").toList().isEmpty());  // 扫描阶段不带曲线

  fs::remove_all(tmp);
}

TEST(HistoryCurveLoader, ExtractTopicEmitsSeries)
{
  const auto tmp = fs::temp_directory_path() / "dr_history_extract_test";
  const std::string session_dir = write_joint_state_bag(tmp);

  data_recorder::HistoryCurveLoader loader;
  QSignalSpy curves_spy(&loader, &data_recorder::HistoryCurveLoader::curvesReady);

  loader.extractTopic(QString::fromStdString(session_dir), "/joint_states");

  ASSERT_EQ(curves_spy.count(), 1);
  const auto topics = curves_spy.at(0).at(0).toList();
  const auto topic = find_topic(topics, "/joint_states");
  ASSERT_FALSE(topic.isEmpty());

  const auto series = topic.value("series").toList();
  const auto pos_a = find_series(series, "pos/a");
  ASSERT_FALSE(pos_a.isEmpty());
  const auto pts = pos_a.value("points").toList();
  EXPECT_EQ(pts.size(), 5);  // 5 条消息，未超抽稀预算
  // pos/a 末值 = 4（第 5 条 position[0]）
  EXPECT_DOUBLE_EQ(pts.last().toMap().value("y").toDouble(), 4.0);

  const auto pos_b = find_series(series, "pos/b");
  ASSERT_FALSE(pos_b.isEmpty());
  EXPECT_DOUBLE_EQ(pos_b.value("points").toList().last().toMap().value("y").toDouble(), 8.0);

  fs::remove_all(tmp);
}
