#include <gtest/gtest.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
}

#include <rapidcsv.h>

#include <filesystem>
#include <string>
#include <vector>

#include "data_recorder/video_recorder.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::ImageFrame make_bgr8_frame(
  int w, int h, int64_t stamp_ns, uint8_t fill,
  const std::string & frame_id = "cam", int64_t header_stamp_ns = 0)
{
  data_recorder::ImageFrame f;
  f.width = w;
  f.height = h;
  f.step = w * 3;
  f.encoding = "bgr8";
  f.is_bigendian = false;
  f.recv_stamp_ns = stamp_ns;
  f.header_stamp_ns = header_stamp_ns;
  f.frame_id = frame_id;
  f.data.assign(static_cast<size_t>(w) * h * 3, fill);
  return f;
}

data_recorder::ImageFrame make_mono8_frame(int w, int h, int64_t stamp_ns, uint8_t fill)
{
  data_recorder::ImageFrame f;
  f.width = w;
  f.height = h;
  f.step = w;
  f.encoding = "mono8";
  f.is_bigendian = true;
  f.recv_stamp_ns = stamp_ns;
  f.header_stamp_ns = stamp_ns;
  f.frame_id = "cam";
  f.data.assign(static_cast<size_t>(w) * h, fill);
  return f;
}

AVPixelFormat video_pixel_format(const std::string & path)
{
  AVFormatContext * format = nullptr;
  if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
    return AV_PIX_FMT_NONE;
  }
  if (avformat_find_stream_info(format, nullptr) < 0) {
    avformat_close_input(&format);
    return AV_PIX_FMT_NONE;
  }
  const int stream_index = av_find_best_stream(
    format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const AVPixelFormat pixel_format = stream_index >= 0 ?
    static_cast<AVPixelFormat>(format->streams[stream_index]->codecpar->format) :
    AV_PIX_FMT_NONE;
  avformat_close_input(&format);
  return pixel_format;
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

  // CSV 行数 = 30，7 列表头
  rapidcsv::Document doc(csv, rapidcsv::LabelParams(0, -1));
  EXPECT_EQ(doc.GetRowCount(), 30u);
  const std::vector<std::string> expected_columns = {
    "frame_index", "recv_stamp_ns", "header_stamp_ns", "pts_ns", "frame_id", "encoding",
    "is_bigendian"};
  EXPECT_EQ(doc.GetColumnNames(), expected_columns);

  // PTS 单调递增（按列名取值，不依赖列位置）
  const std::vector<int64_t> pts = doc.GetColumn<int64_t>("pts_ns");
  int64_t prev = -1;
  for (const int64_t p : pts) {
    EXPECT_GT(p, prev);
    prev = p;
  }

  fs::remove_all(tmp);
}

TEST(VideoRecorder, EscapesCommaInFrameIdAndPersistsEncodingFields)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test_escaping";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  const std::string bgr_mp4 = (tmp / "cam_bgr.mp4").string();
  const std::string bgr_csv = (tmp / "cam_bgr.csv").string();
  const std::string mono_mp4 = (tmp / "cam_mono.mp4").string();
  const std::string mono_csv = (tmp / "cam_mono.csv").string();

  data_recorder::VideoParams params;
  {
    // 编码在一个会话内锁定，这里用两个独立会话分别验证 bgr8/mono8 各自的字段持久化。
    data_recorder::VideoRecorder bgr_rec(bgr_mp4, bgr_csv, 64, 48, params);
    ASSERT_TRUE(bgr_rec.is_open());
    ASSERT_TRUE(bgr_rec.encode(
      make_bgr8_frame(64, 48, 0, 10, "cam,1", 1'000'000LL)));
    bgr_rec.close();

    data_recorder::VideoRecorder mono_rec(mono_mp4, mono_csv, 64, 48, params);
    ASSERT_TRUE(mono_rec.is_open());
    ASSERT_TRUE(mono_rec.encode(make_mono8_frame(64, 48, 33'333'333LL, 20)));
    mono_rec.close();
  }

  rapidcsv::Document bgr_doc(bgr_csv, rapidcsv::LabelParams(0, -1));
  ASSERT_EQ(bgr_doc.GetRowCount(), 1u);

  // rapidcsv 的 pAutoQuote 往返：写入的逗号被自动加引号，读回时自动还原原始字符串。
  const std::vector<std::string> frame_ids = bgr_doc.GetColumn<std::string>("frame_id");
  EXPECT_EQ(frame_ids[0], "cam,1");
  const std::vector<int64_t> header_stamps = bgr_doc.GetColumn<int64_t>("header_stamp_ns");
  EXPECT_EQ(header_stamps[0], 1'000'000LL);
  const std::vector<std::string> bgr_encodings = bgr_doc.GetColumn<std::string>("encoding");
  EXPECT_EQ(bgr_encodings[0], "bgr8");
  const std::vector<int> bgr_is_bigendians = bgr_doc.GetColumn<int>("is_bigendian");
  EXPECT_EQ(bgr_is_bigendians[0], 0);

  rapidcsv::Document mono_doc(mono_csv, rapidcsv::LabelParams(0, -1));
  ASSERT_EQ(mono_doc.GetRowCount(), 1u);
  const std::vector<std::string> mono_encodings = mono_doc.GetColumn<std::string>("encoding");
  EXPECT_EQ(mono_encodings[0], "mono8");
  const std::vector<int> mono_is_bigendians = mono_doc.GetColumn<int>("is_bigendian");
  EXPECT_EQ(mono_is_bigendians[0], 1);

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
  f.recv_stamp_ns = 0;
  f.data.assign(64 * 48 * 3, 0);
  const bool encoded = rec.encode(f);
  EXPECT_FALSE(encoded);  // 跳过，不崩溃
  rec.close();

  fs::remove_all(tmp);
}

