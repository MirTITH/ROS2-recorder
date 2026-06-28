# 时间轴数值曲线 · Plan 2：Config 选项 + 模型接线 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 Plan 1 的提取核心接到 UI 数据层：config 支持每话题 `ui_expanded` 选项；`TopicListModel` 增加 `isPlottable` / `isExpanded` / `messageDots` / 结构化 `seriesList` 四个 role 与 `setExpanded` / `setSeriesVisible` / 折叠点与曲线的回填方法；`AppController` 持有 registry 并把两个写操作暴露给 QML。

**Architecture:** config 解析把 topics 列表项从「只认裸字符串」扩展为「裸字符串 **或** 单键 map」，map 的 value 读 `ui_expanded` 写入新增的 `TopicEntry::default_expanded`。`TopicListModel` 每行新增展开态、可绘制态、折叠点数组与结构化曲线数组；可绘制态经注入的 `ValueExtractorRegistry`（Plan 1 产物）按 ROS 类型判定；曲线可见性默认按 series_key 前缀（`vel/`、`eff/` 默认隐藏，其余可见），用户切换的可见性与配色按 series_key 稳定保留，跨回填刷新不丢。`AppController` 拥有一个注册了内置 extractor 的 registry 并注入模型。

**Tech Stack:** C++17, Qt6 (QAbstractListModel / QVariant), yaml-cpp, gtest（含 Qt 的 test_ui_models）。

**这是整个特性的 Plan 2 / 5**（见 spec [2026-06-28-timeline-numeric-curves-design.md](../specs/2026-06-28-timeline-numeric-curves-design.md)）。复用 Plan 1 的 `ValueExtractorRegistry` / `TopicSeries`。本计划**不**含 QML 渲染（Plan 3）与实时/历史数据流（Plan 4/5）——只产出可单测的 config 解析与模型层；数据由测试手工喂入。

**风格提醒：** 本仓是 ament 风格（包根已有官方 `.clang-format`）；手写匹配周边风格即可，勿对本包跑 clang-format。`ui_models.cpp` / `value_extractor.cpp` / `topic_series.cpp` 均已在 `data_recorder_core`，且该库已链接 `rclcpp sensor_msgs geometry_msgs trajectory_msgs`，故本计划**无需改 CMakeLists**。

**构建/测试命令**（每个 task 都用）：
```bash
source ~/.local/ros2_rc            # humble + DOMAIN 43
cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R '<test_name>'
colcon test-result --verbose
```
git 仓库根在 `src/data_recorder`（不是 ws 根）；提交在该目录下执行。

---

## 文件结构

- Modify `include/data_recorder/config_model.hpp` — `TopicEntry` 加 `bool default_expanded{false}`。
- Modify `src/config_model.cpp` — topics 列表项解析支持「裸字符串 | 单键 map(`ui_expanded`)」。
- Modify `test/test_config_model.cpp` — 解析新形态的用例。
- Modify `include/data_recorder/ui_models.hpp` — `TopicListModel` 新 role / 字段 / 方法；前置声明 `ValueExtractorRegistry`；include `topic_series.hpp`。
- Modify `src/ui_models.cpp` — 实现新 role、`setExpanded`、`updateTopicType`、`updateMessageDots`、`updateSeries`、`setSeriesVisible`、默认可见性与配色。
- Modify `test/test_ui_models.cpp` — 模型层用例。
- Modify `include/data_recorder/app_controller.hpp` / `src/app_controller.cpp` — 持有 registry、注入模型、暴露 `setTopicExpanded` / `setSeriesVisible`。

---

## Task 1: Config `ui_expanded` → `TopicEntry::default_expanded`

**Files:**
- Modify: `include/data_recorder/config_model.hpp`
- Modify: `src/config_model.cpp`
- Modify: `test/test_config_model.cpp`

- [ ] **Step 1: 写失败测试**

In `test/test_config_model.cpp`, append these tests before the final `}` of the file (after `ThrowsWhenFileMissing`):
```cpp
TEST(ConfigModel, ScalarTopicDefaultsToCollapsed)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /joint_states
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 1u);
  EXPECT_EQ(config.topics[0].topic_name, "/joint_states");
  EXPECT_FALSE(config.topics[0].default_expanded);
}

TEST(ConfigModel, SingleKeyMapReadsUiExpanded)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /tf
      - /joint_states: { ui_expanded: true }
      - /wrench: { ui_expanded: false }
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 3u);
  EXPECT_EQ(config.topics[0].topic_name, "/tf");
  EXPECT_FALSE(config.topics[0].default_expanded);          // 裸字符串
  EXPECT_EQ(config.topics[1].topic_name, "/joint_states");
  EXPECT_TRUE(config.topics[1].default_expanded);           // map ui_expanded: true
  EXPECT_EQ(config.topics[2].topic_name, "/wrench");
  EXPECT_FALSE(config.topics[2].default_expanded);          // map ui_expanded: false
}

TEST(ConfigModel, SingleKeyMapWithoutUiExpandedDefaultsCollapsed)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /joint_states: {}
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 1u);
  EXPECT_EQ(config.topics[0].topic_name, "/joint_states");
  EXPECT_FALSE(config.topics[0].default_expanded);
}

TEST(ConfigModel, MultiKeyTopicMapThrows)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /a: { ui_expanded: true }
        /b: { ui_expanded: true }
)yaml");

  const data_recorder::ConfigModel model;
  EXPECT_THROW(model.load_from_file(path), data_recorder::ConfigError);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -20
```
Expected: 编译失败——`TopicEntry` 无 `default_expanded` 成员。

