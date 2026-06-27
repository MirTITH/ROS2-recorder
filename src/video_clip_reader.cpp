#include "data_recorder/video_clip_reader.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
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
  entries_.clear();
  valid_ = false;
}

bool VideoClipReader::open(const std::string & mp4_path, const std::string & csv_path)
{
  close();
  valid_ = false;
  entries_.clear();

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
  const int i = indexForSeconds(rel_seconds);  // floor index
  const int n = static_cast<int>(entries_.size());
  if (i + 1 < n) {
    const double d_lo = std::abs(entries_[i].rel_seconds - rel_seconds);
    const double d_hi = std::abs(entries_[i + 1].rel_seconds - rel_seconds);
    if (d_hi < d_lo) { return i + 1; }
  }
  return i;
}

QImage VideoClipReader::decodeForwardTo(int target_index)
{
  while (true) {
    const int ret = av_read_frame(fmt_, pkt_);
    const bool at_eof = (ret < 0);

    // 发包：EOF 时送 nullptr 进入 flush；否则跳过非视频流的包。
    if (!at_eof && pkt_->stream_index != video_stream_) {
      av_packet_unref(pkt_);
      continue;
    }
    // EAGAIN（解码器输出队列满）不丢包——先排空 receive，再重发同一个包。
    while (true) {
      const int send_ret = avcodec_send_packet(dec_, at_eof ? nullptr : pkt_);

      // 排空所有可取的帧。
      while (true) {
        const int r = avcodec_receive_frame(dec_, frame_);
        if (r == AVERROR(EAGAIN)) { break; }
        if (r == AVERROR_EOF || r < 0) {
          if (!at_eof) { av_packet_unref(pkt_); }
          return cached_;
        }

        int64_t pts = frame_->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE) { pts = frame_->pts; }
        const double rel = (pts == AV_NOPTS_VALUE) ? 0.0 : static_cast<double>(pts) * time_base_;
        cur_index_ = indexNearestPts(rel);

        if (cur_index_ >= target_index) {
          sws_ = sws_getCachedContext(sws_, frame_->width, frame_->height,
            static_cast<AVPixelFormat>(frame_->format), frame_->width, frame_->height,
            AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
          if (!sws_) {
            if (!at_eof) { av_packet_unref(pkt_); }
            return cached_;
          }
          QImage img(frame_->width, frame_->height, QImage::Format_RGB888);
          if (img.isNull()) {
            if (!at_eof) { av_packet_unref(pkt_); }
            return cached_;
          }
          uint8_t * dst[1] = { img.bits() };
          int dst_stride[1] = { static_cast<int>(img.bytesPerLine()) };
          sws_scale(sws_, frame_->data, frame_->linesize, 0, frame_->height, dst, dst_stride);
          cached_ = img;
          if (!at_eof) { av_packet_unref(pkt_); }
          return cached_;
        }
      }

      // 队列已排空：若 send 仍报 EAGAIN，重发同一个包；否则继续读下一个包。
      if (send_ret == AVERROR(EAGAIN)) { continue; }
      break;
    }

    if (!at_eof) { av_packet_unref(pkt_); }
    if (at_eof) { return cached_; }
  }
}

QImage VideoClipReader::frameAtSeconds(double t)
{
  if (!valid_) { return QImage(); }
  int target = indexForSeconds(t);
  if (target < 0) { return QImage(); }
  if (target == cur_index_ && !cached_.isNull()) { return cached_; }

  constexpr int kSeqWindow = 30;
  const bool need_seek = (cur_index_ < 0) || (target < cur_index_) ||
    (target > cur_index_ + kSeqWindow);
  if (need_seek) {
    AVStream * st = fmt_->streams[video_stream_];
    int64_t ts = static_cast<int64_t>(
      entries_[static_cast<std::size_t>(target)].rel_seconds / av_q2d(st->time_base));
    if (av_seek_frame(fmt_, video_stream_, ts, AVSEEK_FLAG_BACKWARD) < 0) { return cached_; }
    avcodec_flush_buffers(dec_);
    cur_index_ = -1;
  }
  return decodeForwardTo(target);
}

}  // namespace data_recorder
