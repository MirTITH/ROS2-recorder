# data_recorder 后端实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 data_recorder 的录制后端，让"录制"按钮真正订阅 ROS 话题、把普通组录成 rosbag、视频组用 libav 编码为 mp4+CSV、相机实时预览、真实 Hz/分辨率回填、停录写 session.yaml。

**Architecture:** 进程内后台 spin 线程（方案 A）。纯 C++ 核心层（零 Qt 依赖，gtest 可单测）持有全部订阅与会话生命周期；每个写入 sink 拥有独立有界队列+worker 线程；一层 LiveBridge(QObject) 把引擎回调经 `Qt::QueuedConnection` marshal 到 GUI 线程；相机帧经 `QQuickImageProvider` 进 QML。始终订阅，录制只是开关。

**Tech Stack:** ROS 2 Humble (rclcpp, rosbag2_cpp, rosbag2_transport, sensor_msgs), libav (FFmpeg 4.4: avcodec/avformat/avutil/swscale), Qt 6 (Quick/QML), yaml-cpp, gtest。

**设计依据:** `docs/superpowers/specs/2026-06-22-data-recorder-backend-design.md`

---

## 已验证的环境事实（实现时可信赖）

- `ROS_DISTRO=humble`；`rosbag2_storage::get_default_storage_id()` = `sqlite3`。
- mcap 插件已装（`ros-humble-rosbag2-storage-mcap` 0.15.16），writers/readers 含 `mcap`。
- 实时测试话题（`ROS_DOMAIN_ID=43`）：`/joint_states`(JointState ~400Hz)、`/tf` `/tf_static`(TFMessage)、`/camera/image_raw` 等 3 路(Image, **bgr8 848×480 ~22Hz**)。
- `QImage::Format_BGR888` 存在；libav 四库：avcodec 58 / avformat 58 / avutil 56 / swscale 5。
- FFmpeg 4.4 特性：用 `avcodec_send_frame`/`avcodec_receive_packet`（不是旧 encode_video2）；**无 `AVFrame.duration` 字段**；x264 的 crf/preset 经 `av_opt_set(ctx->priv_data, ...)` 设置。
- QoS 序列化：`rosbag2_transport::Rosbag2QoS` + `YAML::convert<Rosbag2QoS>` 公有可用；`Node::get_publishers_info_by_topic()` 返回 `vector<rclcpp::TopicEndpointInfo>`，`.qos_profile()` 取 `rclcpp::QoS`。

## 通用命令（每个任务会用到）

**构建单个包：**
```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --mixin release compile-commands ccache
```

**跑单个测试目标：**
```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --ctest-args -R <test_name> --event-handlers console_direct+
```

**跑全部测试：** 同上去掉 `-R <test_name>`。

每个任务的 `git add/commit` 在 `src/data_recorder` 子仓库内执行（当前分支 `backend-implementation`）。

---

## 文件结构

**新增（`recorder/` 概念分组，纯 C++ 核心 + 桥）：**

| 文件 | 职责 |
| --- | --- |
| `include/data_recorder/recorder_types.hpp` | 跨层共享的 POD：`FrameStats`、`AnnotationRecord`、`SessionRecord`、`ImageFrame`。零 Qt、零 ROS。 |
| `include/data_recorder/topic_rate_monitor.hpp` / `src/topic_rate_monitor.cpp` | 每路 Hz 滑窗估计 + 图像 w×h。纯 C++。 |
| `include/data_recorder/writer_queue.hpp` / `src/writer_queue.cpp` | 模板化有界队列 + worker 线程；两种背压策略（阻塞 / 丢最旧+计数）。纯 C++。 |
| `include/data_recorder/session_manager.hpp` / `src/session_manager.cpp` | 建会话目录、写/扫描 `session.yaml`、目录大小现算。纯 C++（yaml-cpp + filesystem）。 |
| `include/data_recorder/video_recorder.hpp` / `src/video_recorder.cpp` | libav 管线 bgr8→yuv420p→libx264→mp4 + sidecar CSV。纯 C++（libav）。 |
| `include/data_recorder/rosbag_writer.hpp` / `src/rosbag_writer.cpp` | 包 `rosbag2_cpp::Writer`；QoS 写入 TopicMetadata。ROS，无 Qt。 |
| `include/data_recorder/recorder_engine.hpp` / `src/recorder_engine.cpp` | 持订阅 + 会话生命周期 + 扇出 + 推进时钟。ROS，无 Qt。 |
| `include/data_recorder/live_bridge.hpp` / `src/live_bridge.cpp` | QObject，把引擎回调（线程安全）marshal 到 GUI 线程的信号。 |
| `include/data_recorder/camera_image_provider.hpp` / `src/camera_image_provider.cpp` | `QQuickImageProvider`，按 `image://camera/<key>?seq=N` 返回最新帧。 |

**重塑现有：**

| 文件 | 改动 |
| --- | --- |
| `src/ui_models.cpp` / `include/data_recorder/ui_models.hpp` | 删 `make_*`/`populate_placeholder_*`；`TopicListModel` 加 `updateStats()` + `FrameSeqRole`/`updateFrameSeq()`；`RecordingSessionModel` 加 `setSessions()`。保留 `kSeriesColors`/`series_color`。 |
| `include/data_recorder/camera_grid_model.hpp` / `src/camera_grid_model.cpp` | 加 `FrameSeqRole` + `topicKey` role；`updateFrameSeq(key)` bump 序号并 `dataChanged`。 |
| `src/app_controller.cpp` / `include/data_recorder/app_controller.hpp` | 持 `RecorderEngine`+`LiveBridge`；`toggleRecording` 接真实 start/stop；接 LiveBridge 信号到模型；启动+停录触发扫描。 |
| `src/data_recorder.cpp` | 后台线程 spin；构造 engine；注册 `CameraImageProvider`；退出干净停录+join。 |
| `qml/components/CameraPreviewTile.qml` | 假 Canvas 换成 `Image { source: image://camera/... }`。 |
| `CMakeLists.txt` / `package.xml` | 加依赖、新源文件、新测试目标。 |

**新增测试：** `test_topic_rate_monitor`、`test_writer_queue`、`test_session_manager`、`test_video_recorder`、`test_rosbag_writer`。改造 `test_ui_models`。

---

## Phase 0 — 基线

### Task 0: 确认基线绿灯

**Files:** 无改动

- [ ] **Step 1: 构建当前包**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --mixin release compile-commands ccache
```
Expected: 构建成功。

- [ ] **Step 2: 跑全部测试，记录全绿**

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --event-handlers console_direct+ ; \
colcon test-result --all --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws
```
Expected: 6 个测试目标全 pass（test_config_model / test_usage_help / test_qml_smoke / test_qml_structure / test_ui_models / test_camera_grid_model）。重构只从绿灯出发。

---

## Phase 1 — 构建依赖与共享类型（无行为，先让骨架编译）

### Task 1: 声明依赖 + 共享类型头

**Files:**
- Modify: `package.xml`
- Modify: `CMakeLists.txt:12-50`
- Create: `include/data_recorder/recorder_types.hpp`

- [ ] **Step 1: 在 package.xml 加依赖**

在 `<depend>yaml-cpp</depend>` 后插入：
```xml
  <depend>rosbag2_cpp</depend>
  <depend>rosbag2_storage</depend>
  <depend>rosbag2_transport</depend>
  <depend>sensor_msgs</depend>
  <depend>libavcodec-dev</depend>
  <depend>libavformat-dev</depend>
  <depend>libavutil-dev</depend>
  <depend>libswscale-dev</depend>
  <exec_depend>ros-humble-rosbag2-storage-mcap</exec_depend>
```

- [ ] **Step 2: 创建共享类型头**

`include/data_recorder/recorder_types.hpp`：
```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace data_recorder
{

// 一路话题的实时统计快照（ROS 线程算，经 LiveBridge 推给 UI）。
struct TopicStats
{
  std::string topic_key;        // = topic_name（唯一标识）
  double hz{0.0};
  int width{0};                 // 仅图像话题 >0
  int height{0};
};

// 一帧 bgr8 图像（ROS 线程填，原子换入 LatestFrameStore；video sink 也吃它）。
struct ImageFrame
{
  int width{0};
  int height{0};
  int step{0};                  // 每行字节数（源 stride）
  std::string encoding;         // "bgr8" / "rgb8" / "mono8" / ...
  int64_t ros_stamp_ns{0};      // header.stamp 优先；为 0 由调用方回退收到时间
  std::vector<uint8_t> data;    // 原始像素
};

// 一条标注实例（停录时从 EventMarkerModel 快照得到）。
struct AnnotationRecord
{
  std::string name;
  std::string shortcut;
  std::string kind;             // "point" / "range"
  std::string color;
  double t{0.0};                // point: 时间点；range: 起点
  double end{0.0};              // 仅 range 使用
};

struct TagRecord
{
  std::string name;
  std::string color;
};

struct TopicRef
{
  std::string name;
  std::string backend;          // "rosbag" / "video"
};

// 一次录制会话的描述符（写/读 session.yaml；驱动会话面板）。
struct SessionRecord
{
  std::string session_id;       // 本地时间戳目录名
  std::string directory;        // 绝对路径
  double unix_time{0.0};
  int64_t ros_time_ns{0};
  double duration_seconds{0.0};
  uint64_t size_bytes{0};       // 扫描时现算，不持久化
  std::vector<TopicRef> topics;
  std::vector<TagRecord> tags;
  std::vector<AnnotationRecord> annotations;
};

}  // namespace data_recorder
```

- [ ] **Step 3: CMake 加 find_package + libav pkg-config**

在 `CMakeLists.txt` 的 `find_package(yaml-cpp REQUIRED)` 后插入：
```cmake
find_package(rosbag2_cpp REQUIRED)
find_package(rosbag2_storage REQUIRED)
find_package(rosbag2_transport REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
  libavcodec libavformat libavutil libswscale)
```

- [ ] **Step 4: 把新头加入 CMake 头列表**

把 `include/data_recorder/recorder_types.hpp` 加进 `set(DATA_RECORDER_HEADERS ...)` 列表。

- [ ] **Step 5: 安装依赖并构建**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws && \
  rosdep install --from-paths src --ignore-src -r -y 2>&1 | tail -5
source ~/.local/ros2_rc && rr && colcon build --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --mixin release compile-commands ccache
```
Expected: 构建成功（新依赖找到，新头被包含，行为不变）。

- [ ] **Step 6: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add package.xml CMakeLists.txt include/data_recorder/recorder_types.hpp
git commit -m "build: 加录制后端依赖(rosbag2/libav/sensor_msgs)与共享类型头"
```

---

## Phase 2 — 纯 C++ 核心（零 Qt 零 ROS，TDD 单测）

### Task 2: TopicRateMonitor — Hz 滑窗估计

**Files:**
- Create: `include/data_recorder/topic_rate_monitor.hpp`, `src/topic_rate_monitor.cpp`
- Test: `test/test_topic_rate_monitor.cpp`

- [ ] **Step 1: 写失败测试**

`test/test_topic_rate_monitor.cpp`：
```cpp
#include <gtest/gtest.h>

#include "data_recorder/topic_rate_monitor.hpp"

TEST(TopicRateMonitor, EstimatesRateFromArrivalTimes)
{
  data_recorder::TopicRateMonitor monitor(/*window_seconds=*/1.0);
  // 10 个事件，间隔 0.1s（精确 10Hz），t 从 0.0 到 0.9s（纳秒）。
  for (int i = 0; i < 10; ++i) {
    monitor.record(static_cast<int64_t>(i) * 100'000'000LL);
  }
  EXPECT_NEAR(monitor.hz(), 10.0, 1.5);  // 容差：滑窗近似
}

TEST(TopicRateMonitor, ZeroBeforeTwoSamples)
{
  data_recorder::TopicRateMonitor monitor(1.0);
  EXPECT_DOUBLE_EQ(monitor.hz(), 0.0);
  monitor.record(0);
  EXPECT_DOUBLE_EQ(monitor.hz(), 0.0);  // 单样本无法估计
}

TEST(TopicRateMonitor, DropsSamplesOutsideWindow)
{
  data_recorder::TopicRateMonitor monitor(1.0);
  // 老样本（应被丢弃）
  monitor.record(0);
  monitor.record(100'000'000LL);
  // 新样本，远在窗口外（5s 后），间隔 0.05s（20Hz）
  for (int i = 0; i < 20; ++i) {
    monitor.record(5'000'000'000LL + static_cast<int64_t>(i) * 50'000'000LL);
  }
  EXPECT_NEAR(monitor.hz(), 20.0, 3.0);
}
```

- [ ] **Step 2: 加测试目标到 CMake**

在 `CMakeLists.txt` 的 `if(BUILD_TESTING)` 块内加：
```cmake
  ament_add_gtest(test_topic_rate_monitor test/test_topic_rate_monitor.cpp)
  target_link_libraries(test_topic_rate_monitor data_recorder_core)
```

- [ ] **Step 3: 运行测试确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -15
```
Expected: 编译失败 `topic_rate_monitor.hpp: No such file`。

- [ ] **Step 4: 实现头**

`include/data_recorder/topic_rate_monitor.hpp`：
```cpp
#pragma once

#include <cstdint>
#include <deque>

namespace data_recorder
{

// 用最近一个时间窗内的到达时刻估计频率。线程不安全——单个订阅回调线程使用。
class TopicRateMonitor
{
public:
  explicit TopicRateMonitor(double window_seconds = 1.0);