- [ ] **Step 3: 加 `default_expanded` 字段**

In `include/data_recorder/config_model.hpp`, in `struct TopicEntry`, add the field after `ui_category` (line 24):
```cpp
struct TopicEntry
{
  std::string topic_name;
  std::string backend_name;
  int group_index{};
  TopicUiCategory ui_category{TopicUiCategory::NumericTrack};
  bool default_expanded{false};
  std::map<std::string, std::string> params;
};
```

- [ ] **Step 4: 解析裸字符串 | 单键 map**

In `src/config_model.cpp`, replace the topic loop body (currently lines 97–107, the `for (const auto & topic_node : group_node["topics"])` block) with:
```cpp
      for (const auto & topic_node : group_node["topics"]) {
        TopicEntry topic;
        topic.backend_name = backend_name;
        topic.group_index = group_index;
        topic.params = params;

        if (topic_node.IsScalar()) {
          topic.topic_name = topic_node.as<std::string>();
          topic.default_expanded = false;
        } else if (topic_node.IsMap()) {
          if (topic_node.size() != 1) {
            throw ConfigError("a topic map entry must have exactly one key (the topic name)");
          }
          const auto pair = *topic_node.begin();
          topic.topic_name = pair.first.as<std::string>();
          const auto & options = pair.second;
          if (options && options.IsMap() && options["ui_expanded"]) {
            topic.default_expanded = options["ui_expanded"].as<bool>();
          } else {
            topic.default_expanded = false;
          }
        } else {
          throw ConfigError("each topic must be a string or a single-key map");
        }

        topic.ui_category = is_camera_topic(topic.topic_name, topic.backend_name) ?
          TopicUiCategory::CameraPreview : TopicUiCategory::NumericTrack;

        config.topics.push_back(topic);
      }
```

- [ ] **Step 5: 跑测试确认通过 + 不回归**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_config_model && colcon test-result --verbose
```
Expected: `ConfigModel.*` 全 PASS（含旧的 `WrapsMalformedTopicValueAsConfigError`——序列节点既非 scalar 又非 map，落到 else 分支抛 `ConfigError`；含 `LoadsExampleShape` 等旧用例）；0 failures。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/config_model.hpp src/config_model.cpp test/test_config_model.cpp
git commit -m "feat(curves): config 支持 topic ui_expanded（default_expanded）

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: 模型 `isExpanded` role + `setExpanded`

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: 写失败测试**

In `test/test_ui_models.cpp`, append after `TEST(TopicListModel, ClassifiesCameraNumericAndEmptyTracks)` (ends ~line 189):
```cpp
TEST(TopicListModel, IsExpandedInitializesFromDefaultExpanded)
{
  data_recorder::TopicEntry collapsed;
  collapsed.topic_name = "/tf";
  collapsed.backend_name = "rosbag";
  collapsed.default_expanded = false;

  data_recorder::TopicEntry expanded;
  expanded.topic_name = "/joint_states";
  expanded.backend_name = "rosbag";
  expanded.default_expanded = true;

  data_recorder::TopicListModel model;
  model.set_topics({collapsed, expanded});

  EXPECT_FALSE(
    model.data(model.index(0, 0), data_recorder::TopicListModel::IsExpandedRole).toBool());
  EXPECT_TRUE(
    model.data(model.index(1, 0), data_recorder::TopicListModel::IsExpandedRole).toBool());
}

TEST(TopicListModel, SetExpandedTogglesAndEmitsDataChanged)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  int signal_count = 0;
  QVector<int> changed_roles;
  QObject::connect(
    &model, &QAbstractItemModel::dataChanged,
    [&](const QModelIndex &, const QModelIndex &, const QList<int> & roles) {
      ++signal_count;
      changed_roles = roles;
    });

  model.setExpanded("/joint_states", true);
  EXPECT_TRUE(
    model.data(model.index(0, 0), data_recorder::TopicListModel::IsExpandedRole).toBool());
  EXPECT_EQ(signal_count, 1);
  EXPECT_TRUE(changed_roles.contains(data_recorder::TopicListModel::IsExpandedRole));

  // 同值不重复发信号
  model.setExpanded("/joint_states", true);
  EXPECT_EQ(signal_count, 1);

  // 未知 topic 安全无操作
  model.setExpanded("/nope", true);
  EXPECT_EQ(signal_count, 1);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -20
