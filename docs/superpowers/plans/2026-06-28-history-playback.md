# 历史回放模式 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让"数据"面板点击历史会话时,整个界面切换到历史回放模式——解码录制的相机 mp4 随播放头播放,加载会话话题/标注/标签/时长,提供播放/暂停 + 拖动 + 返回在线。

**Architecture:** 方案 A——`LiveBridge` 成为唯一"当前帧仓库",加 `playback` 门控:置位时丢弃实时帧/统计,只放行新增的 `SessionPlayer` 推送的解码帧。`SessionPlayer`(独立 `QThread`)持有每路相机的 `VideoClipReader`(libav 解码 mp4),按播放头取帧推给 `LiveBridge`,复用既有 `image://camera` 显示链路。`AppController` 负责历史/在线切换、加载会话元数据、播放控制。

**Tech Stack:** C++17 / Qt6 (Core/Gui/Quick) / libav (libavformat/libavcodec/libswscale,经 `PkgConfig::LIBAV`) / ROS2 ament_cmake / gtest。

---

## 关键事实(实现前必读)

- **topic_key 约定**:实时链路里 `RecorderEngine` 调 `bridge_->push_frame(QString::fromStdString(topic), ...)`,`topic` 是**带斜杠的 topic_name**(如 `/camera/image_raw`)。`CameraGridModel` 的 `TopicKeyRole` 返回 `topic_name`,`CameraImageProvider` 用 `image://camera/<topic_name>?seq=N`。**`SessionPlayer` 推帧的 key 必须是 topic_name(带斜杠)**,否则帧不上屏。
- **视频文件名**:`RecorderEngine::file_name_for_topic` 把 topic_name 去掉前导 `/`、其余 `/`→`_`(如 `/camera/image_raw` → `camera_image_raw`)。文件为 `<session>/video/<base>.mp4` 和 `.csv`。
- **CSV 列**:`frame_index,ros_stamp_ns,pts_ns`。`pts_ns` 实为 90000-tick 流时间基下的 pts(`VideoRecorder` 用 `time_base{1,90000}`、`pts=round(rel_seconds*90000)`),与 ROS 相对时间同源。本计划解码时以**解码帧自身 PTS 换算的相对秒**做时间对齐(与 CSV 的 `ros_stamp` 相对秒数值等价,因同源),CSV 用于建立帧索引(帧数、各帧相对秒、二分目标)。
- **`ament` 风格**:本包 C++ 是 ament 风格,**不要**用 LLVM `.clang-format` 格式化(项目记忆 `dont-run-clang-format-ament-style`)。手动匹配周边风格:2 空格缩进、`{` 不换行跟随、指针 `Type * name`。
- **构建/测试命令**(每个任务的测试步骤都用它;首次构建较慢,之后 ccache 加速):

```bash
# 构建
source ~/.local/ros2_rc && rr && colcon build --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --mixin release compile-commands ccache
# 跑单个测试（替换 <TestTarget>）
source ~/.local/ros2_rc && rr && colcon test --packages-select data_recorder \
  --ctest-args -R <TestTarget> --event-handlers console_direct+
# 查看结果
colcon test-result --all --verbose
```

> 提示:每个任务"运行测试"步骤里,先 build 再 colcon test。若只改一个测试可在 `build/data_recorder` 下直接 `ctest -R <name> -V` 加速。

---

## 文件结构

**新建:**
- `include/data_recorder/video_clip_reader.hpp` / `src/video_clip_reader.cpp` —— 单路 mp4+csv libav 解码器。
- `include/data_recorder/session_player.hpp` / `src/session_player.cpp` —— 播放时钟 + 多路解码器,推帧给 LiveBridge。
- `test/test_video_clip_reader.cpp` —— VideoRecorder→VideoClipReader round-trip 解码测试。
- `test/test_session_player.cpp` —— SessionPlayer 同步加载/seek/推帧冒烟测试。

**修改:**
- `include/data_recorder/live_bridge.hpp` / `src/live_bridge.cpp` —— playback 门控 + `push_playback_frame`。
- `include/data_recorder/ui_models.hpp` / `src/ui_models.cpp` —— `EventMarkerModel::setInstances`、`TagListModel::setSelectedTags`。
- `include/data_recorder/app_controller.hpp` / `src/app_controller.cpp` —— 历史/在线切换接线、播放控制、`playing` 属性、会话缓存。
- `qml/components/TrackInfoColumn.qml` —— 红框处上下文主操作按钮(录制/停止/播放/暂停)。
- `qml/components/StatusBar.qml` —— 移除录制按钮。
- `qml/components/RecordingSessionsPanel.qml` —— 录制中禁点历史行。
- `test/test_ui_models.cpp` —— 新增方法的单测。
- `CMakeLists.txt` —— 登记新源文件与测试目标。

---

## Task 1: LiveBridge playback 门控 + push_playback_frame

**Files:**
- Modify: `include/data_recorder/live_bridge.hpp`
- Modify: `src/live_bridge.cpp`
- Test: `test/test_ui_models.cpp`(在文件末尾追加 LiveBridge 测试;它已链接 `data_recorder_core`)

- [ ] **Step 1: 写失败测试**

在 `test/test_ui_models.cpp` 顶部确保有 `#include "data_recorder/live_bridge.hpp"` 和 `#include <QImage>`(若无则加),并在文件末尾 `}` 之前追加:

```cpp
TEST(LiveBridgeTest, PlaybackModeGatesLiveButAllowsPlayback)
{
  data_recorder::LiveBridge bridge;
  const QString key = "/camera/image_raw";
  auto img = std::make_shared<QImage>(4, 4, QImage::Format_RGB888);

  // 默认（非 playback）：实时 push 生效
  bridge.push_frame(key, img);
  ASSERT_NE(bridge.latest_frame(key), nullptr);

  // 进入 playback：实时 push 被丢弃（仓库不被覆盖为新对象）
  bridge.set_playback_mode(true);
  auto live2 = std::make_shared<QImage>(8, 8, QImage::Format_RGB888);
  bridge.push_frame(key, live2);
  EXPECT_EQ(bridge.latest_frame(key)->width(), 4);  // 仍是旧帧

  // playback push 生效
  auto play = std::make_shared<QImage>(16, 16, QImage::Format_RGB888);
  bridge.push_playback_frame(key, play);
  EXPECT_EQ(bridge.latest_frame(key)->width(), 16);

  // 退出 playback：实时 push 恢复
  bridge.set_playback_mode(false);
  bridge.push_frame(key, live2);
  EXPECT_EQ(bridge.latest_frame(key)->width(), 8);
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `colcon build --packages-select data_recorder ...`(见顶部命令)
Expected: 编译失败 —— `set_playback_mode`/`push_playback_frame` 未声明。

- [ ] **Step 3: 改头文件**

`include/data_recorder/live_bridge.hpp`:加 `#include <atomic>`;在 `public:` 的 ROS 线程区追加声明:

```cpp
  void push_playback_frame(const QString & topic_key, std::shared_ptr<const QImage> image);
  void set_playback_mode(bool on);
```

