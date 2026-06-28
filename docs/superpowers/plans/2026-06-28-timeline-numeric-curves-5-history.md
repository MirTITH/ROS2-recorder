# 时间轴数值曲线 · Plan 5：历史会话回读 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 打开已录会话时，后台读其 `rosbag/` 取各 topic 消息时间戳画折叠点；某 topic 首次展开时懒回读该 topic、反序列化提取曲线。复用 Plan 4 的 QVariant 契约与 `AppController::onCurvesUpdated`/`onTopicTypesUpdated`，历史与实时殊途同归地喂进同一模型。

**Architecture:** 新增 `HistoryCurveLoader`（QObject，跑在自有 QThread，仿 `SessionPlayer` 模式）持一个注册了内置 extractor 的 `ValueExtractorRegistry`。`scanTimestamps(dir, topics)`：用 `rosbag2_cpp::Reader` 打开 `<dir>/rosbag`，一遍读出每 topic 的消息时间戳（相对 bag 起点的秒，**不反序列化**），抽稀后经 `curvesReady` 信号发折叠点 + 经 `topicTypesReady` 发各 topic 类型。`extractTopic(dir, topicName)`：再开 reader 只读该 topic、反序列化提取标量写入临时 `TopicSeries`，经 `curvesReady` 发该 topic 的抽稀曲线（含折叠点）。两信号用与 Plan 4 相同的 `QVariantList` 形态，`AppController` 直接接到既有 `onCurvesUpdated`/`onTopicTypesUpdated` 槽。`AppController::selectHistorySession` 触发 `scanTimestamps`；`setTopicExpanded` 在历史模式触发 `extractTopic`（懒加载，避免开包即解析整包）。

**Tech Stack:** C++17, rosbag2_cpp Reader, rclcpp 序列化, Qt6（QThread/QueuedConnection/QVariantList），gtest（rosbag round-trip，仿 test_rosbag_writer）。

**这是整个特性的 Plan 5 / 5（收官）**。复用 Plan 1 提取核心、Plan 2 模型、Plan 3 QML、Plan 4 的 QVariant 契约与 AppController 还原槽。

**风格提醒：** ament 风格，勿跑 clang-format。`history_curve_loader.*` 加进 `data_recorder_core` 的源列表（核心库已链接 rosbag2_cpp/rosbag2_storage/sensor_msgs/geometry_msgs/trajectory_msgs，**新源文件无需新 find_package**，仅需把 .hpp/.cpp 登进 CMake 源列表 + 加一个 gtest 目标）。

**构建/测试命令**：
```bash
source ~/.local/ros2_rc
cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R '<test_name>'
colcon test-result --verbose
```
git 仓库根在 `src/data_recorder`。

---

## QVariant 契约（与 Plan 4 完全一致）

`HistoryCurveLoader::curvesReady(QVariantList topics)`，每项 `QVariantMap`：
- `"topicKey"`: QString
- `"messageDots"`: QVariantList<double>
- `"series"`: QVariantList，每项 `{ "key": QString, "points": QVariantList<{"x":double,"y":double}> }`

`HistoryCurveLoader::topicTypesReady(QVariantList types)`，每项 `{ "topicKey": QString, "rosType": QString }`。

`AppController` 把这两个信号连到 Plan 4 已有的 `onCurvesUpdated` / `onTopicTypesUpdated` 槽——零新增还原逻辑。

---

## 文件结构

- Create `include/data_recorder/history_curve_loader.hpp` — `HistoryCurveLoader` 类。
- Create `src/history_curve_loader.cpp` — 实现（rosbag2 Reader 扫时间戳 + 懒提取）。
- Create `test/test_history_curve_loader.cpp` — 写含 JointState 的小 bag → 走 scan/extract → 断言 dots/series。
- Modify `CMakeLists.txt` — 登记新源 + gtest 目标。
- Modify `include/data_recorder/app_controller.hpp` / `src/app_controller.cpp` — 持 loader（自有线程）、连信号、selectHistorySession 触发 scan、setTopicExpanded 历史模式触发 extract。

---

## Task 1: HistoryCurveLoader 类 + rosbag 扫描/提取

**Files:**
- Create: `include/data_recorder/history_curve_loader.hpp`
- Create: `src/history_curve_loader.cpp`
- Create: `test/test_history_curve_loader.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写头文件**

Create `include/data_recorder/history_curve_loader.hpp`:
```cpp
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <string>

#include "data_recorder/value_extractor.hpp"