```
Expected: 编译失败——无 `IsExpandedRole`、无 `setExpanded`。

- [ ] **Step 3: 加 role 声明、字段、方法签名**

In `include/data_recorder/ui_models.hpp`, in `enum Roles` of `TopicListModel`, add `IsExpandedRole` after `FrameSeqRole`:
```cpp
  enum Roles
  {
    TopicNameRole = Qt::UserRole + 1,
    BackendNameRole,
    IsVisibleRole,
    FrequencyTextRole,
    SeriesColorRole,
    TrackKindRole,
    IsCameraRole,
    IsDrawableRole,
    SeriesListRole,
    ResolutionTextRole,
    FrameSeqRole,
    IsExpandedRole,
  };
```

In the same class, add the method declaration after `void set_topics(std::vector<TopicEntry> topics);` (line 57):
```cpp
  void setExpanded(const QString & topic_key, bool expanded);
```

In `struct TopicRow`, add the field after `int frame_seq{0};`:
```cpp
    bool is_expanded{false};
```

- [ ] **Step 4: 实现 role 读取、初始化、`setExpanded`**

In `src/ui_models.cpp`, in `TopicListModel::data`, add a case before `default:` (after the `FrameSeqRole` case ~line 106):
```cpp
    case IsExpandedRole:
      return row.is_expanded;
```

In `TopicListModel::roleNames`, add after `{FrameSeqRole, "frameSeq"},`:
```cpp
    {IsExpandedRole, "isExpanded"},
```

In `TopicListModel::set_topics`, initialize the field — add after `row.frame_seq` is implicitly default (the loop sets fields ~lines 183–193); add this line before `topics_.push_back(std::move(row));`:
```cpp
    row.is_expanded = row.topic.default_expanded;
```

Add the new method after `set_topics` (before `updateStats`, ~line 198):
```cpp
void TopicListModel::setExpanded(const QString & topic_key, bool expanded)
{
  for (std::size_t i = 0; i < topics_.size(); ++i) {
    auto & row = topics_[i];
    if (QString::fromStdString(row.topic.topic_name) != topic_key) { continue; }
    if (row.is_expanded == expanded) { return; }
    row.is_expanded = expanded;
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {IsExpandedRole});
    return;
  }
}
```

- [ ] **Step 5: 跑测试确认通过**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_ui_models && colcon test-result --verbose
```
Expected: 新增两个用例 + 旧 `TopicListModel.*` 全 PASS。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat(curves): TopicListModel isExpanded role + setExpanded

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: 模型 `isPlottable` role + registry 注入 + `updateTopicType`

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: 写失败测试**

In `test/test_ui_models.cpp`, ensure the value_extractor header is included near the top includes (after `#include "data_recorder/ui_models.hpp"`):
```cpp
#include "data_recorder/value_extractor.hpp"
```
Then append after the Task 2 tests:
```cpp
TEST(TopicListModel, IsPlottableFromRegistryByType)
{
  data_recorder::ValueExtractorRegistry registry;
  data_recorder::register_builtin_extractors(registry);

  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";

  data_recorder::TopicListModel model;
  model.set_extractor_registry(&registry);
  model.set_topics({topic});

  // 类型未知前不可绘制
  EXPECT_FALSE(
    model.data(model.index(0, 0), data_recorder::TopicListModel::IsPlottableRole).toBool());

  int signal_count = 0;
  QVector<int> changed_roles;
  QObject::connect(
    &model, &QAbstractItemModel::dataChanged,
    [&](const QModelIndex &, const QModelIndex &, const QList<int> & roles) {
      ++signal_count;
      changed_roles = roles;
    });

  model.updateTopicType("/joint_states", "sensor_msgs/msg/JointState");
  EXPECT_TRUE(
    model.data(model.index(0, 0), data_recorder::TopicListModel::IsPlottableRole).toBool());
  EXPECT_EQ(signal_count, 1);
  EXPECT_TRUE(changed_roles.contains(data_recorder::TopicListModel::IsPlottableRole));
}

TEST(TopicListModel, IsPlottableFalseForUnsupportedType)
{
  data_recorder::ValueExtractorRegistry registry;
  data_recorder::register_builtin_extractors(registry);

  data_recorder::TopicEntry topic;
  topic.topic_name = "/tf";
  topic.backend_name = "rosbag";

  data_recorder::TopicListModel model;
  model.set_extractor_registry(&registry);
  model.set_topics({topic});

  model.updateTopicType("/tf", "tf2_msgs/msg/TFMessage");
  EXPECT_FALSE(
    model.data(model.index(0, 0), data_recorder::TopicListModel::IsPlottableRole).toBool());
}

TEST(TopicListModel, IsPlottableFalseWithoutRegistry)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";

  data_recorder::TopicListModel model;  // 未注入 registry
  model.set_topics({topic});

  model.updateTopicType("/joint_states", "sensor_msgs/msg/JointState");
  EXPECT_FALSE(
    model.data(model.index(0, 0), data_recorder::TopicListModel::IsPlottableRole).toBool());
}
```

