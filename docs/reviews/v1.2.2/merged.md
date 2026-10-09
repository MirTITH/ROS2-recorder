# ROS2-recorder v1.2.2 审阅意见汇总

- 审阅日期：2026-10-09。
- 来源： [Sol 审阅意见](gpt_sol_6.1_ultra.md)（下称 Sol）、[Astra 审阅意见](gpt_astra_6_ultra.md)（下称 Astra）。
- 范围：`src/ROS2-recorder` 当前代码及审阅时尚未提交的 v1.2.2 修改，涵盖录制、会话管理、历史回放、迁移脚本和相机 UI；问题不限于本次修改引入。Sol 明确记录基线为 `737bb29`，另包含当时已暂存的图像 rosbag 后端修复及相关测试、文档和版本号修改。
- 优先级：P1 表示可能造成数据丢失或录制进程异常终止、应优先修复的问题；P2 表示功能正确性、稳定性、异常处理或并发问题。
- 合并结果：原始 19 项意见合并为 **14 项独立问题（3 项 P1、11 项 P2）**。重复意见合并证据和建议，独立问题完整保留。以下位置和验证结果均来自原审阅快照，本汇总未重新审查源码、执行复现或运行构建测试。

## 问题索引与来源

| 编号 | 优先级 | 问题 | Sol 原编号 | Astra 原编号 |
| --- | --- | --- | --- | --- |
| 1 | P1 | 迁移旧数据会丢失标签和时间标注 | 1 | 1 |
| 2 | P1 | 录制中退出会丢失标签和标注 | 2 | 2 |
| 3 | P1 | 写入线程异常会终止整个采集进程 | — | 3 |
| 4 | P2 | rosbag 后端的图像无法在 GUI 回放 | 3 | — |
| 5 | P2 | ROS 时钟回退会导致视频静默丢帧 | 4 | — |
| 6 | P2 | 视频 CSV 解析异常会使整个 GUI 退出 | 5 | 6 |
| 7 | P2 | 单个格式异常的会话文件会阻止启动扫描 | 6 | 7 |
| 8 | P2 | 历史曲线、相机和标注使用不同时间起点 | 7 | 4 |
| 9 | P2 | 迁移后的视频可能取错帧 | 8 | — |
| 10 | P2 | 迟到的历史曲线会污染当前视图 | 9 | — |
| 11 | P2 | 相机刷新会让拖拽卡住 | 10 | — |
| 12 | P2 | 曲线更新存在永久停滞的竞态 | — | 5 |
| 13 | P2 | 输出目录创建失败会抛出未处理异常 | — | 8 |
| 14 | P2 | rosbag 图像路径忽略显式 QoS | — | 9 |

## 1. [P1] 迁移旧数据会丢失标签和时间标注

**来源：** Sol #1、Astra #1。

