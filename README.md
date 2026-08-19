# Data Recorder

Data Recorder 是一个带图形界面的 ROS 2 数据录制工具。它可以实时查看话题和相机画面，将普通 ROS 2 话题录制为 rosbag、将图像话题录制为视频，并在录制过程中添加标签和时间标注。录制完成后，可以直接在界面中查看和回放历史数据。

![Data Recorder 界面](docs/screenshot.png)

## 使用方法

以下步骤以 Ubuntu 22.04 和 ROS 2 Humble 为例。开始前，请先安装 ROS 2 Humble，并安装 Git、rosdep 和 colcon：

```bash
sudo apt update
sudo apt install git python3-rosdep python3-colcon-common-extensions

# 本软件支持 db3 和 MCAP（推荐） 两种后端。如需使用 MCAP 后端，请执行：
sudo apt install ros-humble-rosbag2-storage-mcap
```

1. 创建 ROS 2 工作空间，并将本仓库克隆到工作空间的 `src` 目录：

   ```bash
   mkdir -p ~/ros2_recorder_ws/src
   cd ~/ros2_recorder_ws
   git clone https://github.com/MirTITH/ROS2-recorder.git src/data_recorder
   ```

2. 安装依赖：

   首次使用 rosdep 时，需要先初始化：

   ```bash
   sudo rosdep init
   rosdep update
   ```

   然后在工作空间根目录安装本项目依赖：

   ```bash
   cd ~/ros2_recorder_ws
   source /opt/ros/humble/setup.bash
   rosdep install --from-paths src --ignore-src -r -y
   ```

3. 构建工作空间：

   ```bash
   cd ~/ros2_recorder_ws
   source /opt/ros/humble/setup.bash
   colcon build --symlink-install
   ```

4. 编辑 [`config/example_config.yaml`](config/example_config.yaml)：

   - `output_dir`：录制数据的保存目录。
   - `tags`：可选择的会话标签。
   - `annotation_types`：时间标注及其快捷键；`point` 表示时间点，`range` 表示时间区间。
   - `groups`：需要录制的话题。使用 `rosbag` 后端录制普通话题，使用 `video` 后端将图像话题编码为视频。

5. 启动程序：

   ```bash
   cd ~/ros2_recorder_ws
   source /opt/ros/humble/setup.bash
   source install/setup.bash
   ros2 run data_recorder data_recorder \
     --ros-args -p config_file:="$HOME/ros2_recorder_ws/src/data_recorder/config/example_config.yaml"
   ```

6. 在界面中使用：

   - 点击“录制”开始采集，点击“停止”结束采集；也可以按空格键切换。
   - 录制时点击标签，为本次会话添加标签。
   - 点击时间标注按钮或按配置的快捷键添加标注。区间标注需要触发两次，分别设置起点和终点。
   - 在“数据”面板中选择历史会话，然后点击“播放”回放；点击“在线数据”返回实时视图。

每次录制都会在 `output_dir` 下生成一个独立的会话目录，其中包含 rosbag、视频及会话标注信息。

## 播放录制数据

`player` 可以按照录制时间轴重新发布会话中的 rosbag 消息和视频帧，使用方式接近 `ros2 bag play`。启动时通过 `session_dir` 指定包含 `session.yaml`、`rosbag/` 和 `video/` 的会话目录：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 run data_recorder player --ros-args \
  -p session_dir:=/path/to/recording/session
```

可用参数：

- `session_dir`：会话目录，必填。
- `rate`：播放倍率，默认 `1.0`。
- `loop`：播放结束后是否循环，默认 `false`。
- `start_paused`：是否以暂停状态启动，默认 `false`。
- `topic_prefix`：发布话题的前缀，默认空字符串（使用原话题名）。例如设为 `/replay` 时，`/joint_states` 会发布到 `/replay/joint_states`。
- `topics`：需要播放的原始话题名列表，默认为空（播放全部话题）；筛选在添加 `topic_prefix` 前进行。
- `publish_clock`：是否发布 `/clock`，默认 `false`。
- `image_frame_id`：视频帧发布为 `sensor_msgs/msg/Image` 时使用的 `frame_id`，默认空字符串。

播放器提供以下控制服务（默认节点名为 `player`）：

- `/player/pause`：暂停播放。
- `/player/resume`：继续播放。
- `/player/toggle_paused`：切换暂停状态。
- `/player/is_paused`：查询是否暂停。
- `/player/get_rate`：查询当前播放倍率。
- `/player/set_rate`：设置播放倍率。

例如：

```bash
ros2 service call /player/pause rosbag2_interfaces/srv/Pause "{}"
ros2 service call /player/set_rate rosbag2_interfaces/srv/SetRate "{rate: 2.0}"
```

当前版本暂不支持 `start_offset` 和 `seek`（跳转到指定播放位置）。