- [ ] **Step 2: 跑测试确认失败**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -20
```
Expected: 编译失败——无 `IsPlottableRole` / `set_extractor_registry` / `updateTopicType`。

- [ ] **Step 3: 头文件：前置声明、role、方法、字段**

In `include/data_recorder/ui_models.hpp`, after the existing includes and inside `namespace data_recorder` but before `class TopicListModel`, add a forward declaration:
```cpp
class ValueExtractorRegistry;
```
(Place it right after the `namespace data_recorder\n{` opening brace, before `class TopicListModel`.)

In `enum Roles`, add `IsPlottableRole` after `IsExpandedRole`:
```cpp
    IsExpandedRole,
    IsPlottableRole,
```

Add method declarations after the `setExpanded` declaration:
```cpp
  void set_extractor_registry(const ValueExtractorRegistry * registry);
  void updateTopicType(const QString & topic_key, const QString & ros_type);
```

In `struct TopicRow`, add after `bool is_expanded{false};`:
```cpp
    bool is_plottable{false};
```

In the private members of `TopicListModel`, add after `std::vector<TopicRow> topics_;`:
```cpp
  const ValueExtractorRegistry * registry_{nullptr};
```

- [ ] **Step 4: 实现**

In `src/ui_models.cpp`, add the include near the top (after `#include "data_recorder/ui_models.hpp"`):
```cpp
#include "data_recorder/value_extractor.hpp"
```

In `TopicListModel::data`, add a case after the `IsExpandedRole` case:
```cpp
    case IsPlottableRole:
      return row.is_plottable;
```

In `TopicListModel::roleNames`, add after `{IsExpandedRole, "isExpanded"},`:
```cpp
    {IsPlottableRole, "isPlottable"},
```

Add the two methods after `setExpanded` (before `updateStats`):
```cpp
void TopicListModel::set_extractor_registry(const ValueExtractorRegistry * registry)
{
  registry_ = registry;
}

void TopicListModel::updateTopicType(const QString & topic_key, const QString & ros_type)
{
  const bool plottable = registry_ != nullptr && registry_->has(ros_type.toStdString());
  for (std::size_t i = 0; i < topics_.size(); ++i) {
    auto & row = topics_[i];
    if (QString::fromStdString(row.topic.topic_name) != topic_key) { continue; }
    if (row.is_plottable == plottable) { return; }
    row.is_plottable = plottable;
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {IsPlottableRole});
    return;
  }
}
```

- [ ] **Step 5: 跑测试确认通过**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_ui_models && colcon test-result --verbose
```
Expected: 三个新用例 + 旧用例全 PASS。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat(curves): TopicListModel isPlottable role 经 registry 注入判定

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: 模型 `messageDots` role + `updateMessageDots`（折叠点）

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: 写失败测试**

In `test/test_ui_models.cpp`, append after the Task 3 tests:
```cpp
TEST(TopicListModel, UpdateMessageDotsExposesTimestamps)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  EXPECT_TRUE(
    model.data(model.index(0, 0), data_recorder::TopicListModel::MessageDotsRole)
      .toList().isEmpty());

  int signal_count = 0;
  QVector<int> changed_roles;
  QObject::connect(
    &model, &QAbstractItemModel::dataChanged,
    [&](const QModelIndex &, const QModelIndex &, const QList<int> & roles) {
      ++signal_count;
      changed_roles = roles;
    });

  model.updateMessageDots("/joint_states", {0.0, 1.5, 3.0});
  const auto dots =
    model.data(model.index(0, 0), data_recorder::TopicListModel::MessageDotsRole).toList();
  ASSERT_EQ(dots.size(), 3);
  EXPECT_DOUBLE_EQ(dots[0].toDouble(), 0.0);
  EXPECT_DOUBLE_EQ(dots[2].toDouble(), 3.0);
  EXPECT_EQ(signal_count, 1);
  EXPECT_TRUE(changed_roles.contains(data_recorder::TopicListModel::MessageDotsRole));

  // 未知 topic 安全无操作
  model.updateMessageDots("/nope", {9.0});
  EXPECT_EQ(signal_count, 1);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -20
```
Expected: 编译失败——无 `MessageDotsRole` / `updateMessageDots`。

- [ ] **Step 3: 头文件：role、方法、字段**

In `include/data_recorder/ui_models.hpp`, in `enum Roles`, add after `IsPlottableRole`:
```cpp
    IsPlottableRole,
    MessageDotsRole,
```

Add method declaration after `updateTopicType`:
```cpp
  void updateMessageDots(const QString & topic_key, const std::vector<double> & seconds);
```

In `struct TopicRow`, add after `bool is_plottable{false};`:
```cpp
    QVariantList message_dots;
```

- [ ] **Step 4: 实现**

In `src/ui_models.cpp`, in `TopicListModel::data`, add a case after `IsPlottableRole`:
```cpp
    case MessageDotsRole:
      return row.message_dots;
```

In `roleNames`, add after `{IsPlottableRole, "isPlottable"},`:
```cpp
    {MessageDotsRole, "messageDots"},
```

Add the method after `updateTopicType` (before `updateStats`):
```cpp
void TopicListModel::updateMessageDots(
  const QString & topic_key, const std::vector<double> & seconds)
{
  for (std::size_t i = 0; i < topics_.size(); ++i) {
    auto & row = topics_[i];
    if (QString::fromStdString(row.topic.topic_name) != topic_key) { continue; }
    QVariantList dots;
    dots.reserve(static_cast<int>(seconds.size()));
    for (const double t : seconds) { dots.append(t); }
    row.message_dots = std::move(dots);
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {MessageDotsRole});
    return;
  }
}
```

- [ ] **Step 5: 跑测试确认通过**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_ui_models && colcon test-result --verbose
```
Expected: 新用例 + 旧用例全 PASS。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat(curves): TopicListModel messageDots role（折叠点回填）

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: 结构化 `seriesList` + 默认可见性 + 配色 + `updateSeries` + `setSeriesVisible`

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Modify: `test/test_ui_models.cpp`

