# 时间轴数值曲线 · Plan 4：实时数据流 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把实时（在线/录制）rosbag 消息接进数值核心：`RecorderEngine` 在消息回调里记折叠点时间戳、对可绘制类型反序列化提取标量写入 `TopicSeries`；定时器周期快照，经 `LiveBridge` 跨线程推到 GUI 线程的 `TopicListModel`（`updateTopicType` / `updateMessageDots` / `updateSeries`），UI（Plan 3）即显示真实节奏点与曲线。

**Architecture:** `RecorderEngine` 新持一个注册了内置 extractor 的 `ValueExtractorRegistry` 与 `std::map<topic, TopicSeries>`（`series_mutex_` 保护）。`on_rosbag_message(topic,type,msg)` 在 spin 线程：取 `t = live_edge_seconds_` 作时间轴秒，`add_message_time`，若 `registry.has(type)` 则反序列化提取 `add_sample`，并记录该话题首见的类型。一个 `curves_timer_`（~5 Hz）在 spin 线程为每话题构建抽稀快照，转成纯 Qt 值类型（`QVariantList`）交 `LiveBridge`；`LiveBridge` 用 `QMetaObject::invokeMethod(QueuedConnection)` marshal 到 GUI 线程发 `curvesUpdated` / `topicTypesUpdated` 信号；`AppController` 槽把它们拆给 `TopicListModel`。跨线程只传已注册的 Qt 值类型，不传 C++ 自定义结构。

**Tech Stack:** C++17, rclcpp 序列化, Qt6（QVariantList marshalling, QueuedConnection），gtest（含 Qt 的 test_ui_models）。

**这是整个特性的 Plan 4 / 5**。复用 Plan 1 的 `ValueExtractorRegistry` / `TopicSeries`、Plan 2 的模型回填方法、Plan 3 的 QML。历史路径在 Plan 5。

**风格提醒：** ament 风格，勿跑 clang-format。`recorder_engine.*` / `live_bridge.*` / `app_controller.*` 都已在 `data_recorder_core`，且核心库已链接 rclcpp/sensor_msgs/geometry_msgs/trajectory_msgs，**无需改 CMakeLists**。

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

## 数据契约（跨线程 QVariant 形态）

`LiveBridge::curvesUpdated(QVariantList topics)`，每项 `QVariantMap`：
- `"topicKey"`: QString
- `"messageDots"`: QVariantList<double>（抽稀后的折叠点秒）
- `"series"`: QVariantList，每项 QVariantMap `{ "key": QString, "points": QVariantList<{"x":double,"y":double}> }`

`LiveBridge::topicTypesUpdated(QVariantList types)`，每项 QVariantMap `{ "topicKey": QString, "rosType": QString }`。

`AppController` 槽把 `series` 还原成 `std::vector<TopicSeries::SeriesSnapshot>` 调 `updateSeries`，把 `messageDots` 转 `std::vector<double>` 调 `updateMessageDots`，把 `rosType` 调 `updateTopicType`。

---

## 文件结构

- Modify `include/data_recorder/recorder_engine.hpp` / `src/recorder_engine.cpp` — registry + per-topic TopicSeries + 类型记录；`on_rosbag_message` 写入；`curves_timer_` 快照推送；构造注册内置 extractor。
- Modify `include/data_recorder/live_bridge.hpp` / `src/live_bridge.cpp` — `push_curves` / `push_topic_types` + `curvesUpdated` / `topicTypesUpdated` 信号。
- Modify `src/app_controller.cpp`（+ 头声明槽）— 连接两信号到模型回填。
- Modify `test/test_ui_models.cpp` — AppController 槽把 QVariant 还原并回填模型的单测（不依赖 ROS 运行时，直接发信号/调槽）。

---

## Task 1: LiveBridge 新增 curves / topic-types 通道

**Files:**
- Modify: `include/data_recorder/live_bridge.hpp`
- Modify: `src/live_bridge.cpp`

- [ ] **Step 1: 头文件加方法与信号**

In `include/data_recorder/live_bridge.hpp`, in the `// —— ROS 线程调用 ——` block (after `void set_playback_mode(bool on);`, line 32) add:
```cpp
  void push_curves(const QVariantList & topics);
  void push_topic_types(const QVariantList & types);
```

Add `#include <QVariantList>` near the top includes (after `#include <QString>`):
```cpp
#include <QVariantList>
```

In the `signals:` block (after `void liveEdgeChanged(double seconds);`, line 40) add:
```cpp
  void curvesUpdated(const QVariantList & topics);        // QueuedConnection
  void topicTypesUpdated(const QVariantList & types);     // QueuedConnection
```