在 `private:` 区追加成员:

```cpp
  std::atomic<bool> playback_mode_{false};
```

- [ ] **Step 4: 改实现**

`src/live_bridge.cpp`:在 `push_frame` 开头加门控;在 `push_stats` 开头加门控;新增 `push_playback_frame` 与 `set_playback_mode`。具体:

`push_frame` 函数体第一行(`int seq = 0;` 之前)插入:

```cpp
  if (playback_mode_.load()) { return; }  // 回放模式丢弃实时帧
```

`push_stats` 函数体第一行插入:

```cpp
  if (playback_mode_.load()) { return; }  // 回放模式丢弃实时统计
```

在文件末尾 `}  // namespace` 之前追加:

```cpp
void LiveBridge::push_playback_frame(const QString & topic_key, std::shared_ptr<const QImage> image)
{
  int seq = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_[topic_key] = std::move(image);
    seq = ++seqs_[topic_key];
  }
  QMetaObject::invokeMethod(this, "frameReady", Qt::QueuedConnection,
    Q_ARG(QString, topic_key), Q_ARG(int, seq));
}

void LiveBridge::set_playback_mode(bool on)
{
  playback_mode_.store(on);
}
```

- [ ] **Step 5: 运行测试，确认通过**

Run: build + `colcon test --packages-select data_recorder --ctest-args -R test_ui_models`
Expected: PASS(含新 `LiveBridgeTest`)。

- [ ] **Step 6: 提交**

```bash
git add include/data_recorder/live_bridge.hpp src/live_bridge.cpp test/test_ui_models.cpp
git commit -m "feat(live_bridge): playback 门控 + push_playback_frame"
```

---

## Task 2: EventMarkerModel::setInstances

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Test: `test/test_ui_models.cpp`

- [ ] **Step 1: 写失败测试**

在 `test/test_ui_models.cpp` 末尾追加:

```cpp
TEST(EventMarkerModelTest, SetInstancesLoadsHistoryAnnotations)
{
  data_recorder::EventMarkerModel model;
  std::vector<data_recorder::EventMarkerEntry> markers = {
    {"1", "拿起水杯", "point", "#1763c9"},
    {"2", "倒水", "range", "#2f9e44"},
  };
  model.set_markers(markers);

  std::vector<data_recorder::AnnotationRecord> anns;
  data_recorder::AnnotationRecord a1;
  a1.shortcut = "1"; a1.kind = "point"; a1.t = 23.1;
  data_recorder::AnnotationRecord a2;
  a2.shortcut = "2"; a2.kind = "range"; a2.t = 25.0; a2.end = 29.0;
  anns.push_back(a1);
  anns.push_back(a2);

  model.setInstances(anns);

  // row0 (point) count==1, row1 (range) count==1
  EXPECT_EQ(model.data(model.index(0, 0),
    data_recorder::EventMarkerModel::CountRole).toInt(), 1);
  EXPECT_EQ(model.data(model.index(1, 0),
    data_recorder::EventMarkerModel::CountRole).toInt(), 1);

  // 再次调用应先清空（不累加）
  model.setInstances(anns);
  EXPECT_EQ(model.data(model.index(0, 0),
    data_recorder::EventMarkerModel::CountRole).toInt(), 1);
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: build
Expected: 编译失败 —— `setInstances` 未声明。

- [ ] **Step 3: 改头文件**

`include/data_recorder/ui_models.hpp`:在 `EventMarkerModel` 的 `void clearInstances();` 附近追加声明:

```cpp
  void setInstances(const std::vector<AnnotationRecord> & annotations);
```

- [ ] **Step 4: 改实现**

`src/ui_models.cpp`:在 `EventMarkerModel::clearInstances()` 之后追加。按 `shortcut` 匹配 marker 行,`point` 建 `start==end==t`,`range` 建 `start=t,end=end`;无匹配行的标注忽略。用 `beginResetModel/endResetModel` 一次性刷新:

```cpp
void EventMarkerModel::setInstances(const std::vector<AnnotationRecord> & annotations)
{
  beginResetModel();
  for (auto & row : markers_) {
    row.instances.clear();
    row.has_pending_range_start = false;
    row.next_instance_id = 1;
  }
  for (const auto & ann : annotations) {
    const QString shortcut = QString::fromStdString(ann.shortcut);
    auto it = std::find_if(markers_.begin(), markers_.end(),
      [&shortcut](const EventMarkerRow & r) {
        return r.marker.shortcut == shortcut;
      });
    if (it == markers_.end()) { continue; }  // 无匹配行：忽略
    EventInstance instance;
    instance.id = it->next_instance_id++;
    if (ann.kind == "range") {
      instance.kind = QStringLiteral("range");
      instance.start_seconds = ann.t;
      instance.end_seconds = ann.end;
    } else {
      instance.kind = QStringLiteral("point");
      instance.start_seconds = ann.t;
      instance.end_seconds = ann.t;
    }
    it->instances.push_back(instance);
  }
  endResetModel();
}
```

> 注:确认 `src/ui_models.cpp` 顶部已 `#include <algorithm>`(若无则加)。`EventMarkerRow`/`EventInstance` 字段见 `ui_models.hpp`(`marker`、`instances`、`has_pending_range_start`、`next_instance_id`)。

- [ ] **Step 5: 运行测试，确认通过**

Run: build + `colcon test ... -R test_ui_models`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat(event_marker_model): setInstances 加载历史标注"
```

---

## Task 3: TagListModel::setSelectedTags

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Test: `test/test_ui_models.cpp`

- [ ] **Step 1: 写失败测试**

在 `test/test_ui_models.cpp` 末尾追加:

```cpp
TEST(TagListModelTest, SetSelectedTagsMarksMultipleSelected)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}, {"碰撞", "#e8a915"}});

  model.setSelectedTags({{"成功", "#2f9e44"}, {"碰撞", "#e8a915"}});

  EXPECT_TRUE(model.data(model.index(0, 0),
    data_recorder::TagListModel::IsSelectedRole).toBool());   // 成功
  EXPECT_FALSE(model.data(model.index(1, 0),
    data_recorder::TagListModel::IsSelectedRole).toBool());   // 失败
  EXPECT_TRUE(model.data(model.index(2, 0),
    data_recorder::TagListModel::IsSelectedRole).toBool());   // 碰撞

  // clearSelection 清掉会话选中
  model.clearSelection();
  EXPECT_FALSE(model.data(model.index(0, 0),
    data_recorder::TagListModel::IsSelectedRole).toBool());
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: build
Expected: 编译失败 —— `setSelectedTags` 未声明。

- [ ] **Step 3: 改头文件**

`include/data_recorder/ui_models.hpp`:`TagListModel` 加 `#include <string>`(若 ui_models.hpp 未含则加)。在 `void clearSelection();` 附近加声明,并在 `private:` 加成员:

```cpp
  void setSelectedTags(const std::vector<TagRecord> & tags);   // public 区
```

```cpp
  std::vector<std::string> session_selected_names_;            // private 区
```

- [ ] **Step 4: 改实现**

