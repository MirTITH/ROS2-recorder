# Agent 备注

## 版本号规则

版本号格式为 `主版本号.次版本号.修订号`，对应 `package.xml` 的 `<version>`，每次录制时写入该 session 的 `session.yaml` 的 `version` 字段。

判定标准只看**读取/回放路径**能否处理旧版本录制的 session（`session.yaml` + `rosbag` + `video`），不要求新旧版本写出的文件格式一致：

- **修订号 +1**：只修 bug、优化性能，未新增/删除功能，session 文件 schema 不变。
- **次版本号 +1**：新增/删除了功能，但新版本仍能正常读取/回放旧版本录制的 session。
- **主版本号 +1**：功能变动导致新版本无法正常读取/回放旧版本录制的 session。

递增某一位时，其后的低位版本号归零（标准 semver 约定，例如 1.1.0 → 1.2.0，不是 1.1.1）。

修改版本号时，只需改 `package.xml` 的 `<version>`；`data_recorder_core` 通过 CMake 的 `ament_package_xml()` 在编译期读取它并注入 `DATA_RECORDER_VERSION` 宏，无需在代码里另外硬编码。

## GUI 操作

当前桌面已验证可用 X11 + `xdotool` 操作采集程序。操作流程如下：

1. 在工作区根目录执行命令。每次 ROS 命令调用都先加载环境：`source ~/.local/ros2_rc && rs`。
2. 检查已有进程，避免重复启动。按任务指定的配置启动采集程序；后台启动可使用 `subprocess.Popen(..., start_new_session=True)`，重定向日志并保存 PID。
3. 检查启动日志，等待窗口和所需数据就绪。查找目标窗口并读取几何信息：

   ```bash
   xdotool search --onlyvisible --name 'Recorder|采集'
   xdotool getwindowgeometry --shell <窗口ID>
   ```

4. 截图确认界面状态及按钮位置，再激活窗口，按窗口内相对坐标点击：

   ```bash
   xdotool windowactivate --sync <窗口ID>
   xdotool mousemove --window <窗口ID> <按钮X> <按钮Y> click 1
   ```

   窗口 ID 和按钮坐标应根据当前窗口确认；窗口移动、缩放或恢复后重新定位。多个窗口匹配时先核实目标。若 Pillow `ImageGrab` 报 `X get_image failed`，可用 `ctypes` 调用 `libX11.so.6` 的 `XGetImage` 截取目标窗口，按实际像素布局解码。

5. 点击“录制”后确认已进入录制状态，按任务要求等待，再定位并点击“停止”。也可使用空格键切换，但须确保目标窗口获得焦点。
6. 等待写入完成，检查配置 `output_dir` 下的新会话：核对 `session.yaml` 的 `duration_seconds`，并按配置检查录制文件是否完整。点击命令成功不等于录制成功。
7. 需要清理时，仅停止本次启动且身份已核实的进程。
