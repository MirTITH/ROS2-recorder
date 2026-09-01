#include "data_recorder/video_recorder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <cstring>
#include <exception>
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

bool codec_supports_pixel_format(const AVCodec * codec, AVPixelFormat requested)
{
#if LIBAVCODEC_VERSION_MAJOR >= 61
  const void * configs = nullptr;
  int count = 0;
  const int ret = avcodec_get_supported_config(
    nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &count);
  if (ret < 0) { return false; }
  if (!configs) { return true; }

  const auto * formats = static_cast<const AVPixelFormat *>(configs);
  for (int i = 0; i < count; ++i) {
    if (formats[i] == requested) { return true; }
  }
#else
  // FFmpeg 4.4（Ubuntu 22.04）：pix_fmts 以 AV_PIX_FMT_NONE 结尾；nullptr 表示未声明限制。
  if (!codec->pix_fmts) { return true; }
  for (const AVPixelFormat * format = codec->pix_fmts;
    *format != AV_PIX_FMT_NONE; ++format)
  {
    if (*format == requested) { return true; }
  }
#endif
  return false;
}

constexpr const char * kCsvColumns[] = {
  "frame_index", "recv_stamp_ns", "header_stamp_ns", "pts_ns", "frame_id", "encoding",
  "is_bigendian"};
}  // namespace

VideoRecorder::VideoRecorder(
  const std::string & video_path, const std::string & csv_path,
  int width, int height, const VideoParams & params)
: video_path_(video_path), csv_path_(csv_path), width_(width), height_(height),
  csv_doc_(std::string(), rapidcsv::LabelParams(0, -1))
{
  for (size_t i = 0; i < sizeof(kCsvColumns) / sizeof(kCsvColumns[0]); ++i) {
    csv_doc_.InsertColumn<std::string>(i, {}, kCsvColumns[i]);
  }
  csv_ready_ = true;
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

  const AVPixelFormat encode_pix_fmt = av_get_pix_fmt(params.pix_fmt.c_str());
  if (encode_pix_fmt == AV_PIX_FMT_NONE) {
    std::cerr << "[VideoRecorder] 未知像素格式: " << params.pix_fmt << "\n";
    return false;
  }
  const AVPixFmtDescriptor * pix_fmt_desc = av_pix_fmt_desc_get(encode_pix_fmt);
  if (!pix_fmt_desc) {
    std::cerr << "[VideoRecorder] 无法读取像素格式描述: " << params.pix_fmt << "\n";
    return false;
  }
  if ((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_HWACCEL) != 0) {
    std::cerr << "[VideoRecorder] 当前软件帧管线不支持硬件像素格式: "
              << params.pix_fmt << "\n";
    return false;
  }
  if (!sws_isSupportedOutput(encode_pix_fmt)) {
    std::cerr << "[VideoRecorder] swscale 不支持输出像素格式: " << params.pix_fmt << "\n";
    return false;
  }
  if (!codec_supports_pixel_format(codec, encode_pix_fmt)) {
    std::cerr << "[VideoRecorder] 编码器 " << params.codec
              << " 不支持像素格式 " << params.pix_fmt << "\n";
    return false;
  }

  codec_ctx_ = avcodec_alloc_context3(codec);
  if (!codec_ctx_) { return false; }
  codec_ctx_->width = width_;
  codec_ctx_->height = height_;
  codec_ctx_->pix_fmt = encode_pix_fmt;
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

  encode_frame_ = av_frame_alloc();
  if (!encode_frame_) { return false; }
  encode_frame_->format = codec_ctx_->pix_fmt;
  encode_frame_->width = width_;
  encode_frame_->height = height_;
  if (av_frame_get_buffer(encode_frame_, 0) < 0) { return false; }

  packet_ = av_packet_alloc();
  return packet_ != nullptr;
}