namespace data_recorder
{

// 历史会话数值曲线回读器。设计为可直接同步调用（测试）或 moveToThread 后经队列调用
// （AppController）。读 <session_dir>/rosbag：scanTimestamps 一遍取折叠点；
// extractTopic 懒回读单 topic 提取曲线。两者都经 curvesReady/topicTypesReady 发同 Plan 4 契约。
class HistoryCurveLoader : public QObject
{
  Q_OBJECT

public:
  explicit HistoryCurveLoader(QObject * parent = nullptr);

public slots:
  // 扫描整包：每 topic 的消息时间戳（相对 bag 起点秒）→ 折叠点；并发各 topic 类型。
  void scanTimestamps(const QString & session_dir, const QStringList & topic_names);
  // 懒回读单 topic：反序列化提取标量 → 该 topic 的曲线（含折叠点）。
  void extractTopic(const QString & session_dir, const QString & topic_name);

signals:
  void curvesReady(const QVariantList & topics);        // 同 LiveBridge::curvesUpdated 契约
  void topicTypesReady(const QVariantList & types);     // 同 LiveBridge::topicTypesUpdated 契约

private:
  ValueExtractorRegistry registry_;
};

}  // namespace data_recorder
```

- [ ] **Step 2: 写实现**

Create `src/history_curve_loader.cpp`:
```cpp
#include "data_recorder/history_curve_loader.hpp"

#include <QVariantMap>

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
```
Add `#include <cstring>` at the top of the .cpp for `std::memcpy`.

- [ ] **Step 3: 登记 CMake 源 + gtest 目标**

In `CMakeLists.txt`, insert into `DATA_RECORDER_HEADERS` (alphabetical-ish, near other history/include entries — put before `live_bridge.hpp` or wherever fits; exact position not critical):
```cmake
  include/data_recorder/history_curve_loader.hpp
```
Insert into `DATA_RECORDER_SOURCES`:
```cmake
  src/history_curve_loader.cpp
```

In the `if(BUILD_TESTING)` block, after the `test_rosbag_writer` target (the 3 lines at ~161–163), add:
```cmake
  ament_add_gtest(test_history_curve_loader test/test_history_curve_loader.cpp)
  target_link_libraries(test_history_curve_loader data_recorder_core Qt6::Test)
  ament_target_dependencies(test_history_curve_loader rclcpp rosbag2_cpp sensor_msgs)
```
（`Qt6::Test` 提供 `QSignalSpy`，与 `test_session_player` 的链接一致；`data_recorder_core` 已带 QtCore。）

- [ ] **Step 4: 写测试（rosbag round-trip）**

Create `test/test_history_curve_loader.cpp`:
```cpp
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
```

- [ ] **Step 5: 构建并跑测试**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_history_curve_loader && colcon test-result --verbose
```
Expected: `HistoryCurveLoader.ScanTimestampsEmitsDotsAndTypes` 与 `ExtractTopicEmitsSeries` PASS。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/history_curve_loader.hpp src/history_curve_loader.cpp \
  test/test_history_curve_loader.cpp CMakeLists.txt
git commit -m "feat(curves): HistoryCurveLoader — rosbag 扫时间戳 + 懒提取曲线

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: AppController 接入 loader（自有线程）+ 触发 scan/extract

**Files:**
- Modify: `include/data_recorder/app_controller.hpp`
- Modify: `src/app_controller.cpp`

- [ ] **Step 1: 头文件持 loader + 线程**

In `include/data_recorder/app_controller.hpp`, add a forward declaration near the others (after `class SessionPlayer;`):
```cpp
class HistoryCurveLoader;
```

In the private members (after `QThread * player_thread_{nullptr};`, ~line 115) add:
```cpp
  HistoryCurveLoader * curve_loader_{nullptr};
  QThread * curve_loader_thread_{nullptr};
```

- [ ] **Step 2: 构造 loader 于自有线程 + 连信号**

In `src/app_controller.cpp`, add the include at the top (after `session_player.hpp`):
```cpp
#include "data_recorder/history_curve_loader.hpp"
```

In the constructor, after the `player_thread_->start();` block (mirror the SessionPlayer setup), add:
```cpp
  curve_loader_thread_ = new QThread(this);
  curve_loader_ = new HistoryCurveLoader();
  curve_loader_->moveToThread(curve_loader_thread_);
  connect(curve_loader_thread_, &QThread::finished, curve_loader_, &QObject::deleteLater);
  connect(curve_loader_, &HistoryCurveLoader::curvesReady,
    this, &AppController::onCurvesUpdated);
  connect(curve_loader_, &HistoryCurveLoader::topicTypesReady,
    this, &AppController::onTopicTypesUpdated);
  curve_loader_thread_->start();
