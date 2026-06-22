# data_recorder 后端实现设计

状态：草案，待评审
日期：2026-06-22

## 背景与目标

`data_recorder` 的 Qt6/QML 前端经多轮迭代已定型并完成重构（见
`2026-06-21-data-recorder-refactor-design.md`），留下清晰的"占位数据缝"。
当前后端**完全未实现**：`data_recorder.cpp` 创建了 `rclcpp::Node` 但从不 spin，
没有任何订阅在跑；相机网格画 Canvas 假格子，时间轴画假正弦波，`toggleRecording()`
只翻转一个 bool，会话面板是写死的 demo。

本设计交付**完整的录制后端**：让"录制"按钮真正工作，实时预览真实相机画面，
显示真实话题频率/分辨率，并把数据按 group 配置落盘为 rosbag + 视频。

### 第一版范围（本 spec）

经评审确定第一版包含：

- ROS spin + 始终订阅所有配置话题。
- **rosbag 后端组**：用 `rosbag2_cpp` 录制（默认 mcap 存储），保证 `ros2 bag` 可读。
- **video 后端组**：libav 直接编码为视频 + sidecar CSV 记录权威逐帧时间戳。
- **相机实时预览**：`sensor_msgs/Image` → `QImage` → `QQuickImageProvider`，不依赖录制。
- **真实 Hz / 分辨率**：由订阅监测实时回填 UI。
- **推进的时间轴**：live edge 随真实时间前进。
- **会话扫描 + `session.yaml`**：停录写会话描述符；面板扫描真实会话。
- **标注落盘**：停录时把事件标注 + 标签快照写入 `session.yaml`。

### 第一版明确不做（YAGNI，留给后续 spec）

- 数值曲线的消息内省（任意消息类型反序列化 + 字段提取）——时间轴数值轨道 v1 留空。
- 历史会话回放的数据加载（面板能列出会话，但点开加载数据是后续工作）。
- 崩溃恢复半完成的会话。
- rosbag 压缩调优、运行时修改配置。

## 关键设计决策（评审已定）

1. **架构：进程内 + 后台 spin 线程**（方案 A）。保留单 node、单可执行文件；
   ROS executor 跑在专用 `std::thread`；磁盘 I/O 与视频编码放独立写入线程，
   绝不阻塞 ROS 回调或 GUI 线程。否决了独立 recorder 进程（过度设计）与
   在 GUI 线程 spin（会因 400Hz joint_states + 视频编码卡死 GUI）。
2. **始终订阅，录制只是开关**。App 启动即订阅所有配置话题（实时预览 + Hz/分辨率
   监测在"实时查看"即可用）。`toggleRecording()` 只开关 writers 并标记 t=0，不改订阅集。
3. **完全重构现有 C++**（已获用户授权）。现有 C++ 几乎都是为显示示例 UI；
   直接把模型重塑成由真实数据源驱动，而非把真实数据塞进占位形状。
4. **video 第一版即含完整 libav 编码**（直接链接 libav* C 库，非管道 ffmpeg、非 OpenCV）。
5. **libav PTS + sidecar CSV 双保险**。视频容器 PTS 精度受限，**权威逐帧时间记录在 CSV**
   （每帧的 ROS 时间戳），供下游对齐。
6. **rosbag 默认存储 = mcap**，带运行时守卫：mcap 插件未注册时告警并回退到
   `rosbag2_storage::get_default_storage_id()`。已在本机验证：装
   `ros-humble-rosbag2-storage-mcap` 后 `record -s mcap` → `bag info` 自动探测
   `Storage id: mcap` → 隔离 domain `bag play` 正常。
7. **会话描述符文件名 = `session.yaml`**，放 `<session>/` 根级。与 rosbag2 自己的
   `<session>/rosbag/metadata.yaml` 既不同目录也不同名，无歧义。

## 环境事实（已实测，2026-06-22）

- `ROS_DISTRO=humble`。`get_default_storage_id()` = `sqlite3`（装 mcap 不改默认）。
- 装 `ros-humble-rosbag2-storage-mcap`(0.15.16) + `mcap-vendor` 后，
  rosbag2 writers/readers 均含 `mcap`。
- 实时系统（`ROS_DOMAIN_ID=43`）正在发布可用于测试的话题：
  - `/joint_states` — `sensor_msgs/msg/JointState`，~400Hz。
  - `/tf`, `/tf_static` — `tf2_msgs/msg/TFMessage`。
  - `/camera/image_raw`, `/left_camera/image_raw`, `/right_camera/image_raw` —
    `sensor_msgs/msg/Image`，**bgr8 848×480 @ ~22Hz**。