- [ ] **Step 2: 实现**

In `src/live_bridge.cpp`, after `set_live_edge` (line 48) add:
```cpp
void LiveBridge::push_curves(const QVariantList & topics)
{
  if (playback_mode_.load()) { return; }  // 回放模式由历史路径单独驱动
  QMetaObject::invokeMethod(this, "curvesUpdated", Qt::QueuedConnection,
    Q_ARG(QVariantList, topics));
}

void LiveBridge::push_topic_types(const QVariantList & types)
{
  if (playback_mode_.load()) { return; }
  QMetaObject::invokeMethod(this, "topicTypesUpdated", Qt::QueuedConnection,
    Q_ARG(QVariantList, types));
}
```

- [ ] **Step 3: 构建确认**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -5
```
Expected: 编译通过（信号/方法新增，无调用方报错）。

- [ ] **Step 4: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/live_bridge.hpp src/live_bridge.cpp
git commit -m "feat(curves): LiveBridge 新增 curvesUpdated/topicTypesUpdated 通道

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: AppController 连接信号 → 模型回填（含单测）

**Files:**
- Modify: `include/data_recorder/app_controller.hpp`
- Modify: `src/app_controller.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: 写失败测试**

In `test/test_ui_models.cpp`, append after the `AppController.ExposesExpandAndSeriesVisibilityToModel` test. These call the new slots directly (public), feeding the QVariant contract, and assert the model updated — no ROS runtime needed:
```cpp
TEST(AppController, OnTopicTypesUpdatedMarksPlottable)
{
  auto config = make_config_fixture();
  data_recorder::AppController controller(config);
  auto * model = controller.topicModel();

  int joint_row = -1;
  for (int r = 0; r < model->rowCount(); ++r) {
    if (model->data(model->index(r, 0), data_recorder::TopicListModel::TopicNameRole)
        .toString() == "/joint_states") { joint_row = r; break; }
  }
  ASSERT_GE(joint_row, 0);

  QVariantMap t;
  t.insert("topicKey", "/joint_states");
  t.insert("rosType", "sensor_msgs/msg/JointState");
  controller.onTopicTypesUpdated(QVariantList{t});

  EXPECT_TRUE(
    model->data(model->index(joint_row, 0), data_recorder::TopicListModel::IsPlottableRole)
      .toBool());
}

TEST(AppController, OnCurvesUpdatedFillsDotsAndSeries)
{
  auto config = make_config_fixture();
  data_recorder::AppController controller(config);
  auto * model = controller.topicModel();

  int joint_row = -1;
  for (int r = 0; r < model->rowCount(); ++r) {
    if (model->data(model->index(r, 0), data_recorder::TopicListModel::TopicNameRole)
        .toString() == "/joint_states") { joint_row = r; break; }
  }
  ASSERT_GE(joint_row, 0);

  QVariantMap point;
  point.insert("x", 0.5);
  point.insert("y", 1.25);
  QVariantMap series;
  series.insert("key", "pos/a");
  series.insert("points", QVariantList{point});
  QVariantMap topic;
  topic.insert("topicKey", "/joint_states");
  topic.insert("messageDots", QVariantList{0.5, 1.0});
  topic.insert("series", QVariantList{series});

  controller.onCurvesUpdated(QVariantList{topic});

  const auto dots =
    model->data(model->index(joint_row, 0), data_recorder::TopicListModel::MessageDotsRole)
      .toList();
  ASSERT_EQ(dots.size(), 2);
  EXPECT_DOUBLE_EQ(dots[0].toDouble(), 0.5);

  const auto list =
    model->data(model->index(joint_row, 0), data_recorder::TopicListModel::SeriesListRole)
      .toList();
  ASSERT_EQ(list.size(), 1);
  const auto entry = list[0].toMap();
  EXPECT_EQ(entry.value("key").toString().toStdString(), "pos/a");
  const auto pts = entry.value("points").toList();
  ASSERT_EQ(pts.size(), 1);
  EXPECT_DOUBLE_EQ(pts[0].toMap().value("x").toDouble(), 0.5);
  EXPECT_DOUBLE_EQ(pts[0].toMap().value("y").toDouble(), 1.25);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -15
```
Expected: 编译失败——`AppController` 无 `onTopicTypesUpdated` / `onCurvesUpdated`。

- [ ] **Step 3: 头文件声明槽**

In `include/data_recorder/app_controller.hpp`, in the `private:` section near the other `on*` handlers (after `void onLiveEdge(double seconds);`, line 108) add — but make them public so the test can call them directly. Place them in the `public:` block after `bool eventFilter(...)` declaration:
```cpp
  void onCurvesUpdated(const QVariantList & topics);
  void onTopicTypesUpdated(const QVariantList & types);
```

