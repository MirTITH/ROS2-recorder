# Data Recorder UI Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first `data_recorder` ROS 2 package: a Qt 6/QML/QtCharts UI prototype driven by a YAML config file passed through the ROS parameter `config_file`.

**Architecture:** Create an `ament_cmake` C++ package with a small config parser, Qt list models, an `AppController`, and modular QML panels. The app does not subscribe to ROS topics or record data; it parses YAML once on startup and renders simulated previews and chart data.

**Tech Stack:** ROS 2 Humble, `ament_cmake`, C++17, `rclcpp`, `yaml-cpp`, Qt 6 Core/QML/Quick/QuickControls2/Charts, QML `SplitView`, Qt Test or GTest for parser/model tests.

---

## File Map

- Create package with `ros2 pkg create`, then modify `src/data_recorder/CMakeLists.txt`: Qt 6, yaml-cpp, generated moc, resource installation, test targets.
- Modify `src/data_recorder/package.xml`: add ROS, yaml-cpp, Qt 6 dependency keys and documented apt fallback for QtCharts dev files.
- Modify generated `src/data_recorder/src/data_recorder.cpp`: executable entry point, ROS parameter handling, Qt engine setup.
- Create `src/data_recorder/README.md`: build/run commands, dependency fallback, UI verification instructions.
- Create `src/data_recorder/doc/ui_terminology.md`: canonical Chinese/English UI naming.
- Create `src/data_recorder/include/data_recorder/config_model.hpp` and `src/data_recorder/src/config_model.cpp`: YAML parsing into plain structs.
- Create `src/data_recorder/include/data_recorder/ui_models.hpp` and `src/data_recorder/src/ui_models.cpp`: Qt list models and deterministic simulated chart data.
- Create `src/data_recorder/include/data_recorder/app_controller.hpp` and `src/data_recorder/src/app_controller.cpp`: UI state exposed to QML.
- Create `src/data_recorder/test/test_config_model.cpp`: parser behavior tests.
- Create `src/data_recorder/test/test_ui_models.cpp`: role/model behavior tests.
- Create `src/data_recorder/qml/Main.qml`: main application layout.
- Create `src/data_recorder/qml/components/*.qml`: modular panels.

The workspace root is not currently a git repository. Replace commit steps with checkpoint notes unless a repo is initialized before implementation.

---

## Task 1: Verify Dependencies And Create ROS 2 Package

**Files:**
- Create: `src/data_recorder/` via `ros2 pkg create`
- Modify: `src/data_recorder/package.xml`
- Modify: `src/data_recorder/CMakeLists.txt`
- Create: `src/data_recorder/README.md`

- [ ] **Step 1: Verify rosdep coverage**

Run:

```bash
source ~/.local/ros2_rc && rr && rosdep resolve yaml-cpp rclcpp qt6-base-dev qt6-declarative-dev qml6-module-qtcharts
```

Expected: each key resolves to apt packages. `yaml-cpp` resolves to `libyaml-cpp-dev`; `rclcpp` resolves to `ros-humble-rclcpp`; Qt keys resolve to Qt 6 packages.

- [ ] **Step 2: Verify QtCharts apt fallback**

Run:

```bash
apt-cache policy libqt6charts6-dev qml6-module-qtcharts
```

Expected: both packages have candidates. `libqt6charts6-dev` may not be installed yet.

- [ ] **Step 3: Install missing dependencies**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws
rosdep install --from-paths src --ignore-src -r -y
sudo apt install -y libqt6charts6-dev qml6-module-qtcharts
```

Expected: dependencies are installed. If `src` is empty at this point, `rosdep install` may report that there are no packages; that is acceptable before package creation.

- [ ] **Step 4: Create the package with ROS 2 tooling**

Run:

```bash
source ~/.local/ros2_rc && rr && ros2 pkg create \
  --build-type ament_cmake \
  --dependencies rclcpp \
  --destination-directory /home/nros/Documents/Woosh/ros2_recorder_ws/src \
  --node-name data_recorder \
  data_recorder
```

Expected: `src/data_recorder` exists with generated `package.xml`, `CMakeLists.txt`, and `src/data_recorder.cpp`.

- [ ] **Step 5: Replace `package.xml` content**

Edit `src/data_recorder/package.xml` to:

```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>data_recorder</name>
  <version>0.1.0</version>
  <description>Qt/QML UI prototype for ROS 2 data recording workflows.</description>
  <maintainer email="nros@example.com">nros</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>ament_index_cpp</depend>
  <depend>yaml-cpp</depend>
  <depend>qt6-base-dev</depend>
  <depend>qt6-declarative-dev</depend>
  <exec_depend>qml6-module-qtcharts</exec_depend>

  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 6: Replace `CMakeLists.txt` with Qt-aware build**

Edit `src/data_recorder/CMakeLists.txt` to:

```cmake
cmake_minimum_required(VERSION 3.16)
project(data_recorder)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Charts)

add_executable(data_recorder src/data_recorder.cpp)
target_link_libraries(data_recorder
  yaml-cpp
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Charts
)
ament_target_dependencies(data_recorder rclcpp ament_index_cpp)

install(TARGETS data_recorder
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

ament_package()
```

- [ ] **Step 7: Create README**

Create `src/data_recorder/README.md`:

```markdown
# data_recorder

Qt 6/QML UI prototype for a ROS 2 data recorder.

## Dependencies

Use rosdep first:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws
rosdep install --from-paths src --ignore-src -r -y
```

QtCharts development files need an apt fallback on this system:

```bash
sudo apt install libqt6charts6-dev qml6-module-qtcharts
```

## Build

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache
```

## Run

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \
  --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/docs/reference/example_config.yaml
```

Running without `config_file` prints usage help and exits with a non-zero status.

## UI Verification

The app requires a graphical desktop session. Check `DISPLAY` or `WAYLAND_DISPLAY` before launching.

Manual checks:

- Resize splitters.
- Toggle Record/Stop.
- Select recording tags.
- Select event markers.
- Toggle topic visibility.
- Click the timeline to move the playhead.
```

- [ ] **Step 8: Build to expose missing dependencies early**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: the generated package builds far enough to prove Qt 6, QtCharts, yaml-cpp, and ROS dependencies are discoverable.

- [ ] **Checkpoint**

Record in the implementation notes that the package was created using `ros2 pkg create`, not by hand. If the workspace is still not a git repo, do not run git commit.

---

## Task 2: Implement YAML Config Parser With Tests

**Files:**
- Create: `src/data_recorder/include/data_recorder/config_model.hpp`
- Create: `src/data_recorder/src/config_model.cpp`
- Create: `src/data_recorder/test/test_config_model.cpp`

- [ ] **Step 1: Write parser header**

Create `src/data_recorder/include/data_recorder/config_model.hpp`:

```cpp
#pragma once

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
  std::vector<TopicEntry> camera_topics;
  std::vector<TopicEntry> track_topics;
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
```

- [ ] **Step 2: Add missing YAML include to header**

Add this include at the top of `config_model.hpp`, below `#pragma once`:

```cpp
#include <yaml-cpp/yaml.h>
```

- [ ] **Step 3: Extend CMake for core library and parser test**

Replace `src/data_recorder/CMakeLists.txt` with:

```cmake
cmake_minimum_required(VERSION 3.16)
project(data_recorder)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_gtest REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Charts)

set(DATA_RECORDER_HEADERS
  include/data_recorder/config_model.hpp
)

set(DATA_RECORDER_SOURCES
  src/config_model.cpp
)

add_library(data_recorder_core
  ${DATA_RECORDER_HEADERS}
  ${DATA_RECORDER_SOURCES}
)
target_include_directories(data_recorder_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(data_recorder_core
  yaml-cpp
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Charts
)
ament_target_dependencies(data_recorder_core rclcpp ament_index_cpp)

add_executable(data_recorder src/data_recorder.cpp)
target_link_libraries(data_recorder data_recorder_core)
ament_target_dependencies(data_recorder rclcpp ament_index_cpp)

install(TARGETS data_recorder data_recorder_core
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/
  DESTINATION include
)

if(BUILD_TESTING)
  ament_add_gtest(test_config_model test/test_config_model.cpp)
  target_link_libraries(test_config_model data_recorder_core)
endif()

ament_package()
```

- [ ] **Step 4: Write stub parser source for the failing test**

Create `src/data_recorder/src/config_model.cpp`:

```cpp
#include "data_recorder/config_model.hpp"

namespace data_recorder
{

ConfigData ConfigModel::load_from_file(const std::string &) const
{
  throw ConfigError("parser stub always fails");
}

bool ConfigModel::is_camera_topic(const std::string &, const std::string &)
{
  return false;
}

std::string ConfigModel::scalar_to_string(const YAML::Node &)
{
  return {};
}

}  // namespace data_recorder
```

- [ ] **Step 5: Write failing parser tests**

Create `src/data_recorder/test/test_config_model.cpp`:

```cpp
#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "data_recorder/config_model.hpp"

namespace
{

std::string write_temp_config(const std::string & content)
{
  const auto path = std::string("/tmp/data_recorder_config_test.yaml");
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
  ASSERT_EQ(config.track_topics.size(), 2u);
  ASSERT_EQ(config.camera_topics.size(), 1u);
  EXPECT_EQ(config.camera_topics[0].topic_name, "/camera/image_raw");
  EXPECT_EQ(config.camera_topics[0].backend_name, "video");
  EXPECT_EQ(config.camera_topics[0].params.at("codec"), "libx264");
  EXPECT_EQ(config.camera_topics[0].params.at("crf"), "23");
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

TEST(ConfigModel, ThrowsWhenFileMissing)
{
  const data_recorder::ConfigModel model;
  EXPECT_THROW(model.load_from_file("/tmp/does-not-exist-data-recorder.yaml"), data_recorder::ConfigError);
}
```

- [ ] **Step 6: Run tests and verify failure**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder \
  --ctest-args -R test_config_model --output-on-failure
