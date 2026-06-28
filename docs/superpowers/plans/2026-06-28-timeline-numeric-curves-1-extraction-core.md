# 时间轴数值曲线 · Plan 1：数值提取核心 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现纯 C++ 的数值提取核心——把序列化的 ROS 消息按类型提取成标量序列，并提供有界、可抽稀的时间序列缓冲；全部单元测试覆盖，不依赖 UI/ROS 运行时。

**Architecture:** `ValueExtractorRegistry` 按 ROS 类型字符串分发到 `ValueExtractor`；v1 内置 JointState / WrenchStamped / JointTrajectory 三个强类型提取器（`rclcpp::Serialization<T>` 反序列化，按关节名/轴名生成稳定 `series_key`）。`TopicSeries` 为每 topic 存消息时间戳（折叠点）与各 series 的 `(t,value)`（展开曲线），有界环形缓冲；`decimate()` 把曲线抽稀到点数预算并用 min/max 保尖峰。实时与历史两条数据流后续都复用本核心。

**Tech Stack:** C++17, rclcpp 序列化, sensor_msgs / geometry_msgs / trajectory_msgs, ament_cmake_gtest。

**这是整个特性的 Plan 1 / 5**（见 spec [2026-06-28-timeline-numeric-curves-design.md](../specs/2026-06-28-timeline-numeric-curves-design.md)）。本计划完成后产出一个独立、已测试的库片段，被后续 Plan 2–5 复用。

**风格提醒：** 本仓是 ament 风格（包根已有官方 `.clang-format`）；手写匹配周边风格即可。**构建/测试命令**（每个 task 都用）：
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

- Create `include/data_recorder/value_extractor.hpp` — `ExtractedSample`、`ValueExtractor` 接口、`ValueExtractorRegistry`、`register_builtin_extractors()` 声明。
- Create `src/value_extractor.cpp` — registry 实现 + 三个内置提取器 + `register_builtin_extractors()`。
- Create `include/data_recorder/topic_series.hpp` — `SeriesPoint`、`decimate()`、`TopicSeries`。
- Create `src/topic_series.cpp` — 实现。
- Create `test/test_value_extractor.cpp`、`test/test_topic_series.cpp`。
- Modify `CMakeLists.txt` — find_package + 源文件列表 + core 依赖 + 两个 gtest 目标。
- Modify `package.xml` — 加 `geometry_msgs`、`trajectory_msgs` 依赖。

---

## Task 1: ValueExtractor 接口 + Registry

**Files:**
- Create: `include/data_recorder/value_extractor.hpp`
- Create: `src/value_extractor.cpp`
- Create: `test/test_value_extractor.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写头文件（接口 + registry）**

Create `include/data_recorder/value_extractor.hpp`:
```cpp
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rclcpp/serialized_message.hpp>

namespace data_recorder
{

// 一条标量样本：series_key 为稳定标识（如 "pos/joint_a"），value 为数值。
struct ExtractedSample
{
  std::string series_key;
  double value{0.0};
};

// 把某类型的序列化消息反序列化并提取若干标量样本。每种支持的类型一个实现。
class ValueExtractor
{
public:
  virtual ~ValueExtractor() = default;
  virtual std::vector<ExtractedSample> extract(
    const rclcpp::SerializedMessage & serialized) const = 0;
};

// 按 ROS 类型字符串分发到 ValueExtractor。日后通用内省也作为一个 extractor 注册进来。
class ValueExtractorRegistry
{
public:
  void register_extractor(const std::string & type, std::unique_ptr<ValueExtractor> extractor);
  bool has(const std::string & type) const;
  const ValueExtractor * get(const std::string & type) const;  // 不支持返回 nullptr

private:
  std::map<std::string, std::unique_ptr<ValueExtractor>> extractors_;
};

// 注册 v1 内置类型：JointState / WrenchStamped / JointTrajectory。
void register_builtin_extractors(ValueExtractorRegistry & registry);

}  // namespace data_recorder
```

- [ ] **Step 2: 写 registry 实现（暂不含内置提取器）**

Create `src/value_extractor.cpp`:
```cpp
#include "data_recorder/value_extractor.hpp"

