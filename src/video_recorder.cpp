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
#include <fstream>
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
  try {
    for (size_t i = 0; i < sizeof(kCsvColumns) / sizeof(kCsvColumns[0]); ++i) {
      csv_doc_.InsertColumn<std::string>(i, {}, kCsvColumns[i]);
    }
    csv_ready_ = true;
    open_ = init(params);
  } catch (const std::exception & error) {
    record_error(std::string("初始化失败: ") + error.what());
  } catch (...) {
    record_error("初始化失败: 未知异常");
  }
}

VideoRecorder::~VideoRecorder() noexcept
{
  try {
    close();
  } catch (...) {
    // 正常调用方显式检查 close；析构不传播异常。
  }
}

bool VideoRecorder::record_error(const std::string & detail)
{
  if (error_message_.empty()) {
    error_message_ = video_path_ + ": " + detail;
    std::cerr << "[VideoRecorder] " << error_message_ << "\n";
  }
  return false;
}

bool VideoRecorder::record_av_error(const std::string & operation, int code)
{
  char message[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, message, sizeof(message));
  return record_error(operation + ": " + message + " (" + std::to_string(code) + ")");
}

bool VideoRecorder::init(const VideoParams & params)
{
  const AVCodec * codec = avcodec_find_encoder_by_name(params.codec.c_str());
  if (!codec) {
    return record_error("找不到编码器: " + params.codec);
  }

  const AVPixelFormat encode_pix_fmt = av_get_pix_fmt(params.pix_fmt.c_str());
  if (encode_pix_fmt == AV_PIX_FMT_NONE) {
    return record_error("未知像素格式: " + params.pix_fmt);
  }
  const AVPixFmtDescriptor * pix_fmt_desc = av_pix_fmt_desc_get(encode_pix_fmt);
  if (!pix_fmt_desc) {
    return record_error("无法读取像素格式描述: " + params.pix_fmt);
  }
  if ((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_HWACCEL) != 0) {
    return record_error("当前软件帧管线不支持硬件像素格式: " + params.pix_fmt);
  }
  if (!sws_isSupportedOutput(encode_pix_fmt)) {
    return record_error("swscale 不支持输出像素格式: " + params.pix_fmt);
  }
  if (!codec_supports_pixel_format(codec, encode_pix_fmt)) {
    return record_error("编码器 " + params.codec + " 不支持像素格式 " + params.pix_fmt);
  }

  codec_ctx_ = avcodec_alloc_context3(codec);
  if (!codec_ctx_) { return record_error("分配编码器上下文失败"); }
  codec_ctx_->width = width_;
  codec_ctx_->height = height_;
  codec_ctx_->pix_fmt = encode_pix_fmt;
  codec_ctx_->time_base = AVRational{1, kTimebaseDen};
  codec_ctx_->framerate = AVRational{30, 1};  // 提示
  codec_ctx_->gop_size = 60;

  av_opt_set(codec_ctx_->priv_data, "preset", params.preset.c_str(), 0);
  av_opt_set(codec_ctx_->priv_data, "crf", std::to_string(params.crf).c_str(), 0);

  int ret = avcodec_open2(codec_ctx_, codec, nullptr);
  if (ret < 0) {
    return record_av_error("编码器打开失败", ret);
  }

  ret = avformat_alloc_output_context2(&fmt_ctx_, nullptr, params.container.c_str(),
    video_path_.c_str());
  if (ret < 0) {
    return record_av_error("分配容器上下文失败", ret);
  }
  if (!fmt_ctx_) { return record_error("分配容器上下文失败"); }
  stream_ = avformat_new_stream(fmt_ctx_, nullptr);
  if (!stream_) { return record_error("创建视频流失败"); }
  stream_->time_base = codec_ctx_->time_base;
  ret = avcodec_parameters_from_context(stream_->codecpar, codec_ctx_);
  if (ret < 0) { return record_av_error("设置视频流参数失败", ret); }

  ret = avio_open(&fmt_ctx_->pb, video_path_.c_str(), AVIO_FLAG_WRITE);
  if (ret < 0) { return record_av_error("打开视频文件失败", ret); }
  ret = avformat_write_header(fmt_ctx_, nullptr);
  if (ret < 0) { return record_av_error("写视频 header 失败", ret); }
  avio_flush(fmt_ctx_->pb);
  if (fmt_ctx_->pb->error < 0) {
    return record_av_error("写视频 header 失败", fmt_ctx_->pb->error);
  }
  header_written_ = true;

  encode_frame_ = av_frame_alloc();
  if (!encode_frame_) { return record_error("分配编码帧失败"); }
  encode_frame_->format = codec_ctx_->pix_fmt;
  encode_frame_->width = width_;
  encode_frame_->height = height_;
  ret = av_frame_get_buffer(encode_frame_, 0);
  if (ret < 0) { return record_av_error("分配编码帧缓冲失败", ret); }

  packet_ = av_packet_alloc();
  if (!packet_) { return record_error("分配编码包失败"); }
  return true;
}