- [ ] **Step 4: 实现 + 连接**

In `src/app_controller.cpp`, where the existing bridge signals are connected (the `if (bridge_) { connect(...) }` block, ~lines 55–59), add two more connects:
```cpp
  if (bridge_) {
    connect(bridge_, &LiveBridge::statsUpdated, this, &AppController::onStatsUpdated);
    connect(bridge_, &LiveBridge::frameReady, this, &AppController::onFrameReady);
    connect(bridge_, &LiveBridge::liveEdgeChanged, this, &AppController::onLiveEdge);
    connect(bridge_, &LiveBridge::curvesUpdated, this, &AppController::onCurvesUpdated);
    connect(bridge_, &LiveBridge::topicTypesUpdated, this, &AppController::onTopicTypesUpdated);
  }
```

Add the two methods near `onStatsUpdated` (after it). Include `<data_recorder/topic_series.hpp>` at the top of the file if not present:
```cpp
void AppController::onTopicTypesUpdated(const QVariantList & types)
{
  for (const auto & v : types) {
    const auto m = v.toMap();
    topic_model_.updateTopicType(
      m.value("topicKey").toString(), m.value("rosType").toString());
  }
}

void AppController::onCurvesUpdated(const QVariantList & topics)
{
  for (const auto & v : topics) {
    const auto m = v.toMap();
    const QString topic_key = m.value("topicKey").toString();

    std::vector<double> dots;
    const auto dot_list = m.value("messageDots").toList();
    dots.reserve(static_cast<std::size_t>(dot_list.size()));
    for (const auto & d : dot_list) { dots.push_back(d.toDouble()); }
    topic_model_.updateMessageDots(topic_key, dots);

    std::vector<TopicSeries::SeriesSnapshot> series;
    const auto series_list = m.value("series").toList();
    series.reserve(static_cast<std::size_t>(series_list.size()));
    for (const auto & s : series_list) {
      const auto sm = s.toMap();
      TopicSeries::SeriesSnapshot snap;
      snap.key = sm.value("key").toString().toStdString();
      const auto pts = sm.value("points").toList();
      snap.points.reserve(static_cast<std::size_t>(pts.size()));
      for (const auto & p : pts) {
        const auto pm = p.toMap();
        snap.points.emplace_back(pm.value("x").toDouble(), pm.value("y").toDouble());
      }
      series.push_back(std::move(snap));
    }
    topic_model_.updateSeries(topic_key, series);
  }
}
```

Confirm `app_controller.cpp` includes `#include "data_recorder/topic_series.hpp"` — it is transitively included via `ui_models.hpp`, but add it explicitly at the top for clarity:
```cpp
#include "data_recorder/topic_series.hpp"
```

- [ ] **Step 5: 跑测试确认通过 + 全量不回归**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder && colcon test-result --verbose
```
Expected: 两个新用例 PASS；全量 0 failures。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/app_controller.hpp src/app_controller.cpp test/test_ui_models.cpp
git commit -m "feat(curves): AppController 连 curvesUpdated/topicTypesUpdated → 模型回填

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: RecorderEngine 提取 + 周期快照推送

**Files:**
- Modify: `include/data_recorder/recorder_engine.hpp`
- Modify: `src/recorder_engine.cpp`

本 task 无独立单测（依赖 ROS spin / 实时消息；端到端在人工核验）。提取逻辑本身已在 Plan 1 充分单测；本 task 只是接线 + 节流推送。AppController 还原契约已在 Task 2 单测。

- [ ] **Step 1: 头文件加成员**

In `include/data_recorder/recorder_engine.hpp`, add includes near the top (after `#include "data_recorder/topic_rate_monitor.hpp"`):
```cpp
#include "data_recorder/topic_series.hpp"
#include "data_recorder/value_extractor.hpp"
```

In the private members (after `std::mutex dims_mutex_;`, line 74) add:
```cpp
  // 数值曲线核心：registry 判可绘制 + 每 topic 时间序列缓冲。
  ValueExtractorRegistry extractor_registry_;
  std::map<std::string, TopicSeries> series_;     // topic -> 缓冲
  std::map<std::string, std::string> topic_types_;  // topic -> ROS 类型（首见记录）
  std::set<std::string> announced_types_;          // 已经推过类型的 topic（防重复）
  std::mutex series_mutex_;                         // 保护以上四者（spin 线程写，timer 线程读）
```

Add a timer member near the other timers (after `rclcpp::TimerBase::SharedPtr live_edge_timer_;`, line 106):
```cpp
  rclcpp::TimerBase::SharedPtr curves_timer_;
```