**位置：** [scripts/migrate_dataset_to_v1_2_0.py:258](../../../scripts/migrate_dataset_to_v1_2_0.py#L258)（第 258–261 行）。

`migrate_session_yaml()` 重建 YAML 时，仅复制 `recorded_at`、`duration_seconds`、`topics`，遗漏 `tags` 和 `annotations`。引入 `version` 字段之前的正式录制器已保存这两个字段，因此合法旧会话也会受影响。原始输入文件仍保留，但迁移后的数据集失去标签和标注信息。

**证据：** Sol 调用真实迁移函数，输入各包含一条标签和标注，输出中两字段均消失。Astra 在临时目录构造无 `version` 字段且包含标签和区间标注的旧会话，执行迁移脚本返回码为 0，但输出 `session.yaml` 中两字段均不存在。

**建议：** 保留原会话元数据，仅更新版本及迁移所需字段；至少完整保留 `tags`、`annotations`，并验证迁移前后内容一致。

## 2. [P1] 录制中退出会丢失标签和标注

**来源：** Sol #2、Astra #2。

**位置：** [src/recorder_engine.cpp:154](../../../src/recorder_engine.cpp#L154)（第 154–157 行）、[src/app_controller.cpp:108](../../../src/app_controller.cpp#L108)、[src/data_recorder.cpp:119](../../../src/data_recorder.cpp#L119)（第 119–126 行）。

关闭窗口或 ROS shutdown 时，退出路径没有先从 Controller 导出当前标签和标注；停止并等待 ROS 线程后，Controller 及模型被销毁，随后 `RecorderEngine` 析构调用 `stop_session({}, {})`，将空的标签和标注写入会话文件。

**证据：** 两份审阅均使用真实 Controller 和 Engine，按实际对象销毁顺序验证：退出前内存中各有 1 个标签和标注，退出后会话仍存在，但落盘的标签和标注数量均为 0。

**建议：** 在 Controller 及模型销毁前，使用当前标签和标注快照统一完成停录；停止按钮、关闭窗口和 ROS shutdown 共享保存路径，引擎析构仅承担最后的资源清理。

## 3. [P1] 写入线程异常会终止整个采集进程

**来源：** Astra #3。

**位置：** [include/data_recorder/writer_queue.hpp:99](../../../include/data_recorder/writer_queue.hpp#L99)、[src/rosbag_writer.cpp:104](../../../src/rosbag_writer.cpp#L104)（另见第 111 行）。

`WriterQueue::run()` 直接执行 `sink_()`，没有异常边界。实际 rosbag 写入路径可能抛出存储或分配异常；异常逃出 `std::thread` 后触发 `std::terminate`，使其他视频无法正常收尾，也无法保存 `session.yaml`。

**证据与边界：** 使用当前 `WriterQueue` 头文件编译最小程序，让 sink 抛出模拟存储写入失败的 `std::runtime_error`，进程以 `SIGABRT` 结束，子进程返回码为 -6。此复现验证了工作线程异常传播行为，未通过填满实际磁盘制造存储故障。

**建议：** 在工作线程内捕获异常，保存失败状态并通知录制控制层停止录制、报告失败及清理资源。

## 4. [P2] rosbag 后端的图像无法在 GUI 回放

**来源：** Sol #3。

**位置：** [src/session_player.cpp:40](../../../src/session_player.cpp#L40)、[src/session_player.cpp:71](../../../src/session_player.cpp#L71)。

GUI 历史播放器只读取 `backend: video` 的话题，直接跳过 rosbag 后端。v1.2.2 暂存修复使图像正确写入 rosbag、不再额外生成 MP4 后，这些图像没有 GUI 历史读取路径。纯 rosbag 会话的 `clips_` 为空，`play()` 拒绝启动。

**证据：** 加载具有有效时长、话题后端为 rosbag 的会话并调用 `play()`，`playing()` 仍为 false。此问题针对 GUI 历史回放；ROS 命令行 `player` 有独立的 rosbag 发布路径。

**建议：** 为 GUI 增加 rosbag 图像读取路径，并允许播放时间轴在没有视频片段时推进。

## 5. [P2] ROS 时钟回退会导致视频静默丢帧

**来源：** Sol #4。

**位置：** [src/video_recorder.cpp:218](../../../src/video_recorder.cpp#L218)；错误处理见 [src/video_recorder.cpp:189](../../../src/video_recorder.cpp#L189)。

视频 PTS 直接由 `recv_stamp_ns - first_stamp_ns_` 计算，接收时间来自 `node_->now()`。仿真 `/clock` 回退或系统时钟回拨时，PTS 不再单调，FFmpeg 会拒绝写入部分数据包；代码还忽略 `av_interleaved_write_frame()` 返回值。

**证据：** 输入 30 帧，在第 15 帧后将接收时间归零。全部编码及关闭调用均返回成功，CSV 包含 30 条帧记录，但 `ffprobe` 只能读出 15 帧，并出现非单调 DTS 错误。

**建议：** 使用单调的录制时间生成 PTS，另外保留原始 ROS 时间戳；检查并传播视频数据包写入错误。

## 6. [P2] 视频 CSV 解析异常会使整个 GUI 退出

**来源：** Sol #5、Astra #6。

**位置：** [src/session_player.cpp:44](../../../src/session_player.cpp#L44)（第 44–45 行）、[src/video_clip_reader.cpp:81](../../../src/video_clip_reader.cpp#L81)（另见第 108–115 行）。

`VideoClipReader::open()` 对空或未知 `encoding`、无效数值等情况可能抛异常，`SessionPlayer::load()` 未捕获。GUI 将加载操作投递到 Qt 工作线程，异常逃出事件回调后会终止整个进程。

**证据：** Sol 使用含空 `encoding` 的 CSV，经 Qt 队列加载会话，子进程以 `SIGABRT` 退出，输出未捕获的 `std::runtime_error`。Astra 使用与实际路径一致的 `QThread` 和 `Qt::QueuedConnection`，将 `recv_stamp_ns` 写为 `not_a_number`，进程同样以 `SIGABRT` 结束（返回码 -6），输出 Qt 事件处理器异常提示和 `std::invalid_argument: stol`。

**建议：** 在 GUI 历史加载边界捕获解析异常，报告具体文件和错误，跳过损坏片段，保持其他会话及程序可用。

## 7. [P2] 单个格式异常的会话文件会阻止启动扫描

**来源：** Sol #6、Astra #7。

**位置：** [src/session_manager.cpp:118](../../../src/session_manager.cpp#L118)（第 118–126 行）、[src/app_controller.cpp:100](../../../src/app_controller.cpp#L100)。

`SessionManager::scan()` 的异常保护仅覆盖 `YAML::LoadFile()`，后续字段索引和类型转换在保护范围之外。语法合法但字段类型错误的会话 YAML 会使整个扫描抛异常；Controller 构造时立即扫描历史目录，因此一个异常文件即可阻止 GUI 正常启动。

**证据：** Sol 分别使用 `session: [wrong]`、`recorded_at: wrong`、`topics: [wrong]`，均复现未捕获 YAML 异常。Astra 使用 `session: [unexpected, sequence]`，扫描在读取会话名称时抛出 `bad conversion`。

**建议：** 将每个会话的完整解析和结构校验纳入异常边界，跳过无效记录并保留可定位的诊断信息。

## 8. [P2] 历史曲线、相机和标注使用不同时间起点

**来源：** Sol #7、Astra #4。

**位置：** [src/session_player.cpp:61](../../../src/session_player.cpp#L61)（第 61–63 行）、[src/video_clip_reader.cpp:97](../../../src/video_clip_reader.cpp#L97)（第 97–102 行）、[src/history_curve_loader.cpp:73](../../../src/history_curve_loader.cpp#L73)（第 73–75、137–140 行）。

每路视频以自身首帧为零点，历史曲线以 bag 首条消息为零点，GUI 播放器却向所有视频传入同一个会话播放时间。这些路径没有统一使用已保存的 `session.ros_time_ns`；标注使用会话相对时间，因此晚启动的相机、曲线与标注会错位。

**证据：** Sol 构造起点为绝对时间 1 秒的会话，相机 A 首帧为 1 秒、B 为 3 秒。拖到会话第 2 秒时，两路均显示各自第 3 帧，而 B 应显示第 1 帧；绝对时间 3 秒的 bag 首消息被画在 0 秒，实际应为会话第 2 秒。Astra 使用会话起点 100 秒、相机首帧分别为 100 秒和 102 秒，播放头在会话第 3 秒时，A 显示 103 秒图像、B 显示 105 秒图像，B 提前 2 秒；会话第 2、3、4 秒的 bag 消息被画在第 0、1、2 秒。

**建议：** 统一以会话起点作为 GUI 时间轴零点，保留各数据源相对起点的偏移，在读取视频时换算首帧偏移；历史曲线使用同一基准，并为缺少起点字段的旧会话定义明确回退规则。

## 9. [P2] 迁移后的视频可能取错帧

**来源：** Sol #8。

**位置：** [src/video_clip_reader.cpp:237](../../../src/video_clip_reader.cpp#L237)；迁移行为见 [scripts/migrate_dataset_to_v1_2_0.py:230](../../../scripts/migrate_dataset_to_v1_2_0.py#L230)。

迁移重新估算 `recv_stamp_ns`，同时保留原来的 `pts_ns` 和 MP4。读取器却用接收时间差匹配解码帧 PTS，没有使用 CSV 中保留的视频 PTS。当估算接收延迟在帧间变化时，CSV 索引与实际像素内容不再对应。GUI 和 ROS 命令行播放器均使用该读取器。

**证据：** 调用真实 CSV 迁移函数，使第 2 帧起的估算延迟增加 40 毫秒。请求 `frameAtIndex(1)` 时实际返回第 3 帧，测得亮度约 119，而原第 2 帧亮度约 69。

**建议：** 分开处理接收时间轴和视频 PTS：依据实际视频 PTS 定位像素帧，依据接收时间安排播放。

## 10. [P2] 迟到的历史曲线会污染当前视图

**来源：** Sol #9。

**位置：** [src/app_controller.cpp:693](../../../src/app_controller.cpp#L693)；异步加载入口见 [src/app_controller.cpp:415](../../../src/app_controller.cpp#L415)。

历史加载结果没有携带会话标识或请求代次，控制器接收曲线时也不校验当前数据模式。切换到其他会话或在线数据后，旧任务仍可能完成，并覆盖具有相同话题名的当前模型。

**证据：** 选择历史会话，等待工作线程产生结果但暂不处理 GUI 队列，再切回在线数据并开始录制。处理队列后，新时间轴从 0 个点变为旧会话的 5 个点，此时仍处于在线模式。

**建议：** 为异步请求和结果增加会话标识及代次，只接收仍属于当前数据源的结果。

## 11. [P2] 相机刷新会让拖拽卡住

**来源：** Sol #10。

**位置：** [src/camera_grid_model.cpp:12](../../../src/camera_grid_model.cpp#L12)；每帧更新入口见 [src/app_controller.cpp:729](../../../src/app_controller.cpp#L729)。

相机网格将源模型每次 `dataChanged` 都处理成整个模型重置。每帧更新 `FrameSeqRole` 会销毁全部相机卡片及 MouseArea；拖拽期间收到下一帧后，鼠标手势无法正常完成，卡片隐藏和悬浮层状态可能残留。无关曲线和统计更新也会重置网格。

**证据：** 使用实际 QML 卡片模拟按下、移动、帧刷新、松开。模型重置后原卡片被销毁，松开鼠标后 `dragActive` 仍为 true。同一重建路径在无关曲线更新后，还会将已有相机帧序号从 14 重置为 0。

**建议：** 按变更角色过滤源模型更新；帧序号和元数据更新对应行，仅在相机列表或可见性变化时调整网格结构。

## 12. [P2] 曲线更新存在永久停滞的竞态

**来源：** Astra #5。

**位置：** [src/recorder_engine.cpp:147](../../../src/recorder_engine.cpp#L147)（第 147–149 行）、[src/app_controller.cpp:723](../../../src/app_controller.cpp#L723)（第 723–724 行）；提前返回处为 `recorder_engine.cpp` 第 128 行。

`bridge_->push_curves(topics)` 先向 GUI 队列投递事件，随后 ROS 线程才设置 `curves_in_flight_ = true`。GUI 可能先消费事件并清零标志，ROS 线程随后再写入 `true`。之后定时器会一直提前返回，曲线更新停止，且没有待消费事件能再次清零。

**证据与边界：** 原审阅静态确认两个线程之间存在上述合法执行顺序，未进行确定性的竞态注入复现。

**建议：** 在投递前设置背压标志，若投递未被接受则复位，避免完成通知早于置位。

## 13. [P2] 输出目录创建失败会抛出未处理异常

**来源：** Astra #8。

**位置：** [src/recorder_engine.cpp:430](../../../src/recorder_engine.cpp#L430)（第 430–435 行）、[src/session_manager.cpp:17](../../../src/session_manager.cpp#L17)（第 17–19 行）。

`start_session()` 未处理 `create_session_directory()` 的文件系统异常，前一次 `create_directories(output_dir, ec)` 的错误也未检查。Controller 只检查空返回值，无法将异常转换为正常的录制启动失败状态。

**证据：** 将 `output_dir` 指向临时目录中的普通文件，`start_session()` 抛出 `filesystem_error: cannot create directories: Not a directory`，没有返回空字符串。

**建议：** 检查文件系统操作结果，在启动边界处理异常并清理部分初始化资源，将失败信息返回 Controller。

## 14. [P2] rosbag 图像路径忽略显式 QoS

**来源：** Astra #9。

**位置：** [src/recorder_engine.cpp:217](../../../src/recorder_engine.cpp#L217)（第 217–227 行）。

`subscribe_rosbag_topic()` 仅按发布者生成 QoS，没有读取话题显式配置。v1.2.2 将 `backend: rosbag` 的图像从原 CameraPreview 分支转入此路径，失去了原有 QoS 覆盖行为。订阅建立时若只有可靠发布者，之后出现 best-effort 发布者，用户显式配置的 best-effort 也无法使该订阅与其匹配。

**证据：** 为 rosbag 图像话题配置 `best_effort`，在可靠发布者存在时检查实际订阅端点，得到的 reliability 仍为 `RELIABLE`。

**建议：** rosbag 订阅也应使用显式 QoS 配置，仅在未配置时按发布者自动适配。

## 原审阅的构建与测试结果

### 构建

Astra 按工作区 `.vscode/tasks.json` 默认 release 任务执行：

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install \
  --base-paths /home/nros/Documents/Woosh/woosh_ws \
  --continue-on-error --mixin release compile-commands ccache
```

结果：全部 40 个包构建通过。Sol 使用现有构建产物，没有重新构建整个工作区。

### 现有测试

Sol 执行：

```bash
source ~/.local/ros2_rc && rs && ctest --test-dir build/data_recorder --output-on-failure
```

Astra 执行：

```bash
source ~/.local/ros2_rc && rs && colcon test \
  --packages-select data_recorder --event-handlers console_direct+
```

两份审阅均报告 21 个测试目标中 20 个通过、1 个失败，失败为 `test_usage_help` 中的 `UsageHelp.MissingConfigShowsCurrentExamplePath`：

- [test/test_usage_help.cpp:47](../../../test/test_usage_help.cpp#L47)（第 47–50 行）要求帮助文本包含旧开发机绝对路径 `/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml`。
- [src/data_recorder.cpp:29](../../../src/data_recorder.cpp#L29)（第 29–34 行）实际打印 `<config_file_path>` 占位符，导致断言失败。

**建议：** 根据当前帮助文本契约更新测试，避免断言开发机专属的绝对路径。此测试失败单独记录，不计入上述 14 项代码问题；现有测试通过也不代表覆盖了上述触发条件。

## 验证限制与范围说明

- 两份原审阅均未修改源码，额外复现使用临时程序和临时数据。本文件仅整合审阅意见，未新增验证。
- Sol 的临时复现文件包括 `/tmp/dr_review_root.cpp`、`/tmp/dr_recording_review/video_fail_repro.cpp`、`/tmp/ros2_recorder_review_playback.cpp`、`/tmp/review_grid.cpp`；它们不属于仓库，清理临时目录后可能不再存在。
- 问题 12 仅有静态竞态分析；问题 3 使用模拟写入异常，未制造实际磁盘满故障。其余复现范围以各条证据为准。
- Astra 明确未验证真实硬件采集、长时间压力测试或其他 ROS 2 发行版运行情况。
- Sol 明确不再列出提交 `737bb29` 已移除的两项意见：导出 vendored `rapidcsv.h`、CSV 保存失败时的结果传播。本汇总同样不恢复这两项；问题 5 针对视频数据包写入失败，问题 6 针对 CSV 读取异常，与已移除的 CSV 保存问题不同。