namespace data_recorder
{

void ValueExtractorRegistry::register_extractor(
  const std::string & type, std::unique_ptr<ValueExtractor> extractor)
{
  extractors_[type] = std::move(extractor);
}

bool ValueExtractorRegistry::has(const std::string & type) const
{
  return extractors_.find(type) != extractors_.end();
}

const ValueExtractor * ValueExtractorRegistry::get(const std::string & type) const
{
  auto it = extractors_.find(type);
  return it == extractors_.end() ? nullptr : it->second.get();
}

void register_builtin_extractors(ValueExtractorRegistry &)
{
  // Task 2 填充。
}

}  // namespace data_recorder
```

- [ ] **Step 3: 写失败测试（registry has/get）**

Create `test/test_value_extractor.cpp`:
```cpp
#include <gtest/gtest.h>

#include <memory>

#include "data_recorder/value_extractor.hpp"

namespace
{
// 测试用假 extractor：忽略输入，返回一条固定样本。
class FakeExtractor : public data_recorder::ValueExtractor
{
public:
  std::vector<data_recorder::ExtractedSample> extract(
    const rclcpp::SerializedMessage &) const override
  {
    return {{"fake", 42.0}};
  }
};
}  // namespace

TEST(ValueExtractorRegistry, HasAndGet)
{
  data_recorder::ValueExtractorRegistry reg;
  EXPECT_FALSE(reg.has("some/Type"));
  EXPECT_EQ(reg.get("some/Type"), nullptr);

  reg.register_extractor("some/Type", std::make_unique<FakeExtractor>());
  EXPECT_TRUE(reg.has("some/Type"));
  ASSERT_NE(reg.get("some/Type"), nullptr);

  rclcpp::SerializedMessage dummy;
  const auto samples = reg.get("some/Type")->extract(dummy);
  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0].series_key, "fake");
  EXPECT_DOUBLE_EQ(samples[0].value, 42.0);
}
```

- [ ] **Step 4: 接 CMakeLists（源文件 + 测试目标）**

只登记本 task 创建的文件（`topic_series.*` 在 Task 3 再加，避免 CMake 报缺源）。

In `CMakeLists.txt`, insert into `DATA_RECORDER_HEADERS` between `ui_models.hpp` and `video_clip_reader.hpp`:
```cmake
  include/data_recorder/ui_models.hpp
  include/data_recorder/value_extractor.hpp
  include/data_recorder/video_clip_reader.hpp
```

In `CMakeLists.txt`, insert into `DATA_RECORDER_SOURCES` between `src/ui_models.cpp` and `src/video_clip_reader.cpp`:
```cmake
  src/ui_models.cpp
  src/value_extractor.cpp
  src/video_clip_reader.cpp
```

Add the test target inside the `if(BUILD_TESTING)` block (after the `test_rosbag_writer` block, ~line 157):
```cmake
  ament_add_gtest(test_value_extractor test/test_value_extractor.cpp)
  target_link_libraries(test_value_extractor data_recorder_core)
  ament_target_dependencies(test_value_extractor rclcpp)
```

- [ ] **Step 5: 构建并跑测试，确认通过**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_value_extractor && colcon test-result --verbose
```
Expected: `test_value_extractor` PASS（`ValueExtractorRegistry.HasAndGet`）；test-result 0 failures。
（首次构建若因 `topic_series.*` 尚不存在而失败，按 Step 4 注释临时移除那两行再构建。）

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/value_extractor.hpp src/value_extractor.cpp \
  test/test_value_extractor.cpp CMakeLists.txt
git commit -m "feat(curves): ValueExtractor 接口 + registry

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: 三个内置提取器（JointState / WrenchStamped / JointTrajectory）

**Files:**
- Modify: `src/value_extractor.cpp`
- Modify: `test/test_value_extractor.cpp`
- Modify: `CMakeLists.txt`, `package.xml`

- [ ] **Step 1: 加消息包依赖**

In `package.xml`, after `<depend>sensor_msgs</depend>` (line 19):
```xml
  <depend>geometry_msgs</depend>
  <depend>trajectory_msgs</depend>
```

In `CMakeLists.txt`, after `find_package(sensor_msgs REQUIRED)` (line 21):
```cmake
find_package(geometry_msgs REQUIRED)
find_package(trajectory_msgs REQUIRED)
```

