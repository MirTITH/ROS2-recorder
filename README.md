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

### Backend dependencies

```bash
# rosbag2 mcap storage（默认存储优先 mcap）
sudo apt install ros-humble-rosbag2-storage-mcap
# libav（视频编码）
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
```

其余 ROS 依赖经 rosdep 安装。

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
  --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml
```

Running without `config_file` prints usage help and exits with a non-zero status.

## UI Verification

The app requires a graphical desktop session. Check `DISPLAY` or `WAYLAND_DISPLAY` before launching.

Manual checks:

- Resize splitters and verify hover handles become blue with resize cursors.
- Toggle Record/Stop with the button and Space key.
- Verify the camera grid has no scroll bars and does not crop previews.
- Hide camera topics from the Timeline and verify the camera area collapses when none are visible.
- Select recording tags and event marker buttons.
- Drag the Timeline playhead and verify it follows the mouse continuously.
- Wheel over Timeline information rows to scroll vertically.
- Wheel over Timeline curves to zoom horizontally.

## Backend Verification

The recording backend is exercised against the live `ROS_DOMAIN_ID=43` topics
(`/joint_states`, `/tf`, `/tf_static`, `/camera/image_raw` ×3). End-to-end
round-trip (record → info → play → ffprobe):

1. **Launch & live view.** Start the app with a temp `output_dir`. The camera
   grid shows real `/camera/image_raw` frames (not placeholder tiles) and the
   Timeline information rows show real Hz (joint_states ~400Hz) and the camera
   resolution 848x480.
2. **Record ~30s with annotations.** Toggle Record (Space), fire point markers
   (`1` 拿起水杯, `c` 碰撞 several times), a range marker (`2` 倒水, pressed
   twice for start/end), and select a tag chip (e.g. 成功); then Stop. The
   status bar switches between 录制中/实时查看 and a new session row appears in
   the session panel.
3. **Inspect artifacts.** Each session directory contains `rosbag/`
   (`metadata.yaml` + `.mcap`/`.db3`), `video/*.mp4` + `*.csv` (one pair per
   camera), and `session.yaml` (topics / tags / annotations; same-named
   annotations kept as multiple entries in ascending time order).
4. **rosbag readable.** `ros2 bag info <session>/rosbag` reports the storage id
   (mcap or sqlite3) and the recorded topics with sane type/count.
5. **Isolated playback.** `ros2 bag play <session>/rosbag` under a different
   `ROS_DOMAIN_ID` (e.g. 88) replays only the recorded topics — no leakage from
   the live domain — and `ros2 topic hz /joint_states` shows a rate.
6. **Video decodable.** `ffprobe` parses each mp4 (codec h264, 848x480) and the
   sidecar CSV row count (minus header) matches the mp4 frame count, with
   monotonically increasing PTS.
7. **Restart persistence.** Relaunching the app re-lists previously recorded
   sessions (`SessionManager::scan` runs at startup).