- `QImage::Format_BGR888` 在 Qt6 存在（bgr8 转换可行）。
- libav 四库均装：avcodec 58 / avformat 58 / avutil 56 / swscale 5。
- 隔离回放：在不同 `ROS_DOMAIN_ID`（如 77）`ros2 bag play`，topic list 只见回放话题，
  domain 43 实时话题不泄漏。

## 模块分层与线程模型

**核心原则**：ROS/录制核心层**零 Qt 依赖**（纯 C++，可用 gtest 在无 GUI 下、对着实时
系统或 bag 夹具单测）；一层薄桥把它接到 Qt 模型上。

```
┌─────────────────────────── GUI 线程 (Qt) ───────────────────────────┐
│  Main.qml ── AppController ── TopicListModel / CameraGridModel /     │
│      │            │              TagListModel / EventMarkerModel /   │
│      │            │              RecordingSessionModel               │
│      │       CameraImageProvider (QQuickImageProvider)              │
│      └──────────────────┬───────────────────────────────────────────┘
│                         │  Qt::QueuedConnection（帧就绪/Hz/分辨率/推进 liveEdge）
│           ┌─────────────┴── LiveBridge (QObject) ──┐  原子 shared_ptr 交换最新帧
└───────────┼──────────────────────────────────────┼──────────────────┘
            │            进程内边界                   │
┌───────────┴──────────── ROS 线程 (spin) ───────────┴──────────────────┐
│  rclcpp::Node ── Executor（专用 std::thread）                          │
│   RecorderEngine ── 持有全部订阅 + 会话生命周期 + 扇出                 │
│     ├─ GenericSubscription（rosbag 组）                               │
│     ├─ Image 订阅（video 组）                                         │
│     ├─ TopicRateMonitor（每路 Hz/分辨率）                             │
│     └─ SessionManager（建目录 / 写 session.yaml / 扫描）              │
│                              WriterQueue（每 sink 有界队列 + 写线程）  │
│                                ├─ RosbagWriter (rosbag2_cpp)          │
│                                └─ VideoRecorder × N (libav + CSV)     │
└───────────────────────────────────────────────────────────────────────┘
```

**三条线程，职责隔离**：

1. **GUI 线程** — Qt 模型与渲染，只读最新帧、收 queued 更新。
2. **ROS spin 线程** — executor 跑订阅回调；回调**只做轻活**（拷帧入队、计数、
   原子交换预览帧指针），绝不碰磁盘。
3. **Writer 线程** — 从有界队列取消息/帧，写 rosbag、跑 libav 编码、写 CSV。
   I/O 与编码全在这里。

### 新增文件（`recorder/` 子目录，纯 C++ 核心 + 桥）

- `recorder_engine.{hpp,cpp}` — 持订阅 + 会话生命周期 + 扇出。
- `session_manager.{hpp,cpp}` — 建目录、写/扫描 `session.yaml`。
- `rosbag_writer.{hpp,cpp}` — 包 `rosbag2_cpp::Writer`。
- `video_recorder.{hpp,cpp}` — libav 管线 + CSV。
- `topic_rate_monitor.{hpp,cpp}` — 每路 Hz/分辨率估计。
- `writer_queue.{hpp,cpp}` — 有界队列 + worker 线程。
- `live_bridge.{hpp,cpp}` — QObject，把引擎回调 marshal 到 GUI 线程。
- `camera_image_provider.{hpp,cpp}` — `QQuickImageProvider`，供最新帧。

### 重塑现有文件

- `ui_models.*`：删除全部 `make_*` / `populate_placeholder_*` demo 生成器；
  `frequency_text`/`resolution_text` 改由引擎经 LiveBridge 实时回填；
  `RecordingSessionModel` 改由 `SessionManager` 扫描驱动。保留 `kSeriesColors`
  与每话题 `series_color`（真实默认配色）。
- `app_controller.*`：持有 `RecorderEngine` + `LiveBridge`；`toggleRecording`
  接真实 start/stop；新增 liveEdge 推进与帧/统计槽。
- `data_recorder.cpp`：后台线程 spin node；构造引擎；向 `QQmlEngine` 注册
  `CameraImageProvider`；退出时干净停录、join 线程。