```

Expected: `test_config_model` runs and fails because the parser stub always throws.

- [ ] **Step 7: Implement parser**

Create `src/data_recorder/src/config_model.cpp`:

```cpp
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

  if (!root["groups"] || !root["groups"].IsSequence()) {
    throw ConfigError("config must contain a 'groups' sequence");
  }

  ConfigData config;
  config.config_path = path;
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

    std::map<std::string, std::string> params;
    if (group_node["params"] && group_node["params"].IsMap()) {
      for (const auto & param_node : group_node["params"]) {
        params[param_node.first.as<std::string>()] = scalar_to_string(param_node.second);
      }
    }

    for (const auto & topic_node : group_node["topics"]) {
      TopicEntry topic;
      topic.topic_name = topic_node.as<std::string>();
      topic.backend_name = backend_name;
      topic.group_index = group_index;
      topic.params = params;
      topic.ui_category = is_camera_topic(topic.topic_name, topic.backend_name) ?
        TopicUiCategory::CameraPreview : TopicUiCategory::NumericTrack;

      config.topics.push_back(topic);
      if (topic.ui_category == TopicUiCategory::CameraPreview) {
        config.camera_topics.push_back(topic);
      } else {
        config.track_topics.push_back(topic);
      }
    }
    ++group_index;
  }

  if (config.topics.empty()) {
    throw ConfigError("config must contain at least one topic");
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
```

- [ ] **Step 8: Run parser tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder \
  --ctest-args -R test_config_model --output-on-failure
```

Expected: `test_config_model` passes.

- [ ] **Checkpoint**

If git is unavailable, note that parser tests pass and continue.

---

## Task 3: Implement Qt Models And Controller With Tests

**Files:**
- Create: `src/data_recorder/include/data_recorder/ui_models.hpp`
- Create: `src/data_recorder/src/ui_models.cpp`
- Create: `src/data_recorder/include/data_recorder/app_controller.hpp`
- Create: `src/data_recorder/src/app_controller.cpp`
- Create: `src/data_recorder/test/test_ui_models.cpp`

- [ ] **Step 1: Write UI model header**

Create `src/data_recorder/include/data_recorder/ui_models.hpp`:

```cpp
#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QPointF>

#include <vector>

#include "data_recorder/config_model.hpp"

namespace data_recorder
{

class TopicListModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Role {
    TopicNameRole = Qt::UserRole + 1,
    BackendNameRole,
    CategoryRole,
    IsVisibleRole,
    FrequencyTextRole,
    ColorRole,
    SeriesRole,
  };

  explicit TopicListModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex & index) const override;
  QHash<int, QByteArray> roleNames() const override;

  void set_topics(const std::vector<TopicEntry> & topics);
  Q_INVOKABLE void toggleVisible(int row);

private:
  struct Row {
    TopicEntry topic;
    bool is_visible{true};
    QString frequency_text;
    QString color;
    QVariantList series;
  };

  std::vector<Row> rows_;
};

class TagListModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Role {
    NameRole = Qt::UserRole + 1,
    ColorRole,
    IsSelectedRole,
  };

  explicit TagListModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole) override;
  QHash<int, QByteArray> roleNames() const override;

  void set_tags(const std::vector<TagEntry> & tags);
  Q_INVOKABLE void select(int row);

private:
  std::vector<TagEntry> rows_;
  int selected_row_{-1};
};

class EventMarkerModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Role {
    ShortcutRole = Qt::UserRole + 1,
    NameRole,
    KindRole,
    ColorRole,
    IsSelectedRole,
  };

  explicit EventMarkerModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void set_markers(const std::vector<EventMarkerEntry> & markers);
  Q_INVOKABLE void select(int row);

private:
  std::vector<EventMarkerEntry> rows_;
  int selected_row_{-1};
};

class RecordingSessionModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Role {
    NameRole = Qt::UserRole + 1,
    SizeRole,
    DurationRole,
  };

  explicit RecordingSessionModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

private:
  struct Row {
    QString name;
    QString size;
    QString duration;
  };
  std::vector<Row> rows_;
};

}  // namespace data_recorder
```

- [ ] **Step 2: Write model tests**

Create `src/data_recorder/test/test_ui_models.cpp`:

```cpp
#include <gtest/gtest.h>

#include "data_recorder/ui_models.hpp"

TEST(TopicListModel, ExposesTopicRoles)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";
  topic.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  ASSERT_EQ(model.rowCount(), 1);
  const auto index = model.index(0, 0);
  EXPECT_EQ(model.data(index, data_recorder::TopicListModel::TopicNameRole).toString().toStdString(), "/joint_states");
  EXPECT_EQ(model.data(index, data_recorder::TopicListModel::BackendNameRole).toString().toStdString(), "rosbag");
  EXPECT_EQ(model.data(index, data_recorder::TopicListModel::CategoryRole).toString().toStdString(), "numeric");
  EXPECT_TRUE(model.data(index, data_recorder::TopicListModel::IsVisibleRole).toBool());
  EXPECT_FALSE(model.data(index, data_recorder::TopicListModel::SeriesRole).toList().isEmpty());
}

TEST(TopicListModel, TogglesVisibility)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/camera/image_raw";
  topic.backend_name = "video";
  topic.ui_category = data_recorder::TopicUiCategory::CameraPreview;

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  model.toggleVisible(0);
  EXPECT_FALSE(model.data(model.index(0, 0), data_recorder::TopicListModel::IsVisibleRole).toBool());
}

TEST(TagListModel, SelectsOneTag)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}});

  model.select(1);

  EXPECT_FALSE(model.data(model.index(0, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
  EXPECT_TRUE(model.data(model.index(1, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
}

TEST(EventMarkerModel, ExposesShortcutAndKind)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({{"1", "拿起水杯", "point", "#1763c9"}});

  ASSERT_EQ(model.rowCount(), 1);
  EXPECT_EQ(model.data(model.index(0, 0), data_recorder::EventMarkerModel::ShortcutRole).toString().toStdString(), "1");
  EXPECT_EQ(model.data(model.index(0, 0), data_recorder::EventMarkerModel::KindRole).toString().toStdString(), "point");
}
```

- [ ] **Step 3: Extend CMake for UI model sources and tests**

In `src/data_recorder/CMakeLists.txt`, update the header/source lists:

```cmake
set(DATA_RECORDER_HEADERS
  include/data_recorder/app_controller.hpp
  include/data_recorder/config_model.hpp
  include/data_recorder/ui_models.hpp
)

set(DATA_RECORDER_SOURCES
  src/app_controller.cpp
  src/config_model.cpp
  src/ui_models.cpp
)
```

Also update the `BUILD_TESTING` block:

```cmake
if(BUILD_TESTING)
  ament_add_gtest(test_config_model test/test_config_model.cpp)
  target_link_libraries(test_config_model data_recorder_core)

  ament_add_gtest(test_ui_models test/test_ui_models.cpp)
  target_link_libraries(test_ui_models data_recorder_core)
endif()
```

- [ ] **Step 4: Create stub files for failing UI model test**

Create `src/data_recorder/src/ui_models.cpp`:

```cpp
#include "data_recorder/ui_models.hpp"

namespace data_recorder
{

TopicListModel::TopicListModel(QObject * parent) : QAbstractListModel(parent) {}
int TopicListModel::rowCount(const QModelIndex &) const { return 0; }
QVariant TopicListModel::data(const QModelIndex &, int) const { return {}; }
bool TopicListModel::setData(const QModelIndex &, const QVariant &, int) { return false; }
Qt::ItemFlags TopicListModel::flags(const QModelIndex & index) const { return QAbstractListModel::flags(index); }
QHash<int, QByteArray> TopicListModel::roleNames() const { return {}; }
void TopicListModel::set_topics(const std::vector<TopicEntry> &) {}
void TopicListModel::toggleVisible(int) {}

TagListModel::TagListModel(QObject * parent) : QAbstractListModel(parent) {}
int TagListModel::rowCount(const QModelIndex &) const { return 0; }
QVariant TagListModel::data(const QModelIndex &, int) const { return {}; }
bool TagListModel::setData(const QModelIndex &, const QVariant &, int) { return false; }
QHash<int, QByteArray> TagListModel::roleNames() const { return {}; }
void TagListModel::set_tags(const std::vector<TagEntry> &) {}
void TagListModel::select(int) {}

EventMarkerModel::EventMarkerModel(QObject * parent) : QAbstractListModel(parent) {}
int EventMarkerModel::rowCount(const QModelIndex &) const { return 0; }
QVariant EventMarkerModel::data(const QModelIndex &, int) const { return {}; }
QHash<int, QByteArray> EventMarkerModel::roleNames() const { return {}; }
void EventMarkerModel::set_markers(const std::vector<EventMarkerEntry> &) {}
void EventMarkerModel::select(int) {}

RecordingSessionModel::RecordingSessionModel(QObject * parent) : QAbstractListModel(parent) {}
int RecordingSessionModel::rowCount(const QModelIndex &) const { return 0; }
QVariant RecordingSessionModel::data(const QModelIndex &, int) const { return {}; }
QHash<int, QByteArray> RecordingSessionModel::roleNames() const { return {}; }

}  // namespace data_recorder
```

Create `src/data_recorder/include/data_recorder/app_controller.hpp`:

```cpp
#pragma once

#include <QObject>

namespace data_recorder
{

class AppController : public QObject
{
  Q_OBJECT

public:
  explicit AppController(QObject * parent = nullptr) : QObject(parent) {}
};

}  // namespace data_recorder
```

Create `src/data_recorder/src/app_controller.cpp`:

```cpp
#include "data_recorder/app_controller.hpp"
```

- [ ] **Step 5: Run tests and verify failure**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder \
  --ctest-args -R test_ui_models --output-on-failure
```

Expected: `test_ui_models` runs and fails because the stub model returns no rows.

- [ ] **Step 6: Implement UI models**

Create `src/data_recorder/src/ui_models.cpp`:

```cpp
#include "data_recorder/ui_models.hpp"

#include <cmath>

namespace data_recorder
{

namespace
{

QString category_name(TopicUiCategory category)
{
  return category == TopicUiCategory::CameraPreview ? "camera" : "numeric";
}

QVariantList make_series(int row)
{
  QVariantList values;
  for (int i = 0; i < 80; ++i) {
    const double x = static_cast<double>(i);
    const double y = std::sin((x / 10.0) + static_cast<double>(row)) * 0.35 + 0.5 + row * 0.04;
    QVariantMap point;
    point["x"] = x;
    point["y"] = y;
    values.push_back(point);
  }
  return values;
}

QString color_for_row(int row)
{
  static const QStringList colors = {
    "#2563eb", "#16a34a", "#dc2626", "#9333ea", "#0891b2", "#ca8a04"
  };
  return colors[row % colors.size()];
}

}  // namespace

TopicListModel::TopicListModel(QObject * parent)
: QAbstractListModel(parent)
{
}

int TopicListModel::rowCount(const QModelIndex & parent) const
{
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant TopicListModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }
  const auto & row = rows_[static_cast<size_t>(index.row())];
  switch (role) {
    case TopicNameRole:
      return QString::fromStdString(row.topic.topic_name);
    case BackendNameRole:
      return QString::fromStdString(row.topic.backend_name);
    case CategoryRole:
      return category_name(row.topic.ui_category);
    case IsVisibleRole:
      return row.is_visible;
    case FrequencyTextRole:
      return row.frequency_text;
    case ColorRole:
      return row.color;
    case SeriesRole:
      return row.series;
    default:
      return {};
  }
}

bool TopicListModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return false;
  }
  auto & row = rows_[static_cast<size_t>(index.row())];
  if (role == IsVisibleRole) {
    row.is_visible = value.toBool();
    emit dataChanged(index, index, {IsVisibleRole});
    return true;
  }
  return false;
}