`src/ui_models.cpp`:
1. `TagListModel::data` 的 `case IsSelectedRole:` 改为同时考虑会话选中:

```cpp
    case IsSelectedRole:
      return index.row() == selected_row_ ||
        std::find(session_selected_names_.begin(), session_selected_names_.end(),
          tag.name) != session_selected_names_.end();
```

2. `TagListModel::clearSelection()` 改为同时清会话选中并整体刷新(保留原单选清除信号,追加会话清除):

在 `clearSelection()` 函数体最前面加:

```cpp
  if (!session_selected_names_.empty()) {
    session_selected_names_.clear();
    if (!tags_.empty()) {
      emit dataChanged(index(0, 0), index(static_cast<int>(tags_.size()) - 1, 0),
        {IsSelectedRole});
    }
  }
```

3. 在 `clearSelection()` 之后追加新方法:

```cpp
void TagListModel::setSelectedTags(const std::vector<TagRecord> & tags)
{
  session_selected_names_.clear();
  for (const auto & t : tags) {
    session_selected_names_.push_back(t.name);
  }
  selected_row_ = -1;
  if (!tags_.empty()) {
    emit dataChanged(index(0, 0), index(static_cast<int>(tags_.size()) - 1, 0),
      {IsSelectedRole});
  }
}
```

4. `TagListModel::set_tags` 内 `selected_row_ = -1;` 之后加 `session_selected_names_.clear();`(切换话题/标签时一并清)。

> 注:确认 `src/ui_models.cpp` 顶部含 `#include <algorithm>`(Task 2 已确保)。

- [ ] **Step 5: 运行测试，确认通过**

Run: build + `colcon test ... -R test_ui_models`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "feat(tag_model): setSelectedTags 多选展示会话标签"
```

---

## Task 4: VideoClipReader（libav 解码单路 mp4+csv）

**Files:**
- Create: `include/data_recorder/video_clip_reader.hpp`
- Create: `src/video_clip_reader.cpp`
- Create: `test/test_video_clip_reader.cpp`
- Modify: `CMakeLists.txt`(加源文件到 core + 加测试目标)

- [ ] **Step 1: 写头文件**

`include/data_recorder/video_clip_reader.hpp`:

```cpp
#pragma once

#include <QImage>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace data_recorder
{

// 单路相机录制（mp4 + sidecar csv）的解码器：按相对秒返回最接近的已解码帧。
// 非线程安全——由单个线程（SessionPlayer 的播放线程）使用。
class VideoClipReader
{
public:
  VideoClipReader();
  ~VideoClipReader();

  VideoClipReader(const VideoClipReader &) = delete;
  VideoClipReader & operator=(const VideoClipReader &) = delete;

  // 打开 mp4 + 解析 csv。失败返回 false（is_valid() 亦为 false）。
  bool open(const std::string & mp4_path, const std::string & csv_path);
  bool is_valid() const { return valid_; }

  double duration_seconds() const;  // 末帧相对秒（无帧返回 0）

  // 返回相对秒 t 处应显示的帧（rel_seconds ≤ t 的最大帧；t<首帧取首帧）。
  // 无效或解码失败返回空 QImage。
  QImage frameAtSeconds(double t);

private:
  struct FrameIndexEntry
  {
    double rel_seconds{0.0};
  };

  void close();
  int indexForSeconds(double t) const;          // 二分：rel ≤ t 的最大 index
  int indexNearestPts(double rel_seconds) const; // 二分：最接近 rel 的 index
  QImage decodeForwardTo(int target_index);      // 顺序解码直到命中 target_index

  bool valid_{false};
  std::vector<FrameIndexEntry> entries_;

  AVFormatContext * fmt_{nullptr};
  AVCodecContext * dec_{nullptr};
  AVFrame * frame_{nullptr};
  AVPacket * pkt_{nullptr};
  SwsContext * sws_{nullptr};
  int video_stream_{-1};
  double time_base_{0.0};   // av_q2d(stream->time_base)

  int cur_index_{-1};       // 最近一次成功解码帧对应的 entries_ 下标
  QImage cached_;           // 与 cur_index_ 对应的 RGB888 图
};

}  // namespace data_recorder
```

- [ ] **Step 2: 写实现**

`src/video_clip_reader.cpp`:

```cpp
#include "data_recorder/video_clip_reader.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace data_recorder
{

VideoClipReader::VideoClipReader() = default;

VideoClipReader::~VideoClipReader()
{
  close();
}

void VideoClipReader::close()
{
  if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
  if (frame_) { av_frame_free(&frame_); }
  if (pkt_) { av_packet_free(&pkt_); }
  if (dec_) { avcodec_free_context(&dec_); }
  if (fmt_) { avformat_close_input(&fmt_); }
  video_stream_ = -1;
  cur_index_ = -1;
  cached_ = QImage();
}

bool VideoClipReader::open(const std::string & mp4_path, const std::string & csv_path)
{
  close();
  valid_ = false;
  entries_.clear();

  // ---- 解析 CSV: frame_index,ros_stamp_ns,pts_ns ----
  std::ifstream csv(csv_path);
  if (!csv.is_open()) { return false; }
  std::string line;
  std::getline(csv, line);  // 表头
  int64_t first_stamp = 0;
  bool first = true;
  while (std::getline(csv, line)) {
    if (line.empty()) { continue; }
    std::stringstream ss(line);
    std::string col;
    std::getline(ss, col, ',');                       // frame_index
    std::string stamp_str;
    std::getline(ss, stamp_str, ',');                 // ros_stamp_ns
    if (stamp_str.empty()) { continue; }
    int64_t stamp = 0;
    try { stamp = std::stoll(stamp_str); } catch (...) { continue; }
    if (first) { first_stamp = stamp; first = false; }
    FrameIndexEntry e;
    e.rel_seconds = static_cast<double>(stamp - first_stamp) / 1e9;
    entries_.push_back(e);
  }
  if (entries_.empty()) { return false; }

  // ---- 打开 mp4 解码器 ----
  if (avformat_open_input(&fmt_, mp4_path.c_str(), nullptr, nullptr) < 0) { close(); return false; }
  if (avformat_find_stream_info(fmt_, nullptr) < 0) { close(); return false; }
  video_stream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video_stream_ < 0) { close(); return false; }

  AVStream * st = fmt_->streams[video_stream_];
  time_base_ = av_q2d(st->time_base);
  const AVCodec * codec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!codec) { close(); return false; }
  dec_ = avcodec_alloc_context3(codec);
  if (!dec_ || avcodec_parameters_to_context(dec_, st->codecpar) < 0) { close(); return false; }
  if (avcodec_open2(dec_, codec, nullptr) < 0) { close(); return false; }

  frame_ = av_frame_alloc();
  pkt_ = av_packet_alloc();
  if (!frame_ || !pkt_) { close(); return false; }

  valid_ = true;
  return true;
}

double VideoClipReader::duration_seconds() const
{
  return entries_.empty() ? 0.0 : entries_.back().rel_seconds;
}

