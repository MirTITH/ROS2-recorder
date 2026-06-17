# Data Recorder Qt/QML UI Prototype Design

Date: 2026-06-16

## Context

This workspace is a ROS 2 workspace at `/home/nros/Documents/Woosh/ros2_recorder_ws`. The `src`
directory is currently empty. Reference material lives under `docs/reference/`, especially:

- `docs/reference/example_config.yaml`: the configuration shape for topics, backends, tags, and event markers.
- `docs/reference/example_ui.png`: a rough reference for a video-editor-style recording UI.

The first implementation intentionally focuses on the UI shell and a minimal C++ backend. It will not
subscribe to ROS topics, record rosbag files, encode videos, or write session data.

## Scope

Create a new ROS 2 `ament_cmake` package named `data_recorder`. The package must be created with
`ros2 pkg create`, following the workspace instructions:

```bash
source ~/.local/ros2_rc && rr && ros2 pkg create \
  --build-type ament_cmake \
  --dependencies rclcpp \
  --destination-directory /home/nros/Documents/Woosh/ros2_recorder_ws/src \
  --node-name data_recorder \
  data_recorder
```

The resulting application is a C++ Qt Quick/QML desktop program with QtCharts. It parses a YAML
configuration file, exposes the parsed data through Qt models, and renders a modular recording
workspace.

In scope:

- Load the YAML file specified by the ROS parameter `config_file`.
- Parse `output_dir`, `groups`, `topics`, `backend`, `params`, `tags`, and `annotation_types`.
- Render camera preview placeholders for image/video topics.
- Render topic tracks with QtCharts-based simulated curves for non-image topics.
- Render recording sessions, recording tags, event markers, topic list, timeline, status bar, and record-state controls.
- Use a consistent modular panel style across the UI.
- Allow panel resizing with mouse-draggable splitters.
- Add a terminology document at `src/data_recorder/doc/ui_terminology.md`.
- Print help and exit with an error if `config_file` is not provided.

Out of scope:

- ROS topic subscription.
- Runtime topic discovery.
- rosbag recording.
- video encoding.
- writing recordings, annotations, or metadata files.
- opening or hot-reloading configuration files from the UI.
- command-line `--config <path>` parsing.
- persistent layout storage.

## Launch And Missing-Config Behavior

The application only accepts the configuration path through the ROS parameter `config_file`.

Example launch command:

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \
  --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/docs/reference/example_config.yaml
```

If `config_file` is missing or empty, the executable prints help to stderr, includes the example
command above, and exits non-zero before creating the Qt window.

If the file path does not exist, the YAML is invalid, or required groups/topics cannot be parsed, the
program prints a readable error and exits non-zero. This keeps the first version strict and avoids a
misleading empty UI.

## Package Structure

Target structure after package creation:

```text
src/data_recorder/
  CMakeLists.txt
  package.xml
  doc/
    ui_terminology.md
  include/data_recorder/
    app_controller.hpp
    config_model.hpp
    ui_models.hpp
  src/
    app_controller.cpp
    config_model.cpp
    data_recorder.cpp
    ui_models.cpp
  qml/
    Main.qml
    components/
      AppHeader.qml
      CameraPreviewPanel.qml
      EventMarkersPanel.qml
      Panel.qml
      RecordingSessionsPanel.qml
      RecordingTagsPanel.qml
      StatusBar.qml
      TimelinePanel.qml
      TopicListPanel.qml
      TopicTrack.qml