Qt::ItemFlags TopicListModel::flags(const QModelIndex & index) const
{
  auto flags = QAbstractListModel::flags(index);
  if (index.isValid()) {
    flags |= Qt::ItemIsEditable;
  }
  return flags;
}

QHash<int, QByteArray> TopicListModel::roleNames() const
{
  return {
    {TopicNameRole, "topicName"},
    {BackendNameRole, "backendName"},
    {CategoryRole, "category"},
    {IsVisibleRole, "isVisible"},
    {FrequencyTextRole, "frequencyText"},
    {ColorRole, "seriesColor"},
    {SeriesRole, "series"},
  };
}

void TopicListModel::set_topics(const std::vector<TopicEntry> & topics)
{
  beginResetModel();
  rows_.clear();
  rows_.reserve(topics.size());
  for (size_t i = 0; i < topics.size(); ++i) {
    Row row;
    row.topic = topics[i];
    row.frequency_text = row.topic.ui_category == TopicUiCategory::CameraPreview ?
      QString("%1 fps").arg(18 + static_cast<int>(i) % 5) :
      QString("%1 Hz").arg(20 + static_cast<int>(i) * 7);
    row.color = color_for_row(static_cast<int>(i));
    row.series = make_series(static_cast<int>(i));
    rows_.push_back(row);
  }
  endResetModel();
}

void TopicListModel::toggleVisible(int row)
{
  const auto index = this->index(row, 0);
  if (!index.isValid()) {
    return;
  }
  setData(index, !data(index, IsVisibleRole).toBool(), IsVisibleRole);
}

TagListModel::TagListModel(QObject * parent)
: QAbstractListModel(parent)
{
}

int TagListModel::rowCount(const QModelIndex & parent) const
{
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant TagListModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }
  const auto & row = rows_[static_cast<size_t>(index.row())];
  switch (role) {
    case NameRole:
      return QString::fromStdString(row.name);
    case ColorRole:
      return QString::fromStdString(row.color);
    case IsSelectedRole:
      return index.row() == selected_row_;
    default:
      return {};
  }
}

bool TagListModel::setData(const QModelIndex & index, const QVariant &, int role)
{
  if (role == IsSelectedRole && index.isValid()) {
    select(index.row());
    return true;
  }
  return false;
}

QHash<int, QByteArray> TagListModel::roleNames() const
{
  return {{NameRole, "name"}, {ColorRole, "color"}, {IsSelectedRole, "isSelected"}};
}

void TagListModel::set_tags(const std::vector<TagEntry> & tags)
{
  beginResetModel();
  rows_ = tags;
  selected_row_ = -1;
  endResetModel();
}

void TagListModel::select(int row)
{
  if (row < 0 || row >= rowCount()) {
    return;
  }
  const int old_row = selected_row_;
  selected_row_ = row;
  if (old_row >= 0) {
    emit dataChanged(index(old_row, 0), index(old_row, 0), {IsSelectedRole});
  }
  emit dataChanged(index(row, 0), index(row, 0), {IsSelectedRole});
}

EventMarkerModel::EventMarkerModel(QObject * parent)
: QAbstractListModel(parent)
{
}

int EventMarkerModel::rowCount(const QModelIndex & parent) const
{
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant EventMarkerModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }
  const auto & row = rows_[static_cast<size_t>(index.row())];
  switch (role) {
    case ShortcutRole:
      return QString::fromStdString(row.shortcut);
    case NameRole:
      return QString::fromStdString(row.name);
    case KindRole:
      return QString::fromStdString(row.kind);
    case ColorRole:
      return QString::fromStdString(row.color);
    case IsSelectedRole:
      return index.row() == selected_row_;
    default:
      return {};
  }
}

QHash<int, QByteArray> EventMarkerModel::roleNames() const
{
  return {
    {ShortcutRole, "shortcut"},
    {NameRole, "name"},
    {KindRole, "kind"},
    {ColorRole, "color"},
    {IsSelectedRole, "isSelected"},
  };
}

void EventMarkerModel::set_markers(const std::vector<EventMarkerEntry> & markers)
{
  beginResetModel();
  rows_ = markers;
  selected_row_ = -1;
  endResetModel();
}

void EventMarkerModel::select(int row)
{
  if (row < 0 || row >= rowCount()) {
    return;
  }
  const int old_row = selected_row_;
  selected_row_ = row;
  if (old_row >= 0) {
    emit dataChanged(index(old_row, 0), index(old_row, 0), {IsSelectedRole});
  }
  emit dataChanged(index(row, 0), index(row, 0), {IsSelectedRole});
}