int VideoClipReader::indexForSeconds(double t) const
{
  if (entries_.empty()) { return -1; }
  if (t <= entries_.front().rel_seconds) { return 0; }
  // 最大的 i 使 entries_[i].rel_seconds <= t
  int lo = 0, hi = static_cast<int>(entries_.size()) - 1, ans = 0;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (entries_[mid].rel_seconds <= t) { ans = mid; lo = mid + 1; }
    else { hi = mid - 1; }
  }
  return ans;
}

int VideoClipReader::indexNearestPts(double rel_seconds) const
{
  if (entries_.empty()) { return -1; }
  int best = 0;
  double best_d = std::abs(entries_[0].rel_seconds - rel_seconds);
  for (int i = 1; i < static_cast<int>(entries_.size()); ++i) {
    double d = std::abs(entries_[i].rel_seconds - rel_seconds);
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}

// 顺序解码下一帧，把 AVFrame 的 pts 映射回 entries_ 下标；到 target_index 即转 QImage。
QImage VideoClipReader::decodeForwardTo(int target_index)
{
  while (true) {
    int ret = av_read_frame(fmt_, pkt_);
    if (ret < 0) {
      // 文件读完：把解码器里残留帧排空
      avcodec_send_packet(dec_, nullptr);
    } else if (pkt_->stream_index != video_stream_) {
      av_packet_unref(pkt_);
      continue;
    } else {
      if (avcodec_send_packet(dec_, pkt_) < 0) { av_packet_unref(pkt_); continue; }
      av_packet_unref(pkt_);
    }

    while (true) {
      int r = avcodec_receive_frame(dec_, frame_);
      if (r == AVERROR(EAGAIN)) { break; }      // 需更多包
      if (r == AVERROR_EOF || r < 0) { return cached_; }  // 结束/错误：返回当前缓存

      int64_t pts = frame_->best_effort_timestamp;
      if (pts == AV_NOPTS_VALUE) { pts = frame_->pts; }
      double rel = (pts == AV_NOPTS_VALUE) ? 0.0 : static_cast<double>(pts) * time_base_;
      cur_index_ = indexNearestPts(rel);

      if (cur_index_ >= target_index) {
        // 命中（或越过）目标：转 RGB888
        sws_ = sws_getCachedContext(sws_, dec_->width, dec_->height,
          static_cast<AVPixelFormat>(frame_->format), dec_->width, dec_->height,
          AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) { return cached_; }
        QImage img(dec_->width, dec_->height, QImage::Format_RGB888);
        uint8_t * dst[1] = { img.bits() };
        int dst_stride[1] = { static_cast<int>(img.bytesPerLine()) };
        sws_scale(sws_, frame_->data, frame_->linesize, 0, dec_->height, dst, dst_stride);
        cached_ = img;
        return cached_;
      }
    }
    if (ret < 0) { return cached_; }  // EOF 且未命中：返回最后一帧
  }
}

QImage VideoClipReader::frameAtSeconds(double t)
{
  if (!valid_) { return QImage(); }
  int target = indexForSeconds(t);
  if (target < 0) { return QImage(); }
  if (target == cur_index_ && !cached_.isNull()) { return cached_; }

  constexpr int kSeqWindow = 30;  // 小幅前进顺序解码，否则 seek
  const bool need_seek = (cur_index_ < 0) || (target < cur_index_) ||
    (target > cur_index_ + kSeqWindow);
  if (need_seek) {
    AVStream * st = fmt_->streams[video_stream_];
    int64_t ts = static_cast<int64_t>(
      entries_[static_cast<std::size_t>(target)].rel_seconds / av_q2d(st->time_base));
    av_seek_frame(fmt_, video_stream_, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dec_);
    cur_index_ = -1;
  }
  return decodeForwardTo(target);
}

}  // namespace data_recorder
```

- [ ] **Step 3: 加 CMake（源文件 + 测试目标）**

`CMakeLists.txt`:在 core 源列表(`src/video_recorder.cpp` 一行附近)加 `src/video_clip_reader.cpp`;在 `if(BUILD_TESTING)` 块内加:

```cmake
  ament_add_gtest(test_video_clip_reader test/test_video_clip_reader.cpp)
  target_link_libraries(test_video_clip_reader data_recorder_core PkgConfig::LIBAV)
```

- [ ] **Step 4: 写 round-trip 测试**

`test/test_video_clip_reader.cpp`(用 `VideoRecorder` 写已知尺寸的几十帧到临时目录,再用 `VideoClipReader` 读回):

```cpp
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

#include "data_recorder/video_clip_reader.hpp"
#include "data_recorder/video_recorder.hpp"
#include "data_recorder/recorder_types.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::ImageFrame makeFrame(int w, int h, int64_t stamp_ns, uint8_t v)
{
  data_recorder::ImageFrame f;
  f.width = w;
  f.height = h;
  f.step = w * 3;
  f.encoding = "bgr8";
  f.ros_stamp_ns = stamp_ns;
  f.data.assign(static_cast<std::size_t>(w) * h * 3, v);
  return f;
}
}  // namespace

TEST(VideoClipReaderTest, RoundTripDecodesFrames)
{
  fs::path dir = fs::temp_directory_path() / "drc_clip_test";
  fs::create_directories(dir);
  const std::string mp4 = (dir / "clip.mp4").string();
  const std::string csv = (dir / "clip.csv").string();

  const int W = 64, H = 48;
  const int N = 40;
  const int64_t base = 1000000000LL;
  {
    data_recorder::VideoParams params;  // 默认 libx264/mp4
    data_recorder::VideoRecorder rec(mp4, csv, W, H, params);
    for (int i = 0; i < N; ++i) {
      // 每帧间隔 40ms；像素值随帧变化
      rec.encode(makeFrame(W, H, base + static_cast<int64_t>(i) * 40000000LL,
        static_cast<uint8_t>(i * 4)));
    }
    rec.close();
  }

  ASSERT_TRUE(fs::exists(mp4));
  ASSERT_TRUE(fs::exists(csv));

  data_recorder::VideoClipReader reader;
  ASSERT_TRUE(reader.open(mp4, csv));
  EXPECT_GT(reader.duration_seconds(), 1.0);  // ~1.56s

  // t=0 首帧
  QImage f0 = reader.frameAtSeconds(0.0);
  ASSERT_FALSE(f0.isNull());
  EXPECT_EQ(f0.width(), W);
  EXPECT_EQ(f0.height(), H);

  // 前进
  QImage fmid = reader.frameAtSeconds(0.8);
  ASSERT_FALSE(fmid.isNull());
  EXPECT_EQ(fmid.width(), W);

  // 后退（触发 seek 路径）
  QImage fback = reader.frameAtSeconds(0.1);
  ASSERT_FALSE(fback.isNull());
  EXPECT_EQ(fback.width(), W);

  // 超过末尾被钳到末帧
  QImage fend = reader.frameAtSeconds(999.0);
  ASSERT_FALSE(fend.isNull());

  fs::remove_all(dir);
}

