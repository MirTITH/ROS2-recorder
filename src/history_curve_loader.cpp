#include "data_recorder/history_curve_loader.hpp"

#include <QVariantMap>

#include <cstring>
#include <filesystem>
#include <map>
#include <vector>

#include <rclcpp/serialized_message.hpp>
#include <rosbag2_cpp/reader.hpp>

#include "data_recorder/topic_series.hpp"

namespace data_recorder
{

namespace
{
namespace fs = std::filesystem;

std::string rosbag_dir(const QString & session_dir)
{
  return (fs::path(session_dir.toStdString()) / "rosbag").string();
}

// 把 rcutils_uint8_array_t 的序列化字节包成 rclcpp::SerializedMessage（拷贝）。
rclcpp::SerializedMessage to_serialized(const rcutils_uint8_array_t & raw)
{
  rclcpp::SerializedMessage out(raw.buffer_length);
  auto & rcl = out.get_rcl_serialized_message();
  std::memcpy(rcl.buffer, raw.buffer, raw.buffer_length);
  rcl.buffer_length = raw.buffer_length;
  return out;
}

}  // namespace

HistoryCurveLoader::HistoryCurveLoader(QObject * parent)
: QObject(parent)
{
  register_builtin_extractors(registry_);
}

void HistoryCurveLoader::scanTimestamps(
  const QString & session_dir, const QStringList & topic_names)
{
  const std::string dir = rosbag_dir(session_dir);
  if (!fs::exists(dir)) { return; }

  // 仅关心配置里的 topic（topic_names 为空则全收）。
  std::map<std::string, std::vector<double>> dots_by_topic;
  std::map<std::string, std::string> types_by_topic;

  rosbag2_cpp::Reader reader;
  try {
    reader.open(dir);
  } catch (const std::exception &) {
    return;  // 包损坏/缺失：静默放弃，UI 保持空轨。
  }

  for (const auto & meta : reader.get_all_topics_and_types()) {
    types_by_topic[meta.name] = meta.type;
  }

  bool first = true;
  int64_t base_ns = 0;
  while (reader.has_next()) {
    auto bag_msg = reader.read_next();
    if (first) { base_ns = bag_msg->time_stamp; first = false; }
    const double t = static_cast<double>(bag_msg->time_stamp - base_ns) / 1e9;
    dots_by_topic[bag_msg->topic_name].push_back(t);
  }

  // 折叠点：每 topic 用一个有界 TopicSeries 做均匀降采样到预算。
  QVariantList topics;
  for (auto & [topic, times] : dots_by_topic) {
    if (!topic_names.isEmpty() && !topic_names.contains(QString::fromStdString(topic))) {
      continue;
    }
    TopicSeries buffer;
    for (const double t : times) { buffer.add_message_time(t); }

    QVariantMap topic_map;
    topic_map.insert("topicKey", QString::fromStdString(topic));
    QVariantList dots;
    for (const double t : buffer.message_times(/*budget=*/2000)) { dots.push_back(t); }
    topic_map.insert("messageDots", dots);
    topic_map.insert("series", QVariantList{});  // 折叠点阶段不带曲线
    topics.push_back(topic_map);
  }

  QVariantList types;
  for (auto & [topic, type] : types_by_topic) {
    if (!topic_names.isEmpty() && !topic_names.contains(QString::fromStdString(topic))) {
      continue;
    }
    QVariantMap tm;
    tm.insert("topicKey", QString::fromStdString(topic));
    tm.insert("rosType", QString::fromStdString(type));
    types.push_back(tm);
  }

  if (!types.isEmpty()) { emit topicTypesReady(types); }
  if (!topics.isEmpty()) { emit curvesReady(topics); }
}

void HistoryCurveLoader::extractTopic(
  const QString & session_dir, const QString & topic_name)
{
  const std::string dir = rosbag_dir(session_dir);
  if (!fs::exists(dir)) { return; }
  const std::string want = topic_name.toStdString();

  rosbag2_cpp::Reader reader;
  try {
    reader.open(dir);
  } catch (const std::exception &) {
    return;
  }

  std::string type;
  for (const auto & meta : reader.get_all_topics_and_types()) {
    if (meta.name == want) { type = meta.type; break; }
  }
  const ValueExtractor * extractor = type.empty() ? nullptr : registry_.get(type);

  TopicSeries buffer;
  bool first = true;
  int64_t base_ns = 0;
  while (reader.has_next()) {
    auto bag_msg = reader.read_next();
    if (first) { base_ns = bag_msg->time_stamp; first = false; }
    if (bag_msg->topic_name != want) { continue; }
    const double t = static_cast<double>(bag_msg->time_stamp - base_ns) / 1e9;
    buffer.add_message_time(t);
    if (extractor != nullptr && bag_msg->serialized_data) {
      const auto serialized = to_serialized(*bag_msg->serialized_data);
      for (const auto & sample : extractor->extract(serialized)) {
        buffer.add_sample(sample.series_key, t, sample.value);
      }
    }
  }

  QVariantMap topic_map;
  topic_map.insert("topicKey", topic_name);
  QVariantList dots;
  for (const double t : buffer.message_times(/*budget=*/2000)) { dots.push_back(t); }
  topic_map.insert("messageDots", dots);

  QVariantList series_arr;
  for (const auto & snap : buffer.snapshot(/*budget=*/2000)) {
    QVariantMap series_map;
    series_map.insert("key", QString::fromStdString(snap.key));
    QVariantList points;
    for (const auto & p : snap.points) {
      QVariantMap pt;
      pt.insert("x", p.first);
      pt.insert("y", p.second);
      points.push_back(pt);
    }
    series_map.insert("points", points);
    series_arr.push_back(series_map);
  }
  topic_map.insert("series", series_arr);

  emit curvesReady(QVariantList{topic_map});
}

}  // namespace data_recorder
