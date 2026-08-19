#pragma once

#include <QImage>

#include <cstddef>
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

  bool open(const std::string & mp4_path, const std::string & csv_path);
  bool is_valid() const { return valid_; }

  std::size_t frame_count() const;
  int64_t frame_stamp_ns(std::size_t index) const;

  double duration_seconds() const;

  QImage frameAtSeconds(double t);
  QImage frameAtIndex(std::size_t index);

private:
  struct FrameIndexEntry
  {
    double rel_seconds{0.0};
    int64_t ros_stamp_ns{0};
  };

  void close();
  int indexForSeconds(double t) const;
  int indexNearestPts(double rel_seconds) const;
  QImage decodeForwardTo(int target_index);

  bool valid_{false};
  std::vector<FrameIndexEntry> entries_;

  AVFormatContext * fmt_{nullptr};
  AVCodecContext * dec_{nullptr};
  AVFrame * frame_{nullptr};
  AVPacket * pkt_{nullptr};
  SwsContext * sws_{nullptr};
  int video_stream_{-1};
  double time_base_{0.0};

  int cur_index_{-1};
  QImage cached_;
};

}  // namespace data_recorder