每条 series 在 `seriesList` 里是一个 `QVariantMap`：`{key, label, color, visible, points}`，`points` 为 `[{x,y}]`（x=秒、y=值），与现有 [TimelineTrackRow.qml](../../../qml/components/TimelineTrackRow.qml) 读取的 `entry.points`/`entry.color` 兼容。可见性默认：series_key 以 `vel/` 或 `eff/` 开头默认隐藏，其余默认可见（满足 spec 表：JointState 的 vel/eff 隐藏，force/torque、cmd 可见）。用户经 `setSeriesVisible` 改过的可见性与每行的配色按 series_key 稳定保留，跨 `updateSeries` 刷新不丢。

- [ ] **Step 1: 写失败测试**

In `test/test_ui_models.cpp`, ensure `#include "data_recorder/topic_series.hpp"` is present near the top includes (add it if absent). Then append after the Task 4 test:
```cpp
namespace
{
QVariantMap find_series(const QVariantList & list, const QString & key)
{
  for (const auto & v : list) {
    const auto m = v.toMap();
    if (m.value("key").toString() == key) { return m; }
  }
  return {};
}
}  // namespace

TEST(TopicListModel, UpdateSeriesBuildsStructuredEntries)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  std::vector<data_recorder::TopicSeries::SeriesSnapshot> snap;
  snap.push_back({"pos/a", {{0.0, 1.0}, {1.0, 2.0}}});
  snap.push_back({"vel/a", {{0.0, 0.5}}});
  model.updateSeries("/joint_states", snap);

  const auto list =
    model.data(model.index(0, 0), data_recorder::TopicListModel::SeriesListRole).toList();
  ASSERT_EQ(list.size(), 2);

  const auto pos = find_series(list, "pos/a");
  ASSERT_FALSE(pos.isEmpty());
  EXPECT_EQ(pos.value("label").toString().toStdString(), "pos/a");
  EXPECT_TRUE(pos.value("visible").toBool());               // 默认可见
  const auto pts = pos.value("points").toList();
  ASSERT_EQ(pts.size(), 2);
  EXPECT_DOUBLE_EQ(pts[0].toMap().value("x").toDouble(), 0.0);
  EXPECT_DOUBLE_EQ(pts[0].toMap().value("y").toDouble(), 1.0);
  EXPECT_DOUBLE_EQ(pts[1].toMap().value("x").toDouble(), 1.0);
  EXPECT_DOUBLE_EQ(pts[1].toMap().value("y").toDouble(), 2.0);
  const auto color = pos.value("color").toString();
  EXPECT_TRUE(color.startsWith("#"));

  const auto vel = find_series(list, "vel/a");
  ASSERT_FALSE(vel.isEmpty());
  EXPECT_FALSE(vel.value("visible").toBool());              // vel/ 默认隐藏
}

TEST(TopicListModel, SetSeriesVisiblePersistsAcrossUpdate)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  std::vector<data_recorder::TopicSeries::SeriesSnapshot> snap;
  snap.push_back({"pos/a", {{0.0, 1.0}}});
  model.updateSeries("/joint_states", snap);

  int signal_count = 0;
  QVector<int> changed_roles;
  QObject::connect(
    &model, &QAbstractItemModel::dataChanged,
    [&](const QModelIndex &, const QModelIndex &, const QList<int> & roles) {
      ++signal_count;
      changed_roles = roles;
    });

  model.setSeriesVisible("/joint_states", "pos/a", false);
  EXPECT_EQ(signal_count, 1);
  EXPECT_TRUE(changed_roles.contains(data_recorder::TopicListModel::SeriesListRole));
  {
    const auto list =
      model.data(model.index(0, 0), data_recorder::TopicListModel::SeriesListRole).toList();
    EXPECT_FALSE(find_series(list, "pos/a").value("visible").toBool());
  }

  // 再次回填曲线：用户设的隐藏应保留
  std::vector<data_recorder::TopicSeries::SeriesSnapshot> snap2;
  snap2.push_back({"pos/a", {{0.0, 1.0}, {2.0, 9.0}}});
  model.updateSeries("/joint_states", snap2);
  {
    const auto list =
      model.data(model.index(0, 0), data_recorder::TopicListModel::SeriesListRole).toList();
    EXPECT_FALSE(find_series(list, "pos/a").value("visible").toBool());
  }
}

TEST(TopicListModel, SeriesColorStablePerKey)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/wrench";
  topic.backend_name = "rosbag";

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  std::vector<data_recorder::TopicSeries::SeriesSnapshot> snap;
  snap.push_back({"force.x", {{0.0, 1.0}}});
  snap.push_back({"force.y", {{0.0, 2.0}}});
  model.updateSeries("/wrench", snap);
  const auto list1 =
    model.data(model.index(0, 0), data_recorder::TopicListModel::SeriesListRole).toList();
  const auto color_x_1 = find_series(list1, "force.x").value("color").toString();
  const auto color_y_1 = find_series(list1, "force.y").value("color").toString();
  EXPECT_NE(color_x_1, color_y_1);  // 不同 key 不同色

  // 重新回填（含新 key），已有 key 配色不变
  std::vector<data_recorder::TopicSeries::SeriesSnapshot> snap2;
  snap2.push_back({"force.x", {{0.0, 1.0}}});
  snap2.push_back({"force.y", {{0.0, 2.0}}});
  snap2.push_back({"force.z", {{0.0, 3.0}}});
  model.updateSeries("/wrench", snap2);
  const auto list2 =
    model.data(model.index(0, 0), data_recorder::TopicListModel::SeriesListRole).toList();
  EXPECT_EQ(find_series(list2, "force.x").value("color").toString(), color_x_1);
  EXPECT_EQ(find_series(list2, "force.y").value("color").toString(), color_y_1);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -20
```
Expected: 编译失败——无 `updateSeries` / `setSeriesVisible`。

