#include "data_recorder/config_model.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace data_recorder
{

namespace
{

std::string optional_scalar(
  const YAML::Node & node,
  const char * key,
  const std::string & fallback = {})
{
  if (!node[key]) {
    return fallback;
  }
  return node[key].as<std::string>();
}

}  // namespace

ConfigData ConfigModel::load_from_file(const std::string & path) const
{
  if (path.empty()) {
    throw ConfigError("config_file parameter is empty");
  }
  if (!std::filesystem::exists(path)) {
    throw ConfigError("config file does not exist: " + path);
  }

  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception & error) {
    throw ConfigError("failed to parse YAML config '" + path + "': " + error.what());
  }

  ConfigData config;
  config.config_path = path;

  try {
    if (!root["groups"] || !root["groups"].IsSequence()) {
      throw ConfigError("config must contain a 'groups' sequence");
    }

    config.output_dir = optional_scalar(root, "output_dir", "./recordings");

    if (root["tags"] && root["tags"].IsSequence()) {
      for (const auto & tag_node : root["tags"]) {
        TagEntry tag;
        tag.name = optional_scalar(tag_node, "name");
        tag.color = optional_scalar(tag_node, "color", "#8a94a6");
        if (!tag.name.empty()) {
          config.tags.push_back(tag);
        }
      }
    }

    if (root["annotation_types"] && root["annotation_types"].IsMap()) {
      for (const auto & marker_node : root["annotation_types"]) {
        EventMarkerEntry marker;
        marker.shortcut = marker_node.first.as<std::string>();
        marker.name = optional_scalar(marker_node.second, "name");
        marker.kind = optional_scalar(marker_node.second, "kind", "point");
        marker.color = optional_scalar(marker_node.second, "color", "#3b82f6");
        if (!marker.name.empty()) {
          config.event_markers.push_back(marker);
        }
      }
      std::sort(config.event_markers.begin(), config.event_markers.end(), [](const auto & lhs, const auto & rhs) {
        return lhs.shortcut < rhs.shortcut;
      });
    }

    int group_index = 0;
    for (const auto & group_node : root["groups"]) {
      const auto backend_name = optional_scalar(group_node, "backend", "rosbag");
      if (!group_node["topics"] || !group_node["topics"].IsSequence()) {
        throw ConfigError("each group must contain a 'topics' sequence");
      }
      if (group_node["topics"].size() == 0) {
        throw ConfigError("each group must contain at least one topic");
      }

      std::map<std::string, std::string> params;
      if (group_node["params"] && group_node["params"].IsMap()) {
        for (const auto & param_node : group_node["params"]) {
          params[param_node.first.as<std::string>()] = scalar_to_string(param_node.second);
        }
      }

      for (const auto & topic_node : group_node["topics"]) {
        TopicEntry topic;
        topic.backend_name = backend_name;
        topic.group_index = group_index;
        topic.params = params;

        if (topic_node.IsScalar()) {
          topic.topic_name = topic_node.as<std::string>();
          topic.default_expanded = false;
        } else if (topic_node.IsMap()) {
          if (topic_node.size() != 1) {
            throw ConfigError("a topic map entry must have exactly one key (the topic name)");
          }
          const auto pair = *topic_node.begin();
          topic.topic_name = pair.first.as<std::string>();
          const auto & options = pair.second;
          if (options && options.IsMap() && options["ui_expanded"]) {
            topic.default_expanded = options["ui_expanded"].as<bool>();
          } else {
            topic.default_expanded = false;
          }
        } else {
          throw ConfigError("each topic must be a string or a single-key map");
        }

        topic.ui_category = is_camera_topic(topic.topic_name, topic.backend_name) ?
          TopicUiCategory::CameraPreview : TopicUiCategory::NumericTrack;

        config.topics.push_back(topic);
      }
      ++group_index;
    }

    if (config.topics.empty()) {
      throw ConfigError("config must contain at least one topic");
    }
  } catch (const YAML::Exception & error) {
    throw ConfigError("invalid YAML config value in '" + path + "': " + error.what());
  }

  return config;
}

bool ConfigModel::is_camera_topic(const std::string & topic_name, const std::string & backend_name)
{
  if (backend_name == "video") {
    return true;
  }
  auto lower_topic = topic_name;
  std::transform(lower_topic.begin(), lower_topic.end(), lower_topic.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lower_topic.find("image") != std::string::npos;
}

std::string ConfigModel::scalar_to_string(const YAML::Node & node)
{
  if (!node || node.IsNull()) {
    return {};
  }
  if (node.IsScalar()) {
    return node.as<std::string>();
  }
  std::stringstream stream;
  stream << node;
  return stream.str();
}

}  // namespace data_recorder