RecordingSessionModel::RecordingSessionModel(QObject * parent)
: QAbstractListModel(parent),
  rows_({
    {"示例采集 001", "7.8 MB", "0:07"},
    {"示例采集 002", "1.0 MB", "0:06"},
    {"示例采集 003", "1.0 MB", "0:06"},
  })
{
}

int RecordingSessionModel::rowCount(const QModelIndex & parent) const
{
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant RecordingSessionModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
    return {};
  }
  const auto & row = rows_[static_cast<size_t>(index.row())];
  switch (role) {
    case NameRole:
      return row.name;
    case SizeRole:
      return row.size;
    case DurationRole:
      return row.duration;
    default:
      return {};
  }
}

QHash<int, QByteArray> RecordingSessionModel::roleNames() const
{
  return {{NameRole, "name"}, {SizeRole, "size"}, {DurationRole, "duration"}};
}

}  // namespace data_recorder
```

- [ ] **Step 7: Replace stub controller header**

Create `src/data_recorder/include/data_recorder/app_controller.hpp`:

```cpp
#pragma once

#include <QObject>
#include <QString>

#include "data_recorder/config_model.hpp"
#include "data_recorder/ui_models.hpp"

namespace data_recorder
{

class AppController : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString configPath READ configPath CONSTANT)
  Q_PROPERTY(QString outputDirectory READ outputDirectory CONSTANT)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
  Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
  Q_PROPERTY(double playheadSeconds READ playheadSeconds NOTIFY playheadSecondsChanged)
  Q_PROPERTY(TopicListModel * topicModel READ topicModel CONSTANT)
  Q_PROPERTY(TopicListModel * cameraModel READ cameraModel CONSTANT)
  Q_PROPERTY(TagListModel * tagModel READ tagModel CONSTANT)
  Q_PROPERTY(EventMarkerModel * eventMarkerModel READ eventMarkerModel CONSTANT)
  Q_PROPERTY(RecordingSessionModel * recordingSessionModel READ recordingSessionModel CONSTANT)

public:
  explicit AppController(const ConfigData & config, QObject * parent = nullptr);

  QString configPath() const;
  QString outputDirectory() const;
  QString statusText() const;
  bool recording() const;
  double playheadSeconds() const;

  TopicListModel * topicModel();
  TopicListModel * cameraModel();
  TagListModel * tagModel();
  EventMarkerModel * eventMarkerModel();
  RecordingSessionModel * recordingSessionModel();

  Q_INVOKABLE void toggleRecording();
  Q_INVOKABLE void setPlayheadSeconds(double seconds);

signals:
  void statusTextChanged();
  void recordingChanged();
  void playheadSecondsChanged();

private:
  QString config_path_;
  QString output_directory_;
  QString status_text_;
  bool recording_{false};
  double playhead_seconds_{0.0};
  TopicListModel topic_model_;
  TopicListModel camera_model_;
  TagListModel tag_model_;
  EventMarkerModel event_marker_model_;
  RecordingSessionModel recording_session_model_;
};

}  // namespace data_recorder
```

- [ ] **Step 8: Replace stub controller implementation**

Create `src/data_recorder/src/app_controller.cpp`:

```cpp
#include "data_recorder/app_controller.hpp"

namespace data_recorder
{

AppController::AppController(const ConfigData & config, QObject * parent)
: QObject(parent),
  config_path_(QString::fromStdString(config.config_path)),
  output_directory_(QString::fromStdString(config.output_dir)),
  status_text_("就绪")
{
  topic_model_.set_topics(config.topics);
  camera_model_.set_topics(config.camera_topics);
  tag_model_.set_tags(config.tags);
  event_marker_model_.set_markers(config.event_markers);
}

QString AppController::configPath() const { return config_path_; }
QString AppController::outputDirectory() const { return output_directory_; }
QString AppController::statusText() const { return status_text_; }
bool AppController::recording() const { return recording_; }
double AppController::playheadSeconds() const { return playhead_seconds_; }

TopicListModel * AppController::topicModel() { return &topic_model_; }
TopicListModel * AppController::cameraModel() { return &camera_model_; }
TagListModel * AppController::tagModel() { return &tag_model_; }
EventMarkerModel * AppController::eventMarkerModel() { return &event_marker_model_; }
RecordingSessionModel * AppController::recordingSessionModel() { return &recording_session_model_; }

void AppController::toggleRecording()
{
  recording_ = !recording_;
  status_text_ = recording_ ? "录制中（界面原型）" : "已停止";
  emit recordingChanged();
  emit statusTextChanged();
}

void AppController::setPlayheadSeconds(double seconds)
{
  if (seconds < 0.0) {
    seconds = 0.0;
  }
  if (qFuzzyCompare(playhead_seconds_, seconds)) {
    return;
  }
  playhead_seconds_ = seconds;
  emit playheadSecondsChanged();
}

}  // namespace data_recorder
```

- [ ] **Step 9: Run UI model tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder \
  --ctest-args -R test_ui_models --output-on-failure
```

Expected: `test_ui_models` passes.

- [ ] **Checkpoint**

Note that parser and model tests pass before starting QML.

---

## Task 4: Implement Executable Entry Point

**Files:**
- Modify: `src/data_recorder/src/data_recorder.cpp`

- [ ] **Step 1: Replace executable entry point**

Edit `src/data_recorder/src/data_recorder.cpp`:

```cpp
#include <QGuiApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <rclcpp/rclcpp.hpp>

#include "data_recorder/app_controller.hpp"
#include "data_recorder/config_model.hpp"

namespace
{

void print_usage(const char * program_name)
{
  std::cerr
    << "Usage:\n"
    << "  source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \\\n"
    << "    --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/docs/reference/example_config.yaml\n\n"
    << "Required ROS parameter:\n"
    << "  config_file: path to the YAML recorder configuration\n\n"
    << "Program: " << program_name << std::endl;
}

}  // namespace

int main(int argc, char ** argv)
{
  const auto non_ros_args = rclcpp::init_and_remove_ros_arguments(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("data_recorder_ui");
  node->declare_parameter<std::string>("config_file", "");
  const auto config_path = node->get_parameter("config_file").as_string();

  if (config_path.empty()) {
    print_usage(argv[0]);
    rclcpp::shutdown();
    return EXIT_FAILURE;
  }

  data_recorder::ConfigData config;
  try {
    config = data_recorder::ConfigModel().load_from_file(config_path);
  } catch (const data_recorder::ConfigError & error) {
    std::cerr << "Failed to load config: " << error.what() << "\n\n";
    print_usage(argv[0]);
    rclcpp::shutdown();
    return EXIT_FAILURE;
  }

  std::vector<QByteArray> qt_arg_storage;
  qt_arg_storage.reserve(non_ros_args.size());
  std::vector<char *> qt_argv;
  qt_argv.reserve(non_ros_args.size());
  for (const auto & arg : non_ros_args) {
    qt_arg_storage.push_back(QByteArray::fromStdString(arg));
    qt_argv.push_back(qt_arg_storage.back().data());
  }
  int qt_argc = static_cast<int>(qt_argv.size());
  QGuiApplication app(qt_argc, qt_argv.data());
  QQmlApplicationEngine engine;

  data_recorder::AppController controller(config);
  engine.rootContext()->setContextProperty("appController", &controller);
  const auto package_share = QString::fromStdString(
    ament_index_cpp::get_package_share_directory("data_recorder"));
  const auto qml_dir = package_share + QStringLiteral("/qml");
  engine.addImportPath(qml_dir);

  const QUrl main_qml = QUrl::fromLocalFile(qml_dir + QStringLiteral("/Main.qml"));
  QObject::connect(
    &engine,
    &QQmlApplicationEngine::objectCreationFailed,
    &app,
    []() { QCoreApplication::exit(EXIT_FAILURE); },
    Qt::QueuedConnection);
  engine.load(main_qml);

  const int result = app.exec();
  rclcpp::shutdown();
  return result;
}
```

