#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "data_recorder/video_recorder.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::ImageFrame make_bgr8_frame(int w, int h, int64_t stamp_ns, uint8_t fill)
{
  data_recorder::ImageFrame f;
  f.width = w;
  f.height = h;
  f.step = w * 3;
  f.encoding = "bgr8";
  f.ros_stamp_ns = stamp_ns;
  f.data.assign(static_cast<size_t>(w) * h * 3, fill);
  return f;
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

  // CSV 行数 = 帧头 + 30
  std::ifstream in(csv);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(in, line)) { lines.push_back(line); }
  ASSERT_EQ(lines.size(), 31u);  // header + 30
  EXPECT_EQ(lines[0], "frame_index,ros_stamp_ns,pts_ns");

  // PTS 单调递增
  auto pts_of = [](const std::string & l) {
    std::stringstream ss(l); std::string a, b, c;
    std::getline(ss, a, ','); std::getline(ss, b, ','); std::getline(ss, c, ',');
    return std::stoll(c);
  };
  int64_t prev = -1;
  for (size_t i = 1; i < lines.size(); ++i) {
    int64_t pts = pts_of(lines[i]);
    EXPECT_GT(pts, prev);
    prev = pts;
  }

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
  f.ros_stamp_ns = 0;
  f.data.assign(64 * 48 * 3, 0);
  const bool encoded = rec.encode(f);
  EXPECT_FALSE(encoded);  // 跳过，不崩溃
  rec.close();

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