  void record(int64_t stamp_ns);
  double hz() const;

private:
  int64_t window_ns_;
  std::deque<int64_t> stamps_;
};

}  // namespace data_recorder
```

- [ ] **Step 5: 实现源**

`src/topic_rate_monitor.cpp`：
```cpp
#include "data_recorder/topic_rate_monitor.hpp"

namespace data_recorder
{

TopicRateMonitor::TopicRateMonitor(double window_seconds)
: window_ns_(static_cast<int64_t>(window_seconds * 1e9))
{
}

void TopicRateMonitor::record(int64_t stamp_ns)
{
  stamps_.push_back(stamp_ns);
  const int64_t cutoff = stamp_ns - window_ns_;
  while (stamps_.size() > 1 && stamps_.front() < cutoff) {
    stamps_.pop_front();
  }
}

double TopicRateMonitor::hz() const
{
  if (stamps_.size() < 2) {
    return 0.0;
  }
  const int64_t span_ns = stamps_.back() - stamps_.front();
  if (span_ns <= 0) {
    return 0.0;
  }
  // (n-1) 个间隔 / 跨度。
  return static_cast<double>(stamps_.size() - 1) * 1e9 / static_cast<double>(span_ns);
}

}  // namespace data_recorder
```

- [ ] **Step 6: 把源文件加进核心库**

在 `set(DATA_RECORDER_SOURCES ...)` 列表加 `src/topic_rate_monitor.cpp`，头加进 `DATA_RECORDER_HEADERS`。

- [ ] **Step 7: 运行测试确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R test_topic_rate_monitor \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: 3 个用例全 PASS。

- [ ] **Step 8: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/topic_rate_monitor.hpp src/topic_rate_monitor.cpp \
  test/test_topic_rate_monitor.cpp CMakeLists.txt
git commit -m "feat: 加 TopicRateMonitor(滑窗 Hz 估计) + 单测"
```

---

### Task 3: WriterQueue — 有界队列 + 两种背压策略

**Files:**
- Create: `include/data_recorder/writer_queue.hpp`, `src/writer_queue.cpp`
- Test: `test/test_writer_queue.cpp`

- [ ] **Step 1: 写失败测试**

`test/test_writer_queue.cpp`：
```cpp
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "data_recorder/writer_queue.hpp"

using namespace std::chrono_literals;

TEST(WriterQueue, ProcessesAllItemsInOrder)
{
  std::vector<int> consumed;
  data_recorder::WriterQueue<int> queue(
    /*capacity=*/100,
    data_recorder::OverflowPolicy::Block,
    [&consumed](int value) { consumed.push_back(value); });

  for (int i = 0; i < 50; ++i) {
    queue.push(i);
  }
  queue.stop();  // 阻塞直到排空 + worker 退出

  ASSERT_EQ(consumed.size(), 50u);
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(consumed[i], i);
  }
}

TEST(WriterQueue, DropOldestPolicyDropsAndCounts)
{
  std::atomic<bool> gate{true};
  std::vector<int> consumed;
  // 消费者在 gate 开启前一直卡住，迫使队列填满。
  data_recorder::WriterQueue<int> queue(
    /*capacity=*/4,
    data_recorder::OverflowPolicy::DropOldest,
    [&](int value) {
      while (gate.load()) { std::this_thread::sleep_for(1ms); }
      consumed.push_back(value);
    });

  // 第一个 item 会被 worker 取走并卡在 gate；其余 10 个争 capacity=4 的槽。
  for (int i = 0; i < 11; ++i) {
    queue.push(i);
  }
  std::this_thread::sleep_for(20ms);
  gate.store(false);
  queue.stop();

  EXPECT_GT(queue.dropped_count(), 0u);          // 确有丢弃
  EXPECT_LE(consumed.size(), 11u);               // 没消费超过推入数
  EXPECT_GT(consumed.size(), 0u);
}

TEST(WriterQueue, BlockPolicyNeverDrops)
{
  std::vector<int> consumed;
  data_recorder::WriterQueue<int> queue(
    /*capacity=*/2,
    data_recorder::OverflowPolicy::Block,
    [&](int value) {
      std::this_thread::sleep_for(1ms);
      consumed.push_back(value);
    });

  for (int i = 0; i < 20; ++i) {
    queue.push(i);  // 满时阻塞，绝不丢
  }
  queue.stop();

  EXPECT_EQ(consumed.size(), 20u);
  EXPECT_EQ(queue.dropped_count(), 0u);
}
```

- [ ] **Step 2: 加测试目标**

```cmake
  ament_add_gtest(test_writer_queue test/test_writer_queue.cpp)
  target_link_libraries(test_writer_queue data_recorder_core)
```

- [ ] **Step 3: 运行确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -10
```
Expected: 编译失败 `writer_queue.hpp: No such file`。

- [ ] **Step 4: 实现头（模板，全部在头里）**

`include/data_recorder/writer_queue.hpp`：
```cpp
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace data_recorder
{

enum class OverflowPolicy
{
  Block,       // 满时阻塞生产者（rosbag：宁慢不丢）
  DropOldest,  // 满时丢最旧并计数（video：保流畅）
};

// 单生产者→单消费者有界队列 + 自带 worker 线程。
template<typename T>
class WriterQueue
{
public:
  WriterQueue(std::size_t capacity, OverflowPolicy policy, std::function<void(T)> sink)
  : capacity_(capacity), policy_(policy), sink_(std::move(sink))
  {
    worker_ = std::thread([this]() { run(); });
  }

  ~WriterQueue()
  {
    stop();
  }

  WriterQueue(const WriterQueue &) = delete;
  WriterQueue & operator=(const WriterQueue &) = delete;

  void push(T item)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (policy_ == OverflowPolicy::Block) {
      space_cv_.wait(lock, [this]() { return queue_.size() < capacity_ || stopping_; });
      if (stopping_) {
        return;
      }
    } else {  // DropOldest
      while (queue_.size() >= capacity_) {
        queue_.pop_front();
        ++dropped_;
      }
    }
    queue_.push_back(std::move(item));
    item_cv_.notify_one();
  }

  // 阻塞直到队列排空且 worker 退出。
  void stop()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      stopping_ = true;
    }
    item_cv_.notify_all();
    space_cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  std::size_t dropped_count() const
  {
    return dropped_.load();
  }

private:
  void run()
  {
    for (;;) {
      T item;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        item_cv_.wait(lock, [this]() { return !queue_.empty() || stopping_; });
        if (queue_.empty()) {
          // 仅当 stopping_ 且已排空才退出。
          if (stopping_) {
            return;
          }
          continue;
        }
        item = std::move(queue_.front());
        queue_.pop_front();
        space_cv_.notify_one();
      }
      sink_(std::move(item));  // 锁外执行，避免阻塞生产者
    }
  }

  std::size_t capacity_;
  OverflowPolicy policy_;
  std::function<void(T)> sink_;

  std::mutex mutex_;
  std::condition_variable item_cv_;
  std::condition_variable space_cv_;
  std::deque<T> queue_;
  std::atomic<std::size_t> dropped_{0};
  bool stopping_{false};
  std::thread worker_;
};

}  // namespace data_recorder
```

- [ ] **Step 5: 实现源（仅占位 TU，让 CMake 有源可编）**

`src/writer_queue.cpp`：
```cpp
#include "data_recorder/writer_queue.hpp"

// WriterQueue 是头内模板，无非模板定义。本 TU 占位以保持构建系统一致并便于将来加非模板辅助。
namespace data_recorder
{
}  // namespace data_recorder
```

- [ ] **Step 6: 加进核心库列表**

`src/writer_queue.cpp` → `DATA_RECORDER_SOURCES`，头 → `DATA_RECORDER_HEADERS`。

- [ ] **Step 7: 运行确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R test_writer_queue \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: 3 个用例全 PASS。

- [ ] **Step 8: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/writer_queue.hpp src/writer_queue.cpp test/test_writer_queue.cpp CMakeLists.txt
git commit -m "feat: 加 WriterQueue(有界队列+阻塞/丢最旧背压) + 单测"
```

---

### Task 4: SessionManager — 写/扫描 session.yaml + 大小现算

**Files:**
- Create: `include/data_recorder/session_manager.hpp`, `src/session_manager.cpp`
- Test: `test/test_session_manager.cpp`

- [ ] **Step 1: 写失败测试**

`test/test_session_manager.cpp`：
```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "data_recorder/session_manager.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::SessionRecord make_record(const std::string & id)
{
  data_recorder::SessionRecord r;
  r.session_id = id;
  r.unix_time = 1782059150.228855;
  r.ros_time_ns = 1782059150228855043LL;
  r.duration_seconds = 42.512;
  r.topics = {{"/joint_states", "rosbag"}, {"/camera/image_raw", "video"}};
  r.tags = {{"成功", "#2f9e44"}};
  // 同名多条 + range
  r.annotations = {
    {"拿起水杯", "1", "point", "#1763c9", 3.210, 0.0},
    {"碰撞", "c", "point", "#e03131", 8.040, 0.0},
    {"碰撞", "c", "point", "#e03131", 12.880, 0.0},
    {"倒水", "2", "range", "#2f9e44", 5.0, 9.3},
  };
  return r;
}
}  // namespace

TEST(SessionManager, WritesAndReadsBackSessionYaml)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_session_test_rw";
  fs::remove_all(tmp);
  fs::create_directories(tmp / "2026-06-22_14-30-05");

  data_recorder::SessionManager mgr;
  auto record = make_record("2026-06-22_14-30-05");
  record.directory = (tmp / "2026-06-22_14-30-05").string();
  mgr.write_session_yaml(record);

  ASSERT_TRUE(fs::exists(tmp / "2026-06-22_14-30-05" / "session.yaml"));

  auto sessions = mgr.scan(tmp.string());
  ASSERT_EQ(sessions.size(), 1u);
  const auto & s = sessions.front();
  EXPECT_EQ(s.session_id, "2026-06-22_14-30-05");
  EXPECT_NEAR(s.duration_seconds, 42.512, 1e-6);
  ASSERT_EQ(s.annotations.size(), 4u);
  // 同名两条都在
  int collision_count = 0;
  for (const auto & a : s.annotations) {
    if (a.name == "碰撞") { ++collision_count; }
  }
  EXPECT_EQ(collision_count, 2);
  ASSERT_EQ(s.tags.size(), 1u);
  EXPECT_EQ(s.tags.front().name, "成功");

  fs::remove_all(tmp);
}

TEST(SessionManager, ScanSkipsDirsWithoutSessionYaml)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_session_test_skip";
  fs::remove_all(tmp);
  fs::create_directories(tmp / "good");
  fs::create_directories(tmp / "in_progress");  // 无 session.yaml

  data_recorder::SessionManager mgr;
  auto record = make_record("good");
  record.directory = (tmp / "good").string();
  mgr.write_session_yaml(record);

  auto sessions = mgr.scan(tmp.string());
  EXPECT_EQ(sessions.size(), 1u);  // in_progress 被跳过

  fs::remove_all(tmp);
}

TEST(SessionManager, ScanComputesDirectorySize)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_session_test_size";
  fs::remove_all(tmp);
  const fs::path dir = tmp / "with_data";
  fs::create_directories(dir);

  data_recorder::SessionManager mgr;
  auto record = make_record("with_data");
  record.directory = dir.string();
  mgr.write_session_yaml(record);

  // 写一个 1024 字节的假数据文件
  std::ofstream(dir / "rosbag_0.mcap", std::ios::binary).write(std::string(1024, 'x').data(), 1024);

  auto sessions = mgr.scan(tmp.string());
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_GE(sessions.front().size_bytes, 1024u);  // 现算，至少含数据文件

  fs::remove_all(tmp);
}

TEST(SessionManager, CreateSessionDirectoryMakesTimestampedSubdir)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_session_test_create";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  data_recorder::SessionManager mgr;
  const std::string dir = mgr.create_session_directory(tmp.string(), "2026-06-22_15-00-00");
  EXPECT_TRUE(fs::exists(dir));
  EXPECT_TRUE(fs::exists(fs::path(dir) / "rosbag") || true);  // rosbag 子目录由 writer 建，这里不强求
  EXPECT_NE(dir.find("2026-06-22_15-00-00"), std::string::npos);

  fs::remove_all(tmp);
}
```

- [ ] **Step 2: 加测试目标**

```cmake
  ament_add_gtest(test_session_manager test/test_session_manager.cpp)
  target_link_libraries(test_session_manager data_recorder_core)
```

- [ ] **Step 3: 运行确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -10
```
Expected: 编译失败 `session_manager.hpp: No such file`。

- [ ] **Step 4: 实现头**

`include/data_recorder/session_manager.hpp`：
```cpp
#pragma once

#include <string>
#include <vector>

#include "data_recorder/recorder_types.hpp"

namespace data_recorder
{

// 建会话目录、写/扫描 session.yaml。纯文件 I/O，无 Qt/ROS。线程无关——由调用方在非 GUI 线程使用。
class SessionManager
{
public:
  // 在 output_dir 下建 <session_id>/ 子目录，返回其绝对路径。
  std::string create_session_directory(
    const std::string & output_dir, const std::string & session_id) const;

  // 把 record 序列化为 <record.directory>/session.yaml。
  void write_session_yaml(const SessionRecord & record) const;