- [ ] **Step 2: Run missing-config verification**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder

source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder
```

Expected: second command prints usage and exits non-zero. If QML files do not exist yet, the missing-config path should still pass because it exits before Qt starts.

- [ ] **Checkpoint**

Note the exact missing-config output in the implementation summary.

---

## Task 5: Add Terminology Doc And Base QML Components

**Files:**
- Create: `src/data_recorder/doc/ui_terminology.md`
- Create: `src/data_recorder/qml/components/Panel.qml`
- Create: `src/data_recorder/qml/components/AppHeader.qml`
- Create: `src/data_recorder/qml/components/StatusBar.qml`

- [ ] **Step 1: Create terminology doc**

Create `src/data_recorder/doc/ui_terminology.md`:

```markdown
# UI Terminology

| 中文名称 | English Name | Code Symbol | Purpose |
| --- | --- | --- | --- |
| 相机预览 | Camera Preview | `CameraPreview` | Image topic 的画面预览区域 |
| 相机预览区 | Camera Preview Area | `CameraPreviewArea` | 多路相机预览所在的上方面板组 |
| 采集记录 | Recording Sessions | `RecordingSessions` | 历史采集 session 列表 |
| 记录标签 | Recording Tags | `RecordingTags` | 成功、失败、碰撞等整段记录标签 |
| 事件标记 | Event Markers | `EventMarkers` | 快捷键触发的 point/range 时间标记 |
| 话题列表 | Topic List | `TopicList` | 配置中的 ROS topics 列表 |
| 话题轨道 | Topic Track | `TopicTrack` | 时间轴中每个 topic 对应的一行 |
| 时间轴 | Timeline | `Timeline` | 播放头、刻度、曲线、轨道所在区域 |
| 播放头 | Playhead | `Playhead` | 当前时间位置指示线 |
| 保存目录 | Output Directory | `OutputDirectory` | YAML 中的 `output_dir` |
| 后端 | Backend | `Backend` | `rosbag` / `video` 等录制后端 |
| 配置文件 | Config File | `ConfigFile` | 当前加载的 YAML |
| 工作区 | Workspace | `Workspace` | 整个可调整面板布局 |
| 面板 | Panel | `Panel` | 统一样式、可组合的 UI 模块 |
| 分隔条 | Splitter Handle | `SplitterHandle` | 可拖动调整面板大小的分隔控件 |

## Naming Rules

- QML component names use PascalCase, such as `TopicListPanel.qml`.
- C++ type names use PascalCase, such as `TopicListModel`.
- QML properties, Qt properties, and model roles use lowerCamelCase, such as `topicName`, `backendName`, and `isVisible`.
- UI labels use the Chinese names in this document.
- Developer-facing prose may use the English names in this document.
```

- [ ] **Step 2: Create `Panel.qml`**

Create `src/data_recorder/qml/components/Panel.qml`:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property alias title: titleLabel.text
    default property alias contentData: body.data
    property bool active: false

    color: active ? "#eef5ff" : "#f8fafc"
    border.color: active ? "#2563eb" : "#d5dce8"
    border.width: 1
    radius: 6

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: root.active ? "#dbeafe" : "#eef2f7"
            radius: 6

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Label {
                    id: titleLabel
                    Layout.fillWidth: true
                    color: "#162033"
                    font.pixelSize: 13
                    font.bold: true
                    elide: Text.ElideRight
                }
            }
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
        }
    }
}
```

- [ ] **Step 3: Create `AppHeader.qml`**

Create `src/data_recorder/qml/components/AppHeader.qml`:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    property var controller

    color: "#ffffff"
    border.color: "#d8dee9"
    implicitHeight: 56

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 14

        Label {
            text: "DataRecorder"
            font.pixelSize: 20
            font.bold: true
            color: "#111827"
        }

        Label {
            text: "ROS 2 HUMBLE"
            color: "#1d4ed8"
            font.pixelSize: 12
            padding: 5
            background: Rectangle {
                color: "#eff6ff"
                border.color: "#93c5fd"
                radius: 4
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.controller ? root.controller.configPath : ""
            elide: Text.ElideMiddle
            color: "#5b6472"
            font.pixelSize: 12
        }

        Label {
            text: root.controller ? root.controller.statusText : ""
            color: root.controller && root.controller.recording ? "#b91c1c" : "#475569"
            font.pixelSize: 13
        }

        Button {
            text: root.controller && root.controller.recording ? "停止" : "录制"
            highlighted: root.controller && root.controller.recording
            onClicked: root.controller.toggleRecording()
        }
    }
}
```

- [ ] **Step 4: Create `StatusBar.qml`**

Create `src/data_recorder/qml/components/StatusBar.qml`:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    property var controller

    color: "#ffffff"
    border.color: "#d8dee9"
    implicitHeight: 28

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 18

        Label {
            Layout.fillWidth: true
            text: "保存目录: " + (root.controller ? root.controller.outputDirectory : "")
            color: "#334155"
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }

        Label {
            text: "时间: " + (root.controller ? root.controller.playheadSeconds.toFixed(1) : "0.0") + "s"
            color: "#334155"
            font.pixelSize: 12
        }

        Label {
            text: "缩放 1.0x"
            color: "#64748b"
            font.pixelSize: 12
        }

        Label {
            text: "磁盘 512 GB / 754 GB"
            color: "#64748b"
            font.pixelSize: 12
        }
    }
}
```

- [ ] **Step 5: Run a build**