- [ ] **Step 2: 构造时注册内置 extractor + 起 curves_timer_**

In `src/recorder_engine.cpp`, in the constructor (after `setup_subscriptions();`, line 51) add:
```cpp
  register_builtin_extractors(extractor_registry_);
```

After the `live_edge_timer_` setup block (ends ~line 85), add the curves timer:
```cpp
  // 数值曲线快照推送（5 Hz）：把每 topic 的折叠点 + 抽稀曲线打成 QVariant 交 LiveBridge。
  curves_timer_ = node_->create_wall_timer(200ms, [this]() {
    if (!bridge_) { return; }
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
      for (auto & [topic, buffer] : series_) {
        QVariantMap topic_map;
        topic_map.insert("topicKey", QString::fromStdString(topic));

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
        topics.push_back(topic_map);
      }
    }
    if (!new_types.isEmpty()) { bridge_->push_topic_types(new_types); }
    if (!topics.isEmpty()) { bridge_->push_curves(topics); }
  });
```

Add `#include <QVariantList>` / `#include <QVariantMap>` / `#include <QString>` near the top includes of `src/recorder_engine.cpp` (Qt types used in the timer).

- [ ] **Step 3: on_rosbag_message 写入 series**

In `src/recorder_engine.cpp`, in `on_rosbag_message` (after the rate-monitor block, around line 187, before `if (!recording_.load()) { return; }`) add the curve-extraction write. Use `live_edge_seconds_` as the timeline second (matches the dots/playhead used elsewhere):
```cpp
  // 数值曲线：记折叠点；可绘制类型则反序列化提取标量。用 live edge 秒做时间轴（与播放头同源）。
  const double t_seconds = live_edge_seconds_.load();
  {
    std::lock_guard<std::mutex> lock(series_mutex_);
    topic_types_.emplace(topic, type);
    auto & buffer = series_[topic];
    buffer.add_message_time(t_seconds);
    const ValueExtractor * extractor = extractor_registry_.get(type);
    if (extractor != nullptr) {
      for (const auto & sample : extractor->extract(*msg)) {
        buffer.add_sample(sample.series_key, t_seconds, sample.value);
      }
    }
  }
```

Note: `series_[topic]` default-constructs `TopicSeries(20000)` (bounded). `extract(*msg)` takes `const rclcpp::SerializedMessage &`; `msg` is `shared_ptr<rclcpp::SerializedMessage>`, so `*msg` is correct.

- [ ] **Step 4: 构建确认**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -8
```
Expected: 编译通过。

- [ ] **Step 5: 全量测试不回归**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon test --packages-select data_recorder && colcon test-result --verbose
```
Expected: 全量 0 failures（既有测试不受影响；引擎构造现注册 registry + 起 timer，但无消息时 timer 近 no-op）。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/recorder_engine.hpp src/recorder_engine.cpp
git commit -m "feat(curves): RecorderEngine 实时提取 + 周期快照推送

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## 验收（Plan 4 完成标志）
- `colcon build` 通过；全量 `colcon test` 0 failures（含 Task 2 两个 AppController 还原单测）。
- 产出：录制/在线时，`RecorderEngine` 按 `live_edge` 秒记折叠点、对 JointState/WrenchStamped/JointTrajectory 提取曲线写入有界 `TopicSeries`，5 Hz 抽稀快照经 `LiveBridge` QueuedConnection 推 GUI；`TopicListModel` 折叠点/曲线/可绘制态实时刷新；Plan 3 UI 显示真实数据。
- 历史会话（已录 rosbag 回读）在 Plan 5。

## Self-Review 结论
- **Spec 覆盖**：对应 spec「架构·实时」「数据量与保留（抽稀+有界环形缓冲）」。历史在 Plan 5。
- **占位符**：无 TBD；每步完整代码与命令。Task 3 无独立单测有据：提取与抽稀已在 Plan 1 全测，QVariant 还原在 Task 2 测，仅接线/节流不便在无 ROS 运行时下单测，端到端走人工核验（spec「测试·人工」）。
- **类型一致**：QVariant 契约（`topicKey`/`messageDots`/`series`/`key`/`points`/`x`/`y`/`rosType`）在 LiveBridge 推送、AppController 还原、Task 2 测试三处一致；复用 `ValueExtractorRegistry::get`、`ValueExtractor::extract`、`TopicSeries::{add_message_time,add_sample,message_times,snapshot,SeriesSnapshot}`、`register_builtin_extractors`、模型 `updateTopicType/updateMessageDots/updateSeries`，与 Plan 1/2 签名吻合。