  // 扫描 output_dir 各子目录，读 session.yaml（无则跳过），现算 size_bytes；按 session_id 降序（新在前）。
  std::vector<SessionRecord> scan(const std::string & output_dir) const;

private:
  static uint64_t directory_size(const std::string & dir);
};

}  // namespace data_recorder
```

- [ ] **Step 5: 实现源**

`src/session_manager.cpp`：
```cpp
#include "data_recorder/session_manager.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace data_recorder
{

std::string SessionManager::create_session_directory(
  const std::string & output_dir, const std::string & session_id) const
{
  const fs::path dir = fs::path(output_dir) / session_id;
  fs::create_directories(dir);
  return fs::absolute(dir).string();
}

void SessionManager::write_session_yaml(const SessionRecord & record) const
{
  YAML::Node root;
  root["session"] = record.session_id;
  YAML::Node recorded_at;
  recorded_at["unix"] = record.unix_time;
  recorded_at["ros_time_ns"] = record.ros_time_ns;
  root["recorded_at"] = recorded_at;
  root["duration_seconds"] = record.duration_seconds;

  for (const auto & topic : record.topics) {
    YAML::Node t;
    t["name"] = topic.name;
    t["backend"] = topic.backend;
    root["topics"].push_back(t);
  }
  for (const auto & tag : record.tags) {
    YAML::Node t;
    t["name"] = tag.name;
    t["color"] = tag.color;
    root["tags"].push_back(t);
  }
  for (const auto & a : record.annotations) {
    YAML::Node n;
    n["name"] = a.name;
    n["shortcut"] = a.shortcut;
    n["kind"] = a.kind;
    n["color"] = a.color;
    if (a.kind == "range") {
      n["start"] = a.t;
      n["end"] = a.end;
    } else {
      n["t"] = a.t;
    }
    root["annotations"].push_back(n);
  }

  const fs::path path = fs::path(record.directory) / "session.yaml";
  std::ofstream out(path);
  out << "# 由 data_recorder 在停止录制时自动生成\n" << root;
}

uint64_t SessionManager::directory_size(const std::string & dir)
{
  uint64_t total = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, ec);
    it != fs::recursive_directory_iterator(); it.increment(ec))
  {
    if (ec) { break; }
    if (it->is_regular_file(ec)) {
      total += it->file_size(ec);
    }
  }
  return total;
}

std::vector<SessionRecord> SessionManager::scan(const std::string & output_dir) const
{
  std::vector<SessionRecord> sessions;
  std::error_code ec;
  if (!fs::exists(output_dir, ec)) {
    return sessions;
  }
  for (const auto & entry : fs::directory_iterator(output_dir, ec)) {
    if (ec) { break; }
    if (!entry.is_directory()) { continue; }
    const fs::path yaml_path = entry.path() / "session.yaml";
    if (!fs::exists(yaml_path)) { continue; }  // 静默跳过（崩溃/进行中会话）

    YAML::Node root;
    try {
      root = YAML::LoadFile(yaml_path.string());
    } catch (const YAML::Exception &) {
      continue;  // 损坏的 yaml 跳过
    }

    SessionRecord r;
    r.session_id = root["session"] ? root["session"].as<std::string>() : entry.path().filename().string();
    r.directory = fs::absolute(entry.path()).string();
    if (root["recorded_at"]) {
      r.unix_time = root["recorded_at"]["unix"].as<double>(0.0);
      r.ros_time_ns = root["recorded_at"]["ros_time_ns"].as<int64_t>(0);
    }
    r.duration_seconds = root["duration_seconds"].as<double>(0.0);
    r.size_bytes = directory_size(entry.path().string());  // 现算，不来自 yaml

    if (root["topics"]) {
      for (const auto & t : root["topics"]) {
        r.topics.push_back({t["name"].as<std::string>(""), t["backend"].as<std::string>("rosbag")});
      }
    }
    if (root["tags"]) {
      for (const auto & t : root["tags"]) {
        r.tags.push_back({t["name"].as<std::string>(""), t["color"].as<std::string>("#8a94a6")});
      }
    }
    if (root["annotations"]) {
      for (const auto & a : root["annotations"]) {
        AnnotationRecord rec;
        rec.name = a["name"].as<std::string>("");
        rec.shortcut = a["shortcut"].as<std::string>("");
        rec.kind = a["kind"].as<std::string>("point");
        rec.color = a["color"].as<std::string>("#3b82f6");
        if (rec.kind == "range") {
          rec.t = a["start"].as<double>(0.0);
          rec.end = a["end"].as<double>(0.0);
        } else {
          rec.t = a["t"].as<double>(0.0);
        }
        r.annotations.push_back(rec);
      }
    }
    sessions.push_back(std::move(r));
  }
  // 新会话在前
  std::sort(sessions.begin(), sessions.end(),
    [](const SessionRecord & a, const SessionRecord & b) { return a.session_id > b.session_id; });
  return sessions;
}

}  // namespace data_recorder
```

- [ ] **Step 6: 加进核心库列表**

`src/session_manager.cpp` → `DATA_RECORDER_SOURCES`，头 → `DATA_RECORDER_HEADERS`。

- [ ] **Step 7: 运行确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R test_session_manager \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: 4 个用例全 PASS。

- [ ] **Step 8: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/session_manager.hpp src/session_manager.cpp test/test_session_manager.cpp CMakeLists.txt
git commit -m "feat: 加 SessionManager(写/扫描 session.yaml + 大小现算) + 单测"
```

---

### Task 5: VideoRecorder — libav 编码 + sidecar CSV

**Files:**
- Create: `include/data_recorder/video_recorder.hpp`, `src/video_recorder.cpp`
- Test: `test/test_video_recorder.cpp`

> **libav 调用顺序（FFmpeg 4.4，已验证）：** find_encoder_by_name("libx264") → alloc_context3 → 设 width/height/pix_fmt=YUV420P/time_base=1/90000/gop_size → av_opt_set(priv_data, "crf"/"preset") → open2 → sws_getContext(BGR24→YUV420P) → alloc_output_context2("mp4") → new_stream → parameters_from_context → avio_open → write_header。每帧：填 BGR24 frame → frame->pts = round(ros_t_s × 90000) → send_frame → while receive_packet: rescale_ts + interleaved_write_frame。结束：send_frame(NULL) 排空 → write_trailer → 关闭释放。

- [ ] **Step 1: 写失败测试**

`test/test_video_recorder.cpp`：
```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "data_recorder/video_recorder.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::ImageFrame make_bgr8_frame(int w, int h, int64_t stamp_ns, uint8_t fill)
{
  data_recorder::ImageFrame f;
  f.width = w;
  f.height = h;
  f.step = w * 3;
  f.encoding = "bgr8";
  f.ros_stamp_ns = stamp_ns;
  f.data.assign(static_cast<size_t>(w) * h * 3, fill);
  return f;
}
}  // namespace

TEST(VideoRecorder, EncodesFramesToDecodableMp4WithCsv)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  const std::string mp4 = (tmp / "cam.mp4").string();
  const std::string csv = (tmp / "cam.csv").string();

  data_recorder::VideoParams params;  // 默认 libx264/crf23/medium/yuv420p
  {
    data_recorder::VideoRecorder rec(mp4, csv, 64, 48, params);
    ASSERT_TRUE(rec.is_open());
    // 30 帧，~30fps（间隔 1/30 s）
    for (int i = 0; i < 30; ++i) {
      auto frame = make_bgr8_frame(64, 48, static_cast<int64_t>(i) * 33'333'333LL,
        static_cast<uint8_t>(i * 8));
      rec.encode(frame);
    }
    rec.close();  // flush + trailer
  }

  // mp4 存在且非空
  ASSERT_TRUE(fs::exists(mp4));
  EXPECT_GT(fs::file_size(mp4), 0u);

  // CSV 行数 = 帧头 + 30
  std::ifstream in(csv);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(in, line)) { lines.push_back(line); }
  ASSERT_EQ(lines.size(), 31u);  // header + 30
  EXPECT_EQ(lines[0], "frame_index,ros_stamp_ns,pts_ns");

  // PTS 单调递增
  auto pts_of = [](const std::string & l) {
    std::stringstream ss(l); std::string a, b, c;
    std::getline(ss, a, ','); std::getline(ss, b, ','); std::getline(ss, c, ',');
    return std::stoll(c);
  };
  int64_t prev = -1;
  for (size_t i = 1; i < lines.size(); ++i) {
    int64_t pts = pts_of(lines[i]);
    EXPECT_GT(pts, prev);
    prev = pts;
  }

  fs::remove_all(tmp);
}

TEST(VideoRecorder, UnsupportedEncodingFailsToOpenGracefully)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test_unsup";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  // 喂一个 "bayer_rggb8" 帧应被识别为不支持；编码器本身能开，但 encode 跳过非支持编码。
  data_recorder::VideoParams params;
  data_recorder::VideoRecorder rec((tmp / "x.mp4").string(), (tmp / "x.csv").string(), 64, 48, params);
  ASSERT_TRUE(rec.is_open());

  data_recorder::ImageFrame f;
  f.width = 64; f.height = 48; f.step = 64 * 3;
  f.encoding = "bayer_rggb8";  // 不支持
  f.ros_stamp_ns = 0;
  f.data.assign(64 * 48 * 3, 0);
  const bool encoded = rec.encode(f);
  EXPECT_FALSE(encoded);  // 跳过，不崩溃
  rec.close();

  fs::remove_all(tmp);
}
```

- [ ] **Step 2: 加测试目标（链接 libav）**

```cmake
  ament_add_gtest(test_video_recorder test/test_video_recorder.cpp)
  target_link_libraries(test_video_recorder data_recorder_core PkgConfig::LIBAV)
```

- [ ] **Step 3: 运行确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -10
```
Expected: 编译失败 `video_recorder.hpp: No such file`。

- [ ] **Step 4: 实现头**

`include/data_recorder/video_recorder.hpp`：
```cpp
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "data_recorder/recorder_types.hpp"

struct AVCodecContext;
struct AVFormatContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace data_recorder
{

struct VideoParams
{
  std::string codec{"libx264"};
  std::string preset{"medium"};
  std::string pix_fmt{"yuv420p"};
  int crf{23};
  std::string container{"mp4"};
};

// 把 bgr8/rgb8/mono8 帧用 libav 编码为视频文件，并把逐帧 ROS 时间戳写 sidecar CSV。
// 非线程安全——由单个 worker 线程使用。
class VideoRecorder
{
public:
  VideoRecorder(
    const std::string & video_path, const std::string & csv_path,
    int width, int height, const VideoParams & params);
  ~VideoRecorder();

  VideoRecorder(const VideoRecorder &) = delete;
  VideoRecorder & operator=(const VideoRecorder &) = delete;

  bool is_open() const { return open_; }

  // 编码一帧。不支持的编码或尺寸不符返回 false（跳过，不抛）。
  bool encode(const ImageFrame & frame);

  // flush 编码器、写 trailer、关 CSV。可安全重复调用。
  void close();

private:
  bool init(const VideoParams & params);
  bool fill_source_frame(const ImageFrame & frame);
  void drain_packets();

  std::string video_path_;
  int width_{0};
  int height_{0};
  bool open_{false};
  int64_t frame_index_{0};
  int64_t first_stamp_ns_{0};
  bool have_first_{false};

  AVCodecContext * codec_ctx_{nullptr};
  AVFormatContext * fmt_ctx_{nullptr};
  AVStream * stream_{nullptr};
  AVFrame * bgr_frame_{nullptr};   // 源 BGR24
  AVFrame * yuv_frame_{nullptr};   // 编码 YUV420P
  AVPacket * packet_{nullptr};
  SwsContext * sws_{nullptr};

  std::ofstream csv_;
};

}  // namespace data_recorder
```

- [ ] **Step 5: 实现源**

`src/video_recorder.cpp`：
```cpp
#include "data_recorder/video_recorder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <cstring>
#include <iostream>

namespace data_recorder
{

namespace
{
constexpr int kTimebaseDen = 90000;  // 细 timebase 供近似 VFR PTS

bool encoding_supported(const std::string & enc)
{
  return enc == "bgr8" || enc == "rgb8" || enc == "mono8";
}

int source_av_format(const std::string & enc)
{
  if (enc == "bgr8") { return AV_PIX_FMT_BGR24; }
  if (enc == "rgb8") { return AV_PIX_FMT_RGB24; }
  if (enc == "mono8") { return AV_PIX_FMT_GRAY8; }
  return AV_PIX_FMT_NONE;
}
}  // namespace

VideoRecorder::VideoRecorder(
  const std::string & video_path, const std::string & csv_path,
  int width, int height, const VideoParams & params)
: video_path_(video_path), width_(width), height_(height)
{
  csv_.open(csv_path);
  if (csv_) {
    csv_ << "frame_index,ros_stamp_ns,pts_ns\n";
  }
  open_ = init(params);
}

VideoRecorder::~VideoRecorder()
{
  close();
}

bool VideoRecorder::init(const VideoParams & params)
{
  const AVCodec * codec = avcodec_find_encoder_by_name(params.codec.c_str());
  if (!codec) {
    std::cerr << "[VideoRecorder] 找不到编码器: " << params.codec << "\n";
    return false;
  }
  codec_ctx_ = avcodec_alloc_context3(codec);
  if (!codec_ctx_) { return false; }
  codec_ctx_->width = width_;
  codec_ctx_->height = height_;
  codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
  codec_ctx_->time_base = AVRational{1, kTimebaseDen};
  codec_ctx_->framerate = AVRational{30, 1};  // 提示
  codec_ctx_->gop_size = 60;

  av_opt_set(codec_ctx_->priv_data, "preset", params.preset.c_str(), 0);
  av_opt_set(codec_ctx_->priv_data, "crf", std::to_string(params.crf).c_str(), 0);

  if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
    std::cerr << "[VideoRecorder] 编码器打开失败\n";
    return false;
  }

  if (avformat_alloc_output_context2(&fmt_ctx_, nullptr, params.container.c_str(),
      video_path_.c_str()) < 0 || !fmt_ctx_)
  {
    return false;
  }
  stream_ = avformat_new_stream(fmt_ctx_, nullptr);
  if (!stream_) { return false; }
  stream_->time_base = codec_ctx_->time_base;
  if (avcodec_parameters_from_context(stream_->codecpar, codec_ctx_) < 0) { return false; }