Before building, add these install rules to `src/data_recorder/CMakeLists.txt` after the existing
`install(DIRECTORY include/ ...)` rule:

```cmake
install(DIRECTORY qml
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY doc
  DESTINATION share/${PROJECT_NAME}
)
```

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: C++ build passes. QML is installed as files.

- [ ] **Checkpoint**

Record that terminology doc and base QML components exist.

---

## Task 6: Add QML Panels For Lists, Tags, Events, Preview, And Timeline

**Files:**
- Create: `src/data_recorder/qml/components/RecordingSessionsPanel.qml`
- Create: `src/data_recorder/qml/components/RecordingTagsPanel.qml`
- Create: `src/data_recorder/qml/components/EventMarkersPanel.qml`
- Create: `src/data_recorder/qml/components/CameraPreviewPanel.qml`
- Create: `src/data_recorder/qml/components/TopicListPanel.qml`
- Create: `src/data_recorder/qml/components/TopicTrack.qml`
- Create: `src/data_recorder/qml/components/TimelinePanel.qml`

- [ ] **Step 1: Create `RecordingSessionsPanel.qml`**

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root
    property var model
    title: "采集记录"

    ListView {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        model: root.model
        delegate: Rectangle {
            width: ListView.view.width
            height: 54
            color: "#ffffff"
            border.color: "#d9e1ee"
            radius: 5
            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4
                Label { text: name; color: "#0f172a"; font.bold: true; font.pixelSize: 12 }
                Label { text: size + " · " + duration; color: "#64748b"; font.pixelSize: 12 }
            }
        }
    }
}
```

- [ ] **Step 2: Create `RecordingTagsPanel.qml`**

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root
    property var model
    title: "记录标签"

    Flow {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        Repeater {
            model: root.model
            delegate: Button {
                text: model.name
                checkable: true
                checked: model.isSelected
                onClicked: root.model.select(index)
                background: Rectangle {
                    color: model.isSelected ? model.color : "#ffffff"
                    border.color: model.color
                    radius: 5
                }
                contentItem: Text {
                    text: parent.text
                    color: model.isSelected ? "#ffffff" : "#334155"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 12
                }
            }
        }
    }
}
```

- [ ] **Step 3: Create `EventMarkersPanel.qml`**

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root
    property var model
    title: "事件标记"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Repeater {
            model: root.model
            delegate: Button {
                Layout.preferredHeight: 34
                text: model.shortcut + "  " + model.name
                checkable: true
                checked: model.isSelected
                onClicked: root.model.select(index)
                background: Rectangle {
                    color: model.isSelected ? model.color : "#ffffff"
                    border.color: model.color
                    radius: 5
                }
                contentItem: Text {
                    text: parent.text
                    color: model.isSelected ? "#ffffff" : "#334155"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
        Item { Layout.fillWidth: true }
    }
}
```

- [ ] **Step 4: Create `CameraPreviewPanel.qml`**

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root
    property string topicName
    property string backendName
    property string frequencyText
    property string seriesColor
    property bool visibleState: true
    title: topicName

    Rectangle {
        anchors.fill: parent
        anchors.margins: 8
        color: visibleState ? "#111827" : "#334155"
        radius: 5

        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.fillStyle = "#1f2937"
                ctx.fillRect(0, 0, width, height)
                for (var y = 0; y < height; y += 18) {
                    ctx.strokeStyle = y % 36 === 0 ? "#475569" : "#334155"
                    ctx.beginPath()
                    ctx.moveTo(0, y)
                    ctx.lineTo(width, y)
                    ctx.stroke()
                }
                ctx.fillStyle = seriesColor || "#2563eb"
                ctx.fillRect(width * 0.18, height * 0.55, width * 0.18, height * 0.16)
                ctx.fillStyle = "#38bdf8"
                ctx.fillRect(width * 0.48, height * 0.38, width * 0.30, height * 0.20)
            }
        }

        Column {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 10
            spacing: 3
            Label { text: backendName; color: "#cbd5e1"; font.pixelSize: 11 }
            Label { text: frequencyText; color: "#e2e8f0"; font.pixelSize: 12; font.bold: true }
        }
    }
}
```

- [ ] **Step 5: Create `TopicListPanel.qml`**

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root
    property var model
    title: "话题列表"

    ListView {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        model: root.model
        delegate: Rectangle {
            width: ListView.view.width
            height: 42
            color: isVisible ? "#ffffff" : "#f1f5f9"
            border.color: "#d9e1ee"
            radius: 5

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 24
                    color: seriesColor
                    radius: 2
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label { text: topicName; color: "#0f172a"; font.pixelSize: 12; font.bold: true; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    Label { text: backendName + " · " + frequencyText; color: "#64748b"; font.pixelSize: 11; Layout.fillWidth: true }
                }
                Button {
                    Layout.preferredWidth: 34
                    text: isVisible ? "显" : "隐"
                    onClicked: root.model.toggleVisible(index)
                }
            }
        }
    }
}
```

- [ ] **Step 6: Create `TopicTrack.qml`**

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Rectangle {
    id: root
    property string topicName
    property string backendName
    property string frequencyText
    property string seriesColor
    property var series
    property bool visibleState: true

    color: visibleState ? "#ffffff" : "#f8fafc"
    border.color: "#e2e8f0"
    height: 76

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            Layout.margins: 8
            spacing: 2
            Label { text: topicName; color: "#0f172a"; font.bold: true; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true }
            Label { text: backendName; color: "#64748b"; font.pixelSize: 11 }
            Label { text: frequencyText; color: "#64748b"; font.pixelSize: 11 }
        }

        ChartView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            antialiasing: true
            legend.visible: false
            backgroundColor: "transparent"
            margins.left: 0
            margins.right: 0
            margins.top: 0
            margins.bottom: 0

            ValueAxis { id: axisX; min: 0; max: 79; labelsVisible: false; gridVisible: true }
            ValueAxis { id: axisY; min: 0; max: 1.2; labelsVisible: false; gridVisible: false }

            LineSeries {
                id: line
                axisX: axisX
                axisY: axisY
                color: root.seriesColor
                width: 2
            }

            Component.onCompleted: {
                line.clear()
                for (var i = 0; i < root.series.length; ++i) {
                    line.append(root.series[i].x, root.series[i].y)
                }
            }
        }
    }
}
```

