#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

#include <QCoreApplication>

#include "data_recorder/live_bridge.hpp"
#include "data_recorder/recorder_types.hpp"
#include "data_recorder/session_player.hpp"
#include "data_recorder/video_recorder.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::ImageFrame makeFrame(int w, int h, int64_t stamp_ns, uint8_t v)
{
  data_recorder::ImageFrame f;
  f.width = w; f.height = h; f.step = w * 3;
  f.encoding = "bgr8"; f.ros_stamp_ns = stamp_ns;
  f.data.assign(static_cast<std::size_t>(w) * h * 3, v);
  return f;
}

data_recorder::SessionRecord makeSession(const fs::path & dir)
{
  const int W = 48, H = 32, N = 30;
  const int64_t base = 1000000000LL;
  fs::create_directories(dir / "video");
  data_recorder::VideoParams params;
  data_recorder::VideoRecorder rec(
    (dir / "video" / "cam.mp4").string(),
    (dir / "video" / "cam.csv").string(), W, H, params);
  for (int i = 0; i < N; ++i) {
    rec.encode(makeFrame(W, H, base + static_cast<int64_t>(i) * 40000000LL,
      static_cast<uint8_t>(i * 5)));
  }
  rec.close();

  data_recorder::SessionRecord s;
  s.session_id = "tmp";
  s.directory = dir.string();
  s.duration_seconds = 1.2;
  s.topics.push_back({"/cam", "video"});
  return s;
}
}  // namespace

TEST(SessionPlayerTest, LoadAndSeekPushFrames)
{
  int argc = 0;
  char ** argv = nullptr;
  QCoreApplication app(argc, argv);

  fs::path dir = fs::temp_directory_path() / "drc_player_test";
  fs::remove_all(dir);
  auto session = makeSession(dir);

  data_recorder::LiveBridge bridge;
  bridge.set_playback_mode(true);
  data_recorder::SessionPlayer player(&bridge);

  player.load(session);
  EXPECT_GT(player.durationSeconds(), 1.0);
  EXPECT_FALSE(player.playing());
  ASSERT_NE(bridge.latest_frame("/cam"), nullptr);
  EXPECT_EQ(bridge.latest_frame("/cam")->width(), 48);

  player.seek(0.8);
  EXPECT_NEAR(player.playheadSeconds(), 0.8, 1e-6);
  ASSERT_NE(bridge.latest_frame("/cam"), nullptr);

  player.play();
  EXPECT_TRUE(player.playing());
  player.pause();
  EXPECT_FALSE(player.playing());

  player.seek(999.0);
  EXPECT_LE(player.playheadSeconds(), player.durationSeconds() + 1e-6);

  player.stop();
  fs::remove_all(dir);
}