bool VideoRecorder::fill_source_frame(const ImageFrame & frame)
{
  // 编码在 encode() 中已锁定并校验不变，这里只按首帧编码懒建 swscale。
  if (!sws_) {
    const int src_fmt = source_av_format(frame.encoding);
    if (src_fmt == AV_PIX_FMT_NONE) { return false; }
    sws_ = sws_getContext(width_, height_, static_cast<AVPixelFormat>(src_fmt),
      width_, height_, codec_ctx_->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) { return false; }
  }

  const uint8_t * src_slices[4] = {frame.data.data(), nullptr, nullptr, nullptr};
  int src_stride[4] = {frame.step, 0, 0, 0};
  if (av_frame_make_writable(encode_frame_) < 0) { return false; }
  sws_scale(
    sws_, src_slices, src_stride, 0, height_, encode_frame_->data, encode_frame_->linesize);
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
    std::cerr << "[VideoRecorder] 不支持的编码，丢弃该帧: " << frame.encoding << "\n";
    return false;
  }
  if (frame.width != width_ || frame.height != height_) {
    std::cerr << "[VideoRecorder] 分辨率变化，丢弃该帧: 期望 " << width_ << "x" << height_
              << "，实际 " << frame.width << "x" << frame.height << "\n";
    return false;
  }
  if (!have_source_encoding_) {
    source_encoding_ = frame.encoding;
    have_source_encoding_ = true;
  } else if (frame.encoding != source_encoding_) {
    std::cerr << "[VideoRecorder] 编码变化，丢弃该帧: 期望 " << source_encoding_
              << "，实际 " << frame.encoding << "\n";
    return false;
  }
  if (!fill_source_frame(frame)) { return false; }

  // PTS：用每帧接收时刻（相对首帧），换算到 1/90000 timebase。
  if (!have_first_) { first_stamp_ns_ = frame.recv_stamp_ns; have_first_ = true; }
  const double rel_seconds = static_cast<double>(frame.recv_stamp_ns - first_stamp_ns_) / 1e9;
  const int64_t pts = std::llround(rel_seconds * kTimebaseDen);
  encode_frame_->pts = pts;

  if (avcodec_send_frame(codec_ctx_, encode_frame_) < 0) { return false; }
  drain_packets();

  if (csv_ready_) {
    const std::vector<std::string> row = {
      std::to_string(frame_index_), std::to_string(frame.recv_stamp_ns),
      std::to_string(frame.header_stamp_ns), std::to_string(pts), frame.frame_id,
      frame.encoding, frame.is_bigendian ? "1" : "0"};
    csv_doc_.InsertRow<std::string>(static_cast<size_t>(frame_index_), row);
  }
  ++frame_index_;
  return true;
}

bool VideoRecorder::close()
{
  if (header_written_ && codec_ctx_ && fmt_ctx_) {
    avcodec_send_frame(codec_ctx_, nullptr);  // flush
    drain_packets();
    av_write_trailer(fmt_ctx_);
  }
  bool csv_saved = true;
  if (csv_ready_) {
    // rapidcsv 内部以 exceptions 模式打开输出流，磁盘满/目录不可写会抛异常；
    // close() 可能从析构（noexcept）调用，必须兜住，否则 FFmpeg 资源清理会被跳过并 terminate。
    try {
      csv_doc_.Save(csv_path_);
    } catch (const std::exception & error) {
      std::cerr << "[VideoRecorder] 保存 CSV 失败: " << csv_path_ << ": " << error.what() << "\n";
      csv_saved = false;
    }
    csv_ready_ = false;
  }
  if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
  if (packet_) { av_packet_free(&packet_); }
  if (encode_frame_) { av_frame_free(&encode_frame_); }
  if (fmt_ctx_) {
    if (fmt_ctx_->pb) { avio_closep(&fmt_ctx_->pb); }
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
  }
  if (codec_ctx_) { avcodec_free_context(&codec_ctx_); }
  open_ = false;
  return csv_saved;
}

}  // namespace data_recorder