In `CMakeLists.txt`, extend the core deps line (line 81) to include the two packages:
```cmake
ament_target_dependencies(data_recorder_core rclcpp ament_index_cpp rosbag2_cpp rosbag2_storage pluginlib rosbag2_transport sensor_msgs geometry_msgs trajectory_msgs)
```

Also extend the test deps (the line added in Task 1 Step 4):
```cmake
  ament_target_dependencies(test_value_extractor rclcpp sensor_msgs geometry_msgs trajectory_msgs)
```

- [ ] **Step 2: 写失败测试（三个提取器 + 内置注册）**

Append to `test/test_value_extractor.cpp` (add includes at top, and the tests below):
```cpp
#include <rclcpp/serialization.hpp>
#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <trajectory_msgs/msg/joint_trajectory.hpp>

namespace
{
template<class T>
rclcpp::SerializedMessage serialize(const T & msg)
{
  rclcpp::Serialization<T> ser;
  rclcpp::SerializedMessage out;
  ser.serialize_message(&msg, &out);
  return out;
}

double value_of(const std::vector<data_recorder::ExtractedSample> & s, const std::string & key)
{
  for (const auto & e : s) {
    if (e.series_key == key) { return e.value; }
  }
  ADD_FAILURE() << "series_key not found: " << key;
  return 0.0;
}
}  // namespace

TEST(BuiltinExtractors, JointStatePairsByName)
{
  data_recorder::ValueExtractorRegistry reg;
  data_recorder::register_builtin_extractors(reg);
  ASSERT_TRUE(reg.has("sensor_msgs/msg/JointState"));

  sensor_msgs::msg::JointState msg;
  msg.name = {"a", "b"};
  msg.position = {1.0, 2.0};
  msg.velocity = {0.5};  // 只有 a 有速度
  const auto out = reg.get("sensor_msgs/msg/JointState")->extract(serialize(msg));

  EXPECT_DOUBLE_EQ(value_of(out, "pos/a"), 1.0);
  EXPECT_DOUBLE_EQ(value_of(out, "pos/b"), 2.0);
  EXPECT_DOUBLE_EQ(value_of(out, "vel/a"), 0.5);
  // b 无 velocity、无 effort
  for (const auto & e : out) {
    EXPECT_NE(e.series_key, "vel/b");
    EXPECT_NE(e.series_key, "eff/a");
  }
}

TEST(BuiltinExtractors, WrenchStampedSixAxes)
{
  data_recorder::ValueExtractorRegistry reg;
  data_recorder::register_builtin_extractors(reg);
  geometry_msgs::msg::WrenchStamped msg;
  msg.wrench.force.x = 1.0; msg.wrench.force.y = 2.0; msg.wrench.force.z = 3.0;
  msg.wrench.torque.x = 4.0; msg.wrench.torque.y = 5.0; msg.wrench.torque.z = 6.0;
  const auto out = reg.get("geometry_msgs/msg/WrenchStamped")->extract(serialize(msg));
  ASSERT_EQ(out.size(), 6u);
  EXPECT_DOUBLE_EQ(value_of(out, "force.x"), 1.0);
  EXPECT_DOUBLE_EQ(value_of(out, "force.z"), 3.0);
  EXPECT_DOUBLE_EQ(value_of(out, "torque.y"), 5.0);
}

TEST(BuiltinExtractors, JointTrajectoryUsesLastPoint)
{
  data_recorder::ValueExtractorRegistry reg;
  data_recorder::register_builtin_extractors(reg);
  trajectory_msgs::msg::JointTrajectory msg;
  msg.joint_names = {"a", "b"};
  trajectory_msgs::msg::JointTrajectoryPoint p0, p1;
  p0.positions = {0.0, 0.0};
  p1.positions = {7.0, 8.0};
  msg.points = {p0, p1};
  const auto out = reg.get("trajectory_msgs/msg/JointTrajectory")->extract(serialize(msg));
  EXPECT_DOUBLE_EQ(value_of(out, "cmd/a"), 7.0);
  EXPECT_DOUBLE_EQ(value_of(out, "cmd/b"), 8.0);
}

TEST(BuiltinExtractors, JointTrajectoryEmptyPointsYieldsNothing)
{
  data_recorder::ValueExtractorRegistry reg;
  data_recorder::register_builtin_extractors(reg);
  trajectory_msgs::msg::JointTrajectory msg;
  msg.joint_names = {"a"};
  const auto out = reg.get("trajectory_msgs/msg/JointTrajectory")->extract(serialize(msg));
  EXPECT_TRUE(out.empty());
}
```