TEST(VideoRecorder, ResolutionChangeMidStreamDropsFrame)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test_res_change";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  const std::string mp4 = (tmp / "cam.mp4").string();
  const std::string csv = (tmp / "cam.csv").string();

  data_recorder::VideoParams params;
  data_recorder::VideoRecorder rec(mp4, csv, 64, 48, params);
  ASSERT_TRUE(rec.is_open());
  ASSERT_TRUE(rec.encode(make_bgr8_frame(64, 48, 0, 10)));
  // 分辨率与首帧（构造时锁定的 64x48）不符，应丢弃且不崩溃。
  EXPECT_FALSE(rec.encode(make_bgr8_frame(32, 24, 33'333'333LL, 20)));
  ASSERT_TRUE(rec.encode(make_bgr8_frame(64, 48, 66'666'666LL, 30)));
  rec.close();

  rapidcsv::Document doc(csv, rapidcsv::LabelParams(0, -1));
  EXPECT_EQ(doc.GetRowCount(), 2u);  // 只有两帧分辨率匹配的帧被写入

  fs::remove_all(tmp);
}

TEST(VideoRecorder, EncodingChangeMidStreamDropsFrameInsteadOfCorrupting)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test_enc_change";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  const std::string mp4 = (tmp / "cam.mp4").string();
  const std::string csv = (tmp / "cam.csv").string();

  data_recorder::VideoParams params;
  data_recorder::VideoRecorder rec(mp4, csv, 64, 48, params);
  ASSERT_TRUE(rec.is_open());
  ASSERT_TRUE(rec.encode(make_bgr8_frame(64, 48, 0, 10)));
  // mono8 本身是受支持的编码，但与首帧锁定的 bgr8 不同，必须丢弃而不是复用旧 swscale。
  EXPECT_FALSE(rec.encode(make_mono8_frame(64, 48, 33'333'333LL, 20)));
  ASSERT_TRUE(rec.encode(make_bgr8_frame(64, 48, 66'666'666LL, 30)));
  rec.close();

  rapidcsv::Document doc(csv, rapidcsv::LabelParams(0, -1));
  EXPECT_EQ(doc.GetRowCount(), 2u);
  const std::vector<std::string> encodings = doc.GetColumn<std::string>("encoding");
  EXPECT_EQ(encodings[0], "bgr8");
  EXPECT_EQ(encodings[1], "bgr8");

  fs::remove_all(tmp);
}

TEST(VideoRecorder, AppliesConfiguredPixelFormat)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test_pix_fmt";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  const std::string mp4 = (tmp / "x.mp4").string();

  data_recorder::VideoParams params;
  params.pix_fmt = "yuv444p";
  {
    data_recorder::VideoRecorder rec(mp4, (tmp / "x.csv").string(), 64, 48, params);
    ASSERT_TRUE(rec.is_open());
    for (int i = 0; i < 3; ++i) {
      EXPECT_TRUE(rec.encode(make_bgr8_frame(64, 48, i * 33'333'333LL, i * 30)));
    }
    rec.close();
  }

  EXPECT_EQ(video_pixel_format(mp4), AV_PIX_FMT_YUV444P);
  fs::remove_all(tmp);
}

TEST(VideoRecorder, BogusPixelFormatFailsToOpenAndDestructsCleanly)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test_bogus_pix_fmt";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  data_recorder::VideoParams params;
  params.pix_fmt = "no_such_pixel_format";
  {
    data_recorder::VideoRecorder rec(
      (tmp / "x.mp4").string(), (tmp / "x.csv").string(), 64, 48, params);
    EXPECT_FALSE(rec.is_open());
  }

  fs::remove_all(tmp);
}

TEST(VideoRecorder, EncoderUnsupportedPixelFormatFailsToOpen)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test_unsupported_pix_fmt";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  data_recorder::VideoParams params;
  params.pix_fmt = "rgba";  // swscale 可输出，但 libx264 不接受。
  {
    data_recorder::VideoRecorder rec(
      (tmp / "x.mp4").string(), (tmp / "x.csv").string(), 64, 48, params);
    EXPECT_FALSE(rec.is_open());
  }

  fs::remove_all(tmp);
}

// 回归：init() 失败路径（不存在的编码器）必须 !is_open() 且析构干净不崩溃。
// 防止 close() 在没成功写 header 的 muxer 上调 av_write_trailer。
TEST(VideoRecorder, BogusCodecFailsToOpenAndDestructsCleanly)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test_bogus_codec";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  data_recorder::VideoParams params;
  params.codec = "no_such_codec";  // avcodec_find_encoder_by_name 返回 null → init 失败
  {
    data_recorder::VideoRecorder rec(
      (tmp / "x.mp4").string(), (tmp / "x.csv").string(), 64, 48, params);
    EXPECT_FALSE(rec.is_open());
  }  // 离开作用域 → 析构 → close()；header 未写，不应崩溃

  fs::remove_all(tmp);
}

// 回归：init() 失败路径（不存在的容器）必须 !is_open() 且析构干净不崩溃。
TEST(VideoRecorder, BogusContainerFailsToOpenAndDestructsCleanly)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_video_test_bogus_container";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  data_recorder::VideoParams params;
  params.container = "this_is_not_a_container";  // avformat_alloc_output_context2 失败 → init 失败
  {
    data_recorder::VideoRecorder rec(
      (tmp / "x.mp4").string(), (tmp / "x.csv").string(), 64, 48, params);
    EXPECT_FALSE(rec.is_open());
  }  // 离开作用域 → 析构 → close()；header 未写，不应崩溃

  fs::remove_all(tmp);
}
