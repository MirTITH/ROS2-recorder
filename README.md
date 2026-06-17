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
