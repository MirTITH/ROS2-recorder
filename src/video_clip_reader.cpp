#include "data_recorder/video_clip_reader.hpp"

#include <cmath>
#include <stdexcept>

#include <rapidcsv.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace data_recorder
{

namespace
{
// 目标 sws/QImage 格式跟着录制时记录的 encoding 走；缺失（老文件无 encoding 列）回退到
// RGB24，维持既有行为。只要 CSV 包含 encoding 列，空值或未知值都表示索引无效，
// 禁止在图像字节与 encoding 标签不一致时继续回放。
bool encoding_recognized(const std::string & encoding)
{
  return encoding == "bgr8" || encoding == "mono8" || encoding == "rgb8";
}

AVPixelFormat target_av_format(const std::string & encoding)
{
  if (encoding == "bgr8") { return AV_PIX_FMT_BGR24; }
  if (encoding == "mono8") { return AV_PIX_FMT_GRAY8; }
  return AV_PIX_FMT_RGB24;
}

QImage::Format target_qimage_format(const std::string & encoding)
{
  if (encoding == "bgr8") { return QImage::Format_BGR888; }
  if (encoding == "mono8") { return QImage::Format_Grayscale8; }
  return QImage::Format_RGB888;
}
}  // namespace

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

  rapidcsv::Document csv_doc;
  try {
    csv_doc = rapidcsv::Document(csv_path, rapidcsv::LabelParams(0, -1));
  } catch (...) {
    return false;
  }

  // 新格式有 recv_stamp_ns 列；老 3 列格式退而取 ros_stamp_ns（旧折叠值，语义与该文件自己
  // 编码时用的 PTS 基准一致，indexNearestPts 的定位逻辑不受影响）。
  const bool has_recv_col = csv_doc.GetColumnIdx("recv_stamp_ns") >= 0;
  const std::string stamp_col = has_recv_col ? "recv_stamp_ns" : "ros_stamp_ns";
  if (csv_doc.GetColumnIdx(stamp_col) < 0) { return false; }
  const std::vector<int64_t> stamps = csv_doc.GetColumn<int64_t>(stamp_col);
  if (stamps.empty()) { return false; }

  const bool has_header_stamp = csv_doc.GetColumnIdx("header_stamp_ns") >= 0;
  const bool has_frame_id = csv_doc.GetColumnIdx("frame_id") >= 0;
  const bool has_encoding = csv_doc.GetColumnIdx("encoding") >= 0;
  const bool has_is_bigendian = csv_doc.GetColumnIdx("is_bigendian") >= 0;
  const std::vector<int64_t> header_stamps =
    has_header_stamp ? csv_doc.GetColumn<int64_t>("header_stamp_ns") : std::vector<int64_t>();
  const std::vector<std::string> frame_ids =
    has_frame_id ? csv_doc.GetColumn<std::string>("frame_id") : std::vector<std::string>();
  const std::vector<std::string> encodings =
    has_encoding ? csv_doc.GetColumn<std::string>("encoding") : std::vector<std::string>();
  const std::vector<int> is_bigendians =
    has_is_bigendian ? csv_doc.GetColumn<int>("is_bigendian") : std::vector<int>();

  const int64_t first_stamp = stamps.front();
  entries_.reserve(stamps.size());
  for (std::size_t i = 0; i < stamps.size(); ++i) {
    FrameIndexEntry e;
    e.recv_stamp_ns = stamps[i];
    e.rel_seconds = static_cast<double>(stamps[i] - first_stamp) / 1e9;
    // 老 3 列格式没有单独的 header_stamp_ns 列，该列数据本身就是 header 时间戳，直接复用。
    e.header_stamp_ns = (i < header_stamps.size()) ? header_stamps[i] : stamps[i];
    e.frame_id = (i < frame_ids.size()) ? frame_ids[i] : std::string();
    e.encoding = (i < encodings.size()) ? encodings[i] : std::string();
    e.is_bigendian = (i < is_bigendians.size()) && is_bigendians[i] != 0;
    if (has_encoding && e.encoding.empty()) {
      throw std::runtime_error(
        "empty encoding at data row " + std::to_string(i + 1) + " in CSV: " + csv_path);
    }
    if (has_encoding && !encoding_recognized(e.encoding)) {
      throw std::runtime_error(
        "unsupported encoding '" + e.encoding + "' at data row " +
        std::to_string(i + 1) + " in CSV: " + csv_path);
    }
    entries_.push_back(std::move(e));
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

std::size_t VideoClipReader::frame_count() const
{
  return entries_.size();
}

int64_t VideoClipReader::frame_stamp_ns(std::size_t index) const
{
  if (index >= entries_.size()) { return 0; }
  return entries_[index].recv_stamp_ns;
}

int64_t VideoClipReader::header_stamp_ns(std::size_t index) const
{
  if (index >= entries_.size()) { return 0; }
  return entries_[index].header_stamp_ns;
}

const std::string & VideoClipReader::frame_id(std::size_t index) const
{
  static const std::string kEmpty;
  if (index >= entries_.size()) { return kEmpty; }
  return entries_[index].frame_id;
}

const std::string & VideoClipReader::encoding(std::size_t index) const
{
  static const std::string kEmpty;
  if (index >= entries_.size()) { return kEmpty; }
  return entries_[index].encoding;
}

bool VideoClipReader::is_bigendian(std::size_t index) const
{
  if (index >= entries_.size()) { return false; }
  return entries_[index].is_bigendian;
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
          const std::string & enc = entries_[static_cast<std::size_t>(cur_index_)].encoding;
          const AVPixelFormat dst_fmt = target_av_format(enc);
          sws_ = sws_getCachedContext(sws_, frame_->width, frame_->height,
            static_cast<AVPixelFormat>(frame_->format), frame_->width, frame_->height,
            dst_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
          if (!sws_) {
            if (!at_eof) { av_packet_unref(pkt_); }
            return cached_;
          }
          QImage img(frame_->width, frame_->height, target_qimage_format(enc));
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

QImage VideoClipReader::frameAtIndex(std::size_t index)
{
  if (!valid_) { return QImage(); }
  if (index >= entries_.size()) { return QImage(); }
  const int target = static_cast<int>(index);
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

QImage VideoClipReader::frameAtSeconds(double t)
{
  if (!valid_) { return QImage(); }
  const int target = indexForSeconds(t);
  if (target < 0) { return QImage(); }
  return frameAtIndex(static_cast<std::size_t>(target));
}

}  // namespace data_recorder
