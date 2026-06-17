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