```

`data_recorder.cpp` is the executable entry point generated from the `ros2 pkg create` node. It
initializes `rclcpp`, reads the `config_file` parameter, initializes Qt, creates the C++ controller and
models, and loads `qml/Main.qml`.

## C++ Components

`ConfigModel` parses YAML into plain data structures:

- `ConfigData`: output directory, topic entries, tag entries, event marker entries.
- `TopicEntry`: topic name, backend name, group index, params, and inferred UI category.
- `TagEntry`: display name and color.
- `EventMarkerEntry`: shortcut key, display name, kind (`point` or `range`), and color.

`UiModels` provides `QAbstractListModel` classes for QML:

- `TopicListModel`
- `CameraPreviewModel`
- `RecordingTagModel`
- `EventMarkerModel`
- `RecordingSessionModel`

`AppController` exposes application state to QML:

- current config path.
- output directory.
- record-state flag for UI-only recording state.
- current playhead time.
- selected tag and selected event marker.
- user-visible status text.

No real recorder or ROS subscriber object is introduced in this prototype. Future recorder boundaries
can be added behind `AppController` without changing the UI terminology or layout structure.

## Configuration Mapping

The YAML structure follows `docs/reference/example_config.yaml`.

Rules:

- `groups[].backend` defaults to `rosbag` when omitted.
- `groups[].topics` must contain one or more topic names.
- `groups[].params` is preserved as displayable key/value strings.
- A topic is shown in Camera Preview when its backend is `video` or its topic name contains `image`.
- Other topics are shown as topic tracks with simulated QtCharts curves.
- `tags` become Recording Tags.
- `annotation_types` become Event Markers.

The image-topic classification is a prototype heuristic. It will later be replaced by ROS type
discovery when live subscriptions are added.

## UI Layout

The UI keeps the broad idea of the reference image but does not copy its visual styling.

Main regions:

- App Header: app name, config file name, recording status, and a Record/Stop toggle.
- Camera Preview Area: resizable panel group containing one preview panel per image/video topic.
- Workspace: the main lower region, split into navigation and timeline areas.
- Navigation column: Recording Sessions and Recording Tags panels.
- Timeline column: Event Markers toolbar, Topic List, Timeline, and Topic Tracks.
- Status Bar: output directory, simulated time, zoom, and placeholder storage information.

The UI does not include an "open config file" button in this version.

## Panel System

All major UI regions use a shared `Panel.qml` component. The panel style defines:

- consistent background.
- consistent border.
- consistent title bar.
- consistent toolbar area.
- consistent padding and spacing.
- consistent selected, disabled, warning, and recording states.

The layout uses QML `SplitView` so users can adjust panel sizes with the mouse. At minimum:

- vertical split between Camera Preview Area and Workspace.
- horizontal split between Navigation column and Timeline column.
- internal horizontal or grid layout for multiple camera previews.

Splitter positions do not need to persist across launches.

## UI Terminology

The package must include `src/data_recorder/doc/ui_terminology.md`. Its initial content should define
the names used in UI text, design discussions, QML components, and C++ symbols:

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

Naming rules:

- QML component names use PascalCase, such as `TopicListPanel.qml`.
- C++ type names use PascalCase, such as `TopicListModel`.
- QML properties, C++ members exposed as Qt properties, and model roles use lowerCamelCase, such as
  `topicName`, `backendName`, and `isVisible`.
- UI labels use the Chinese names above.
- Code comments and developer-facing docs may use the English names above.

## Dependencies

The prototype uses Qt 6, not Qt 5. This workspace already has Qt 6.2.4, `qt6-base-dev`, and
`qt6-declarative-dev` available, and the application is an independent Qt/QML ROS 2 executable rather
than an rqt plugin. Qt 5 remains relevant for some ROS GUI packages, but it is not the recommended
choice for this new QML-first application.

Preferred dependency management uses rosdep where rules exist.

Expected package dependencies:

- `rclcpp`
- `yaml-cpp`
- Qt6 Core
- Qt6 Quick
- Qt6 QML
- Qt6 Charts

Verified dependency notes for this system:

- rosdep resolves `qt6-base-dev` to apt package `qt6-base-dev`.
- rosdep resolves `qt6-declarative-dev` to apt package `qt6-declarative-dev`.
- rosdep resolves `qml6-module-qtcharts` to apt package `qml6-module-qtcharts`.
- rosdep does not have a rule for `libqt6charts6-dev`, so install it with apt when needed.

Manual apt fallback for QtCharts development files:

```bash
sudo apt install libqt6charts6-dev qml6-module-qtcharts
```

Document this fallback in `src/data_recorder/README.md` during implementation.

## Testing And Verification

Build verification:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache
```

Missing-config verification:

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder
```

Expected result: help text is printed with an example `config_file` command, and the process exits
non-zero without opening a window.

UI launch verification:

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \
  --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/docs/reference/example_config.yaml
```

Expected result: a Qt window opens with:

- three camera preview panels from `/camera/image_raw`, `/right_camera/image_raw`, and `/left_camera/image_raw`.
- topic rows for rosbag and video topics.
- Recording Tags from YAML `tags`.
- Event Markers from YAML `annotation_types`.
- QtCharts curves on non-image topic tracks.
- a visible playhead and timeline ruler.
- a consistent panel style across all regions.

Manual UI operation checks:

- Drag splitters between Camera Preview Area, Navigation column, and Timeline column.
- Toggle Record/Stop and confirm the header/status state changes.
- Click Recording Tags and confirm selection state changes.
- Click Event Markers and confirm selection state changes.
- Toggle topic visibility and confirm the row/preview state changes.
- Click or drag the timeline and confirm the playhead moves.

Implementation should also verify how the UI can be viewed in the current desktop environment. If
`DISPLAY` or `WAYLAND_DISPLAY` is available, launch the app normally and inspect the window directly.
If no graphical session is available, run an offscreen smoke check only for startup and QML loading,
then document that interactive verification requires a graphical desktop session.

## Future Extension Boundaries

The prototype should leave clear extension points:

- ROS subscriptions can populate the existing camera preview and topic track models.
- rosbag and video recording backends can attach behind `AppController`.
- Image messages can later be converted to `QImage` and bound to `CameraPreview`.
- Numeric extraction can later replace simulated chart series.
- Recording annotations can later be written to `annotations.yaml`.

These extensions are intentionally excluded from the first implementation.
