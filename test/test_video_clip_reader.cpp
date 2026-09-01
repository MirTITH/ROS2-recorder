#include <gtest/gtest.h>

#include <rapidcsv.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "data_recorder/video_clip_reader.hpp"
#include "data_recorder/video_recorder.hpp"
#include "data_recorder/recorder_types.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::ImageFrame makeFrame(
  int w, int h, int64_t stamp_ns, uint8_t v,
  const std::string & encoding = "bgr8", const std::string & frame_id = "cam",
  bool is_bigendian = false)
{
  data_recorder::ImageFrame f;
  f.width = w;
  f.height = h;
  f.step = w * (encoding == "mono8" ? 1 : 3);
  f.encoding = encoding;
  f.is_bigendian = is_bigendian;
  f.recv_stamp_ns = stamp_ns;
  f.header_stamp_ns = stamp_ns + 1'000'000LL;
  f.frame_id = frame_id;
  f.data.assign(static_cast<std::size_t>(w) * h * (encoding == "mono8" ? 1 : 3), v);
  return f;
}
}  // namespace

TEST(VideoClipReaderTest, RoundTripDecodesFrames)
{
  fs::path dir = fs::temp_directory_path() / "drc_clip_test";
  fs::create_directories(dir);
  const std::string mp4 = (dir / "clip.mp4").string();
  const std::string csv = (dir / "clip.csv").string();

  const int W = 64, H = 48;
  const int N = 40;
  const int64_t base = 1000000000LL;
  {
    data_recorder::VideoParams params;
    data_recorder::VideoRecorder rec(mp4, csv, W, H, params);
    for (int i = 0; i < N; ++i) {
      rec.encode(makeFrame(W, H, base + static_cast<int64_t>(i) * 40000000LL,
        static_cast<uint8_t>(i * 4)));
    }
    rec.close();
  }

  ASSERT_TRUE(fs::exists(mp4));
  ASSERT_TRUE(fs::exists(csv));

  data_recorder::VideoClipReader reader;
  ASSERT_TRUE(reader.open(mp4, csv));
  EXPECT_EQ(reader.frame_count(), static_cast<std::size_t>(N));
  EXPECT_EQ(reader.frame_stamp_ns(0), base);
  EXPECT_EQ(reader.frame_stamp_ns(static_cast<std::size_t>(N - 1)),
    base + static_cast<int64_t>(N - 1) * 40000000LL);
  EXPECT_EQ(reader.frame_stamp_ns(static_cast<std::size_t>(N)), 0);
  EXPECT_GT(reader.duration_seconds(), 1.0);

  EXPECT_EQ(reader.header_stamp_ns(0), base + 1'000'000LL);
  EXPECT_EQ(reader.frame_id(0), "cam");
  EXPECT_EQ(reader.encoding(0), "bgr8");
  EXPECT_FALSE(reader.is_bigendian(0));

  QImage f0_by_index = reader.frameAtIndex(0);
  ASSERT_FALSE(f0_by_index.isNull());
  EXPECT_EQ(f0_by_index.width(), W);
  EXPECT_EQ(f0_by_index.height(), H);

  QImage f0 = reader.frameAtSeconds(0.0);
  ASSERT_FALSE(f0.isNull());
  EXPECT_EQ(f0.width(), W);
  EXPECT_EQ(f0.height(), H);

  QImage fmid = reader.frameAtSeconds(0.8);
  ASSERT_FALSE(fmid.isNull());
  EXPECT_EQ(fmid.width(), W);

  QImage fback = reader.frameAtSeconds(0.1);
  ASSERT_FALSE(fback.isNull());
  EXPECT_EQ(fback.width(), W);

  QImage fend = reader.frameAtSeconds(999.0);
  ASSERT_FALSE(fend.isNull());

  QImage fend_by_index = reader.frameAtIndex(static_cast<std::size_t>(N - 1));
  ASSERT_FALSE(fend_by_index.isNull());
  EXPECT_EQ(fend_by_index.width(), W);
  EXPECT_TRUE(reader.frameAtIndex(static_cast<std::size_t>(N)).isNull());

  fs::remove_all(dir);
}