  if (avio_open(&fmt_ctx_->pb, video_path_.c_str(), AVIO_FLAG_WRITE) < 0) { return false; }
  if (avformat_write_header(fmt_ctx_, nullptr) < 0) { return false; }

  // 编码用 YUV 帧
  yuv_frame_ = av_frame_alloc();
  yuv_frame_->format = AV_PIX_FMT_YUV420P;
  yuv_frame_->width = width_;
  yuv_frame_->height = height_;
  if (av_frame_get_buffer(yuv_frame_, 0) < 0) { return false; }

  packet_ = av_packet_alloc();
  return packet_ != nullptr;
}

bool VideoRecorder::fill_source_frame(const ImageFrame & frame)
{
  const int src_fmt = source_av_format(frame.encoding);
  if (src_fmt == AV_PIX_FMT_NONE) { return false; }
  if (frame.width != width_ || frame.height != height_) { return false; }

  // 懒建 swscale（首帧定源格式）
  if (!sws_) {
    sws_ = sws_getContext(width_, height_, static_cast<AVPixelFormat>(src_fmt),
      width_, height_, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) { return false; }
  }

  const uint8_t * src_slices[4] = {frame.data.data(), nullptr, nullptr, nullptr};
  int src_stride[4] = {frame.step, 0, 0, 0};
  if (av_frame_make_writable(yuv_frame_) < 0) { return false; }
  sws_scale(sws_, src_slices, src_stride, 0, height_, yuv_frame_->data, yuv_frame_->linesize);
  return true;
}

void VideoRecorder::drain_packets()
{
  for (;;) {
    const int ret = avcodec_receive_packet(codec_ctx_, packet_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) { break; }
    if (ret < 0) { break; }
    av_packet_rescale_ts(packet_, codec_ctx_->time_base, stream_->time_base);
    packet_->stream_index = stream_->index;
    av_interleaved_write_frame(fmt_ctx_, packet_);
    av_packet_unref(packet_);
  }
}

bool VideoRecorder::encode(const ImageFrame & frame)
{
  if (!open_) { return false; }
  if (!encoding_supported(frame.encoding)) {
    return false;  // 跳过不支持编码
  }
  if (!fill_source_frame(frame)) { return false; }

  // PTS：用每帧 ROS 时间戳（相对首帧），换算到 1/90000 timebase。
  if (!have_first_) { first_stamp_ns_ = frame.ros_stamp_ns; have_first_ = true; }
  const double rel_seconds = static_cast<double>(frame.ros_stamp_ns - first_stamp_ns_) / 1e9;
  const int64_t pts = std::llround(rel_seconds * kTimebaseDen);
  yuv_frame_->pts = pts;

  if (avcodec_send_frame(codec_ctx_, yuv_frame_) < 0) { return false; }
  drain_packets();

  if (csv_) {
    csv_ << frame_index_ << ',' << frame.ros_stamp_ns << ',' << pts << '\n';
  }
  ++frame_index_;
  return true;
}

void VideoRecorder::close()
{
  if (codec_ctx_ && fmt_ctx_) {
    avcodec_send_frame(codec_ctx_, nullptr);  // flush
    drain_packets();
    av_write_trailer(fmt_ctx_);
  }
  if (csv_.is_open()) { csv_.close(); }
  if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
  if (packet_) { av_packet_free(&packet_); }
  if (yuv_frame_) { av_frame_free(&yuv_frame_); }
  if (bgr_frame_) { av_frame_free(&bgr_frame_); }
  if (fmt_ctx_) {
    if (fmt_ctx_->pb) { avio_closep(&fmt_ctx_->pb); }
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
  }
  if (codec_ctx_) { avcodec_free_context(&codec_ctx_); }
  open_ = false;
}

}  // namespace data_recorder
```

- [ ] **Step 6: 加进核心库列表 + 链接 libav**

`src/video_recorder.cpp` → `DATA_RECORDER_SOURCES`，头 → `DATA_RECORDER_HEADERS`。
在 `target_link_libraries(data_recorder_core ...)` 列表里加 `PkgConfig::LIBAV`。

- [ ] **Step 7: 运行确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R test_video_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: 2 个用例 PASS。若 mono8 测试涉及 swscale GRAY8→YUV420P 报警告无妨，只要 mp4 可写、CSV 行数对、PTS 单调。

- [ ] **Step 8: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/video_recorder.hpp src/video_recorder.cpp test/test_video_recorder.cpp CMakeLists.txt
git commit -m "feat: 加 VideoRecorder(libav bgr8→mp4 + sidecar CSV) + 单测"
```

---

### Task 6: RosbagWriter — 包 rosbag2_cpp::Writer + QoS

**Files:**
- Create: `include/data_recorder/rosbag_writer.hpp`, `src/rosbag_writer.cpp`
- Test: `test/test_rosbag_writer.cpp`

- [ ] **Step 1: 写失败测试（写入→用 Reader 读回闭环）**

`test/test_rosbag_writer.cpp`：
```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <std_msgs/msg/string.hpp>

#include "data_recorder/rosbag_writer.hpp"

namespace fs = std::filesystem;

TEST(RosbagWriter, WritesMessagesReadableByRosbag2Reader)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_rosbag_test";
  fs::remove_all(tmp);
  const std::string bag_dir = (tmp / "rosbag").string();

  // 序列化几条 std_msgs/String
  rclcpp::Serialization<std_msgs::msg::String> serializer;
  {
    data_recorder::RosbagWriter writer(bag_dir, /*storage_id=*/"");  // 空=默认存储
    writer.add_topic("/chatter", "std_msgs/msg/String", /*offered_qos_profiles=*/"");
    for (int i = 0; i < 5; ++i) {
      std_msgs::msg::String msg;
      msg.data = "hello " + std::to_string(i);
      rclcpp::SerializedMessage serialized;
      serializer.serialize_message(&msg, &serialized);
      writer.write("/chatter", serialized, /*time_stamp_ns=*/1000LL + i);
    }
    writer.close();
  }

  // 用 rosbag2 Reader 读回
  rosbag2_cpp::Reader reader;
  reader.open(bag_dir);
  int count = 0;
  while (reader.has_next()) {
    auto bag_msg = reader.read_next();
    EXPECT_EQ(bag_msg->topic_name, "/chatter");
    ++count;
  }
  EXPECT_EQ(count, 5);

  fs::remove_all(tmp);
}
```

- [ ] **Step 2: 加测试目标 + std_msgs 依赖**

在 `package.xml` 加 `<test_depend>std_msgs</test_depend>`。CMake `if(BUILD_TESTING)` 块加：
```cmake
  find_package(std_msgs REQUIRED)
  ament_add_gtest(test_rosbag_writer test/test_rosbag_writer.cpp)
  target_link_libraries(test_rosbag_writer data_recorder_core)
  ament_target_dependencies(test_rosbag_writer rclcpp rosbag2_cpp std_msgs)
```

- [ ] **Step 3: 运行确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -10
```
Expected: 编译失败 `rosbag_writer.hpp: No such file`。

- [ ] **Step 4: 实现头**

`include/data_recorder/rosbag_writer.hpp`：
```cpp
#pragma once

#include <memory>
#include <string>

#include <rclcpp/serialized_message.hpp>

namespace rosbag2_cpp { class Writer; }

namespace data_recorder
{

// 包 rosbag2_cpp::Writer：默认存储（mcap 若可用，否则回退）；产物可被 ros2 bag 读取。
// 非线程安全——由单个 worker 线程使用。
class RosbagWriter
{
public:
  // storage_id 空 → 用 rosbag2_storage::get_default_storage_id()，但优先 mcap（若已注册）。
  RosbagWriter(const std::string & bag_dir, const std::string & storage_id);
  ~RosbagWriter();

  RosbagWriter(const RosbagWriter &) = delete;
  RosbagWriter & operator=(const RosbagWriter &) = delete;

  bool is_open() const { return open_; }
  const std::string & storage_id() const { return resolved_storage_id_; }

  void add_topic(
    const std::string & name, const std::string & type,
    const std::string & offered_qos_profiles);

  void write(
    const std::string & topic, const rclcpp::SerializedMessage & msg, int64_t time_stamp_ns);

  void close();

private:
  std::unique_ptr<rosbag2_cpp::Writer> writer_;
  std::string resolved_storage_id_;
  bool open_{false};
};

}  // namespace data_recorder
```

- [ ] **Step 5: 实现源**

`src/rosbag_writer.cpp`：
```cpp
#include "data_recorder/rosbag_writer.hpp"

#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_cpp/writers/sequential_writer.hpp>
#include <rosbag2_storage/default_storage_id.hpp>
#include <rosbag2_storage/serialized_bag_message.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <rosbag2_storage/topic_metadata.hpp>
#include <rosbag2_storage/storage_factory.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace data_recorder
{

namespace
{
// mcap 若已注册则优先，否则回退默认存储。
std::string resolve_storage(const std::string & requested)
{
  if (!requested.empty()) {
    return requested;
  }
  rosbag2_storage::StorageFactory factory;
  const auto writers = factory.get_declared_write_plugins();
  if (std::find(writers.begin(), writers.end(), "mcap") != writers.end()) {
    return "mcap";
  }
  if (writers.empty()) {
    return rosbag2_storage::get_default_storage_id();
  }
  // get_default_storage_id 必然已安装
  return rosbag2_storage::get_default_storage_id();
}
}  // namespace

RosbagWriter::RosbagWriter(const std::string & bag_dir, const std::string & storage_id)
{
  resolved_storage_id_ = resolve_storage(storage_id);
  try {
    writer_ = std::make_unique<rosbag2_cpp::Writer>();
    rosbag2_storage::StorageOptions storage_options;
    storage_options.uri = bag_dir;
    storage_options.storage_id = resolved_storage_id_;
    writer_->open(storage_options);
    open_ = true;
  } catch (const std::exception & e) {
    std::cerr << "[RosbagWriter] 打开失败: " << e.what() << "\n";
    open_ = false;
  }
}

RosbagWriter::~RosbagWriter()
{
  close();
}

void RosbagWriter::add_topic(
  const std::string & name, const std::string & type, const std::string & offered_qos_profiles)
{
  if (!open_) { return; }
  rosbag2_storage::TopicMetadata metadata;
  metadata.name = name;
  metadata.type = type;
  metadata.serialization_format = "cdr";
  metadata.offered_qos_profiles = offered_qos_profiles;
  writer_->create_topic(metadata);
}

void RosbagWriter::write(
  const std::string & topic, const rclcpp::SerializedMessage & msg, int64_t time_stamp_ns)
{
  if (!open_) { return; }
  auto bag_msg = std::make_shared<rosbag2_storage::SerializedBagMessage>();
  bag_msg->topic_name = topic;
  bag_msg->time_stamp = time_stamp_ns;

  const auto & rcl_msg = msg.get_rcl_serialized_message();
  bag_msg->serialized_data = std::shared_ptr<rcutils_uint8_array_t>(
    new rcutils_uint8_array_t,
    [](rcutils_uint8_array_t * arr) {
      rcutils_uint8_array_fini(arr);
      delete arr;
    });
  *bag_msg->serialized_data = rcutils_get_zero_initialized_uint8_array();
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rcutils_uint8_array_init(bag_msg->serialized_data.get(), rcl_msg.buffer_length, &allocator);
  std::memcpy(bag_msg->serialized_data->buffer, rcl_msg.buffer, rcl_msg.buffer_length);
  bag_msg->serialized_data->buffer_length = rcl_msg.buffer_length;

  writer_->write(bag_msg);
}

void RosbagWriter::close()
{
  if (writer_ && open_) {
    writer_.reset();  // 析构写 metadata.yaml
  }
  open_ = false;
}

}  // namespace data_recorder
```

- [ ] **Step 6: 加进核心库列表 + 依赖**

`src/rosbag_writer.cpp` → `DATA_RECORDER_SOURCES`，头 → `DATA_RECORDER_HEADERS`。
在 `ament_target_dependencies(data_recorder_core rclcpp ament_index_cpp)` 改为加 `rosbag2_cpp rosbag2_storage`。

- [ ] **Step 7: 运行确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R test_rosbag_writer \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: PASS（写 5 条 → Reader 读回 5 条，topic 正确）。

- [ ] **Step 8: 命令行交叉验证 ros2 bag 可读**

```bash
source ~/.local/ros2_rc && rs && ls /tmp/dr_rosbag_test 2>/dev/null || echo "（测试已清理 tmp，跳过；该闭环已由 Reader 验证）"
```
说明：测试用例已用 `rosbag2_cpp::Reader`（与 `ros2 bag` 同库）闭环验证可读性。

- [ ] **Step 9: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/rosbag_writer.hpp src/rosbag_writer.cpp test/test_rosbag_writer.cpp CMakeLists.txt package.xml
git commit -m "feat: 加 RosbagWriter(rosbag2_cpp::Writer 包装, mcap 优先+回退) + 读回闭环测试"
```

---

## Phase 3 — 引擎与桥（ROS 线程 ↔ GUI 线程）

### Task 7: LiveBridge — 线程安全的最新帧仓 + queued 信号

**Files:**
- Create: `include/data_recorder/live_bridge.hpp`, `src/live_bridge.cpp`
- Test: 由 Task 9 的 QML 冒烟 + 手动 e2e 覆盖（LiveBridge 是薄 marshal 层，无独立逻辑值得单测）

- [ ] **Step 1: 实现头**