- [ ] **Step 3: 跑测试确认失败**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -20
```
Expected: 编译通过但 `register_builtin_extractors` 为空 → `reg.has("sensor_msgs/msg/JointState")` 断言失败（或运行时无 extractor）。即 `BuiltinExtractors.*` 测试 FAIL。

- [ ] **Step 4: 实现三个提取器**

Replace the body of `src/value_extractor.cpp` — add includes after the existing include, and replace `register_builtin_extractors`:
```cpp
#include "data_recorder/value_extractor.hpp"

#include <rclcpp/serialization.hpp>

#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <trajectory_msgs/msg/joint_trajectory.hpp>
```
(registry 三个方法保持 Task 1 不变。把空的 `register_builtin_extractors` 之上、`namespace data_recorder` 内补一个匿名命名空间与三个类，并填充注册函数：)
```cpp
namespace
{

// sensor_msgs/msg/JointState → 每关节 pos/<name>，有则 vel/<name>、eff/<name>。按关节名配对。
class JointStateExtractor : public ValueExtractor
{
public:
  std::vector<ExtractedSample> extract(const rclcpp::SerializedMessage & serialized) const override
  {
    sensor_msgs::msg::JointState msg;
    rclcpp::Serialization<sensor_msgs::msg::JointState> ser;
    ser.deserialize_message(&serialized, &msg);
    std::vector<ExtractedSample> out;
    for (size_t i = 0; i < msg.name.size(); ++i) {
      const std::string & jn = msg.name[i];
      if (i < msg.position.size()) { out.push_back({"pos/" + jn, msg.position[i]}); }
      if (i < msg.velocity.size()) { out.push_back({"vel/" + jn, msg.velocity[i]}); }
      if (i < msg.effort.size()) { out.push_back({"eff/" + jn, msg.effort[i]}); }
    }
    return out;
  }
};

// geometry_msgs/msg/WrenchStamped → force.x/y/z, torque.x/y/z。
class WrenchStampedExtractor : public ValueExtractor
{
public:
  std::vector<ExtractedSample> extract(const rclcpp::SerializedMessage & serialized) const override
  {
    geometry_msgs::msg::WrenchStamped msg;
    rclcpp::Serialization<geometry_msgs::msg::WrenchStamped> ser;
    ser.deserialize_message(&serialized, &msg);
    const auto & w = msg.wrench;
    return {
      {"force.x", w.force.x}, {"force.y", w.force.y}, {"force.z", w.force.z},
      {"torque.x", w.torque.x}, {"torque.y", w.torque.y}, {"torque.z", w.torque.z},
    };
  }
};

// trajectory_msgs/msg/JointTrajectory → 取最后一个轨迹点，每关节 cmd/<name>。
class JointTrajectoryExtractor : public ValueExtractor
{
public:
  std::vector<ExtractedSample> extract(const rclcpp::SerializedMessage & serialized) const override
  {
    trajectory_msgs::msg::JointTrajectory msg;
    rclcpp::Serialization<trajectory_msgs::msg::JointTrajectory> ser;
    ser.deserialize_message(&serialized, &msg);
    std::vector<ExtractedSample> out;
    if (msg.points.empty()) { return out; }
    const auto & pt = msg.points.back();
    for (size_t i = 0; i < msg.joint_names.size() && i < pt.positions.size(); ++i) {
      out.push_back({"cmd/" + msg.joint_names[i], pt.positions[i]});
    }
    return out;
  }
};

}  // namespace
```
And replace `register_builtin_extractors`:
```cpp
void register_builtin_extractors(ValueExtractorRegistry & registry)
{
  registry.register_extractor(
    "sensor_msgs/msg/JointState", std::make_unique<JointStateExtractor>());
  registry.register_extractor(
    "geometry_msgs/msg/WrenchStamped", std::make_unique<WrenchStampedExtractor>());
  registry.register_extractor(
    "trajectory_msgs/msg/JointTrajectory", std::make_unique<JointTrajectoryExtractor>());
}
```

- [ ] **Step 5: 跑测试确认通过**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_value_extractor && colcon test-result --verbose
```
Expected: `ValueExtractorRegistry.*` 与 `BuiltinExtractors.*` 全 PASS；0 failures。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add src/value_extractor.cpp test/test_value_extractor.cpp CMakeLists.txt package.xml
git commit -m "feat(curves): 内置 JointState/WrenchStamped/JointTrajectory 提取器

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: decimate() 抽稀函数