```

In the destructor (`AppController::~AppController()`), where `player_thread_` is stopped, mirror cleanup for the loader thread. Find the existing player-thread teardown (likely `player_thread_->quit(); player_thread_->wait();`) and add alongside:
```cpp
  if (curve_loader_thread_) {
    curve_loader_thread_->quit();
    curve_loader_thread_->wait();
  }
```

- [ ] **Step 3: selectHistorySession 触发 scan**

In `src/app_controller.cpp`, in `selectHistorySession`, inside the `if (row >= 0 && row < scanned_sessions_.size())` block — after `topic_model_.set_topics(session_topics);` and the player `load` invoke — add a scan trigger. Build the topic-name list and queue the loader call:
```cpp
    if (curve_loader_) {
      QStringList topic_names;
      for (const auto & e : session_topics) {
        topic_names.push_back(QString::fromStdString(e.topic_name));
      }
      const QString dir = QString::fromStdString(session.directory);
      QMetaObject::invokeMethod(curve_loader_,
        [loader = curve_loader_, dir, topic_names] {
          loader->scanTimestamps(dir, topic_names);
        },
        Qt::QueuedConnection);
    }
```

- [ ] **Step 4: setTopicExpanded 历史模式触发懒提取**

In `src/app_controller.cpp`, change `setTopicExpanded` to also kick a lazy extract when expanding in history mode:
```cpp
void AppController::setTopicExpanded(const QString & topic_key, bool expanded)
{
  topic_model_.setExpanded(topic_key, expanded);
  if (expanded && history_mode_ && curve_loader_ &&
    selected_session_row_ >= 0 &&
    selected_session_row_ < static_cast<int>(scanned_sessions_.size()))
  {
    const QString dir = QString::fromStdString(
      scanned_sessions_[static_cast<std::size_t>(selected_session_row_)].directory);
    QMetaObject::invokeMethod(curve_loader_,
      [loader = curve_loader_, dir, topic_key] {
        loader->extractTopic(dir, topic_key);
      },
      Qt::QueuedConnection);
  }
}
```

- [ ] **Step 5: 构建 + 全量测试不回归**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder && colcon test-result --verbose
```
Expected: 全量 0 failures（含 Task 1 两个 loader 测试；既有 AppController 测试不回归——新线程构造/析构干净）。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/app_controller.hpp src/app_controller.cpp
git commit -m "feat(curves): AppController 接 HistoryCurveLoader — 打开会话扫描 + 展开懒提取

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## 验收（Plan 5 完成标志 = 整特性收官）
- `colcon build` 通过；全量 `colcon test` 0 failures（含 history round-trip 两个新测试）。
- 产出：打开历史会话后台扫 `rosbag/` 填折叠点 + 类型（→ chevron 可点）；展开某 topic 懒回读提取曲线，复用 Plan 4 的还原槽喂进同一模型；实时与历史两条数据流殊途同归。
- 整特性五个 Plan 全部完成：提取核心（1）→ config+模型（2）→ QML（3）→ 实时（4）→ 历史（5）。

## Self-Review 结论
- **Spec 覆盖**：对应 spec「架构·历史（会话打开扫时间戳画折叠点；首次展开懒回读 rosbag、worker 线程）」「测试·历史 round-trip（写含 JointState 的小包→走回读→断言曲线）」。
- **占位符**：无 TBD；每步完整代码与命令。Task 1 Step 3 / Task 2 Step 2 标注「按邻近 Qt-using 目标镜像 Qt 链接 / 镜像 player 线程 teardown」是对既有工程约定的适配指引（先看邻近代码），非占位。
- **类型一致**：QVariant 契约字段（`topicKey`/`messageDots`/`series`/`key`/`points`/`x`/`y`/`rosType`）与 Plan 4 逐字一致，故能直接复用 `onCurvesUpdated`/`onTopicTypesUpdated`；复用 `register_builtin_extractors`、`ValueExtractorRegistry::get`、`ValueExtractor::extract`、`TopicSeries::{add_message_time,add_sample,message_times,snapshot}`、`RosbagWriter::{add_topic,write,close}`、`rosbag2_cpp::Reader::{open,get_all_topics_and_types,has_next,read_next}`（字段 `topic_name`/`time_stamp`/`serialized_data`）。线程模式镜像既有 `SessionPlayer`（moveToThread + QueuedConnection）。
```