TEST(VideoClipReaderTest, DecodedFormatFollowsRecordedEncoding)
{
  fs::path dir = fs::temp_directory_path() / "drc_clip_test_encoding";
  fs::create_directories(dir);
  const std::string mono_mp4 = (dir / "mono.mp4").string();
  const std::string mono_csv = (dir / "mono.csv").string();
  const std::string bgr_mp4 = (dir / "bgr.mp4").string();
  const std::string bgr_csv = (dir / "bgr.csv").string();

  const int W = 64, H = 48;
  {
    // 编码在录制会话内锁定，这里用两个会话分别录制 mono8/bgr8，验证读回时各自跟随编码。
    data_recorder::VideoParams params;
    data_recorder::VideoRecorder mono_rec(mono_mp4, mono_csv, W, H, params);
    ASSERT_TRUE(mono_rec.encode(makeFrame(W, H, 0, 10, "mono8", "cam", true)));
    ASSERT_TRUE(mono_rec.encode(makeFrame(W, H, 33'333'333LL, 15, "mono8", "cam", true)));
    mono_rec.close();

    data_recorder::VideoRecorder bgr_rec(bgr_mp4, bgr_csv, W, H, params);
    ASSERT_TRUE(bgr_rec.encode(makeFrame(W, H, 0, 20, "bgr8", "cam", false)));
    ASSERT_TRUE(bgr_rec.encode(makeFrame(W, H, 33'333'333LL, 25, "bgr8", "cam", false)));
    bgr_rec.close();
  }

  data_recorder::VideoClipReader mono_reader;
  ASSERT_TRUE(mono_reader.open(mono_mp4, mono_csv));
  ASSERT_EQ(mono_reader.frame_count(), 2u);
  EXPECT_EQ(mono_reader.encoding(0), "mono8");
  EXPECT_TRUE(mono_reader.is_bigendian(0));

  QImage mono_frame = mono_reader.frameAtIndex(0);
  ASSERT_FALSE(mono_frame.isNull());
  EXPECT_EQ(mono_frame.format(), QImage::Format_Grayscale8);

  data_recorder::VideoClipReader bgr_reader;
  ASSERT_TRUE(bgr_reader.open(bgr_mp4, bgr_csv));
  ASSERT_EQ(bgr_reader.frame_count(), 2u);
  EXPECT_EQ(bgr_reader.encoding(0), "bgr8");
  EXPECT_FALSE(bgr_reader.is_bigendian(0));

  QImage bgr_frame = bgr_reader.frameAtIndex(0);
  ASSERT_FALSE(bgr_frame.isNull());
  EXPECT_EQ(bgr_frame.format(), QImage::Format_BGR888);

  fs::remove_all(dir);
}

TEST(VideoClipReaderTest, LegacyThreeColumnCsvFallsBackToDefaults)
{
  fs::path dir = fs::temp_directory_path() / "drc_clip_test_legacy";
  fs::create_directories(dir);
  const std::string mp4 = (dir / "clip.mp4").string();
  const std::string csv = (dir / "clip.csv").string();

  const int W = 64, H = 48;
  const int N = 5;
  const int64_t base = 1000000000LL;
  {
    data_recorder::VideoParams params;
    data_recorder::VideoRecorder rec(mp4, csv, W, H, params);
    for (int i = 0; i < N; ++i) {
      ASSERT_TRUE(rec.encode(makeFrame(
        W, H, base + static_cast<int64_t>(i) * 40000000LL, static_cast<uint8_t>(i * 4))));
    }
    rec.close();
  }

  // 手工改写成旧 3 列格式（frame_index,ros_stamp_ns,pts_ns），模拟历史录制的会话。
  {
    rapidcsv::Document doc(csv, rapidcsv::LabelParams(0, -1));
    rapidcsv::Document legacy(std::string(), rapidcsv::LabelParams(0, -1));
    legacy.InsertColumn<std::string>(0, {}, "frame_index");
    legacy.InsertColumn<std::string>(1, {}, "ros_stamp_ns");
    legacy.InsertColumn<std::string>(2, {}, "pts_ns");
    const std::vector<std::string> frame_indices = doc.GetColumn<std::string>("frame_index");
    const std::vector<std::string> recv_stamps = doc.GetColumn<std::string>("recv_stamp_ns");
    const std::vector<std::string> pts = doc.GetColumn<std::string>("pts_ns");
    for (std::size_t i = 0; i < frame_indices.size(); ++i) {
      legacy.InsertRow<std::string>(
        i, std::vector<std::string>{frame_indices[i], recv_stamps[i], pts[i]});
    }
    legacy.Save(csv);
  }

  data_recorder::VideoClipReader reader;
  ASSERT_TRUE(reader.open(mp4, csv));
  EXPECT_EQ(reader.frame_count(), static_cast<std::size_t>(N));
  EXPECT_EQ(reader.frame_stamp_ns(0), base);
  // 老 3 列格式没有单独的 header_stamp_ns 列，该列数据本身就是 header 时间戳，应恢复而非置零。
  EXPECT_EQ(reader.header_stamp_ns(0), base);
  EXPECT_EQ(reader.frame_id(0), "");
  EXPECT_EQ(reader.encoding(0), "");
  EXPECT_FALSE(reader.is_bigendian(0));

  QImage frame = reader.frameAtIndex(0);
  ASSERT_FALSE(frame.isNull());
  EXPECT_EQ(frame.format(), QImage::Format_RGB888);

  fs::remove_all(dir);
}

TEST(VideoClipReaderTest, OpenMissingReturnsFalse)
{
  data_recorder::VideoClipReader reader;
  EXPECT_FALSE(reader.open("/nonexistent/x.mp4", "/nonexistent/x.csv"));
  EXPECT_FALSE(reader.is_valid());
  EXPECT_TRUE(reader.frameAtSeconds(0.0).isNull());
}
