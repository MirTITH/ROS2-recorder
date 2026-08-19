#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

#include "data_recorder/video_clip_reader.hpp"
#include "data_recorder/video_recorder.hpp"
#include "data_recorder/recorder_types.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::ImageFrame makeFrame(int w, int h, int64_t stamp_ns, uint8_t v)
{
  data_recorder::ImageFrame f;
  f.width = w;
  f.height = h;
  f.step = w * 3;
  f.encoding = "bgr8";
  f.ros_stamp_ns = stamp_ns;
  f.data.assign(static_cast<std::size_t>(w) * h * 3, v);
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

TEST(VideoClipReaderTest, OpenMissingReturnsFalse)
{
  data_recorder::VideoClipReader reader;
  EXPECT_FALSE(reader.open("/nonexistent/x.mp4", "/nonexistent/x.csv"));
  EXPECT_FALSE(reader.is_valid());
  EXPECT_TRUE(reader.frameAtSeconds(0.0).isNull());
}
