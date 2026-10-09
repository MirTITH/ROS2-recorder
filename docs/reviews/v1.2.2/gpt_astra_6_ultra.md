# ROS2-recorder v1.2.2 代码审查

- 审查日期：2026-10-09
- 审查范围：`src/ROS2-recorder` 当前目录，包括审查时尚未提交的 v1.2.2 改动；发现不限于本次改动引入的问题。
- 审查方式：静态代码检查、默认 release 构建、现有测试，以及在临时目录中运行最小复现程序。
- 严重程度：P1 表示可能造成数据丢失或录制进程异常终止，应优先修复；P2 表示功能正确性、异常处理或并发问题。
- 本次审查未修改源码。以下行号对应审查时的文件内容。

## 1. [P1] 数据迁移会丢失标签和时间标注

**位置：** [scripts/migrate_dataset_to_v1_2_0.py](../../../scripts/migrate_dataset_to_v1_2_0.py)，第 258–261 行。

`migrate_session_yaml()` 重建 YAML 时，仅复制 `recorded_at`、`duration_seconds`、`topics`，没有保留旧会话的 `tags` 和 `annotations`。因此迁移虽然成功结束，输出数据集却失去了原有标签和时间标注。

**复现证据：** 在临时目录中构造无 `version` 字段、包含标签和区间标注的旧会话，执行迁移脚本。脚本返回码为 0，但输出的 `session.yaml` 中两字段均不存在。

**建议：** 在保留原有元数据的基础上添加或更新版本与迁移字段，至少确保 `tags`、`annotations` 完整保留。

## 2. [P1] 录制中直接退出会丢失全部标注

**位置：** [src/recorder_engine.cpp](../../../src/recorder_engine.cpp)，第 154–157 行；[src/data_recorder.cpp](../../../src/data_recorder.cpp)，第 119–126 行。

正常关闭窗口或 ROS shutdown 后，退出路径只停止并等待 ROS 线程，没有先从 Controller 导出当前标签和标注。随后 `RecorderEngine` 析构调用 `stop_session({}, {})`，将空的标签和标注写入会话文件。

**复现证据：** 使用现有 Controller 和 Engine 按实际对象销毁顺序运行：退出前内存中有 1 个标签、1 条标注，退出后的会话仍存在，但两者数量均为 0。

**建议：** 在 Controller 及其模型销毁前统一执行停录，传入标签和标注快照；关闭窗口、ROS shutdown 和正常停止按钮应共享这条保存路径。

## 3. [P1] 写入异常会终止整个采集进程

**位置：** [include/data_recorder/writer_queue.hpp](../../../include/data_recorder/writer_queue.hpp)，第 99 行；[src/rosbag_writer.cpp](../../../src/rosbag_writer.cpp)，第 104、111 行。

`WriterQueue::run()` 直接执行 `sink_()`，没有异常边界。实际 rosbag 写入路径可以抛出存储或分配异常；异常逃出 `std::thread` 后会触发 `std::terminate`，使其他视频无法正常收尾，也无法保存 `session.yaml`。

**复现证据：** 使用当前 `WriterQueue` 头文件编译最小程序，让 sink 抛出模拟存储写入失败的 `std::runtime_error`。进程以 `SIGABRT` 结束，子进程返回码为 -6。

**验证边界：** 该复现验证了工作线程的异常传播行为，没有通过填满实际磁盘来制造存储故障。

**建议：** 在工作线程内捕获异常，保存失败状态并通知录制控制层停止录制、报告失败及执行资源清理。

## 4. [P2] GUI 历史回放的时间基准不一致

**位置：** [src/session_player.cpp](../../../src/session_player.cpp)，第 61–63 行；[src/video_clip_reader.cpp](../../../src/video_clip_reader.cpp)，第 97–102 行；[src/history_curve_loader.cpp](../../../src/history_curve_loader.cpp)，第 73–75、137–140 行。

GUI 播放器把同一个会话播放头直接传给每路视频的 `frameAtSeconds(t)`，而每个视频读取器都按自己的首帧归零。历史曲线又按 bag 首消息归零。这些路径没有使用已经保存的 `session.ros_time_ns`，因此不同开始时间的视频、曲线和按会话起点记录的事件标注会错位。

**复现证据：** 构造从 ROS 时间 100 秒开始的会话，相机 A 首帧为 100 秒，相机 B 首帧为 102 秒。播放头为会话第 3 秒时，A 显示 103 秒的图像，B 显示 105 秒的图像，即 B 提前了 2 秒。同一复现中，bag 内发生于会话第 2、3、4 秒的消息，被显示在第 0、1、2 秒。

**建议：** 统一以会话起点作为 GUI 时间轴零点，并在调用各视频读取器时换算首帧偏移；历史曲线使用同一基准。对缺少起点字段的旧会话采用明确的回退策略。

## 5. [P2] 曲线更新存在永久停滞的竞态

**位置：** [src/recorder_engine.cpp](../../../src/recorder_engine.cpp)，第 147–149 行；[src/app_controller.cpp](../../../src/app_controller.cpp)，第 723–724 行。