- [ ] **Step 3: 头文件：include、方法、每行状态字段**

In `include/data_recorder/ui_models.hpp`, add the include after `#include "data_recorder/recorder_types.hpp"`:
```cpp
#include "data_recorder/topic_series.hpp"
```
Also add `<map>` to the includes (for the per-row state maps):
```cpp
#include <map>
```

Add method declarations after `updateMessageDots`:
```cpp
  void updateSeries(
    const QString & topic_key,
    const std::vector<TopicSeries::SeriesSnapshot> & series);
  void setSeriesVisible(const QString & topic_key, const QString & series_key, bool visible);
```

In `struct TopicRow`, add after `QVariantList message_dots;`:
```cpp
    // 按 series_key 稳定保留的 UI 状态，跨 updateSeries 刷新不丢。
    std::map<std::string, bool> series_visible_override;
    std::map<std::string, int> series_color_index;
    int next_color_index{0};
```

- [ ] **Step 4: 实现 helper + `updateSeries` + `setSeriesVisible`**

In `src/ui_models.cpp`, in the anonymous namespace (near `track_kind_for_topic`, before the closing `}  // namespace` at ~line 62), add a default-visibility helper:
```cpp
// 默认可见性：速度/力矩力的导出量（vel/、eff/ 前缀）默认隐藏，其余默认可见。
bool default_series_visible(const std::string & series_key)
{
  return series_key.rfind("vel/", 0) != 0 && series_key.rfind("eff/", 0) != 0;
}
```

