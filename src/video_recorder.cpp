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
  header_written_ = true;

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
  if (header_written_ && codec_ctx_ && fmt_ctx_) {
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
