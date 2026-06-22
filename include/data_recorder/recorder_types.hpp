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