`bridge_->push_curves(topics)` 先向 GUI 队列投递事件，随后 ROS 线程才设置 `curves_in_flight_ = true`。GUI 可能先消费事件并将标志清零，ROS 线程随后再写入 `true`。此后定时器会一直在第 128 行提前返回，曲线更新停止，且没有待消费事件能够再次清零。

**验证方式：** 静态确认两个线程之间存在上述合法执行顺序，未进行确定性的竞态注入复现。

**建议：** 在投递前设置背压标志，若投递未被接受则复位，避免完成通知早于置位。

## 6. [P2] 损坏的视频索引会使整个 GUI 崩溃

**位置：** [src/session_player.cpp](../../../src/session_player.cpp)，第 44–45 行；[src/video_clip_reader.cpp](../../../src/video_clip_reader.cpp)，第 81、108–115 行。

`SessionPlayer::load()` 未捕获 `reader->open()` 抛出的异常。CSV 数值转换和编码验证均可能抛出异常，异常会穿过 `AppController` 投递的 Qt 工作线程事件回调，终止整个 GUI。

**复现证据：** 使用与实际加载路径一致的 `QThread` 和 `Qt::QueuedConnection`，将 CSV 的 `recv_stamp_ns` 写为 `not_a_number`。进程以 `SIGABRT` 结束，返回码为 -6，并输出 Qt 事件处理器异常提示以及 `std::invalid_argument: stol`。

**建议：** 在 GUI 历史加载边界捕获解析异常，报告或跳过损坏片段，保持其他会话和程序可用。

## 7. [P2] 单个异常会话文件可阻止程序启动

**位置：** [src/session_manager.cpp](../../../src/session_manager.cpp)，第 118–126 行；[src/app_controller.cpp](../../../src/app_controller.cpp)，第 100 行。

会话扫描的 try/catch 仅覆盖 `YAML::LoadFile()`，后续字段访问和类型转换位于捕获范围之外。语法合法但字段类型错误的 YAML 仍会抛出未捕获异常。Controller 构造时会立即扫描历史目录，因此一个无效会话即可阻止 GUI 正常启动。

**复现证据：** 使用 `session: [unexpected, sequence]` 构造语法合法的会话 YAML，`SessionManager::scan()` 在读取会话名称时抛出 `bad conversion`。

**建议：** 将单个会话的完整解析与校验纳入异常边界，跳过无效记录并提供可定位的诊断信息。

## 8. [P2] 输出目录创建失败会抛出未处理异常

**位置：** [src/recorder_engine.cpp](../../../src/recorder_engine.cpp)，第 430–435 行；[src/session_manager.cpp](../../../src/session_manager.cpp)，第 17–19 行。

`start_session()` 没有处理 `create_session_directory()` 的文件系统异常。前一次 `create_directories(output_dir, ec)` 的错误也未被检查。Controller 只检查空返回值，无法将异常转换为正常的“录制启动失败”状态。

**复现证据：** 将 `output_dir` 指向临时目录中的一个普通文件，`start_session()` 抛出 `filesystem_error: cannot create directories: Not a directory`，没有返回空字符串。

**建议：** 检查文件系统操作结果，在启动边界处理异常并清理部分初始化的资源，将失败信息返回 Controller。

## 9. [P2] rosbag 图像路径忽略显式 QoS

**位置：** [src/recorder_engine.cpp](../../../src/recorder_engine.cpp)，第 217–227 行。

`subscribe_rosbag_topic()` 仅按发布者生成 QoS，没有读取话题的显式配置。当前 v1.2.2 改动将 `backend: rosbag` 的图像从原先的 CameraPreview 分支转入此路径，因此失去了原有的 QoS 覆盖行为。订阅建立时若只有可靠发布者，之后出现 best-effort 发布者，用户显式配置的 best-effort 也无法使该订阅与其匹配。

**复现证据：** 为 rosbag 图像话题配置 `best_effort`，在可靠发布者存在时检查实际订阅端点，得到的 reliability 仍为 `RELIABLE`。

**建议：** rosbag 订阅也应使用显式 QoS 配置，仅在未配置时按发布者自动适配。

## 构建与测试结果

按工作区 `.vscode/tasks.json` 的默认 release 任务执行：

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install \
  --base-paths /home/nros/Documents/Woosh/woosh_ws \
  --continue-on-error --mixin release compile-commands ccache
```

结果：全部 40 个包构建通过。

执行包内现有测试：

```bash
source ~/.local/ros2_rc && rs && colcon test \
  --packages-select data_recorder --event-handlers console_direct+
```

结果：21 个测试目标中 20 个通过，1 个失败。失败项是 `UsageHelp.MissingConfigShowsCurrentExamplePath`：

- [test/test_usage_help.cpp](../../../test/test_usage_help.cpp) 第 47–50 行仍要求帮助文本包含旧开发机绝对路径 `/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml`。
- [src/data_recorder.cpp](../../../src/data_recorder.cpp) 第 29–34 行实际只打印 `<config_file_path>` 占位符，因此断言无法通过。

建议根据当前帮助文本契约更新测试，并避免断言开发机专属的绝对路径。

本次验证未包含真实硬件采集、长时间压力测试或其他 ROS 2 发行版的运行验证。