## 录制引擎内部

### 会话生命周期（`RecorderEngine`，持 `rclcpp::Node*` + `ConfigData`）

- **构造时**：建立全部订阅（rosbag 组用 generic、video 组用 typed Image），
  启动 `TopicRateMonitor`。订阅活在 App 全生命周期。
- **`startSession()`**：`SessionManager` 建 `<output_dir>/<timestamp>/`；打开 1 个
  `RosbagWriter`、每个 video 话题 1 个 `VideoRecorder`；stamp `record_start`
  （wall + ROS + steady 三种时钟）；将 live-edge 时钟重置到 0；置 `recording_=true`。
  此后回调开始入队。
- **`stopSession()`**：停止入队 → 排空各队列 → 关 bag → 每个 `VideoRecorder` flush
  编码器 + 写 trailer + 关 CSV → `SessionManager` 写 `session.yaml`（标注 + 标签快照）
  → 触发会话面板重扫；置 `recording_=false`。

### 订阅策略

- **rosbag 组** → `create_generic_subscription(topic, type, qos, cb)`，回调收到
  `SerializedMessage`，录制时把（topic, 序列化字节, 收到时间）入队，**无需反序列化**。
  QoS **自适应发布者**（查 `get_publishers_info_by_topic`，像 `ros2 bag record`）。
- **video 组** → typed `Image` 订阅，回调做三件轻活：① bgr8 → 预览缓冲，原子换入
  `LatestFrameStore`；② 更新 Hz/分辨率；③ 录制时把帧入队给 `VideoRecorder`。
  **video 组的图像只进视频、不进 bag**。

### 写入队列（每 sink 一条）

不是单一全局写入线程，而是**每个 sink 各自拥有一条有界队列 + worker 线程**——
RosbagWriter 一条，每个 VideoRecorder 一条。慢的视频编码不会拖累 bag 写入；
3 路相机编码天然并行（x264 本身也多线程，会限核数）。背压策略见"错误处理"。

### `RosbagWriter`

包 `rosbag2_cpp::Writer`（SequentialWriter）。存储默认 **mcap**（带运行时守卫，
见决策 6）。为每个 rosbag 话题 `create_topic({name, type, cdr})`，
`write(SerializedBagMessage)`。把发布者 offered QoS 写进 `TopicMetadata`
（像 `ros2 bag record`），保证 `ros2 bag play` 用匹配 QoS 重放。析构写 metadata。
**用 `rosbag2_cpp::Writer`（与 `ros2 bag record` 同库）→ `ros2 bag` 可读性由构造保证。**

### `VideoRecorder`（每路一个）

libav 管线 `bgr8 → sws_scale → yuv420p → libx264 → mp4(libavformat)`。
codec/crf/preset/pix_fmt/container 从组 `params` 经 `av_opt_set` 应用。

- **PTS / CSV 双保险**：容器用细 timebase（如 1/90000）按 `ros_stamp` 设近似 VFR PTS；
  **权威时间在 sidecar CSV**——每帧到达即写一行。`ros_stamp` 优先取 `header.stamp`，
  为 0 则回退收到时间。
- 仅支持 bgr8/rgb8/mono8；遇 compressed/bayer/yuv 清晰告警并跳过该路（预览也跳过）。

CSV 格式 `<topic>.csv`：

```
frame_index,ros_stamp_ns,pts_ns
0,1718000000123456789,0
1,1718000000168912345,45455638
```

### 会话目录布局（`SessionManager`）

```
<output_dir>/2026-06-22_14-30-05/
  rosbag/        metadata.yaml + rosbag_0.mcap   (rosbag2 自己的)
  video/         camera__image_raw.mp4 + camera__image_raw.csv  (×N 路)
  session.yaml   会话描述符（停录时写，扫描只读这一个小文件）
```

话题名 → 文件名：去掉前导 `/`、其余 `/` 换 `__`。会话 id = 本地时间戳。

## 实时预览 + UI 模型重接线

### 相机帧通路（实时预览核心）

- ROS 线程：Image 回调把 bgr8 缓冲转成 `QImage`（`Format_BGR888`，轻量），存为
  per-topic 的 `shared_ptr<const QImage>`（原子换入 `LatestFrameStore`），帧序号 +1；
  发 queued `frameReady(topicKey, seq)` 给 GUI。