**Files:**
- Create: `include/data_recorder/topic_series.hpp`
- Create: `src/topic_series.cpp`
- Create: `test/test_topic_series.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写头（仅 decimate 与类型，TopicSeries 留 Task 4）**

Create `include/data_recorder/topic_series.hpp`:
```cpp
#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace data_recorder
{

// 一个时间点的值：(秒, 值)。
using SeriesPoint = std::pair<double, double>;

// 把点序列抽稀到至多 budget 个点。size<=budget 原样返回；否则按 budget/2 个桶、每桶输出
// 该桶内的 min 与 max（按时间先后），保留尖峰；结果按时间升序。budget<2 视为 2。
// 输入须按时间（first）升序。
std::vector<SeriesPoint> decimate(const std::vector<SeriesPoint> & points, std::size_t budget);

}  // namespace data_recorder
```

Create `src/topic_series.cpp`:
```cpp
#include "data_recorder/topic_series.hpp"

#include <algorithm>

namespace data_recorder
{

std::vector<SeriesPoint> decimate(const std::vector<SeriesPoint> & points, std::size_t budget)
{
  if (budget < 2) { budget = 2; }
  if (points.size() <= budget) { return points; }
  const std::size_t buckets = budget / 2;
  const std::size_t n = points.size();
  std::vector<SeriesPoint> out;
  out.reserve(buckets * 2);
  for (std::size_t b = 0; b < buckets; ++b) {
    const std::size_t lo = b * n / buckets;
    const std::size_t hi = (b + 1) * n / buckets;  // 独占
    if (lo >= hi) { continue; }
    std::size_t min_i = lo, max_i = lo;
    for (std::size_t i = lo + 1; i < hi; ++i) {
      if (points[i].second < points[min_i].second) { min_i = i; }
      if (points[i].second > points[max_i].second) { max_i = i; }
    }
    const std::size_t first = std::min(min_i, max_i);
    const std::size_t second = std::max(min_i, max_i);
    out.push_back(points[first]);
    if (second != first) { out.push_back(points[second]); }
  }
  return out;
}

}  // namespace data_recorder
```

- [ ] **Step 2: 写失败测试**

Create `test/test_topic_series.cpp`:
```cpp
#include <gtest/gtest.h>

#include <vector>

#include "data_recorder/topic_series.hpp"

using data_recorder::SeriesPoint;
using data_recorder::decimate;

TEST(Decimate, KeepsAllWhenUnderBudget)
{
  std::vector<SeriesPoint> in{{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}};
  const auto out = decimate(in, 10);
  EXPECT_EQ(out, in);
}

TEST(Decimate, ReducesToAtMostBudgetAndStaysTimeSorted)
{
  std::vector<SeriesPoint> in;
  for (int i = 0; i < 1000; ++i) { in.push_back({static_cast<double>(i), static_cast<double>(i % 7)}); }
  const auto out = decimate(in, 100);
  EXPECT_LE(out.size(), 100u);
  EXPECT_GT(out.size(), 0u);
  for (std::size_t i = 1; i < out.size(); ++i) {
    EXPECT_LE(out[i - 1].first, out[i].first);  // 时间升序
  }
}

