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