- GUI 线程：`CameraImageProvider`（`QQuickImageProvider`）按
  `image://camera/<topicKey>?seq=N` 返回最新帧。
- QML：`CameraPreviewTile.qml` 里的假 Canvas 换成
  `Image { source: "image://camera/"+topicKey+"?seq="+frameSeq; cache:false;
  fillMode: PreserveAspectFit }`——`seq` 变化即触发重新拉帧。`CameraGridModel`
  加 `FrameSeqRole`，`frameReady` 时 bump 并 `dataChanged`。
- 预览**不依赖录制**，实时查看就有画面。（22Hz×3 路对 image provider 无压力；
  未来要更高帧率再上 `QVideoSink`。）

### 真实 Hz / 分辨率

`TopicRateMonitor` 估每路 Hz（~1s 滑窗）+ 图像 w×h；引擎 ~2Hz 推快照 → LiveBridge →
`TopicListModel::updateStats()` 实时回填 `frequency_text`/`resolution_text`，
只对相关 role 发 `dataChanged`。删掉 `make_frequency_text`/`make_resolution_text`。

### 推进的时间轴

引擎以 steady_clock 为原点，~30Hz 定时器经 queued `advanceLiveEdge(t)` 推进 live edge
（取代当前永不前进的 live edge）。`startSession()` 把 live-edge 时钟重置到 0，
记 `record_start`，会话 t=0=record_start。于是录制中播放头读数 = 距录制起点秒数，
**标注时间无需换算直接落盘**。

### 数值轨道 v1 留空

删 `make_series_list`；`series_list` 保持空，`TimelineTrackRow` 已能优雅渲染空 lane
（只画网格）。数值内省是第二个 spec。

## 会话扫描 + session.yaml + 标注落盘

### `session.yaml` 格式

```yaml
# 由 data_recorder 在停止录制时自动生成
session: "2026-06-22_14-30-05"
recorded_at: { unix: 1782059150.228855, ros_time_ns: 1782059150228855043 }
duration_seconds: 42.512
topics:
  - { name: "/joint_states", backend: "rosbag" }
  - { name: "/camera/image_raw", backend: "video" }
tags:                                   # 本次会话选中的标签
  - { name: "成功", color: "#2f9e44" }
annotations:                            # 扁平实例列表，同名可多条，按时间升序
  - { name: "拿起水杯", shortcut: "1", kind: "point", color: "#1763c9", t: 3.210 }
  - { name: "碰撞",     shortcut: "c", kind: "point", color: "#e03131", t: 8.040 }
  - { name: "碰撞",     shortcut: "c", kind: "point", color: "#e03131", t: 12.880 }
  - { name: "倒水",     shortcut: "2", kind: "range", color: "#2f9e44", start: 5.0, end: 9.3 }
```

- `recorded_at` 把相对时间锚到 wall/ROS 绝对时钟，从而和 bag、视频 CSV 对齐。
- **不持久化 size_bytes**：会话面板的大小列改为扫描时按目录文件大小求和现算
  （stat 不读内容，很便宜）。size 是可派生量，不持久化以免变味。
- **annotations 是扁平实例列表**：同一 `name` 自然可出现多条（每条 = 一个被触发的
  实例）；序列化时遍历每个 marker 的全部 instances 各出一行，按时间升序。

### 标注落盘契约

一次会话的标注 = `stopSession()` 时对 `EventMarkerModel` + `TagListModel` 的快照 →
写文件。模型在 `startSession()` 时清空，每次录制从干净状态开始。标注的启用/禁用时机
沿用现有 UI 交互，后端只认"停录时的快照"。

### `RecordingSessionModel` 重接线

删 `populate_placeholder_sessions`；改由 `SessionManager::scan(output_dir)` 读各子目录的
`session.yaml` → 现有 role 直接映射（SizeText ← 格式化现算 size、Duration ←
duration_seconds、TagName/Color ← 首个 tag）。`AppController` 在**启动时**和
**每次停录后**触发扫描；扫描是文件 I/O，放 GUI 线程外做、结果 queued 回灌。

### 健壮性

缺 `session.yaml` 的目录（崩溃/进行中的会话）扫描时静默跳过并 log（崩溃恢复出 v1 范围）。
`output_dir`（`./recordings` 相对路径）启动时 `std::filesystem::absolute()` 解析为绝对
路径、`create_directories` 建好、并通过 `outputDirectory` 暴露解析后的绝对路径
（建议 config 用绝对路径）。