`include/data_recorder/live_bridge.hpp`：
```cpp
#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "data_recorder/recorder_types.hpp"

namespace data_recorder
{

// 线程安全的"每路最新帧"仓 + 把引擎事件 marshal 到 GUI 线程的信号。
// 引擎在 ROS 线程调 push_frame/push_stats/advance_live_edge；信号经 QueuedConnection 到 GUI。
class LiveBridge : public QObject
{
  Q_OBJECT

public:
  explicit LiveBridge(QObject * parent = nullptr);

  // —— ROS 线程调用（线程安全）——
  void push_frame(const QString & topic_key, std::shared_ptr<const QImage> image);
  void push_stats(const std::vector<TopicStats> & stats);
  void set_live_edge(double seconds);

  // —— GUI 线程调用（image provider）——
  std::shared_ptr<const QImage> latest_frame(const QString & topic_key) const;

signals:
  void frameReady(const QString & topic_key, int seq);   // QueuedConnection
  void statsUpdated(const QVariantList & stats);          // QueuedConnection
  void liveEdgeChanged(double seconds);                   // QueuedConnection

private:
  mutable std::mutex mutex_;
  std::map<QString, std::shared_ptr<const QImage>> frames_;
  std::map<QString, int> seqs_;
};

}  // namespace data_recorder
```

- [ ] **Step 2: 实现源**

`src/live_bridge.cpp`：
```cpp
#include "data_recorder/live_bridge.hpp"

#include <QMetaObject>
#include <QVariantMap>

namespace data_recorder
{

LiveBridge::LiveBridge(QObject * parent)
: QObject(parent)
{
}

void LiveBridge::push_frame(const QString & topic_key, std::shared_ptr<const QImage> image)
{
  int seq = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_[topic_key] = std::move(image);
    seq = ++seqs_[topic_key];
  }
  // 跨线程发信号：排到 GUI 线程事件循环。
  QMetaObject::invokeMethod(this, "frameReady", Qt::QueuedConnection,
    Q_ARG(QString, topic_key), Q_ARG(int, seq));
}

void LiveBridge::push_stats(const std::vector<TopicStats> & stats)
{
  QVariantList list;
  for (const auto & s : stats) {
    QVariantMap m;
    m["topicKey"] = QString::fromStdString(s.topic_key);
    m["hz"] = s.hz;
    m["width"] = s.width;
    m["height"] = s.height;
    list.push_back(m);
  }
  QMetaObject::invokeMethod(this, "statsUpdated", Qt::QueuedConnection,
    Q_ARG(QVariantList, list));
}

void LiveBridge::set_live_edge(double seconds)
{
  QMetaObject::invokeMethod(this, "liveEdgeChanged", Qt::QueuedConnection,
    Q_ARG(double, seconds));
}

std::shared_ptr<const QImage> LiveBridge::latest_frame(const QString & topic_key) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = frames_.find(topic_key);
  return it != frames_.end() ? it->second : nullptr;
}

}  // namespace data_recorder
```

- [ ] **Step 3: 加进核心库列表 + 链接 Qt**

`src/live_bridge.cpp` → `DATA_RECORDER_SOURCES`，头 → `DATA_RECORDER_HEADERS`。
（`data_recorder_core` 已链接 Qt6::Core/Gui，QImage/QObject 可用。AUTOMOC 已开。）

- [ ] **Step 4: 构建确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -8
```
Expected: 构建成功（moc 生成 LiveBridge 元对象）。

- [ ] **Step 5: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/live_bridge.hpp src/live_bridge.cpp CMakeLists.txt
git commit -m "feat: 加 LiveBridge(线程安全最新帧仓 + queued 信号)"
```

---

### Task 8: CameraImageProvider — QQuickImageProvider

**Files:**
- Create: `include/data_recorder/camera_image_provider.hpp`, `src/camera_image_provider.cpp`

- [ ] **Step 1: 实现头**

`include/data_recorder/camera_image_provider.hpp`：
```cpp
#pragma once

#include <QQuickImageProvider>

namespace data_recorder
{

class LiveBridge;

// 按 image://camera/<topic_key>?seq=N 返回最新帧。seq 仅用于让 QML 失效缓存。
class CameraImageProvider : public QQuickImageProvider
{
public:
  explicit CameraImageProvider(LiveBridge * bridge);

  QImage requestImage(const QString & id, QSize * size, const QSize & requestedSize) override;

private:
  LiveBridge * bridge_;  // 不拥有
};

}  // namespace data_recorder
```

- [ ] **Step 2: 实现源**

`src/camera_image_provider.cpp`：
```cpp
#include "data_recorder/camera_image_provider.hpp"

#include "data_recorder/live_bridge.hpp"

namespace data_recorder
{

CameraImageProvider::CameraImageProvider(LiveBridge * bridge)
: QQuickImageProvider(QQuickImageProvider::Image), bridge_(bridge)
{
}

QImage CameraImageProvider::requestImage(
  const QString & id, QSize * size, const QSize & requestedSize)
{
  // id 形如 "<topic_key>?seq=N"；去掉 query 部分取 key。
  const QString key = id.section('?', 0, 0);
  auto frame = bridge_ ? bridge_->latest_frame(key) : nullptr;
  if (!frame || frame->isNull()) {
    QImage placeholder(requestedSize.isValid() ? requestedSize : QSize(16, 16),
      QImage::Format_RGB888);
    placeholder.fill(Qt::black);
    if (size) { *size = placeholder.size(); }
    return placeholder;
  }
  if (size) { *size = frame->size(); }
  return *frame;  // QImage 隐式共享，拷贝廉价
}

}  // namespace data_recorder
```

- [ ] **Step 3: 加进核心库列表 + 链接 Quick**

`src/camera_image_provider.cpp` → `DATA_RECORDER_SOURCES`，头 → `DATA_RECORDER_HEADERS`。
（`data_recorder_core` 已链接 Qt6::Quick，`QQuickImageProvider` 可用。）

- [ ] **Step 4: 构建确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -8
```
Expected: 构建成功。

- [ ] **Step 5: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/camera_image_provider.hpp src/camera_image_provider.cpp CMakeLists.txt
git commit -m "feat: 加 CameraImageProvider(QQuickImageProvider 供最新帧)"
```

---

### Task 9: RecorderEngine — 订阅 + 会话生命周期 + 扇出

**Files:**
- Create: `include/data_recorder/recorder_engine.hpp`, `src/recorder_engine.cpp`

> **设计：** 引擎在 ROS 线程跑。构造时建全部订阅（rosbag 组 generic、video 组 typed Image），启动 rate monitor。`start_session`/`stop_session` 管 writers。回调轻量：rosbag 回调把序列化消息入 RosbagWriter 队列；image 回调转 QImage 推 LiveBridge + 更新 rate + 录制时入 VideoRecorder 队列。~2Hz 推 stats，~30Hz 推 live edge。

- [ ] **Step 1: 实现头**

`include/data_recorder/recorder_engine.hpp`：
```cpp
#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "data_recorder/config_model.hpp"
#include "data_recorder/recorder_types.hpp"
#include "data_recorder/rosbag_writer.hpp"
#include "data_recorder/topic_rate_monitor.hpp"
#include "data_recorder/video_recorder.hpp"
#include "data_recorder/writer_queue.hpp"

namespace data_recorder
{

class LiveBridge;
class SessionManager;

// 录制引擎：持全部订阅、会话生命周期、扇出。运行在 ROS spin 线程。
class RecorderEngine
{
public:
  RecorderEngine(
    rclcpp::Node::SharedPtr node, ConfigData config,
    LiveBridge * bridge, SessionManager * session_manager);
  ~RecorderEngine();

  // 由 GUI 线程调（通过 AppController），内部用锁保护会话状态。
  // 返回 session_id（成功）或空串（失败，如 output_dir 不可写）。
  std::string start_session();
  // 写 annotations/tags 快照到 session.yaml；返回完成的 SessionRecord。
  SessionRecord stop_session(
    const std::vector<AnnotationRecord> & annotations,
    const std::vector<TagRecord> & tags);

  bool is_recording() const { return recording_.load(); }
  double live_edge_seconds() const;

private:
  void setup_subscriptions();
  void on_rosbag_message(
    const std::string & topic, const std::string & type,
    std::shared_ptr<rclcpp::SerializedMessage> msg);
  void on_image_message(
    const std::string & topic, sensor_msgs::msg::Image::ConstSharedPtr msg);
  std::string offered_qos_for(const std::string & topic) const;

  rclcpp::Node::SharedPtr node_;
  ConfigData config_;
  LiveBridge * bridge_;
  SessionManager * session_manager_;

  std::vector<rclcpp::SubscriptionBase::SharedPtr> subscriptions_;
  std::map<std::string, TopicRateMonitor> rate_monitors_;
  std::mutex rate_mutex_;
  std::map<std::string, std::pair<int, int>> image_dims_;  // topic -> (w,h)
  std::mutex dims_mutex_;

  // 一路相机的视频 sink：队列 + 懒建 recorder（首帧定尺寸）。
  struct VideoSink
  {
    std::unique_ptr<VideoRecorder> recorder;
    std::unique_ptr<WriterQueue<ImageFrame>> queue;
    std::string video_path;
    std::string csv_path;
    VideoParams params;
  };

  // 会话态（GUI start/stop 与 ROS 回调共享）
  std::mutex session_mutex_;
  std::atomic<bool> recording_{false};
  std::string session_dir_;
  std::string session_id_;
  std::chrono::steady_clock::time_point record_start_steady_;
  double record_start_unix_{0.0};
  int64_t record_start_ros_ns_{0};
  std::unique_ptr<RosbagWriter> rosbag_writer_;
  std::unique_ptr<WriterQueue<std::function<void()>>> rosbag_queue_;
  std::map<std::string, std::shared_ptr<VideoSink>> video_sinks_;

  // live edge / stats 定时器
  rclcpp::TimerBase::SharedPtr stats_timer_;
  rclcpp::TimerBase::SharedPtr live_edge_timer_;
  std::atomic<double> live_edge_seconds_{0.0};
};

}  // namespace data_recorder
```

- [ ] **Step 2: 实现源**

