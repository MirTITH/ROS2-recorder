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

TEST(ConfigModel, DefaultsToRosDefaultQos)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /camera/image_raw
    backend: video
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 1u);
  const auto & qos = config.topics[0].qos;
  EXPECT_EQ(qos.history, data_recorder::QosHistory::KeepLast);
  EXPECT_EQ(qos.depth, 10u);
  EXPECT_EQ(qos.reliability, data_recorder::QosReliability::Reliable);
  EXPECT_EQ(qos.durability, data_recorder::QosDurability::Volatile);
}

TEST(ConfigModel, TopicQosOverridesGroupQos)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /camera/image_raw
      - /left_camera/image_raw:
          qos:
            reliability: reliable
            durability: transient_local
            depth: 3
    backend: video
    qos:
      history: keep_last
      depth: 20
      reliability: best_effort
      durability: system_default
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 2u);
  EXPECT_EQ(config.topics[0].qos.depth, 20u);
  EXPECT_EQ(config.topics[0].qos.reliability, data_recorder::QosReliability::BestEffort);
  EXPECT_EQ(config.topics[0].qos.durability, data_recorder::QosDurability::SystemDefault);
  EXPECT_EQ(config.topics[1].qos.depth, 3u);
  EXPECT_EQ(config.topics[1].qos.reliability, data_recorder::QosReliability::Reliable);
  EXPECT_EQ(config.topics[1].qos.durability, data_recorder::QosDurability::TransientLocal);
}

TEST(ConfigModel, ReadsKeepAllQos)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /camera/image_raw
    backend: video
    qos:
      history: keep_all
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 1u);
  EXPECT_EQ(config.topics[0].qos.history, data_recorder::QosHistory::KeepAll);
}

TEST(ConfigModel, RejectsInvalidQos)
{
  const data_recorder::ConfigModel model;

  const auto invalid_reliability = write_temp_config(R"yaml(
groups:
  - topics: [/camera/image_raw]
    qos: { reliability: sometimes }
)yaml");
  EXPECT_THROW(model.load_from_file(invalid_reliability), data_recorder::ConfigError);

  const auto invalid_depth = write_temp_config(R"yaml(
groups:
  - topics: [/camera/image_raw]
    qos: { depth: 0 }
)yaml");
  EXPECT_THROW(model.load_from_file(invalid_depth), data_recorder::ConfigError);

  const auto keep_all_with_depth = write_temp_config(R"yaml(
groups:
  - topics: [/camera/image_raw]
    qos: { history: keep_all, depth: 10 }
)yaml");
  EXPECT_THROW(model.load_from_file(keep_all_with_depth), data_recorder::ConfigError);
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

TEST(ConfigModel, ScalarTopicDefaultsToCollapsed)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /joint_states
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 1u);
  EXPECT_EQ(config.topics[0].topic_name, "/joint_states");
  EXPECT_FALSE(config.topics[0].default_expanded);
}

TEST(ConfigModel, SingleKeyMapReadsUiExpanded)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /tf
      - /joint_states: { ui_expanded: true }
      - /wrench: { ui_expanded: false }
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 3u);
  EXPECT_EQ(config.topics[0].topic_name, "/tf");
  EXPECT_FALSE(config.topics[0].default_expanded);          // 裸字符串
  EXPECT_EQ(config.topics[1].topic_name, "/joint_states");
  EXPECT_TRUE(config.topics[1].default_expanded);           // map ui_expanded: true
  EXPECT_EQ(config.topics[2].topic_name, "/wrench");
  EXPECT_FALSE(config.topics[2].default_expanded);          // map ui_expanded: false
}

TEST(ConfigModel, SingleKeyMapWithoutUiExpandedDefaultsCollapsed)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /joint_states: {}
)yaml");

  const data_recorder::ConfigModel model;
  const auto config = model.load_from_file(path);

  ASSERT_EQ(config.topics.size(), 1u);
  EXPECT_EQ(config.topics[0].topic_name, "/joint_states");
  EXPECT_FALSE(config.topics[0].default_expanded);
}

TEST(ConfigModel, MultiKeyTopicMapThrows)
{
  const auto path = write_temp_config(R"yaml(
groups:
  - topics:
      - /a: { ui_expanded: true }
        /b: { ui_expanded: true }
)yaml");

  const data_recorder::ConfigModel model;
  EXPECT_THROW(model.load_from_file(path), data_recorder::ConfigError);
}
