#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <string>

#include <unistd.h>

#include "data_recorder/config_model.hpp"

namespace
{

std::string write_temp_config(const std::string & content)
{
  static int file_index = 0;
  const auto path = "/tmp/data_recorder_config_test_" + std::to_string(getpid()) + "_" +
    std::to_string(file_index++) + ".yaml";
  std::ofstream out(path);
  out << content;
  return path;
}

}  // namespace

TEST(ConfigModel, LoadsExampleShape)
{
  const auto path = write_temp_config(R"yaml(
output_dir: "./recordings"
tags:
  - { name: "成功", color: "#2f9e44" }
annotation_types:
  "1": { name: "拿起水杯", kind: "point", color: "#1763c9" }
  "2": { name: "倒水", kind: "range", color: "#2f9e44" }
groups:
  - topics:
      - /tf
      - /joint_states
    backend: rosbag
  - topics:
      - /camera/image_raw
    backend: video
    params:
      codec: "libx264"
      crf: 23
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.output_dir, "./recordings");
  ASSERT_EQ(config.topics.size(), 3u);
  const auto camera_it = std::find_if(
    config.topics.begin(), config.topics.end(),
    [](const auto & t) { return t.topic_name == "/camera/image_raw"; });
  ASSERT_NE(camera_it, config.topics.end());
  EXPECT_EQ(camera_it->backend_name, "video");
  EXPECT_EQ(camera_it->params.at("codec"), "libx264");
  EXPECT_EQ(camera_it->params.at("crf"), "23");
  ASSERT_EQ(config.tags.size(), 1u);
  EXPECT_EQ(config.tags[0].name, "成功");
  ASSERT_EQ(config.event_markers.size(), 2u);
  EXPECT_EQ(config.event_markers[0].shortcut, "1");
  EXPECT_EQ(config.event_markers[1].kind, "range");
}

TEST(ConfigModel, DefaultsBackendToRosbag)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /joint_states
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 1u);
  EXPECT_EQ(config.topics[0].backend_name, "rosbag");
}

TEST(ConfigModel, ThrowsWhenGroupsMissing)
{
  const auto path = write_temp_config(R"yaml(
output_dir: "./recordings"
)yaml");

  const data_recorder::ConfigModel model;
  EXPECT_THROW(model.load_from_file(path), data_recorder::ConfigError);
}

TEST(ConfigModel, ThrowsWhenGroupTopicsEmpty)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics: []
  - topics:
      - /joint_states
)yaml");

  const data_recorder::ConfigModel model;
  EXPECT_THROW(model.load_from_file(path), data_recorder::ConfigError);
}

TEST(ConfigModel, WrapsMalformedTopicValueAsConfigError)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - [/joint_states]
)yaml");

  const data_recorder::ConfigModel model;
  EXPECT_THROW(model.load_from_file(path), data_recorder::ConfigError);
}

TEST(ConfigModel, ThrowsWhenFileMissing)
{
  const data_recorder::ConfigModel model;
  EXPECT_THROW(model.load_from_file("/tmp/does-not-exist-data-recorder.yaml"), data_recorder::ConfigError);
}