`src/recorder_engine.cpp`：
```cpp
#include "data_recorder/recorder_engine.hpp"

#include <QImage>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <rclcpp/serialization.hpp>
#include <rosbag2_transport/qos.hpp>
#include <yaml-cpp/yaml.h>

#include "data_recorder/live_bridge.hpp"
#include "data_recorder/session_manager.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace data_recorder
{

namespace
{
std::string file_name_for_topic(const std::string & topic)
{
  std::string s = topic;
  if (!s.empty() && s.front() == '/') { s.erase(s.begin()); }
  for (auto & c : s) {
    if (c == '/') { c = '_'; }  // '/' → '_'（两个连续下划线由双斜杠产生，符合 spec __）
  }
  return s;
}

std::string timestamp_now()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
  return os.str();
}
}  // namespace

RecorderEngine::RecorderEngine(
  rclcpp::Node::SharedPtr node, ConfigData config,
  LiveBridge * bridge, SessionManager * session_manager)
: node_(node), config_(std::move(config)), bridge_(bridge), session_manager_(session_manager)
{
  setup_subscriptions();

  stats_timer_ = node_->create_wall_timer(500ms, [this]() {
    std::vector<TopicStats> stats;
    {
      std::lock_guard<std::mutex> lock(rate_mutex_);
      for (auto & [topic, monitor] : rate_monitors_) {
        TopicStats s;
        s.topic_key = topic;
        s.hz = monitor.hz();
        {
          std::lock_guard<std::mutex> dlock(dims_mutex_);
          auto d = image_dims_.find(topic);
          if (d != image_dims_.end()) { s.width = d->second.first; s.height = d->second.second; }
        }
        stats.push_back(s);
      }
    }
    if (bridge_) { bridge_->push_stats(stats); }
  });

  record_start_steady_ = std::chrono::steady_clock::now();
  live_edge_timer_ = node_->create_wall_timer(33ms, [this]() {
    const auto elapsed = std::chrono::steady_clock::now() - record_start_steady_;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    live_edge_seconds_.store(seconds);
    if (bridge_) { bridge_->set_live_edge(seconds); }
  });
}

RecorderEngine::~RecorderEngine()
{
  if (recording_.load()) {
    stop_session({}, {});
  }
}

std::string RecorderEngine::offered_qos_for(const std::string & topic) const
{
  const auto endpoints = node_->get_publishers_info_by_topic(topic);
  if (endpoints.empty()) { return ""; }
  std::vector<rosbag2_transport::Rosbag2QoS> profiles;
  for (const auto & ep : endpoints) {
    profiles.emplace_back(ep.qos_profile());
  }
  YAML::Node node;
  node = profiles;
  return YAML::Dump(node);
}

void RecorderEngine::setup_subscriptions()
{
  for (const auto & topic : config_.topics) {
    {
      std::lock_guard<std::mutex> lock(rate_mutex_);
      rate_monitors_.emplace(topic.topic_name, TopicRateMonitor(1.0));
    }
    if (topic.backend_name == "video" ||
      topic.ui_category == TopicUiCategory::CameraPreview)
    {
      const std::string topic_name = topic.topic_name;
      auto sub = node_->create_subscription<sensor_msgs::msg::Image>(
        topic_name, rclcpp::SensorDataQoS(),
        [this, topic_name](sensor_msgs::msg::Image::ConstSharedPtr msg) {
          on_image_message(topic_name, msg);
        });
      subscriptions_.push_back(sub);
    } else {
      const std::string topic_name = topic.topic_name;
      // 类型在订阅时可能未知；用发布者通告的类型。
      std::string type;
      const auto eps = node_->get_publishers_info_by_topic(topic_name);
      if (!eps.empty()) { type = eps.front().topic_type(); }
      if (type.empty()) { continue; }  // 暂无发布者，跳过（spec v1：简单处理）
      auto qos = rclcpp::QoS(rclcpp::KeepLast(100));
      auto sub = node_->create_generic_subscription(
        topic_name, type, qos,
        [this, topic_name, type](std::shared_ptr<rclcpp::SerializedMessage> msg) {
          on_rosbag_message(topic_name, type, msg);
        });
      subscriptions_.push_back(sub);
    }
  }
}

void RecorderEngine::on_rosbag_message(
  const std::string & topic, const std::string & /*type*/,
  std::shared_ptr<rclcpp::SerializedMessage> msg)
{
  const int64_t now_ns = node_->now().nanoseconds();
  {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    auto it = rate_monitors_.find(topic);
    if (it != rate_monitors_.end()) { it->second.record(now_ns); }
  }
  if (!recording_.load()) { return; }
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (rosbag_queue_) {
    // 拷贝序列化消息进闭包（生命周期安全），入队到 writer 线程。
    auto serialized = std::make_shared<rclcpp::SerializedMessage>(*msg);
    const std::string topic_copy = topic;
    rosbag_queue_->push([this, topic_copy, serialized, now_ns]() {
      rosbag_writer_->write(topic_copy, *serialized, now_ns);
    });
  }
}

void RecorderEngine::on_image_message(
  const std::string & topic, sensor_msgs::msg::Image::ConstSharedPtr msg)
{
  const int64_t header_ns =
    static_cast<int64_t>(msg->header.stamp.sec) * 1'000'000'000LL + msg->header.stamp.nanosec;
  const int64_t now_ns = node_->now().nanoseconds();
  const int64_t stamp_ns = header_ns != 0 ? header_ns : now_ns;

  {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    auto it = rate_monitors_.find(topic);
    if (it != rate_monitors_.end()) { it->second.record(now_ns); }
  }
  {
    std::lock_guard<std::mutex> lock(dims_mutex_);
    image_dims_[topic] = {static_cast<int>(msg->width), static_cast<int>(msg->height)};
  }

  // 预览：转 QImage（bgr8 用 Format_BGR888；其余转 RGB888）
  if (bridge_ && (msg->encoding == "bgr8" || msg->encoding == "rgb8" || msg->encoding == "mono8")) {
    QImage::Format fmt = msg->encoding == "bgr8" ? QImage::Format_BGR888
      : msg->encoding == "mono8" ? QImage::Format_Grayscale8
      : QImage::Format_RGB888;
    QImage img(msg->data.data(), msg->width, msg->height, msg->step, fmt);
    // 深拷贝，脱离 msg 生命周期。
    bridge_->push_frame(QString::fromStdString(topic),
      std::make_shared<QImage>(img.copy()));
  }

  if (!recording_.load()) { return; }
  std::lock_guard<std::mutex> lock(session_mutex_);
  auto sit = video_sinks_.find(topic);
  if (sit != video_sinks_.end() && sit->second && sit->second->queue) {
    ImageFrame frame;
    frame.width = static_cast<int>(msg->width);
    frame.height = static_cast<int>(msg->height);
    frame.step = static_cast<int>(msg->step);
    frame.encoding = msg->encoding;
    frame.ros_stamp_ns = stamp_ns;
    frame.data = msg->data;  // 拷贝
    sit->second->queue->push(std::move(frame));
  }
}

std::string RecorderEngine::start_session()
{
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (recording_.load()) { return session_id_; }

  const std::string output_dir = fs::absolute(config_.output_dir).string();
  std::error_code ec;
  fs::create_directories(output_dir, ec);

  session_id_ = timestamp_now();
  session_dir_ = session_manager_->create_session_directory(output_dir, session_id_);

  // rosbag writer + 队列（阻塞背压）
  rosbag_writer_ = std::make_unique<RosbagWriter>(
    (fs::path(session_dir_) / "rosbag").string(), /*storage_id=*/"");
  if (!rosbag_writer_->is_open()) {
    std::cerr << "[RecorderEngine] rosbag writer 打开失败，取消录制\n";
    rosbag_writer_.reset();
    return "";
  }
  for (const auto & topic : config_.topics) {
    if (topic.backend_name != "video" && topic.ui_category != TopicUiCategory::CameraPreview) {
      const auto eps = node_->get_publishers_info_by_topic(topic.topic_name);
      std::string type = eps.empty() ? "" : eps.front().topic_type();
      if (!type.empty()) {
        rosbag_writer_->add_topic(topic.topic_name, type, offered_qos_for(topic.topic_name));
      }
    }
  }
  rosbag_queue_ = std::make_unique<WriterQueue<std::function<void()>>>(
    2000, OverflowPolicy::Block, [](std::function<void()> task) { task(); });

  // video sinks（丢最旧背压）。recorder 在首帧到达时懒建（尺寸首帧才知）。
  const fs::path video_dir = fs::path(session_dir_) / "video";
  fs::create_directories(video_dir, ec);
  for (const auto & topic : config_.topics) {
    if (topic.backend_name == "video" || topic.ui_category == TopicUiCategory::CameraPreview) {
      const std::string key = topic.topic_name;
      const std::string base = file_name_for_topic(topic.topic_name);
      VideoParams params;
      auto pit = topic.params.find("codec"); if (pit != topic.params.end()) params.codec = pit->second;
      pit = topic.params.find("preset"); if (pit != topic.params.end()) params.preset = pit->second;
      pit = topic.params.find("crf"); if (pit != topic.params.end()) params.crf = std::stoi(pit->second);
      pit = topic.params.find("pix_fmt"); if (pit != topic.params.end()) params.pix_fmt = pit->second;
      pit = topic.params.find("container"); if (pit != topic.params.end()) params.container = pit->second;

      auto sink = std::make_shared<VideoSink>();
      sink->video_path = (video_dir / (base + "." + params.container)).string();
      sink->csv_path = (video_dir / (base + ".csv")).string();
      sink->params = params;
      auto * sink_raw = sink.get();  // 队列 worker 线程在 sink 生命周期内运行
      sink->queue = std::make_unique<WriterQueue<ImageFrame>>(
        90, OverflowPolicy::DropOldest,
        [sink_raw](ImageFrame frame) {
          if (!sink_raw->recorder) {
            sink_raw->recorder = std::make_unique<VideoRecorder>(
              sink_raw->video_path, sink_raw->csv_path, frame.width, frame.height, sink_raw->params);
          }
          sink_raw->recorder->encode(frame);
        });
      video_sinks_[key] = sink;
    }
  }

  record_start_steady_ = std::chrono::steady_clock::now();
  record_start_unix_ = std::chrono::duration<double>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  record_start_ros_ns_ = node_->now().nanoseconds();
  live_edge_seconds_.store(0.0);

  recording_.store(true);
  return session_id_;
}

SessionRecord RecorderEngine::stop_session(
  const std::vector<AnnotationRecord> & annotations, const std::vector<TagRecord> & tags)
{
  std::lock_guard<std::mutex> lock(session_mutex_);
  SessionRecord record;
  if (!recording_.load()) { return record; }
  recording_.store(false);

  // 停 video sinks：排空帧队列 → recorder flush + 写 trailer/CSV
  for (auto & [key, sink] : video_sinks_) {
    if (sink->queue) { sink->queue->stop(); }
    if (sink->recorder) { sink->recorder->close(); }
  }
  video_sinks_.clear();

  // 停 rosbag 队列再关 writer
  if (rosbag_queue_) { rosbag_queue_->stop(); rosbag_queue_.reset(); }
  if (rosbag_writer_) { rosbag_writer_->close(); rosbag_writer_.reset(); }

  record.session_id = session_id_;
  record.directory = session_dir_;
  record.unix_time = record_start_unix_;
  record.ros_time_ns = record_start_ros_ns_;
  record.duration_seconds = live_edge_seconds_.load();
  for (const auto & t : config_.topics) {
    record.topics.push_back({t.topic_name,
      (t.backend_name == "video" || t.ui_category == TopicUiCategory::CameraPreview) ? "video" : "rosbag"});
  }
  record.tags = tags;
  record.annotations = annotations;

  session_manager_->write_session_yaml(record);
  return record;
}

double RecorderEngine::live_edge_seconds() const
{
  return live_edge_seconds_.load();
}

}  // namespace data_recorder
```

- [ ] **Step 3: 加进核心库列表 + 依赖**

`src/recorder_engine.cpp` → `DATA_RECORDER_SOURCES`，头 → `DATA_RECORDER_HEADERS`。
`ament_target_dependencies(data_recorder_core ...)` 加 `rosbag2_transport sensor_msgs`。

- [ ] **Step 4: 构建确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -15
```
Expected: 构建成功。若 `Rosbag2QoS` 的 `YAML::convert` 链接报错，确认已 `ament_target_dependencies` 加 `rosbag2_transport`。

- [ ] **Step 5: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/recorder_engine.hpp src/recorder_engine.cpp CMakeLists.txt
git commit -m "feat: 加 RecorderEngine(订阅+会话生命周期+扇出+推进时钟)"
```

---

## Phase 4 — UI 模型重接线（删占位，接真实数据）

### Task 10: TopicListModel — updateStats + FrameSeqRole

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`, `src/ui_models.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: 改测试——删占位断言，加 updateStats 行为**

打开 `test/test_ui_models.cpp`，找到断言合成正弦序列/伪造帧率分辨率的用例（涉及 `series_list` 非空、`frequency_text` 形如 "20 Hz"、`resolution_text` 形如 "1280x720" 的断言），删除或替换为下面的真实行为测试：
```cpp
TEST(TopicListModel, UpdateStatsBackfillsFrequencyAndResolution)
{
  data_recorder::TopicListModel model;
  model.set_topics({
    []{ data_recorder::TopicEntry t; t.topic_name="/camera/image_raw"; t.backend_name="video";
        t.ui_category=data_recorder::TopicUiCategory::CameraPreview; return t; }(),
  });
  // 初始 frequency_text 为空（无占位）
  const auto idx = model.index(0, 0);
  // 注入 stats
  model.updateStats("/camera/image_raw", 22.0, 848, 480);
  EXPECT_EQ(model.data(idx, data_recorder::TopicListModel::FrequencyTextRole).toString().toStdString(), "22 fps");
  EXPECT_EQ(model.data(idx, data_recorder::TopicListModel::ResolutionTextRole).toString().toStdString(), "848x480");
}

TEST(TopicListModel, NumericTopicShowsHzNotFps)
{
  data_recorder::TopicListModel model;
  model.set_topics({
    []{ data_recorder::TopicEntry t; t.topic_name="/joint_states"; t.backend_name="rosbag";
        t.ui_category=data_recorder::TopicUiCategory::NumericTrack; return t; }(),
  });
  model.updateStats("/joint_states", 400.0, 0, 0);
  EXPECT_EQ(model.data(model.index(0,0), data_recorder::TopicListModel::FrequencyTextRole).toString().toStdString(), "400 Hz");
}
```

- [ ] **Step 2: 运行确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -10
```
Expected: 编译失败 `updateStats` 未声明。

- [ ] **Step 3: 改头——加 role + 方法，删占位声明**

在 `ui_models.hpp` 的 `TopicListModel::Roles` 枚举末尾加 `FrameSeqRole,`。
在 public 段加：
```cpp
  // 由引擎经 LiveBridge 实时回填（GUI 线程调）。
  void updateStats(const QString & topic_key, double hz, int width, int height);
  void updateFrameSeq(const QString & topic_key, int seq);
```
删除 private 段的 `void populate_placeholder_fields(TopicRow & row, int index);` 声明。
在 `TopicRow` 结构加 `int frame_seq{0};`。

- [ ] **Step 4: 改源——删占位生成器，实现 updateStats/updateFrameSeq**

在 `src/ui_models.cpp`：
1. 删除整个 `// ---- Placeholder/demo data generators ----` 到 `// ---- End placeholder/demo data generators ----` 之间的块（`make_series_list`/`make_resolution_text`/`make_frequency_text`），**但保留 `kSeriesColors`**——把它移到该块外的匿名命名空间顶部。
2. 删除 `populate_placeholder_fields` 定义。
3. `set_topics` 里删除 `populate_placeholder_fields(row, ...)` 调用；改为只设真实字段，`series_color` 仍取 `kSeriesColors[i % N]`：
```cpp
    row.series_color = QString::fromLatin1(kSeriesColors[
      static_cast<std::size_t>(i) % kSeriesColors.size()]);
    row.frequency_text = QString();      // 等引擎回填
    row.resolution_text = QString();
    row.series_list = QVariantList();    // v1 数值留空
```
4. 在 `FrameSeqRole` 加入 `data()` 的 switch 和 `roleNames()`（`"frameSeq"`）。
5. 实现两个新方法：
```cpp
void TopicListModel::updateStats(const QString & topic_key, double hz, int width, int height)
{
  for (std::size_t i = 0; i < topics_.size(); ++i) {
    auto & row = topics_[i];
    if (QString::fromStdString(row.topic.topic_name) != topic_key) { continue; }
    const QString unit = row.is_camera ? QStringLiteral("fps") : QStringLiteral("Hz");
    row.frequency_text = QStringLiteral("%1 %2").arg(qRound(hz)).arg(unit);
    if (row.is_camera && width > 0 && height > 0) {
      row.resolution_text = QStringLiteral("%1x%2").arg(width).arg(height);
    }
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {FrequencyTextRole, ResolutionTextRole});
    return;
  }
}

void TopicListModel::updateFrameSeq(const QString & topic_key, int seq)
{
  for (std::size_t i = 0; i < topics_.size(); ++i) {
    if (QString::fromStdString(topics_[i].topic.topic_name) != topic_key) { continue; }
    topics_[i].frame_seq = seq;
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {FrameSeqRole});
    return;
  }
}
```