TEST(VideoClipReaderTest, OpenMissingReturnsFalse)
{
  data_recorder::VideoClipReader reader;
  EXPECT_FALSE(reader.open("/nonexistent/x.mp4", "/nonexistent/x.csv"));
  EXPECT_FALSE(reader.is_valid());
  EXPECT_TRUE(reader.frameAtSeconds(0.0).isNull());
}
```

- [ ] **Step 5: 运行测试，确认通过**

Run: build + `colcon test ... -R test_video_clip_reader`
Expected: PASS（两个用例）。若 round-trip 因编码器关键帧间隔导致中段帧解码偏移,断言只校验尺寸/非空,不校验像素值,故应稳定通过。

- [ ] **Step 6: 提交**

```bash
git add include/data_recorder/video_clip_reader.hpp src/video_clip_reader.cpp \
  test/test_video_clip_reader.cpp CMakeLists.txt
git commit -m "feat(video_clip_reader): libav 解码单路 mp4+csv（round-trip 测试）"
```

---

## Task 5: SessionPlayer（播放时钟 + 多路解码器 → LiveBridge）

**Files:**
- Create: `include/data_recorder/session_player.hpp`
- Create: `src/session_player.cpp`
- Create: `test/test_session_player.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写头文件**

`include/data_recorder/session_player.hpp`:

```cpp
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <string>
#include <vector>

#include "data_recorder/recorder_types.hpp"

namespace data_recorder
{

class LiveBridge;
class VideoClipReader;

// 历史会话回放器：每路相机一个 VideoClipReader，按播放头取帧推给 LiveBridge。
// 设计为可同步直接调用（测试）或 moveToThread 后经队列调用（AppController）。
class SessionPlayer : public QObject
{
  Q_OBJECT

public:
  explicit SessionPlayer(LiveBridge * bridge, QObject * parent = nullptr);
  ~SessionPlayer() override;

  double playheadSeconds() const { return playhead_; }
  double durationSeconds() const { return duration_; }
  bool playing() const { return playing_; }

public slots:
  void load(const data_recorder::SessionRecord & session);  // 建 reader、推首帧、暂停态
  void play();
  void pause();
  void togglePlay();
  void seek(double seconds);
  void stop();   // 暂停 + 清空 reader

signals:
  void playheadAdvanced(double seconds);
  void playingChanged(bool playing);

private slots:
  void onTick();

private:
  void pushFramesAt(double t);   // 各路 reader 取帧推 bridge
  std::string fileBaseForTopic(const std::string & topic) const;

  LiveBridge * bridge_{nullptr};  // 非拥有
  std::unique_ptr<QTimer> timer_;

  struct Clip
  {
    QString topic_key;   // = topic_name（带斜杠），推帧/查帧的 key
    std::unique_ptr<VideoClipReader> reader;
  };
  std::vector<Clip> clips_;

  double playhead_{0.0};
  double duration_{0.0};
  bool playing_{false};
};

}  // namespace data_recorder
```

- [ ] **Step 2: 写实现**

`src/session_player.cpp`:

```cpp
#include "data_recorder/session_player.hpp"

#include <algorithm>
#include <filesystem>

#include "data_recorder/live_bridge.hpp"
#include "data_recorder/video_clip_reader.hpp"

namespace fs = std::filesystem;

namespace data_recorder
{

namespace
{
constexpr int kTickMs = 33;  // ~30fps
}

SessionPlayer::SessionPlayer(LiveBridge * bridge, QObject * parent)
: QObject(parent), bridge_(bridge)
{
  timer_ = std::make_unique<QTimer>(this);
  timer_->setInterval(kTickMs);
  connect(timer_.get(), &QTimer::timeout, this, &SessionPlayer::onTick);
}

SessionPlayer::~SessionPlayer() = default;

// 与 RecorderEngine::file_name_for_topic 保持一致：去前导 '/'，其余 '/'→'_'
std::string SessionPlayer::fileBaseForTopic(const std::string & topic) const
{
  std::string s = topic;
  if (!s.empty() && s.front() == '/') { s.erase(s.begin()); }
  for (auto & c : s) { if (c == '/') { c = '_'; } }
  return s;
}

void SessionPlayer::load(const SessionRecord & session)
{
  timer_->stop();
  playing_ = false;
  clips_.clear();
  playhead_ = 0.0;
  duration_ = session.duration_seconds;

  const fs::path video_dir = fs::path(session.directory) / "video";
  for (const auto & topic : session.topics) {
    if (topic.backend != "video") { continue; }
    const std::string base = fileBaseForTopic(topic.name);
    const std::string mp4 = (video_dir / (base + ".mp4")).string();
    const std::string csv = (video_dir / (base + ".csv")).string();
    auto reader = std::make_unique<VideoClipReader>();
    if (!reader->open(mp4, csv)) { continue; }  // 该路无效则跳过（显示占位）
    duration_ = std::max(duration_, reader->duration_seconds());
    Clip clip;
    clip.topic_key = QString::fromStdString(topic.name);  // 带斜杠
    clip.reader = std::move(reader);
    clips_.push_back(std::move(clip));
  }

  pushFramesAt(0.0);
  emit playheadAdvanced(playhead_);
  emit playingChanged(playing_);
}

void SessionPlayer::pushFramesAt(double t)
{
  if (!bridge_) { return; }
  for (auto & clip : clips_) {
    if (!clip.reader) { continue; }
    QImage img = clip.reader->frameAtSeconds(t);
    if (img.isNull()) { continue; }
    bridge_->push_playback_frame(clip.topic_key, std::make_shared<QImage>(std::move(img)));
  }
}

void SessionPlayer::play()
{
  if (playing_ || clips_.empty()) { return; }
  if (playhead_ >= duration_) { playhead_ = 0.0; }  // 到末尾再播则从头
  playing_ = true;
  timer_->start();
  emit playingChanged(playing_);
}

void SessionPlayer::pause()
{
  if (!playing_) { return; }
  playing_ = false;
  timer_->stop();
  emit playingChanged(playing_);
}

void SessionPlayer::togglePlay()
{
  if (playing_) { pause(); } else { play(); }
}

void SessionPlayer::seek(double seconds)
{
  playhead_ = std::clamp(seconds, 0.0, duration_ > 0.0 ? duration_ : 0.0);
  pushFramesAt(playhead_);
  emit playheadAdvanced(playhead_);
}

void SessionPlayer::stop()
{
  timer_->stop();
  playing_ = false;
  clips_.clear();
  playhead_ = 0.0;
  emit playingChanged(playing_);
}

void SessionPlayer::onTick()
{
  playhead_ += static_cast<double>(kTickMs) / 1000.0;
  if (playhead_ >= duration_) {
    playhead_ = duration_;
    pushFramesAt(playhead_);
    emit playheadAdvanced(playhead_);
    pause();  // 到末尾自动暂停
    return;
  }
  pushFramesAt(playhead_);
  emit playheadAdvanced(playhead_);
}

}  // namespace data_recorder
```

- [ ] **Step 3: 加 CMake（源文件 + 测试目标）**