- [ ] **Step 7: Create `TimelinePanel.qml`**

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root
    property var controller
    property var model
    title: "时间轴"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: "#f8fafc"
            border.color: "#e2e8f0"

            Repeater {
                model: 12
                delegate: Label {
                    x: index * parent.width / 12 + 4
                    y: 9
                    text: "0:" + (index * 5).toString().padStart(2, "0")
                    color: "#64748b"
                    font.pixelSize: 11
                }
            }

            Rectangle {
                width: 2
                height: parent.height
                x: Math.min(parent.width - width, root.controller.playheadSeconds / 60.0 * parent.width)
                color: "#2563eb"
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.controller.setPlayheadSeconds(mouse.x / width * 60.0)
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.model
            clip: true
            delegate: TopicTrack {
                width: ListView.view.width
                topicName: model.topicName
                backendName: model.backendName
                frequencyText: model.frequencyText
                seriesColor: model.seriesColor
                series: model.series
                visibleState: model.isVisible
            }
        }
    }
}
```

- [ ] **Step 8: Build after QML additions**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: build succeeds.

- [ ] **Checkpoint**

Note any QML import warnings that need to be resolved in Task 7.

---

## Task 7: Compose Main QML Layout With Resizable Panels

**Files:**
- Create: `src/data_recorder/qml/Main.qml`

- [ ] **Step 1: Create `Main.qml`**

Create `src/data_recorder/qml/Main.qml`:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

ApplicationWindow {
    id: window
    width: 1480
    height: 930
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: "DataRecorder"
    color: "#e9edf3"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppHeader {
            Layout.fillWidth: true
            controller: appController
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            Item {
                SplitView.preferredHeight: 280
                SplitView.minimumHeight: 180

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8
                    contentWidth: Math.max(cameraRow.implicitWidth, availableWidth)

                    RowLayout {
                        id: cameraRow
                        height: parent.height
                        spacing: 8

                        Repeater {
                            model: appController.cameraModel
                            delegate: CameraPreviewPanel {
                                Layout.preferredWidth: 470
                                Layout.minimumWidth: 320
                                Layout.fillHeight: true
                                topicName: model.topicName
                                backendName: model.backendName
                                frequencyText: model.frequencyText
                                seriesColor: model.seriesColor
                                visibleState: model.isVisible
                            }
                        }
                    }
                }
            }

            SplitView {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 320
                orientation: Qt.Horizontal

                SplitView {
                    SplitView.preferredWidth: 260
                    SplitView.minimumWidth: 220
                    SplitView.maximumWidth: 380
                    orientation: Qt.Vertical

                    RecordingSessionsPanel {
                        SplitView.preferredHeight: 280
                        SplitView.minimumHeight: 160
                        model: appController.recordingSessionModel
                    }

                    RecordingTagsPanel {
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 120
                        model: appController.tagModel
                    }
                }

                SplitView {
                    SplitView.fillWidth: true
                    orientation: Qt.Vertical

                    EventMarkersPanel {
                        SplitView.preferredHeight: 70
                        SplitView.minimumHeight: 56
                        SplitView.maximumHeight: 100
                        model: appController.eventMarkerModel
                    }

                    SplitView {
                        SplitView.fillHeight: true
                        orientation: Qt.Horizontal

                        TopicListPanel {
                            SplitView.preferredWidth: 300
                            SplitView.minimumWidth: 230
                            SplitView.maximumWidth: 420
                            model: appController.topicModel
                        }

                        TimelinePanel {
                            SplitView.fillWidth: true
                            controller: appController
                            model: appController.topicModel
                        }
                    }
                }
            }
        }

        StatusBar {
            Layout.fillWidth: true
            controller: appController
        }
    }
}
```

- [ ] **Step 2: Run QML startup with config**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder

source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \
  --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/docs/reference/example_config.yaml
```

Expected: a Qt window opens. If it fails, use the QML error line from stderr to fix imports, property names, or component paths.

- [ ] **Step 3: Confirm current desktop access**

Run:

```bash
printf 'DISPLAY=%s\nWAYLAND_DISPLAY=%s\n' "$DISPLAY" "$WAYLAND_DISPLAY"
xdotool search --name DataRecorder
```

Expected: `DISPLAY` or `WAYLAND_DISPLAY` is non-empty. `xdotool` finds the `DataRecorder` window when running under X11 with `DISPLAY=:0`.

- [ ] **Step 4: Manual UI verification**

With the app window open:

1. Drag the splitter between the Camera Preview Area and lower Workspace.
2. Drag the splitter between the left navigation column and the timeline column.
3. Click `录制`; verify the button changes to `停止` and status changes to recording.
4. Click `停止`; verify status changes back.
5. Click several Recording Tags; verify only the latest tag is highlighted.
6. Click several Event Markers; verify only the latest marker is highlighted.
7. Click topic visibility buttons in Topic List; verify visible/hidden state changes in the row.
8. Click the timeline ruler; verify the Playhead and status time update.

- [ ] **Checkpoint**

Record the UI launch command and any manual verification observations.

---

## Task 8: Final Build, Tests, And Documentation Pass

**Files:**
- Modify if needed: `src/data_recorder/README.md`
- Modify if needed: `docs/superpowers/specs/2026-06-16-data-recorder-ui-prototype-design.md`

- [ ] **Step 1: Run full package build**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: `data_recorder` builds successfully.

- [ ] **Step 2: Run full tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder \
  --event-handlers console_direct+

source ~/.local/ros2_rc && rr && colcon test-result \
  --test-result-base /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder \
  --verbose
```

Expected: all tests pass.

- [ ] **Step 3: Verify missing-config behavior**

Run:

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder
```

Expected: process exits non-zero and prints usage containing:

```text
--ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/docs/reference/example_config.yaml
```

- [ ] **Step 4: Verify UI launch behavior**

Run:

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \
  --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/docs/reference/example_config.yaml
```

Expected: UI opens and manual checks from Task 7 pass.

- [ ] **Step 5: Update README if verification differs**

If any command differs from the README, update `src/data_recorder/README.md` with the exact working command and observed caveat. Do not leave stale commands.

- [ ] **Step 6: Final checkpoint**

Since the workspace root is not a git repository, list changed files with:

```bash
find src/data_recorder docs/superpowers -maxdepth 4 -type f | sort
```

Expected: new package files, spec, and plan are listed. Include this in the final implementation summary.

---

## Self-Review Notes

- Spec coverage: package creation with `ros2 pkg create`, Qt6/QtCharts dependencies, ROS parameter-only config, no UI file-open/hot reload, terminology doc, modular panels, splitters, parser/model tests, missing-config behavior, and manual UI verification all have tasks.
- No implementation task introduces ROS subscriptions, rosbag writing, video encoding, or annotation persistence.
- Type consistency: parser structs feed `TopicListModel`, `TagListModel`, `EventMarkerModel`, and `AppController`; QML role names match `roleNames()` in `ui_models.cpp`.
- Git note: commit steps are replaced by checkpoints because `/home/nros/Documents/Woosh/ros2_recorder_ws` is not currently a git repository.