Add the two methods after `updateMessageDots` (before `updateStats`):
```cpp
void TopicListModel::updateSeries(
  const QString & topic_key,
  const std::vector<TopicSeries::SeriesSnapshot> & series)
{
  for (std::size_t i = 0; i < topics_.size(); ++i) {
    auto & row = topics_[i];
    if (QString::fromStdString(row.topic.topic_name) != topic_key) { continue; }

    QVariantList list;
    list.reserve(static_cast<int>(series.size()));
    for (const auto & s : series) {
      // 配色按 key 稳定分配（首次见到该 key 时占用下一个色位）。
      auto color_it = row.series_color_index.find(s.key);
      if (color_it == row.series_color_index.end()) {
        color_it = row.series_color_index.emplace(s.key, row.next_color_index++).first;
      }
      const QString color = QString::fromLatin1(
        kSeriesColors[static_cast<std::size_t>(color_it->second) % kSeriesColors.size()]);

      // 可见性：用户改过则用覆盖值，否则用默认规则。
      const auto vis_it = row.series_visible_override.find(s.key);
      const bool visible = vis_it != row.series_visible_override.end() ?
        vis_it->second : default_series_visible(s.key);

      QVariantList points;
      points.reserve(static_cast<int>(s.points.size()));
      for (const auto & p : s.points) {
        QVariantMap point;
        point.insert("x", p.first);
        point.insert("y", p.second);
        points.append(point);
      }

      QVariantMap entry;
      entry.insert("key", QString::fromStdString(s.key));
      entry.insert("label", QString::fromStdString(s.key));
      entry.insert("color", color);
      entry.insert("visible", visible);
      entry.insert("points", points);
      list.append(entry);
    }

    row.series_list = std::move(list);
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {SeriesListRole});
    return;
  }
}

void TopicListModel::setSeriesVisible(
  const QString & topic_key, const QString & series_key, bool visible)
{
  for (std::size_t i = 0; i < topics_.size(); ++i) {
    auto & row = topics_[i];
    if (QString::fromStdString(row.topic.topic_name) != topic_key) { continue; }

    const std::string key = series_key.toStdString();
    row.series_visible_override[key] = visible;

    // 同步已构建的 seriesList 条目的 visible 字段。
    bool changed = false;
    for (auto & v : row.series_list) {
      auto entry = v.toMap();
      if (entry.value("key").toString() == series_key) {
        if (entry.value("visible").toBool() != visible) {
          entry.insert("visible", visible);
          v = entry;
          changed = true;
        }
        break;
      }
    }
    if (changed) {
      const auto idx = index(static_cast<int>(i), 0);
      emit dataChanged(idx, idx, {SeriesListRole});
    }
    return;
  }
}
```

Note: `set_topics` already sets `row.series_list = QVariantList();` — leave that initial empty state; the per-row state maps default-construct empty, which is correct.

- [ ] **Step 5: 跑测试确认通过 + 全量不回归**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder && colcon test-result --verbose
```
Expected: 全部（既有 + Task 1–5 新增）0 failures。注意旧用例 `ClassifiesCameraNumericAndEmptyTracks` 断言 `/tf` 的 `SeriesListRole` 为空——`updateSeries` 没被调用，故仍空，PASS。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat(curves): TopicListModel 结构化 seriesList + 可见性/配色 + setSeriesVisible

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: AppController 持有 registry、注入模型、暴露写操作

**Files:**
- Modify: `include/data_recorder/app_controller.hpp`
- Modify: `src/app_controller.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: 写失败测试**

In `test/test_ui_models.cpp`, append a test that drives the model through the controller (the file already constructs `AppController` elsewhere; reuse `make_config_fixture()`):
```cpp
TEST(AppController, ExposesExpandAndSeriesVisibilityToModel)
{
  auto config = make_config_fixture();
  data_recorder::AppController controller(config);
  auto * model = controller.topicModel();
  ASSERT_NE(model, nullptr);

  // 找到 /joint_states 行
  int joint_row = -1;
  for (int r = 0; r < model->rowCount(); ++r) {
    if (model->data(model->index(r, 0), data_recorder::TopicListModel::TopicNameRole)
        .toString() == "/joint_states") {
      joint_row = r;
      break;
    }
  }
  ASSERT_GE(joint_row, 0);

  controller.setTopicExpanded("/joint_states", true);
  EXPECT_TRUE(
    model->data(model->index(joint_row, 0), data_recorder::TopicListModel::IsExpandedRole)
      .toBool());

  // registry 已注入：joint_states 类型已知后可绘制
  model->updateTopicType("/joint_states", "sensor_msgs/msg/JointState");
  EXPECT_TRUE(
    model->data(model->index(joint_row, 0), data_recorder::TopicListModel::IsPlottableRole)
      .toBool());

  // setSeriesVisible 透传不崩（无 series 时安全无操作）
  EXPECT_NO_FATAL_FAILURE(controller.setSeriesVisible("/joint_states", "pos/a", false));
}
```