`CMakeLists.txt`:core 源列表加 `src/session_player.cpp`;`if(BUILD_TESTING)` 内加:

```cmake
  ament_add_gtest(test_session_player test/test_session_player.cpp
    ENV "QT_QPA_PLATFORM=offscreen"
  )
  target_link_libraries(test_session_player data_recorder_core PkgConfig::LIBAV Qt6::Test)
```

- [ ] **Step 4: 写同步冒烟测试**

`test/test_session_player.cpp`(造临时会话目录:`video/cam.mp4`+`.csv`,topic_name 取 `/cam`;同步调用 load/seek,验证 LiveBridge 拿到帧;验证 play/pause 状态):

```cpp
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

#include <QCoreApplication>

#include "data_recorder/live_bridge.hpp"
#include "data_recorder/recorder_types.hpp"
#include "data_recorder/session_player.hpp"
#include "data_recorder/video_recorder.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::ImageFrame makeFrame(int w, int h, int64_t stamp_ns, uint8_t v)
{
  data_recorder::ImageFrame f;
  f.width = w; f.height = h; f.step = w * 3;
  f.encoding = "bgr8"; f.ros_stamp_ns = stamp_ns;
  f.data.assign(static_cast<std::size_t>(w) * h * 3, v);
  return f;
}

data_recorder::SessionRecord makeSession(const fs::path & dir)
{
  const int W = 48, H = 32, N = 30;
  const int64_t base = 1000000000LL;
  fs::create_directories(dir / "video");
  data_recorder::VideoParams params;
  data_recorder::VideoRecorder rec(
    (dir / "video" / "cam.mp4").string(),
    (dir / "video" / "cam.csv").string(), W, H, params);
  for (int i = 0; i < N; ++i) {
    rec.encode(makeFrame(W, H, base + static_cast<int64_t>(i) * 40000000LL,
      static_cast<uint8_t>(i * 5)));
  }
  rec.close();

  data_recorder::SessionRecord s;
  s.session_id = "tmp";
  s.directory = dir.string();
  s.duration_seconds = 1.2;
  s.topics.push_back({"/cam", "video"});
  return s;
}
}  // namespace

TEST(SessionPlayerTest, LoadAndSeekPushFrames)
{
  int argc = 0;
  char ** argv = nullptr;
  QCoreApplication app(argc, argv);

  fs::path dir = fs::temp_directory_path() / "drc_player_test";
  fs::remove_all(dir);
  auto session = makeSession(dir);

  data_recorder::LiveBridge bridge;
  bridge.set_playback_mode(true);
  data_recorder::SessionPlayer player(&bridge);

  player.load(session);
  EXPECT_GT(player.durationSeconds(), 1.0);
  EXPECT_FALSE(player.playing());
  // load 推了首帧
  ASSERT_NE(bridge.latest_frame("/cam"), nullptr);
  EXPECT_EQ(bridge.latest_frame("/cam")->width(), 48);

  // seek 推该处帧
  player.seek(0.8);
  EXPECT_NEAR(player.playheadSeconds(), 0.8, 1e-6);
  ASSERT_NE(bridge.latest_frame("/cam"), nullptr);

  // play/pause 状态切换
  player.play();
  EXPECT_TRUE(player.playing());
  player.pause();
  EXPECT_FALSE(player.playing());

  // seek 超界被钳
  player.seek(999.0);
  EXPECT_LE(player.playheadSeconds(), player.durationSeconds() + 1e-6);

  player.stop();
  fs::remove_all(dir);
}
```

- [ ] **Step 5: 运行测试，确认通过**

Run: build + `colcon test ... -R test_session_player`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add include/data_recorder/session_player.hpp src/session_player.cpp \
  test/test_session_player.cpp CMakeLists.txt
git commit -m "feat(session_player): 播放时钟 + 多路解码推帧给 LiveBridge"
```

---

## Task 6: AppController 历史/在线切换接线 + 播放控制

**Files:**
- Modify: `include/data_recorder/app_controller.hpp`
- Modify: `src/app_controller.cpp`
- Test: `test/test_ui_models.cpp`(追加 AppController 历史切换集成测试)

> AppController 已 `#include` 了模型;需新增 `SessionPlayer`/`QThread` 成员与 `playing` 属性。`SessionManager`/`LiveBridge` 已前置声明/包含。

- [ ] **Step 1: 写失败测试**

在 `test/test_ui_models.cpp` 末尾追加(用 VideoRecorder 造一个临时会话目录,经真实 `SessionManager` 扫描后切换)。先确保该文件含必要头:`#include "data_recorder/app_controller.hpp"`、`#include "data_recorder/live_bridge.hpp"`、`#include "data_recorder/session_manager.hpp"`、`#include "data_recorder/video_recorder.hpp"`、`#include <QCoreApplication>`、`#include <filesystem>`(缺则加):

```cpp
TEST(AppControllerHistoryTest, SelectHistoryEntersPlaybackMode)
{
  namespace fs = std::filesystem;
  int argc = 0; char ** argv = nullptr;
  QCoreApplication app(argc, argv);

  fs::path out = fs::temp_directory_path() / "drc_appctrl_test";
  fs::remove_all(out);
  fs::path sdir = out / "2026-06-27_00-00-00";
  fs::create_directories(sdir / "video");
  // 造一路相机 mp4+csv
  {
    data_recorder::VideoParams params;
    data_recorder::VideoRecorder rec(
      (sdir / "video" / "cam.mp4").string(),
      (sdir / "video" / "cam.csv").string(), 32, 24, params);
    for (int i = 0; i < 20; ++i) {
      data_recorder::ImageFrame f;
      f.width = 32; f.height = 24; f.step = 96; f.encoding = "bgr8";
      f.ros_stamp_ns = 1000000000LL + i * 40000000LL;
      f.data.assign(32 * 24 * 3, static_cast<uint8_t>(i * 6));
      rec.encode(f);
    }
    rec.close();
  }
  // 写最小 session.yaml（SessionManager::scan 需要）
  {
    std::ofstream y((sdir / "session.yaml").string());
    y << "session: 2026-06-27_00-00-00\n"
      << "duration_seconds: 0.8\n"
      << "topics:\n  - name: /cam\n    backend: video\n"
      << "tags: []\nannotations: []\n";
  }

  data_recorder::ConfigData config;
  config.output_dir = out.string();
  config.topics.push_back({"/cam", "video", 0,
    data_recorder::TopicUiCategory::CameraPreview, {}});

  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager sm;
  data_recorder::AppController ctrl(config, &bridge, nullptr, &sm);

  ASSERT_GT(ctrl.recordingSessionModel()->rowCount(), 0);
  EXPECT_FALSE(ctrl.historyMode());
  EXPECT_TRUE(ctrl.canRecord());

  ctrl.selectHistorySession(0);
  EXPECT_TRUE(ctrl.historyMode());
  EXPECT_FALSE(ctrl.canRecord());
  EXPECT_NEAR(ctrl.timelineDurationSeconds(), 0.8, 0.5);
  // 回放帧已推给 bridge
  ASSERT_NE(bridge.latest_frame("/cam"), nullptr);

  ctrl.selectOnlineData();
  EXPECT_FALSE(ctrl.historyMode());
  EXPECT_TRUE(ctrl.canRecord());

  fs::remove_all(out);
}
```