TEST(Decimate, PreservesSpike)
{
  std::vector<SeriesPoint> in;
  for (int i = 0; i < 1000; ++i) { in.push_back({static_cast<double>(i), 0.0}); }
  in[500].second = 999.0;  // 单点尖峰
  const auto out = decimate(in, 50);
  bool found = false;
  for (const auto & p : out) {
    if (p.second == 999.0) { found = true; }
  }
  EXPECT_TRUE(found) << "min/max 抽稀应保留尖峰";
}
```

- [ ] **Step 3: 接 CMakeLists（源文件 + 测试目标）**

In `CMakeLists.txt`, insert into `DATA_RECORDER_HEADERS` between `topic_rate_monitor.hpp` and `ui_models.hpp`:
```cmake
  include/data_recorder/topic_rate_monitor.hpp
  include/data_recorder/topic_series.hpp
  include/data_recorder/ui_models.hpp
```

In `CMakeLists.txt`, insert into `DATA_RECORDER_SOURCES` between `src/topic_rate_monitor.cpp` and `src/ui_models.cpp`:
```cmake
  src/topic_rate_monitor.cpp
  src/topic_series.cpp
  src/ui_models.cpp
```

In `CMakeLists.txt`, inside `if(BUILD_TESTING)` (after the `test_value_extractor` block):
```cmake
  ament_add_gtest(test_topic_series test/test_topic_series.cpp)
  target_link_libraries(test_topic_series data_recorder_core)
```

- [ ] **Step 4: 跑测试确认失败→通过**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_topic_series && colcon test-result --verbose
```
Expected: `Decimate.*` 三个测试 PASS。（实现已随 Step 1 写好，本 task 的"失败"体现在未接测试目标前无法运行；接好后即通过。）

- [ ] **Step 5: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/topic_series.hpp src/topic_series.cpp \
  test/test_topic_series.cpp CMakeLists.txt
git commit -m "feat(curves): decimate() min/max 抽稀

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: TopicSeries 有界缓冲

**Files:**
- Modify: `include/data_recorder/topic_series.hpp`
- Modify: `src/topic_series.cpp`
- Modify: `test/test_topic_series.cpp`

- [ ] **Step 1: 写失败测试**

Append to `test/test_topic_series.cpp`:
```cpp
#include "data_recorder/topic_series.hpp"  // 已在顶部，无需重复

TEST(TopicSeries, RingBufferDropsOldest)
{
  data_recorder::TopicSeries ts(/*max_points=*/3);
  for (int i = 0; i < 5; ++i) { ts.add_sample("k", static_cast<double>(i), static_cast<double>(i)); }
  const auto snap = ts.snapshot(/*budget=*/100);
  ASSERT_EQ(snap.size(), 1u);
  EXPECT_EQ(snap[0].key, "k");
  ASSERT_EQ(snap[0].points.size(), 3u);          // 仅留最近 3 个
  EXPECT_DOUBLE_EQ(snap[0].points.front().first, 2.0);  // 最旧 0,1 被丢
  EXPECT_DOUBLE_EQ(snap[0].points.back().first, 4.0);
}

TEST(TopicSeries, MessageTimesUniformDownsample)
{
  data_recorder::TopicSeries ts(/*max_points=*/1000);
  for (int i = 0; i < 100; ++i) { ts.add_message_time(static_cast<double>(i)); }
  const auto few = ts.message_times(/*budget=*/10);
  EXPECT_LE(few.size(), 10u);
  EXPECT_GT(few.size(), 0u);
  for (std::size_t i = 1; i < few.size(); ++i) { EXPECT_LT(few[i - 1], few[i]); }

  const auto all = ts.message_times(/*budget=*/1000);
  EXPECT_EQ(all.size(), 100u);  // 不超预算则全返回
}

TEST(TopicSeries, SnapshotSortedByKey)
{
  data_recorder::TopicSeries ts;
  ts.add_sample("b", 0.0, 0.0);
  ts.add_sample("a", 0.0, 0.0);
  const auto snap = ts.snapshot(100);
  ASSERT_EQ(snap.size(), 2u);
  EXPECT_EQ(snap[0].key, "a");  // std::map 保证 key 升序
  EXPECT_EQ(snap[1].key, "b");
}
```