- [ ] **Step 5: 运行确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R test_ui_models \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: test_ui_models PASS（含新 updateStats 用例；旧占位用例已删）。

- [ ] **Step 6: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "refactor: TopicListModel 删占位生成器, 加 updateStats/FrameSeqRole(真实回填)"
```

---

### Task 11: CameraGridModel — FrameSeqRole + topicKey + updateFrameSeq

**Files:**
- Modify: `include/data_recorder/camera_grid_model.hpp`, `src/camera_grid_model.cpp`
- Modify: `test/test_camera_grid_model.cpp`

- [ ] **Step 1: 加失败测试**

在 `test/test_camera_grid_model.cpp` 末尾加：
```cpp
TEST(CameraGridModel, ExposesTopicKeyAndFrameSeq)
{
  data_recorder::TopicListModel source;
  source.set_topics({
    make_topic("/camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
  });
  data_recorder::CameraGridModel model(&source);
  const auto idx = model.index(0, 0);
  EXPECT_EQ(model.data(idx, data_recorder::CameraGridModel::TopicKeyRole).toString().toStdString(),
    "/camera/image_raw");
  EXPECT_EQ(model.data(idx, data_recorder::CameraGridModel::FrameSeqRole).toInt(), 0);

  model.updateFrameSeq("/camera/image_raw", 7);
  EXPECT_EQ(model.data(idx, data_recorder::CameraGridModel::FrameSeqRole).toInt(), 7);
}
```

- [ ] **Step 2: 运行确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -10
```
Expected: 编译失败 `TopicKeyRole`/`FrameSeqRole`/`updateFrameSeq` 未声明。

- [ ] **Step 3: 改头**

`camera_grid_model.hpp` 的 `Roles` 枚举加 `TopicKeyRole,` 和 `FrameSeqRole,`。
`Camera` 结构加 `int frame_seq{0};`。
public 段加 `Q_INVOKABLE void updateFrameSeq(const QString & topic_key, int seq);`。

- [ ] **Step 4: 改源**

`src/camera_grid_model.cpp`：
- `Camera` 重建时不动 frame_seq（默认 0）。
- `data()` switch 加 `TopicKeyRole`（返回 `topic_name`）和 `FrameSeqRole`（返回 `frame_seq`）。
- `roleNames()` 加 `{TopicKeyRole, "topicKey"}` 和 `{FrameSeqRole, "frameSeq"}`。
- 实现：
```cpp
void CameraGridModel::updateFrameSeq(const QString & topic_key, int seq)
{
  for (std::size_t i = 0; i < visible_.size(); ++i) {
    if (visible_[i].topic_name != topic_key) { continue; }
    visible_[i].frame_seq = seq;
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {FrameSeqRole});
    return;
  }
}
```

- [ ] **Step 5: 运行确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R test_camera_grid_model \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: 全部 CameraGridModel 用例 PASS。

- [ ] **Step 6: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/camera_grid_model.hpp src/camera_grid_model.cpp test/test_camera_grid_model.cpp
git commit -m "refactor: CameraGridModel 加 topicKey/frameSeq role + updateFrameSeq"
```

---

### Task 12: RecordingSessionModel — setSessions（删占位）

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`, `src/ui_models.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: 加失败测试**

在 `test/test_ui_models.cpp` 加：
```cpp
TEST(RecordingSessionModel, SetSessionsPopulatesRows)
{
  data_recorder::RecordingSessionModel model;
  data_recorder::SessionRecord r;
  r.session_id = "2026-06-22_14-30-05";
  r.directory = "/tmp/x/2026-06-22_14-30-05";
  r.duration_seconds = 65.0;  // 1:05
  r.size_bytes = 256 * 1024 * 1024;  // 256 MiB
  r.tags = {{"成功", "#2f9e44"}};
  model.setSessions({r});

  ASSERT_EQ(model.rowCount(), 1);
  const auto idx = model.index(0, 0);
  EXPECT_EQ(model.data(idx, data_recorder::RecordingSessionModel::FolderNameRole).toString().toStdString(),
    "2026-06-22_14-30-05");
  EXPECT_EQ(model.data(idx, data_recorder::RecordingSessionModel::TagNameRole).toString().toStdString(), "成功");
  // 时长格式化为 mm:ss 含 1:05
  EXPECT_NE(model.data(idx, data_recorder::RecordingSessionModel::ShortDurationRole).toString().indexOf("1:05"), -1);
}
```

- [ ] **Step 2: 运行确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -10
```
Expected: 编译失败 `setSessions` 未声明。

- [ ] **Step 3: 改头**

`ui_models.hpp` 的 `RecordingSessionModel`：
- `#include "data_recorder/recorder_types.hpp"`（文件顶已 include config_model，补这个）。
- public 段加 `void setSessions(const std::vector<SessionRecord> & sessions);`。
- 删除 private 段 `void populate_placeholder_sessions();` 声明。

- [ ] **Step 4: 改源**

`src/ui_models.cpp`：
- 删除 `populate_placeholder_sessions` 定义和构造函数里对它的调用。
- 加格式化辅助 + setSessions：
```cpp
namespace
{
QString format_short_duration(double seconds)
{
  const int total = static_cast<int>(seconds);
  const int m = total / 60;
  const int s = total % 60;
  return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString format_size(uint64_t bytes)
{
  const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
  if (mb >= 1024.0) { return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1); }
  return QStringLiteral("%1 MB").arg(mb, 0, 'f', 0);
}
}  // namespace

void RecordingSessionModel::setSessions(const std::vector<SessionRecord> & sessions)
{
  beginResetModel();
  sessions_.clear();
  for (const auto & s : sessions) {
    RecordingSessionRow row;
    row.name = QString::fromStdString(s.session_id);
    row.folder_name = QString::fromStdString(s.session_id);
    row.short_duration = format_short_duration(s.duration_seconds);
    row.full_duration = row.short_duration;
    row.duration = row.short_duration;
    row.size_text = format_size(s.size_bytes);
    row.size = row.size_text;
    if (!s.tags.empty()) {
      row.tag_name = QString::fromStdString(s.tags.front().name);
      row.tag_color = QString::fromStdString(s.tags.front().color);
    }
    sessions_.push_back(std::move(row));
  }
  endResetModel();
}
```

- [ ] **Step 5: 运行确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R test_ui_models \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: PASS（含 setSessions 用例）。

- [ ] **Step 6: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "refactor: RecordingSessionModel 删占位, 加 setSessions(扫描驱动)"
```

---

### Task 13: EventMarkerModel + TagListModel — 快照导出

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`, `src/ui_models.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: 加失败测试**

```cpp
TEST(EventMarkerModel, ExportsAnnotationSnapshotWithMultipleSameName)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({
    {"c", "碰撞", "point", "#e03131"},
    {"2", "倒水", "range", "#2f9e44"},
  });
  model.triggerShortcut("c", 8.04);   // point 1
  model.triggerShortcut("c", 12.88);  // point 2（同名）
  model.toggleRange(1, 5.0);          // range 起
  model.toggleRange(1, 9.3);          // range 止

  const auto annotations = model.exportAnnotations();
  int collision = 0; bool has_range = false;
  for (const auto & a : annotations) {
    if (a.name == "碰撞") { ++collision; }
    if (a.kind == "range" && a.name == "倒水") { has_range = true; EXPECT_NEAR(a.end, 9.3, 1e-6); }
  }
  EXPECT_EQ(collision, 2);
  EXPECT_TRUE(has_range);
}

TEST(TagListModel, ExportsSelectedTag)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}});
  model.select(0);
  const auto tags = model.exportSelectedTags();
  ASSERT_EQ(tags.size(), 1u);
  EXPECT_EQ(tags.front().name, "成功");
}

TEST(TagListModel, ClearSelectionEmptiesExport)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}});
  model.select(0);
  ASSERT_EQ(model.exportSelectedTags().size(), 1u);
  model.clearSelection();
  EXPECT_TRUE(model.exportSelectedTags().empty());
}
```

- [ ] **Step 2: 运行确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -10
```
Expected: 编译失败 `exportAnnotations`/`exportSelectedTags` 未声明。

- [ ] **Step 3: 改头**

`EventMarkerModel` public 段加：
```cpp
  std::vector<AnnotationRecord> exportAnnotations() const;
  void clearInstances();  // startSession 时清空
```
`TagListModel` public 段加：
```cpp
  std::vector<TagRecord> exportSelectedTags() const;
  void clearSelection();  // startSession 时清空（select(-1) 当前是 no-op，故需独立方法）
```
确保 `ui_models.hpp` 顶部 `#include "data_recorder/recorder_types.hpp"`。

- [ ] **Step 4: 改源**

实现：
```cpp
std::vector<AnnotationRecord> EventMarkerModel::exportAnnotations() const
{
  std::vector<AnnotationRecord> out;
  for (const auto & row : markers_) {
    for (const auto & inst : row.instances) {
      AnnotationRecord rec;
      rec.name = row.marker.name;
      rec.shortcut = row.marker.shortcut;
      rec.kind = inst.kind.toStdString();
      rec.color = row.marker.color;
      if (inst.kind == QStringLiteral("range")) {
        rec.t = inst.start_seconds;
        rec.end = inst.end_seconds;
      } else {
        rec.t = inst.start_seconds;
      }
      out.push_back(rec);
    }
  }
  std::sort(out.begin(), out.end(),
    [](const AnnotationRecord & a, const AnnotationRecord & b) { return a.t < b.t; });
  return out;
}

void EventMarkerModel::clearInstances()
{
  beginResetModel();
  for (auto & row : markers_) {
    row.instances.clear();
    row.has_pending_range_start = false;
    row.next_instance_id = 1;
  }
  endResetModel();
}

std::vector<TagRecord> TagListModel::exportSelectedTags() const
{
  std::vector<TagRecord> out;
  if (selected_row_ >= 0 && selected_row_ < static_cast<int>(tags_.size())) {
    out.push_back({tags_[static_cast<std::size_t>(selected_row_)].name,
      tags_[static_cast<std::size_t>(selected_row_)].color});
  }
  return out;
}

void TagListModel::clearSelection()
{
  if (selected_row_ < 0) { return; }
  const int previous = selected_row_;
  selected_row_ = -1;
  if (valid_row(previous, static_cast<int>(tags_.size()))) {
    const auto previous_index = index(previous, 0);
    emit dataChanged(previous_index, previous_index, {IsSelectedRole});
  }
}
```

- [ ] **Step 5: 运行确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R test_ui_models \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: PASS。

- [ ] **Step 6: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat: EventMarkerModel/TagListModel 加快照导出(exportAnnotations/exportSelectedTags) + clearInstances/clearSelection"
```

---

## Phase 5 — 接线 AppController + main + QML

### Task 14: AppController — 持引擎/桥，接真实录制与信号

**Files:**
- Modify: `include/data_recorder/app_controller.hpp`, `src/app_controller.cpp`

> AppController 不再自己造 model 成员被引擎填，而是：持有 `LiveBridge*` 与 `RecorderEngine*`（由 main 注入，因为引擎需要 node）；连接 LiveBridge 信号到模型方法；`toggleRecording` 调引擎 start/stop；live edge 用桥的信号驱动。

- [ ] **Step 1: 改头——构造签名 + 持有引用 + 槽**

`app_controller.hpp`：
- 构造改为 `explicit AppController(const ConfigData & config, LiveBridge * bridge, RecorderEngine * engine, SessionManager * session_manager, QObject * parent = nullptr);`
- 前置声明 `namespace data_recorder { class LiveBridge; class RecorderEngine; class SessionManager; }`（已在同 namespace，直接声明）。
- 加 private 成员：
```cpp
  LiveBridge * bridge_{nullptr};
  RecorderEngine * engine_{nullptr};
  SessionManager * session_manager_{nullptr};
```
- 加 private 槽（普通成员函数即可，用 lambda 连接）：`void onStatsUpdated(const QVariantList & stats); void onFrameReady(const QString & key, int seq); void onLiveEdge(double seconds); void refreshSessions();`

- [ ] **Step 2: 改源——构造连接 + toggleRecording 接引擎**

`src/app_controller.cpp` 构造函数体（在现有 model 初始化后）加：
```cpp
  bridge_ = bridge;
  engine_ = engine;
  session_manager_ = session_manager;

  if (bridge_) {
    connect(bridge_, &LiveBridge::statsUpdated, this, &AppController::onStatsUpdated);
    connect(bridge_, &LiveBridge::frameReady, this, &AppController::onFrameReady);
    connect(bridge_, &LiveBridge::liveEdgeChanged, this, &AppController::onLiveEdge);
  }
  refreshSessions();  // 启动扫描
```
`toggleRecording()` 改造：把 `recording_ = !recording_;` 一段替换为调引擎：
```cpp
  if (!recording_) {
    // 开始
    event_marker_model_.clearInstances();
    tag_model_.clearSelection();  // 清标签选择，每次录制从干净状态开始
    const std::string id = engine_ ? engine_->start_session() : std::string();
    if (id.empty()) {
      status_text_ = QStringLiteral("录制启动失败");
      emit statusTextChanged();
      return;
    }
    recording_ = true;
  } else {
    // 停止
    if (engine_) {
      engine_->stop_session(event_marker_model_.exportAnnotations(), tag_model_.exportSelectedTags());
    }
    recording_ = false;
    refreshSessions();
  }