> 注:`SessionManager::scan` 解析 `session.yaml` 的字段名需与上面一致。实现时若 `scan` 要求更多字段(如 `recorded_at`),按 `recordings/*/session.yaml` 真实样例补全测试里的 yaml。`AppController` 构造里会 `refreshSessions()` 自动扫描 `output_dir`。

- [ ] **Step 2: 运行测试，确认失败**

Run: build
Expected: 编译失败 —— `playing()` 等未声明 / 链接缺失。

- [ ] **Step 3: 改头文件**

`include/data_recorder/app_controller.hpp`:
1. 顶部加 `#include <QThread>`、`#include <vector>`、`#include "data_recorder/recorder_types.hpp"`;前置声明区加 `class SessionPlayer;`。
2. 属性区加:

```cpp
  Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
```

3. 公有方法区加:

```cpp
  bool playing() const;
  Q_INVOKABLE void togglePlayback();
```

4. signals 区加:

```cpp
  void playingChanged();
```

5. private 成员区加:

```cpp
  SessionPlayer * player_{nullptr};
  QThread * player_thread_{nullptr};
  std::vector<SessionRecord> scanned_sessions_;
  std::vector<TopicEntry> live_topics_;
  bool playing_{false};
```

- [ ] **Step 4: 改实现**

`src/app_controller.cpp`:

1. 顶部加 include:

```cpp
#include "data_recorder/session_player.hpp"
#include "data_recorder/video_clip_reader.hpp"
```

2. 构造函数:保存 live topics、建播放线程与 player,接信号。在 `topic_model_.set_topics(config.topics);` 之后加 `live_topics_ = config.topics;`;在 `if (bridge_) {...}` 之后加:

```cpp
  player_thread_ = new QThread(this);
  player_ = new SessionPlayer(bridge_);
  player_->moveToThread(player_thread_);
  connect(player_thread_, &QThread::finished, player_, &QObject::deleteLater);
  connect(player_, &SessionPlayer::playheadAdvanced, this,
    [this](double seconds) {
      if (playhead_seconds_ != seconds) {
        playhead_seconds_ = seconds;
        emit playheadSecondsChanged();
      }
    });
  connect(player_, &SessionPlayer::playingChanged, this,
    [this](bool on) {
      if (playing_ != on) {
        playing_ = on;
        emit playingChanged();
      }
    });
  player_thread_->start();
```

3. 析构(若无析构函数则新增 `AppController::~AppController()`,并在头文件声明 `~AppController() override;`):

```cpp
AppController::~AppController()
{
  if (player_thread_) {
    player_thread_->quit();
    player_thread_->wait();
  }
}
```

4. `playing()` / `togglePlayback()`:

```cpp
bool AppController::playing() const
{
  return playing_;
}

void AppController::togglePlayback()
{
  if (!history_mode_ || !player_) { return; }
  QMetaObject::invokeMethod(player_, "togglePlay", Qt::QueuedConnection);
}
```

5. `refreshSessions()`:缓存完整记录。改为:

```cpp
void AppController::refreshSessions()
{
  if (!session_manager_) { return; }
  const std::string dir = std::filesystem::absolute(output_directory_.toStdString()).string();
  scanned_sessions_ = session_manager_->scan(dir);
  recording_session_model_.setSessions(scanned_sessions_);
}
```

6. `selectHistorySession(int row)`:在现有函数体末尾(`if (previous_mode != modeText()) {...}` 之后、函数结束 `}` 之前)追加加载逻辑。同时把会话元数据加载放在设置 `history_mode_=true` 之后:

```cpp
  // —— 加载会话进入回放 ——
  if (row >= 0 && row < static_cast<int>(scanned_sessions_.size())) {
    const SessionRecord & session = scanned_sessions_[static_cast<std::size_t>(row)];

    // 会话话题 → topic_model_（video→CameraPreview）
    std::vector<TopicEntry> session_topics;
    for (const auto & tref : session.topics) {
      TopicEntry e;
      e.topic_name = tref.name;
      e.backend_name = tref.backend;
      e.ui_category = (tref.backend == "video")
        ? TopicUiCategory::CameraPreview : TopicUiCategory::NumericTrack;
      session_topics.push_back(e);
    }
    topic_model_.set_topics(session_topics);
    event_marker_model_.setInstances(session.annotations);
    tag_model_.setSelectedTags(session.tags);

    playhead_seconds_ = 0.0;
    emit playheadSecondsChanged();
    emit timelineDurationSecondsChanged();

    if (bridge_) { bridge_->set_playback_mode(true); }
    if (player_) {
      QMetaObject::invokeMethod(player_, "load", Qt::QueuedConnection,
        Q_ARG(data_recorder::SessionRecord, session));
    }
  }
```

> 为让 `Q_ARG(data_recorder::SessionRecord, ...)` 跨线程队列调用可用:在 `session_player.hpp` 末尾(`}  // namespace` 之后、文件末)加 `Q_DECLARE_METATYPE(data_recorder::SessionRecord)`(该头已 `#include "recorder_types.hpp"`,顶部再加 `#include <QMetaType>`);并在 `AppController` 构造函数最前面加 `qRegisterMetaType<data_recorder::SessionRecord>("data_recorder::SessionRecord");`。放 `session_player.hpp` 而非 `recorder_types.hpp`,避免给纯 C++ 头引入 Qt 依赖。

7. `selectOnlineData()`:在现有体内(`history_mode_ = false;` 之后)追加恢复逻辑:

```cpp
  if (bridge_) { bridge_->set_playback_mode(false); }
  if (player_) { QMetaObject::invokeMethod(player_, "stop", Qt::QueuedConnection); }
  topic_model_.set_topics(live_topics_);
  event_marker_model_.clearInstances();
  tag_model_.clearSelection();
  playhead_seconds_ = 0.0;
  emit playheadSecondsChanged();
  emit timelineDurationSecondsChanged();
```

8. `timelineDurationSeconds()`:历史模式返回会话时长:

```cpp
double AppController::timelineDurationSeconds() const
{
  if (history_mode_ && selected_session_row_ >= 0 &&
    selected_session_row_ < static_cast<int>(scanned_sessions_.size()))
  {
    const double d = scanned_sessions_[static_cast<std::size_t>(selected_session_row_)]
      .duration_seconds;
    if (d > 0.0) { return d; }
  }
  return std::max({live_edge_seconds_, playhead_seconds_, kDefaultTimelineSpanSeconds});
}
```

9. `setPlayheadSeconds(double seconds)`:历史模式改为 seek 播放器。函数开头加:

```cpp
  if (history_mode_) {
    const double clamped = std::max(0.0, seconds);
    if (player_) {
      QMetaObject::invokeMethod(player_, "seek", Qt::QueuedConnection,
        Q_ARG(double, clamped));
    }
    return;
  }
```