- [ ] **Step 2: 跑测试确认失败**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder 2>&1 | tail -20
```
Expected: 编译失败——`TopicSeries` 未定义。

- [ ] **Step 3: 加 TopicSeries 声明**

In `include/data_recorder/topic_series.hpp`, before the closing `}  // namespace data_recorder`, add:
```cpp
// 每个 topic 的时间序列缓冲：消息时间戳（折叠点）+ 各 series 的 (t,value)（展开曲线）。
// 有界环形缓冲，超 max_points 丢最旧。线程无关——调用方加锁。
class TopicSeries
{
public:
  explicit TopicSeries(std::size_t max_points = 20000);

  void add_message_time(double t_seconds);
  void add_sample(const std::string & series_key, double t_seconds, double value);

  std::vector<double> message_times(std::size_t budget) const;  // 折叠点，均匀降采样

  struct SeriesSnapshot
  {
    std::string key;
    std::vector<SeriesPoint> points;  // 已抽稀
  };
  std::vector<SeriesSnapshot> snapshot(std::size_t budget) const;  // 按 key 升序

  void clear();

private:
  std::size_t max_points_;
  std::deque<double> message_times_;
  std::map<std::string, std::deque<SeriesPoint>> series_;
};
```

- [ ] **Step 4: 实现 TopicSeries**

Append to `src/topic_series.cpp` (before the closing namespace brace):
```cpp
TopicSeries::TopicSeries(std::size_t max_points)
: max_points_(max_points == 0 ? 1 : max_points)
{
}

void TopicSeries::add_message_time(double t_seconds)
{
  message_times_.push_back(t_seconds);
  while (message_times_.size() > max_points_) { message_times_.pop_front(); }
}

void TopicSeries::add_sample(const std::string & series_key, double t_seconds, double value)
{
  auto & buf = series_[series_key];
  buf.emplace_back(t_seconds, value);
  while (buf.size() > max_points_) { buf.pop_front(); }
}

std::vector<double> TopicSeries::message_times(std::size_t budget) const
{
  if (budget < 1) { budget = 1; }
  const std::size_t n = message_times_.size();
  if (n <= budget) { return std::vector<double>(message_times_.begin(), message_times_.end()); }
  std::vector<double> out;
  out.reserve(budget);
  for (std::size_t k = 0; k < budget; ++k) {
    out.push_back(message_times_[k * n / budget]);
  }
  return out;
}

std::vector<TopicSeries::SeriesSnapshot> TopicSeries::snapshot(std::size_t budget) const
{
  std::vector<SeriesSnapshot> out;
  for (const auto & [key, buf] : series_) {
    std::vector<SeriesPoint> pts(buf.begin(), buf.end());
    out.push_back({key, decimate(pts, budget)});
  }
  return out;
}

void TopicSeries::clear()
{
  message_times_.clear();
  series_.clear();
}
```

- [ ] **Step 5: 跑测试确认通过**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_topic_series && colcon test-result --verbose
```
Expected: `Decimate.*` 与 `TopicSeries.*` 全 PASS。

- [ ] **Step 6: 全量测试不回归 + 提交**

```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon test --packages-select data_recorder && colcon test-result --verbose
```
Expected: 全部既有 + 新增测试 0 failures。
```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/topic_series.hpp src/topic_series.cpp test/test_topic_series.cpp
git commit -m "feat(curves): TopicSeries 有界缓冲 + 快照抽稀

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## 验收（Plan 1 完成标志）
- `colcon build --packages-select data_recorder` 通过。
- `test_value_extractor`、`test_topic_series` 全 PASS；全包 `colcon test` 0 failures。
- 产出：`ValueExtractorRegistry`（has/get/register + 3 内置提取器，按关节名/轴名稳定 key）、`TopicSeries`（有界缓冲 + min/max 抽稀），供 Plan 2–5 复用。

## Self-Review 结论
- **Spec 覆盖**：本计划对应 spec「内置 extractor 与默认 series」「TopicSeries 环形缓冲+抽稀」「测试·单元」三处；其余（config/模型/QML/实时/历史）在 Plan 2–5。
- **占位符**：无 TBD；每步含完整代码与命令。
- **类型一致**：`ExtractedSample{series_key,value}`、`ValueExtractorRegistry::{register_extractor,has,get}`、`register_builtin_extractors`、`SeriesPoint`、`decimate`、`TopicSeries::{add_message_time,add_sample,message_times,snapshot,clear,SeriesSnapshot}` 跨 task 一致。