## 依赖

`package.xml` + rosdep 新增：

- `rosbag2_cpp`、`rosbag2_storage`、`sensor_msgs`、`tf2_msgs`。
- libav：`libavcodec-dev` / `libavformat-dev` / `libavutil-dev` / `libswscale-dev`
  （rosdep key 形如 `ffmpeg`/`libav*`）。
- `ros-humble-rosbag2-storage-mcap` 作 `exec_depend`。

CMake：`find_package` 上述 ROS 包；libav 用
`pkg_check_modules(LIBAV REQUIRED libavcodec libavformat libavutil libswscale)`
链接到 `data_recorder_core`。

## 错误处理 / 背压（v1 明确策略）

- **每 sink 有界队列**。**rosbag 队列**：录制是第一优先级——队列高水位时**阻塞回调**
  （短暂背压，宁慢不丢，保证 bag 完整）。**video 编码队列**：满时**丢最旧帧**并计数
  （视频可容忍丢帧，CSV 只记实际编码的帧；绝不阻塞到拖垮订阅）。两种策略都 log。
- **启动期失败**：output_dir 不可写、libav 编码器打不开、rosbag writer 建不了 →
  `toggleRecording` 不进入录制态，经 `statusText` 报错，订阅/预览继续。
- **不支持的图像编码**（非 bgr8/rgb8/mono8）：该路跳过预览与编码、清晰告警，
  其余话题照常。
- **退出**：main 收到 rclcpp shutdown → 若在录制则 `stopSession()` 干净收尾
  （flush 编码器、写 trailer/CSV、写 session.yaml）→ join spin/writer 线程 →
  Qt 退出。复用现有 shutdown timer 触发点。

## 测试策略

沿用项目 TDD + gtest 纪律，核心层零 Qt 可单测：

- `test_session_manager`：建目录布局、写/回读 `session.yaml`（含同名多 annotations、
  tags、时间字段）、扫描跳过无 session.yaml 的目录、大小现算。
- `test_video_recorder`：喂合成 bgr8 帧 → 产出可解码 mp4 + 行数匹配的 CSV +
  PTS 单调；不支持编码走跳过路径。
- `test_rosbag_writer`：写几条 serialized 消息 → 用 `rosbag2_cpp::Reader` 读回断言
  话题/类型/计数（程序内闭环验证 `ros2 bag` 可读性）。
- `test_writer_queue`：有界队列的阻塞（rosbag）vs 丢最旧（video）+ 计数语义。
- `test_topic_rate_monitor`：已知到达时刻 → Hz 估计在容差内。
- 改造现有 `test_ui_models`/`test_camera_grid_model`：删占位断言，改为"由注入的 stub
  更新驱动"（stats/帧序号回填、会话扫描结果映射）。
- QML 冒烟测试保持绿（image-provider 可在 offscreen 装 stub）。

## 验收

1. 全部 gtest 目标绿灯。
2. **真机端到端**（domain 43 实时话题）：跑 app → 看到 3 路相机实时画面 +
   真实 Hz/分辨率 → 录制 ~30s（其间打几个点/区间标注、选标签）→ 停止。
3. **产物验证**：`<session>/` 下有 `rosbag/`、`video/*.mp4`+`*.csv`、`session.yaml`；
   `ros2 bag info <session>/rosbag` 正确；**隔离 domain `ros2 bag play` 可放**；
   mp4 可解码、CSV 行数=帧数、时间戳单调；`session.yaml` 标注/标签/时间正确。
4. 会话面板出现该真实会话；重启 app 仍在（扫描生效）。
5. README 更新（依赖、运行、验证清单）。

## 风险与缓解

- **ROS 线程 vs Qt GUI 线程并发**：用有界队列 + `Qt::QueuedConnection` + 原子
  `shared_ptr` 交换最新帧，均为成熟模式；核心层零 Qt 依赖便于隔离单测。
- **libav C API 易错**：先以 `test_video_recorder`（合成帧 → 可解码 mp4 + CSV）
  锁定管线，再接真实订阅。
- **高频话题背压拖累订阅**：rosbag 阻塞背压保完整性、video 丢最旧保流畅，
  策略明确且计数可观测。
- **mcap 插件缺失**：运行时守卫检测注册情况，未注册则告警回退默认存储；
  `ros2 bag` 可读性由 `rosbag2_cpp::Writer` 构造保证（无论 mcap/sqlite3）。