- [ ] **Step 2: 跑测试确认失败**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -20
```
Expected: 编译失败——`AppController` 无 `setTopicExpanded` / `setSeriesVisible`。

- [ ] **Step 3: 头文件：include、Q_INVOKABLE、registry 成员**

In `include/data_recorder/app_controller.hpp`, add the include after `#include "data_recorder/ui_models.hpp"`:
```cpp
#include "data_recorder/value_extractor.hpp"
```

Add the two `Q_INVOKABLE` declarations after `Q_INVOKABLE void toggleTopicVisible(int row);` (line 85):
```cpp
  Q_INVOKABLE void setTopicExpanded(const QString & topic_key, bool expanded);
  Q_INVOKABLE void setSeriesVisible(
    const QString & topic_key, const QString & series_key, bool visible);
```

Add the registry member in the private members, after `TopicListModel topic_model_;` (line 130):
```cpp
  ValueExtractorRegistry extractor_registry_;
```

- [ ] **Step 4: 实现：构造时注册 + 注入；两个透传方法**

In `src/app_controller.cpp`, in the `AppController::AppController(...)` constructor body, the line `topic_model_.set_topics(config.topics);` is followed by `live_topics_ = config.topics;`. Insert the two registry lines right after `topic_model_.set_topics(config.topics);`:
```cpp
  topic_model_.set_topics(config.topics);
  register_builtin_extractors(extractor_registry_);
  topic_model_.set_extractor_registry(&extractor_registry_);
  live_topics_ = config.topics;
```

Add the two methods next to `toggleTopicVisible` (after it, ~line 461):
```cpp
void AppController::setTopicExpanded(const QString & topic_key, bool expanded)
{
  topic_model_.setExpanded(topic_key, expanded);
}

void AppController::setSeriesVisible(
  const QString & topic_key, const QString & series_key, bool visible)
{
  topic_model_.setSeriesVisible(topic_key, series_key, visible);
}
```

- [ ] **Step 5: 跑测试确认通过 + 全量不回归**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder && colcon test-result --verbose
```
Expected: 全部 0 failures（含新增 `AppController.ExposesExpandAndSeriesVisibilityToModel`）。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/app_controller.hpp src/app_controller.cpp test/test_ui_models.cpp
git commit -m "feat(curves): AppController 持有 registry 并暴露 setTopicExpanded/setSeriesVisible

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## 验收（Plan 2 完成标志）
- `colcon build --packages-select data_recorder` 通过。
- 全包 `colcon test` 0 failures（Plan 1 的 139 项 + 本计划新增约 12 项）。
- 产出：
  - config 解析 topic `ui_expanded` → `TopicEntry::default_expanded`；
  - `TopicListModel` 新 role `isExpanded` / `isPlottable` / `messageDots` / 结构化 `seriesList`，写方法 `setExpanded` / `updateTopicType` / `updateMessageDots` / `updateSeries` / `setSeriesVisible`，可见性默认规则 + 跨刷新稳定的可见性/配色；
  - `AppController` 持有内置 registry 并注入模型，QML 可调 `setTopicExpanded` / `setSeriesVisible`。
- 供 Plan 3（QML chevron / 折叠点 / 展开曲线 / 显隐）、Plan 4（实时数据流调 `updateTopicType` / `updateMessageDots` / `updateSeries`）、Plan 5（历史懒回读复用同一批回填方法）。

## Self-Review 结论
- **Spec 覆盖**：本计划对应 spec「配置（config_model.cpp）」「模型与 QML 改动·TopicListModel 新增 role/方法」「测试·单元（config 解析 scalar vs 单键 map、新 role + setExpanded/setSeriesVisible 触发 dataChanged）」三处；QML（Plan 3）、实时（Plan 4）、历史（Plan 5）不在本计划。
- **占位符**：无 TBD；每步含完整代码与命令。Task 6 Step 4 因 `app_controller.cpp` 构造体的确切行依实现而定，给了「在 topic 模型建好后」的定位法而非硬行号——这是定位指引，非占位。
- **类型一致**：`TopicEntry::default_expanded`、role 名 `IsExpandedRole`/`IsPlottableRole`/`MessageDotsRole`/`SeriesListRole`、方法 `setExpanded`/`set_extractor_registry`/`updateTopicType`/`updateMessageDots`/`updateSeries`/`setSeriesVisible`、QML role 名 `isExpanded`/`isPlottable`/`messageDots`/`seriesList`、series 条目字段 `key`/`label`/`color`/`visible`/`points{x,y}`、AppController `setTopicExpanded`/`setSeriesVisible` 跨 task 一致；复用 Plan 1 的 `ValueExtractorRegistry::has`、`TopicSeries::SeriesSnapshot{key,points}`、`SeriesPoint{first,second}`、`register_builtin_extractors`、`kSeriesColors`。
```