- [ ] **Step 5: 运行测试，确认通过**

Run: build + `colcon test ... -R test_ui_models`
Expected: PASS（含 `AppControllerHistoryTest`)。

> 若链接报 moc 相关错误,确认 `app_controller.hpp` 的新 `Q_PROPERTY`/signal 已被 autogen 处理(colcon 重新构建即可)。

- [ ] **Step 6: 提交**

```bash
git add include/data_recorder/app_controller.hpp src/app_controller.cpp \
  include/data_recorder/recorder_types.hpp test/test_ui_models.cpp
git commit -m "feat(app_controller): 历史回放切换接线 + 播放控制 + 会话加载"
```

---

## Task 7: QML UI（红框主操作按钮 / 状态栏 / 禁点历史行）

**Files:**
- Modify: `qml/components/TrackInfoColumn.qml`
- Modify: `qml/components/StatusBar.qml`
- Modify: `qml/components/RecordingSessionsPanel.qml`
- Test: `test/test_qml_structure.cpp`(若其按对象名/属性断言,可加一条;否则依赖 `test_qml_smoke` 保证加载不崩)

- [ ] **Step 1: 改 TrackInfoColumn.qml —— 红框处上下文主操作按钮**

在 [TrackInfoColumn.qml](../../qml/components/TrackInfoColumn.qml) 顶部 header 行的 `RowLayout`(行 39-62)里,在"回到实时"`Button` **之前**插入一个上下文主操作按钮:

```qml
            Button {
                objectName: "primaryActionButton"
                Layout.preferredWidth: 72
                Layout.preferredHeight: 24
                font.pixelSize: 10
                visible: root.controller !== undefined && root.controller !== null
                enabled: root.controller
                    ? (root.controller.historyMode ? true : root.controller.canRecord)
                    : false
                text: {
                    if (!root.controller) return ""
                    if (root.controller.historyMode)
                        return root.controller.playing ? "暂停" : "播放"
                    return root.controller.recording ? "停止" : "录制"
                }
                onClicked: {
                    if (!root.controller) return
                    if (root.controller.historyMode)
                        root.controller.togglePlayback()
                    else
                        root.controller.toggleRecording()
                }
            }
```

> "回到实时"`Button`(行 54-61)保留不动:它仅在 `recording && !followingLiveEdge` 时可见,与主操作按钮并列。

- [ ] **Step 2: 改 StatusBar.qml —— 移除录制按钮**

删除 [StatusBar.qml](../../qml/components/StatusBar.qml) 里的 `Button { objectName: "recordButton" ... }`(行 24-31)。保留状态文字 `Label`、右侧弹簧 `Item`、磁盘 `Label`。`isRecording` 属性保留(状态文字颜色仍用)。

- [ ] **Step 3: 改 RecordingSessionsPanel.qml —— 录制中禁点历史行**

在 [RecordingSessionsPanel.qml](../../qml/components/RecordingSessionsPanel.qml) 的历史会话 `delegate`(行 89 的 `Rectangle`)加 `enabled` 与视觉灰显;`MouseArea`(行 144)加守卫:

delegate `Rectangle` 加属性:

```qml
                enabled: !(root.controller && root.controller.recording)
                opacity: enabled ? 1.0 : 0.45
```

`MouseArea` 的 `onClicked` 改为:

```qml
                onClicked: {
                    if (root.controller && !root.controller.recording) {
                        root.controller.selectHistorySession(index)
                    }
                }
```

- [ ] **Step 4: 运行 QML 烟雾/结构测试**

Run: build + `colcon test ... -R "test_qml_smoke|test_qml_structure"`
Expected: PASS(QML 能加载、结构断言通过)。若 `test_qml_structure` 显式断言 `recordButton` 存在,更新该断言为 `primaryActionButton`。

- [ ] **Step 5: 提交**

```bash
git add qml/components/TrackInfoColumn.qml qml/components/StatusBar.qml \
  qml/components/RecordingSessionsPanel.qml test/test_qml_structure.cpp
git commit -m "feat(qml): 主操作按钮迁到时间轴 header（录制/停止/播放/暂停）+ 录制中禁点历史"
```

---

## Task 8: 全量构建 + 全测试 + 手动验证清单

**Files:** 无新增(集成校验)。

- [ ] **Step 1: 全量构建**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --mixin release compile-commands ccache
```

Expected: 构建成功,无错误。

- [ ] **Step 2: 全量测试**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --packages-select data_recorder \
  --event-handlers console_direct+ && colcon test-result --all
```

Expected: 全部测试 PASS,包括 `test_ui_models`、`test_video_clip_reader`、`test_session_player`、`test_qml_smoke`、`test_qml_structure` 及既有测试。

- [ ] **Step 3: 手动验证（需图形会话；记录结果，不通过则回到对应 Task）**

按 `README.md` 的 UI Verification 启动应用:

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \
  --ros-args -p config_file:=.../src/data_recorder/config/example_config.yaml
```

逐项确认:
1. 点击"数据"面板里某个**历史会话**行 → 整个界面进入历史回放:相机网格显示该录制的首帧、时间轴时长变为录制时长、标注出现在时间轴、对应标签高亮、话题列表为会话话题。
2. 时间轴 header 红框处出现**"播放"**按钮;点击 → 相机随播放头播放视频,再点变**"暂停"**。
3. 拖动时间轴播放头 → 相机帧跟随定位(seek)。
4. 点击"在线数据" → 回到实时,相机恢复实时帧,红框处按钮变回**"录制"**。
5. 在线状态下红框处是**"录制"**按钮(底部状态栏已无录制按钮);点击开始录制 → 变**"停止"**。
6. **录制进行中**,历史会话行**灰显且点不动**。

- [ ] **Step 4: 最终提交(若手动验证触发任何小修)**

```bash
git add -A && git commit -m "chore: 历史回放集成校验与收尾"
```

---

## 自检对照（spec → task 覆盖）

- 解码相机 mp4 随播放头播放 → Task 4(VideoClipReader)+ Task 5(SessionPlayer)。
- 加载会话话题/标注/标签/时长 → Task 2/3(模型方法)+ Task 6(AppController 接线、timelineDuration)。
- 播放/暂停 + 拖动 seek + 返回在线 → Task 5(player)+ Task 6(togglePlayback/setPlayheadSeconds/selectOnlineData)+ Task 7(按钮)。
- 选中默认暂停显示首帧 → Task 5(load 推首帧、暂停态)。
- 不读 mcap、非图像话题静态列表 → Task 6(set_topics 用会话话题,不解析 mcap)。
- LiveBridge 帧仓库门控(方案 A)→ Task 1。
- topic_key 一致性(§7)→ Task 5(`topic_key=topic_name`)+ Task 4 round-trip 与 Task 6 集成测试中 `bridge.latest_frame("/cam")` 命中验证。
- 红框主操作按钮 / 状态栏移除录制 / 录制中禁点历史 → Task 7。
- 测试:VideoClipReader round-trip、SessionPlayer 冒烟、模型单测、AppController 集成、QML 烟雾 → 各 Task。