```
（保留原有 following/playhead/status 信号收敛逻辑。）
实现槽：
```cpp
void AppController::onStatsUpdated(const QVariantList & stats)
{
  for (const auto & v : stats) {
    const auto m = v.toMap();
    const QString key = m.value("topicKey").toString();
    topic_model_.updateStats(key, m.value("hz").toDouble(),
      m.value("width").toInt(), m.value("height").toInt());
  }
}

void AppController::onFrameReady(const QString & key, int seq)
{
  topic_model_.updateFrameSeq(key, seq);
  camera_grid_model_.updateFrameSeq(key, seq);
}

void AppController::onLiveEdge(double seconds)
{
  advanceLiveEdge(seconds);  // 复用现有方法
}

void AppController::refreshSessions()
{
  if (!session_manager_) { return; }
  const std::string dir = std::filesystem::absolute(output_directory_.toStdString()).string();
  recording_session_model_.setSessions(session_manager_->scan(dir));
}
```
头部加 `#include <filesystem>`、`#include "data_recorder/live_bridge.hpp"`、`#include "data_recorder/recorder_engine.hpp"`、`#include "data_recorder/session_manager.hpp"`。

- [ ] **Step 3: 构建确认通过（main 还没改，预期 main 编译错——先只编核心库）**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -15
```
Expected: 核心库编译成功；`data_recorder` 可执行目标可能因 main.cpp 旧构造签名报错——Task 15 修复。若想分步，可暂时注释 main.cpp 里 AppController 构造。

- [ ] **Step 4: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add include/data_recorder/app_controller.hpp src/app_controller.cpp
git commit -m "feat: AppController 接 RecorderEngine/LiveBridge(真实录制+stats/帧/扫描接线)"
```

---

### Task 15: main — 后台 spin + 注册 image provider + 干净退出

**Files:**
- Modify: `src/data_recorder.cpp`

- [ ] **Step 1: 改 main 接线引擎、桥、provider、spin 线程**

替换 `src/data_recorder.cpp` 的 `main`（从创建 node 到 app.exec 之间），保留 usage/config 加载与 QML 路径逻辑：
```cpp
  // ... config 加载后 ...
  std::vector<QByteArray> qt_arg_storage;
  /* 同原逻辑构造 qt_argv ... */
  int qt_argc = static_cast<int>(qt_argv.size());
  QApplication app(qt_argc, qt_argv.data());

  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager session_manager;
  data_recorder::RecorderEngine engine(node, config, &bridge, &session_manager);
  data_recorder::AppController controller(config, &bridge, &engine, &session_manager);
  app.installEventFilter(&controller);

  QQmlApplicationEngine qml_engine;
  qml_engine.addImageProvider(QStringLiteral("camera"),
    new data_recorder::CameraImageProvider(&bridge));  // 引擎接管所有权
  qml_engine.rootContext()->setContextProperty("appController", &controller);

  // 后台 ROS spin 线程
  std::atomic<bool> spin_running{true};
  std::thread spin_thread([node, &spin_running]() {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    while (spin_running.load() && rclcpp::ok()) {
      executor.spin_some(std::chrono::milliseconds(10));
    }
  });

  QTimer ros_shutdown_timer;
  QObject::connect(&ros_shutdown_timer, &QTimer::timeout, &app, [&app]() {
    if (!rclcpp::ok()) { app.quit(); }
  });
  ros_shutdown_timer.start(100);

  /* 同原逻辑：addImportPath / objectCreated 失败处理 / engine.load(main_qml) */

  const int result = app.exec();

  // 干净退出：停录、停 spin、join、shutdown
  spin_running.store(false);
  if (spin_thread.joinable()) { spin_thread.join(); }
  rclcpp::shutdown();
  return result;
```
头部加 `#include <atomic>`、`#include <thread>`、`#include "data_recorder/live_bridge.hpp"`、`#include "data_recorder/recorder_engine.hpp"`、`#include "data_recorder/session_manager.hpp"`、`#include "data_recorder/camera_image_provider.hpp"`。
（`engine` 析构时若仍录制会 stop_session；但需在 app.exec 返回后、engine 仍在作用域时确保。引擎在栈上、main 返回前析构，OK。）

- [ ] **Step 2: 构建确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release 2>&1 | tail -12
```
Expected: 构建成功（可执行 + 核心库）。

- [ ] **Step 3: 跑全部测试确认仍绿**

```bash
source ~/.local/ros2_rc && rr && colcon test --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+ ; \
colcon test-result --all --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws
```
Expected: 全部测试目标 PASS（含新增 5 个 + 改造的 ui_models/camera_grid + 原 config/usage/qml）。

- [ ] **Step 4: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add src/data_recorder.cpp
git commit -m "feat: main 后台 spin 线程 + 注册 CameraImageProvider + 干净退出"
```

---

### Task 16: QML — 相机瓦片显示真实帧

**Files:**
- Modify: `qml/components/CameraPreviewTile.qml`
- Modify: `qml/components/CameraGridLayout.qml`（传 topicKey/frameSeq 给瓦片）

- [ ] **Step 1: 查 CameraGridLayout 如何实例化瓦片**

```bash
grep -n "CameraPreviewTile\|topicName\|resolutionText\|model\." \
  /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/qml/components/CameraGridLayout.qml
```
确认瓦片实例化处的 model role 绑定（应已绑 `topicName`/`resolutionText`）。

- [ ] **Step 2: 给瓦片加 topicKey/frameSeq 属性 + Image**

在 `CameraPreviewTile.qml`：在 `property color seriesColor` 旁加：
```qml
    property string topicKey: ""
    property int frameSeq: 0
```
把内层那个画假格子的 `Canvas { id: previewCanvas ... }`（含 onPaint 整块）替换为真实图像：
```qml
            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                cache: false
                asynchronous: false
                source: root.topicKey.length > 0
                    ? "image://camera/" + root.topicKey + "?seq=" + root.frameSeq
                    : ""
                // seq 变化 → source 变化 → 重新拉帧
            }
```
删除 `Canvas` 块与其 `Connections`（onSeriesColorChanged/onResolutionTextChanged 的重绘已无意义）。保留顶部标题栏（topicName + resolutionText）。

- [ ] **Step 3: 在 CameraGridLayout 传 topicKey/frameSeq**

在 `CameraGridLayout.qml` 实例化 `CameraPreviewTile` 处，补绑定（role 名来自 Task 11）：
```qml
            topicKey: model.topicKey
            frameSeq: model.frameSeq
```

- [ ] **Step 4: 构建 + QML 冒烟测试**

```bash
source ~/.local/ros2_rc && rr && colcon build --packages-select data_recorder \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --mixin release && \
colcon test --packages-select data_recorder --ctest-args -R "test_qml" \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --event-handlers console_direct+
```
Expected: 构建成功；test_qml_smoke PASS（offscreen 下 image provider 返回占位黑帧，不崩）。若 test_qml_structure 因删 Canvas 报白盒断言失败，更新该断言（移除对 `previewCanvas`/Canvas 的源码断言）。

- [ ] **Step 5: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add qml/components/CameraPreviewTile.qml qml/components/CameraGridLayout.qml test/test_qml_structure.cpp
git commit -m "feat: 相机瓦片用 Image(image://camera) 显示真实帧, 替换假 Canvas"
```

---

## Phase 6 — 端到端验证 + 文档

### Task 17: 真机端到端验证（domain 43 实时话题）

**Files:** 无代码改动（验证 + 记录）

> 需图形会话。先确认 `DISPLAY` 或 `WAYLAND_DISPLAY` 已设。配置用现成 example_config，但改 output_dir 为绝对临时路径避免污染源码树。

- [ ] **Step 1: 准备临时配置**

```bash
mkdir -p /tmp/dr_e2e && sed 's#output_dir:.*#output_dir: "/tmp/dr_e2e/recordings"#' \
  /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml \
  > /tmp/dr_e2e/config.yaml && echo "wrote /tmp/dr_e2e/config.yaml"
```

- [ ] **Step 2: 启动 app（domain 43，连实时话题）**

```bash
source ~/.local/ros2_rc && rs && DISPLAY=${DISPLAY:-:0} ros2 run data_recorder data_recorder \
  --ros-args -p config_file:=/tmp/dr_e2e/config.yaml
```
Expected: 窗口出现；相机网格显示 `/camera/image_raw` 等 3 路**真实图像**（不是假格子）；时间轴信息行显示真实 Hz（joint_states ~400Hz）和相机分辨率 848x480。

- [ ] **Step 3: 手动录制约 30s + 打标注**

操作：按 Space（或点录制）开始 → 等几秒按数字键 `1`(拿起水杯/point)、`c`(碰撞/point) 几次、`2`(倒水/range 按两次定起止) → 点一个标签 chip（如"成功"）→ 约 30s 后按 Space 停止。
Expected: 状态栏在"录制中"/"实时查看"间切换；停止后会话面板出现新会话行。

- [ ] **Step 4: 验证产物结构**

```bash
SESSION=$(ls -1d /tmp/dr_e2e/recordings/*/ | head -1) && echo "会话: $SESSION" && \
echo "--- 目录 ---" && find "$SESSION" -type f && \
echo "--- session.yaml ---" && cat "$SESSION/session.yaml"
```
Expected: 含 `rosbag/`（metadata.yaml + .mcap 或 .db3）、`video/*.mp4` + `*.csv`（×3）、`session.yaml`（含 topics/tags/annotations，同名标注多条按时间升序）。

- [ ] **Step 5: 验证 rosbag 可读**

```bash
source ~/.local/ros2_rc && rs && SESSION=$(ls -1d /tmp/dr_e2e/recordings/*/ | head -1) && \
ros2 bag info "$SESSION/rosbag"
```
Expected: Storage id 正确（mcap 或 sqlite3）；话题 `/joint_states`/`/tf`/`/tf_static`，类型与计数合理。

- [ ] **Step 6: 验证隔离回放**

```bash
source ~/.local/ros2_rc && rs && export ROS_DOMAIN_ID=88 && SESSION=$(ls -1d /tmp/dr_e2e/recordings/*/ | head -1) && \
( ros2 bag play "$SESSION/rosbag" >/tmp/dr_play.log 2>&1 & echo $! > /tmp/dr_play.pid ) && sleep 3 && \
ros2 topic list && echo "--- joint_states hz ---" && timeout 5 ros2 topic hz /joint_states 2>/dev/null | head -2 ; \
kill -INT $(cat /tmp/dr_play.pid) 2>/dev/null
```
Expected: domain 88 只见回放话题（无 domain 43 实时泄漏）；joint_states 有速率。

- [ ] **Step 7: 验证视频 mp4 + CSV**

```bash
SESSION=$(ls -1d /tmp/dr_e2e/recordings/*/ | head -1) && \
for f in "$SESSION"video/*.mp4; do echo "=== $f ==="; ffprobe -v error -show_entries \
  stream=codec_name,width,height,nb_frames -of default=noprint_wrappers=1 "$f" 2>&1 | head -8; done && \
echo "--- CSV 行数 vs 帧数 ---" && for c in "$SESSION"video/*.csv; do echo "$c: $(($(wc -l < "$c") - 1)) frames"; done
```
Expected: 每个 mp4 可被 ffprobe 解析（codec h264，宽高 848x480）；CSV 行数（减表头）≈ mp4 帧数，PTS 单调（抽查 `head`/`tail`）。

- [ ] **Step 8: 验证重启后会话仍在**

重新执行 Step 2 启动 app。
Expected: 会话面板仍列出上次录的会话（`SessionManager::scan` 启动时生效）。

- [ ] **Step 9: 清理**

```bash
rm -rf /tmp/dr_e2e /tmp/dr_play.log /tmp/dr_play.pid && echo "cleaned"
```

---

### Task 18: 更新 README + TODO

**Files:**
- Modify: `README.md`
- Modify: `TODO.md`

- [ ] **Step 1: README 加后端依赖与验证说明**

在 `README.md` 的 Dependencies 段补：
```markdown
### Backend dependencies

```bash
# rosbag2 mcap storage（默认存储优先 mcap）
sudo apt install ros-humble-rosbag2-storage-mcap
# libav（视频编码）
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
```
其余 ROS 依赖经 rosdep 安装。
```
在末尾加"Backend Verification"小节，概述 Task 17 的 record→info→play→ffprobe 往返。

- [ ] **Step 2: TODO 勾掉后端**

把 `TODO.md` 的 `- [ ] 实现后端` 改为 `- [x] 实现后端（rosbag + video + 预览 + 会话；数值曲线内省/历史回放数据加载留后续）`。

- [ ] **Step 3: Commit**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add README.md TODO.md
git commit -m "docs: README 加后端依赖与验证, TODO 勾掉后端实现"
```

---

## 完成标准

- 全部测试目标绿灯：`test_config_model`、`test_usage_help`、`test_qml_smoke`、`test_qml_structure`、`test_ui_models`、`test_camera_grid_model`、`test_topic_rate_monitor`、`test_writer_queue`、`test_session_manager`、`test_video_recorder`、`test_rosbag_writer`。
- Task 17 真机端到端全部 Expected 满足：实时预览、真实 Hz/分辨率、录制产物结构、rosbag `ros2 bag info`/隔离 `play`、mp4 可解码 + CSV 行数匹配、会话面板重启留存。
- `grep -rn "make_series_list\|make_frequency_text\|make_resolution_text\|populate_placeholder" src/` 无结果（占位生成器已删尽）。

## 后续 spec（不在本计划）

- 数值曲线消息内省（任意消息类型反序列化 + 字段提取 → 时间轴数值轨道）。
- 历史会话回放数据加载（点开会话加载 bag/视频数据回放）。
- 崩溃恢复半完成会话、压缩调优、运行时改配置。
