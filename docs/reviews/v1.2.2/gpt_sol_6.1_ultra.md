# ROS2-recorder v1.2.2 审阅意见

- 审阅日期：2026-10-09。
- 审阅范围：当前代码及已暂存的 v1.2.2 修改，包括录制、会话管理、历史回放、迁移脚本和相机 UI。
- 代码基线：`737bb29`，另包含当时已暂存的图像 rosbag 后端修复及相关测试、文档和版本号修改。
- 优先级：P1 表示应优先修复的数据丢失问题；P2 表示在相应触发条件下影响功能或稳定性的问题。

共确认 10 项问题，其中两项会丢失标签或时间标注。以下行号对应审阅时的代码快照。

## 1. [P1] 迁移旧数据会丢失标签和时间标注

**位置：** [scripts/migrate_dataset_to_v1_2_0.py:258](../../../scripts/migrate_dataset_to_v1_2_0.py#L258)。

`migrate_session_yaml()` 重建 YAML 时，仅从原会话复制 `recorded_at`、`duration_seconds`、`topics`，遗漏 `tags` 和 `annotations`。引入 `version` 字段之前的正式录制器已经保存这两个字段，因此合法旧会话也会受影响。

**验证：** 调用真实迁移函数，输入会话各包含一条标签和标注，输出 YAML 中两个字段均消失。原始输入文件保留，但迁移后的数据集失去标注信息。

**建议：** 保留原会话字段，仅修改迁移需要更新的字段；至少完整保留标签和标注，并验证迁移前后的内容一致性。

## 2. [P1] 录制中关闭窗口会丢失标签和标注

**位置：** [src/recorder_engine.cpp:157](../../../src/recorder_engine.cpp#L157)；退出路径见 [src/app_controller.cpp:108](../../../src/app_controller.cpp#L108)。

关闭窗口后，控制器析构没有导出当前标签和标注。随后录制引擎析构执行 `stop_session({}, {})`，以空数组保存会话。

**验证：** 通过真实控制器开始录制、选择一条标签并添加一条标注，再按程序退出顺序销毁控制器和引擎。退出前标签和标注数量各为 1，落盘后各为 0。

**建议：** 在控制器及其模型销毁前，使用当前标签和标注完成停录；引擎析构只承担最后的资源清理。

## 3. [P2] rosbag 后端的图像无法在 GUI 回放

**位置：** [src/session_player.cpp:40](../../../src/session_player.cpp#L40)，以及 [src/session_player.cpp:71](../../../src/session_player.cpp#L71)。

GUI 历史播放器只读取 `backend: video` 的话题，直接跳过 rosbag 后端。当前暂存修复让图像正确写入 rosbag、不再额外生成 MP4 后，这些图像没有 GUI 历史读取路径。纯 rosbag 会话的 `clips_` 为空，`play()` 也会拒绝启动。

**验证：** 加载具有有效时长、话题后端为 rosbag 的会话并调用 `play()`，`playing()` 仍为 false。此问题针对 GUI 历史回放；ROS 命令行 `player` 具有独立的 rosbag 发布路径。

**建议：** 为 GUI 增加 rosbag 图像读取路径，并让播放时间轴能够在没有视频片段时推进。

## 4. [P2] ROS 时钟回退会导致视频静默丢帧

**位置：** [src/video_recorder.cpp:218](../../../src/video_recorder.cpp#L218)；相关错误处理见 [src/video_recorder.cpp:189](../../../src/video_recorder.cpp#L189)。

视频 PTS 直接由 `recv_stamp_ns - first_stamp_ns_` 计算，接收时间来自 `node_->now()`。仿真 `/clock` 回退或系统时钟回拨时，PTS 不再单调，FFmpeg 会拒绝写入部分数据包；代码还忽略 `av_interleaved_write_frame()` 的返回值。

**验证：** 输入 30 帧，在第 15 帧之后将接收时间归零。全部编码调用及关闭调用均返回成功，CSV 包含 30 条帧记录，但 `ffprobe` 只能读出 15 帧，并出现非单调 DTS 错误。

**建议：** 使用单调的录制时间生成 PTS，另外保留原始 ROS 时间戳；检查并传播视频写入错误。

## 5. [P2] 视频 CSV 字段异常会使整个 GUI 退出

**位置：** [src/session_player.cpp:45](../../../src/session_player.cpp#L45)。

`VideoClipReader::open()` 对空或未知的 `encoding`、无效数值等情况可能抛异常，而 `SessionPlayer::load()` 未捕获这些异常。GUI 将加载操作投递到 Qt 工作线程，异常逃逸后会终止整个进程。

**验证：** 使用含空 `encoding` 的 CSV，经 Qt 队列加载会话，子进程以 `SIGABRT` 退出，输出未捕获的 `std::runtime_error`。

**建议：** 在历史片段加载边界捕获解析异常，跳过有问题的片段，并向界面报告具体文件和错误。

## 6. [P2] 格式异常的 session.yaml 会中断启动扫描

**位置：** [src/session_manager.cpp:125](../../../src/session_manager.cpp#L125)。

`SessionManager::scan()` 仅保护 `YAML::LoadFile()`，随后对字段的索引和转换不在异常保护范围内。语法合法但字段类型错误的会话文件会使整个扫描抛异常；启动时控制器直接调用扫描，因此一个异常文件就能阻止程序启动。

**验证：** 分别使用 `session: [wrong]`、`recorded_at: wrong`、`topics: [wrong]`，均复现未被扫描函数捕获的 YAML 异常。

**建议：** 对每个会话的完整解析过程进行异常隔离和结构校验，跳过无效会话并保留错误信息。

## 7. [P2] 历史曲线、相机和标注使用不同时间起点

**位置：** [src/video_clip_reader.cpp:102](../../../src/video_clip_reader.cpp#L102)、[src/history_curve_loader.cpp:74](../../../src/history_curve_loader.cpp#L74)；视频调用处见 [src/session_player.cpp:63](../../../src/session_player.cpp#L63)。

每路相机以自身首帧为零点，历史曲线以 bag 首条消息为零点，播放器又向所有相机传入同一个会话播放时间。它们没有统一使用 `session.yaml` 中的会话开始时间，而标注使用会话相对时间，因此晚启动的相机、曲线和标注会错位。

**验证：** 会话起点为绝对时间 1 秒，相机 A 首帧在 1 秒、相机 B 首帧在 3 秒。拖到会话第 2 秒时，两路都显示各自第 3 帧，而 B 应显示第 1 帧。另首条 bag 消息在绝对时间 3 秒时，历史曲线被画在 0 秒，实际应为会话第 2 秒。

**建议：** 使用统一的会话时间基准，保留各数据源相对会话起点的偏移，并为缺少起点信息的旧格式明确设置回退规则。

## 8. [P2] 迁移后的视频可能取错帧

**位置：** [src/video_clip_reader.cpp:237](../../../src/video_clip_reader.cpp#L237)；迁移行为见 [scripts/migrate_dataset_to_v1_2_0.py:230](../../../scripts/migrate_dataset_to_v1_2_0.py#L230)。

迁移重新估算 `recv_stamp_ns`，同时保留原来的 `pts_ns` 和 MP4。读取器却用接收时间差匹配解码帧的 PTS，没有使用 CSV 中保留的视频 PTS。当估算接收延迟在帧间发生变化时，CSV 索引与实际像素内容不再对应。GUI 和 ROS 命令行播放器都使用该读取器。

**验证：** 调用真实 CSV 迁移函数，使第 2 帧起的估算延迟增加 40 毫秒。请求 `frameAtIndex(1)` 时，实际返回第 3 帧：测得亮度约 119，而原第 2 帧亮度约 69。

**建议：** 分开处理接收时间轴和视频 PTS；依据实际视频 PTS 定位像素帧，依据接收时间安排播放。

## 9. [P2] 迟到的历史曲线会污染当前视图

**位置：** [src/app_controller.cpp:693](../../../src/app_controller.cpp#L693)；异步加载入口见 [src/app_controller.cpp:415](../../../src/app_controller.cpp#L415)。

历史加载结果没有携带会话标识或请求代次，控制器接收曲线时也不校验当前数据模式。用户切换到其他会话或在线数据后，旧任务仍可能完成，并覆盖具有相同话题名的当前模型。

**验证：** 选择历史会话，等待工作线程产生结果但暂不处理 GUI 队列，再切回在线数据并开始录制。处理队列后，新时间轴从 0 个点变为旧会话的 5 个点，此时仍处于在线模式。

**建议：** 为异步请求和结果增加会话标识及代次，只接收仍属于当前数据源的结果。

## 10. [P2] 相机刷新会让拖拽卡住

**位置：** [src/camera_grid_model.cpp:12](../../../src/camera_grid_model.cpp#L12)；每帧更新入口见 [src/app_controller.cpp:729](../../../src/app_controller.cpp#L729)。

相机网格将源模型的每次 `dataChanged` 都处理成整个模型重置。每帧更新 `FrameSeqRole` 会销毁全部相机卡片及其 MouseArea；拖拽途中收到下一帧后，鼠标手势无法正常完成，卡片隐藏和悬浮层状态也可能残留。无关的曲线和统计更新同样会重置网格。

**验证：** 使用实际 QML 卡片模拟按下、移动、帧刷新、松开。模型重置后原卡片被销毁，松开鼠标后 `dragActive` 仍为 true。同一重建路径还会在无关曲线更新后，将已有相机帧序号从 14 重置为 0。

**建议：** 按变更角色过滤源模型更新；帧序号和元数据应更新对应行，仅在相机列表或可见性变化时调整网格结构。

## 验证记录与限制

已按工作区约定加载 ROS 环境，使用现有构建产物执行：

```bash
source ~/.local/ros2_rc && rs && ctest --test-dir build/data_recorder --output-on-failure
```

21 个测试目标中，20 个通过。`test_usage_help` 失败：测试硬编码了旧工作区的示例配置绝对路径，而当前帮助输出使用通用配置路径占位符，两者不一致。现有测试通过并未覆盖上述复现条件。

额外验证使用临时程序及临时数据，均放在 `/tmp`，没有修改仓库源码，也没有重新构建整个工作区。临时复现文件包括 `/tmp/dr_review_root.cpp`、`/tmp/dr_recording_review/video_fail_repro.cpp`、`/tmp/ros2_recorder_review_playback.cpp`、`/tmp/review_grid.cpp`；这些文件不属于本仓库，清理临时目录后可能不再存在。

此次审阅没有重新列出提交 `737bb29` 已明确移除的两项意见：导出 vendored `rapidcsv.h`，以及 CSV 保存失败时的结果传播。问题 4 针对视频数据包写入失败，问题 5 针对 CSV 读取异常，与上述 CSV 保存问题不同。