bool VideoRecorder::fill_source_frame(const ImageFrame & frame)
{
  // 编码在 encode() 中已锁定并校验不变，这里只按首帧编码懒建 swscale。
  if (!sws_) {
    const int src_fmt = source_av_format(frame.encoding);
    if (src_fmt == AV_PIX_FMT_NONE) { return false; }
    sws_ = sws_getContext(width_, height_, static_cast<AVPixelFormat>(src_fmt),
      width_, height_, codec_ctx_->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) { return record_error("创建像素转换上下文失败"); }
  }

  const uint8_t * src_slices[4] = {frame.data.data(), nullptr, nullptr, nullptr};
  int src_stride[4] = {frame.step, 0, 0, 0};
  int ret = av_frame_make_writable(encode_frame_);
  if (ret < 0) { return record_av_error("编码帧不可写", ret); }
  ret = sws_scale(
    sws_, src_slices, src_stride, 0, height_, encode_frame_->data, encode_frame_->linesize);
  if (ret < 0) { return record_av_error("像素转换失败", ret); }
  if (ret != height_) { return record_error("像素转换未生成完整帧"); }
  return true;
}

bool VideoRecorder::drain_packets()
{
  for (;;) {
    const int ret = avcodec_receive_packet(codec_ctx_, packet_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) { return true; }
    if (ret < 0) { return record_av_error("读取编码包失败", ret); }
    av_packet_rescale_ts(packet_, codec_ctx_->time_base, stream_->time_base);
    packet_->stream_index = stream_->index;
    const int write_ret = av_interleaved_write_frame(fmt_ctx_, packet_);
    av_packet_unref(packet_);
    if (write_ret < 0) { return record_av_error("写视频编码包失败", write_ret); }
    if (fmt_ctx_->pb && fmt_ctx_->pb->error < 0) {
      return record_av_error("写视频编码包失败", fmt_ctx_->pb->error);
    }
  }
}

bool VideoRecorder::encode(const ImageFrame & frame)
{
  if (!error_message_.empty()) { return false; }
  if (!open_) { return record_error("编码失败: recorder 已关闭"); }
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

  const int ret = avcodec_send_frame(codec_ctx_, encode_frame_);
  if (ret < 0) { return record_av_error("发送编码帧失败", ret); }
  if (!drain_packets()) { return false; }

  if (csv_ready_) {
    try {
      const std::vector<std::string> row = {
        std::to_string(frame_index_), std::to_string(frame.recv_stamp_ns),
        std::to_string(frame.header_stamp_ns), std::to_string(pts), frame.frame_id,
        frame.encoding, frame.is_bigendian ? "1" : "0"};
      csv_doc_.InsertRow<std::string>(static_cast<size_t>(frame_index_), row);
    } catch (const std::exception & error) {
      return record_error(std::string("记录 CSV 帧信息失败: ") + error.what());
    } catch (...) {
      return record_error("记录 CSV 帧信息失败: 未知异常");
    }
  }
  ++frame_index_;
  return true;
}

bool VideoRecorder::close()
{
  open_ = false;
  if (header_written_ && codec_ctx_ && fmt_ctx_) {
    // 初始化可能在写 header 后失败；只有分配了 packet 才能排空编码器。
    if (packet_) {
      const int ret = avcodec_send_frame(codec_ctx_, nullptr);
      if (ret < 0 && ret != AVERROR_EOF) { record_av_error("flush 编码器失败", ret); }
      drain_packets();
    }
    const int ret = av_write_trailer(fmt_ctx_);
    if (ret < 0) { record_av_error("写视频 trailer 失败", ret); }
  }
  header_written_ = false;
  if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
  if (packet_) { av_packet_free(&packet_); }
  if (encode_frame_) { av_frame_free(&encode_frame_); }
  if (fmt_ctx_) {
    if (fmt_ctx_->pb) {
      avio_flush(fmt_ctx_->pb);
      if (fmt_ctx_->pb->error < 0) {
        record_av_error("flush 视频文件失败", fmt_ctx_->pb->error);
      }
      const int ret = avio_closep(&fmt_ctx_->pb);
      if (ret < 0) { record_av_error("关闭视频文件失败", ret); }
    }
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
    stream_ = nullptr;
  }
  if (codec_ctx_) { avcodec_free_context(&codec_ctx_); }
  if (csv_ready_) {
    // 直接写目标文件；显式 flush/close 捕获缓冲输出在最后一步的失败。
    // rapidcsv::Save(path) 的流析构无法可靠报告这些错误。
    try {
      std::ofstream output;
      output.exceptions(std::ofstream::failbit | std::ofstream::badbit);
      output.open(csv_path_, std::ios::binary | std::ios::trunc);
      csv_doc_.Save(output);
      output.flush();
      output.close();
    } catch (const std::exception & error) {
      record_error("保存 CSV 失败: " + csv_path_ + ": " + error.what());
    } catch (...) {
      record_error("保存 CSV 失败: " + csv_path_ + ": 未知异常");
    }
    csv_ready_ = false;
  }
  return error_message_.empty();
}

}  // namespace data_recorder
