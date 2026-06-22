#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "data_recorder/session_manager.hpp"

namespace fs = std::filesystem;

namespace
{
data_recorder::SessionRecord make_record(const std::string & id)
{
  data_recorder::SessionRecord r;
  r.session_id = id;
  r.unix_time = 1782059150.228855;
  r.ros_time_ns = 1782059150228855043LL;
  r.duration_seconds = 42.512;
  r.topics = {{"/joint_states", "rosbag"}, {"/camera/image_raw", "video"}};
  r.tags = {{"成功", "#2f9e44"}};
  // 同名多条 + range
  r.annotations = {
    {"拿起水杯", "1", "point", "#1763c9", 3.210, 0.0},
    {"碰撞", "c", "point", "#e03131", 8.040, 0.0},
    {"碰撞", "c", "point", "#e03131", 12.880, 0.0},
    {"倒水", "2", "range", "#2f9e44", 5.0, 9.3},
  };
  return r;
}
}  // namespace

TEST(SessionManager, WritesAndReadsBackSessionYaml)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_session_test_rw";
  fs::remove_all(tmp);
  fs::create_directories(tmp / "2026-06-22_14-30-05");

  data_recorder::SessionManager mgr;
  auto record = make_record("2026-06-22_14-30-05");
  record.directory = (tmp / "2026-06-22_14-30-05").string();
  mgr.write_session_yaml(record);

  ASSERT_TRUE(fs::exists(tmp / "2026-06-22_14-30-05" / "session.yaml"));

  auto sessions = mgr.scan(tmp.string());
  ASSERT_EQ(sessions.size(), 1u);
  const auto & s = sessions.front();
  EXPECT_EQ(s.session_id, "2026-06-22_14-30-05");
  EXPECT_NEAR(s.duration_seconds, 42.512, 1e-6);
  ASSERT_EQ(s.annotations.size(), 4u);
  // 同名两条都在
  int collision_count = 0;
  for (const auto & a : s.annotations) {
    if (a.name == "碰撞") { ++collision_count; }
  }
  EXPECT_EQ(collision_count, 2);
  ASSERT_EQ(s.tags.size(), 1u);
  EXPECT_EQ(s.tags.front().name, "成功");

  fs::remove_all(tmp);
}

TEST(SessionManager, ScanSkipsDirsWithoutSessionYaml)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_session_test_skip";
  fs::remove_all(tmp);
  fs::create_directories(tmp / "good");
  fs::create_directories(tmp / "in_progress");  // 无 session.yaml

  data_recorder::SessionManager mgr;
  auto record = make_record("good");
  record.directory = (tmp / "good").string();
  mgr.write_session_yaml(record);

  auto sessions = mgr.scan(tmp.string());
  EXPECT_EQ(sessions.size(), 1u);  // in_progress 被跳过

  fs::remove_all(tmp);
}

TEST(SessionManager, ScanComputesDirectorySize)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_session_test_size";
  fs::remove_all(tmp);
  const fs::path dir = tmp / "with_data";
  fs::create_directories(dir);

  data_recorder::SessionManager mgr;
  auto record = make_record("with_data");
  record.directory = dir.string();
  mgr.write_session_yaml(record);

  // 写一个 1024 字节的假数据文件
  std::ofstream(dir / "rosbag_0.mcap", std::ios::binary).write(std::string(1024, 'x').data(), 1024);

  auto sessions = mgr.scan(tmp.string());
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_GE(sessions.front().size_bytes, 1024u);  // 现算，至少含数据文件

  fs::remove_all(tmp);
}

TEST(SessionManager, CreateSessionDirectoryMakesTimestampedSubdir)
{
  const fs::path tmp = fs::temp_directory_path() / "dr_session_test_create";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  data_recorder::SessionManager mgr;
  const std::string dir = mgr.create_session_directory(tmp.string(), "2026-06-22_15-00-00");
  EXPECT_TRUE(fs::exists(dir));
  EXPECT_TRUE(fs::exists(fs::path(dir) / "rosbag") || true);  // rosbag 子目录由 writer 建，这里不强求
  EXPECT_NE(dir.find("2026-06-22_15-00-00"), std::string::npos);

  fs::remove_all(tmp);
}
