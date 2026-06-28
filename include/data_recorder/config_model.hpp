#pragma once

#include <yaml-cpp/yaml.h>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace data_recorder
{

enum class TopicUiCategory
{
  CameraPreview,
  NumericTrack,
};

struct TopicEntry
{
  std::string topic_name;
  std::string backend_name;
  int group_index{};
  TopicUiCategory ui_category{TopicUiCategory::NumericTrack};
  bool default_expanded{false};
  std::map<std::string, std::string> params;
};

struct TagEntry
{
  std::string name;
  std::string color;
};

struct EventMarkerEntry
{
  std::string shortcut;
  std::string name;
  std::string kind;
  std::string color;
};

struct ConfigData
{
  std::string config_path;
  std::string output_dir;
  std::vector<TopicEntry> topics;
  std::vector<TagEntry> tags;
  std::vector<EventMarkerEntry> event_markers;
};

class ConfigError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class ConfigModel
{
public:
  ConfigData load_from_file(const std::string & path) const;

private:
  static bool is_camera_topic(const std::string & topic_name, const std::string & backend_name);
  static std::string scalar_to_string(const YAML::Node & node);
};

}  // namespace data_recorder
