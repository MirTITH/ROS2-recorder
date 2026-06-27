# 历史回放模式设计文档

日期:2026-06-28
模块:`src/data_recorder`

## 1. 背景与问题

点击"数据"面板里的历史会话行时,整个界面没有切换到历史回放模式。

根因:`AppController::selectHistorySession()`(`src/app_controller.cpp:236`)只翻转 `history_mode_`
标志并更新状态栏文字。实时帧/统计信号(`onFrameReady`/`onStatsUpdated`)仍无条件把实时数据推进
`camera_grid_model_`/`topic_model_`;被选会话的录制数据(时长、话题、标注、标签)从未被加载;时间轴
时长仍由实时端驱动;播放头不复位;主视图面板(`CameraGridPanel`/`TimelinePanel`)根本没有绑定
`historyMode`。后端也没有任何回放/读取能力——`SessionManager` 只做目录扫描,`RecorderEngine` 只录制。

因此现状下选中历史会话只会:高亮会话行、改状态栏文字、禁用录制按钮;主视图(相机 + 时间轴)始终
停留在实时数据。

## 2. 目标与范围

实现"完整回放":

- 解码会话录制的相机 mp4,随播放头在相机网格中播放。
- 从 `session.yaml` 加载会话的话题列表、标注(annotations)、标签(tags)、时长。
- 播放控制:播放/暂停(固定 1x)、拖动时间轴播放头定位(seek)、返回在线数据。
- 选中会话后默认**暂停并显示首帧**,需手动点播放。

**不在范围内**:

- 不读取 `rosbag/*.mcap`。非图像话题(`/tf`、`/joint_states` 等)只作为**静态列表**展示
  (来自 `session.yaml`),不随播放头还原其速率/活动。
- 不支持倍速(只 1x)。

## 3. 录制数据的磁盘格式(已确认)

每个会话目录 `recordings/<session_id>/`:

- `session.yaml`:`duration_seconds`、`topics`(每项 `name` + `backend` ∈ {`rosbag`,`video`})、
  `tags`、`annotations`(`kind` ∈ {`point`,`range`},point 用 `t`,range 用 `start`/`end`)。
- `video/<topic>.mp4` + `video/<topic>.csv`:每个 `backend==video` 的相机一对。
  CSV 列:`frame_index,ros_stamp_ns,pts_ns`。每帧相对秒 = `(ros_stamp_ns - 首帧 ros_stamp_ns) / 1e9`。
- `rosbag/rosbag_0.mcap` + `rosbag/metadata.yaml`:非图像话题(本设计不读取)。

`SessionRecord`(`include/data_recorder/recorder_types.hpp`)已携带上述全部字段,由
`SessionManager::scan()` 填充。

## 4. 架构总览

帧路由采用**方案 A:复用 `LiveBridge` 作为唯一"当前帧仓库"**。`LiveBridge` 增加 `playback` 标志:
置位时丢弃来自 ROS 引擎的实时帧/统计,只放行播放器推送的帧。整个显示侧
(`CameraImageProvider`、`CameraGridModel`、QML)完全不改。

```
                   ┌─────────────── GUI 线程 ───────────────┐
SessionPlayer 线程  │  AppController                          │
┌──────────────┐   │   ├─ history_mode_ / playing_ 状态      │
│ SessionPlayer│   │   ├─ scanned_sessions_ 缓存(完整记录)  │
│  ├ QTimer时钟│   │   ├─ topic_model_ / event_marker_model_ │
│  ├ VideoClip │   │   ├─ tag_model_ / camera_grid_model_    │
│  │  Reader×N │   │   └─ SessionPlayer*(持有,跨线程)      │
│  └ push帧 ───┼───┼──► LiveBridge(playback 标志 + 帧仓库)  │
└──────────────┘   │        └─ frameReady ─► CameraGridModel │
                   │             └─► image://camera ─► 显示  │
                   └────────────────────────────────────────┘
```

`SessionPlayer` 跑在独立 `QThread`,镜像现有 "ROS 线程 → LiveBridge → GUI" 架构,使多路 H.264
解码不阻塞 GUI 线程。

## 5. 新增组件

### 5.1 `VideoClipReader`(新文件 `include/.../video_clip_reader.hpp` + `src/video_clip_reader.cpp`)

单路相机 mp4+csv 的解码器。一个实例对应一个相机话题。

- 构造/`open(mp4_path, csv_path)`:
  - libav 打开 mp4:`avformat_open_input` → `avformat_find_stream_info` →
    `av_find_best_stream(AVMEDIA_TYPE_VIDEO)` → `avcodec_alloc_context3` +
    `avcodec_parameters_to_context` → `avcodec_open2`。
  - 解析 CSV,构建 `std::vector<FrameIndexEntry{ double rel_seconds; int64_t pts_ns; }>`,
    按时间升序。`rel_seconds` 用首帧 `ros_stamp_ns` 归零。
- `QImage frameAtSeconds(double t)`:
  - 二分查找 `rel_seconds ≤ t` 的最大帧(t 小于首帧则取第 0 帧);得到目标帧的索引/pts。
  - 若目标帧索引 == 上次已解码帧索引,直接返回缓存的 `QImage`(顺序播放/同帧 seek 时零解码)。
  - 否则:若目标在当前解码位置之后且相邻,顺序 `av_read_frame`/`avcodec_send_packet`/
    `avcodec_receive_frame` 前进解码;若向后跳或跨度大,先 `av_seek_frame`(`AVSEEK_FLAG_BACKWARD`)
    到目标 pts 的关键帧,再解码前进到目标帧。
  - 解出的 YUV420P 帧经 `sws_scale` 转 `AV_PIX_FMT_RGB24` → 拷入 `QImage(RGB888)`。镜像
    `VideoRecorder` 的 libav/sws 用法(同库已链接)。
  - 缓存最近解码帧索引与 `QImage`。
- 析构:释放 sws、codec context、format context。
- 错误处理:打开失败 / CSV 缺失 / 流缺失时进入"无效"态,`frameAtSeconds` 返回空 `QImage`;
  `SessionPlayer` 跳过无效 reader(对应相机显示占位黑帧,沿用 `CameraImageProvider` 现有占位)。

> 备注:不假设 CSV 的 `pts_ns` 与解码器时间基一致。seek 以 CSV 行的 `pts_ns` 为目标,实际解码以
> 逐帧 `frame_index` 对齐;若 seek 后落点与目标帧不符,顺序解码前进到目标 `frame_index`。

### 5.2 `SessionPlayer`(新文件 `include/.../session_player.hpp` + `src/session_player.cpp`)

`QObject`,运行在独立 `QThread`;持有 `LiveBridge*`(非拥有,线程安全)。

- `void load(const SessionRecord & session)`:
  - 清空旧 reader;对每个 `backend=="video"` 的话题建一个 `VideoClipReader`(topic_key 与
    `CameraGridModel` 一致,见 §7);记录 `duration_seconds_`;`playhead_ = 0`;`playing_ = false`。
  - 立即解码并推送一次首帧(t=0),让选中会话默认显示首帧。
- `void play()` / `void pause()` / `void togglePlay()`:切换 `playing_`;`play` 时启动
  `QTimer`(间隔约 33ms),`pause` 时停。`emit playingChanged(playing_)`。
- `void seek(double t)`:`playhead_ = clamp(t, 0, duration)`;立即解码并推送该 t 的帧;
  `emit playheadAdvanced(playhead_)`。
- `void stop()`:暂停、清空 reader、`playing_=false`、`playhead_=0`。
- 定时器槽:`playhead_ += 间隔秒`;到达/超过 `duration_seconds_` 则钳到末尾并 `pause()`;
  对每个 reader 取 `frameAtSeconds(playhead_)` 并 `bridge_->push_playback_frame(key, img)`;
  `emit playheadAdvanced(playhead_)`。
- 信号:`playheadAdvanced(double)`、`playingChanged(bool)`。
- 线程:`SessionPlayer` `moveToThread(player_thread_)`;`load/play/pause/seek/stop` 经
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 调用,定时器在该线程运行。推帧经
  `LiveBridge`(既有 mutex + queued `frameReady`)安全跨回 GUI 线程。

## 6. 改造既有组件

### 6.1 `LiveBridge`

- 新增 `std::atomic<bool> playback_mode_{false}` 与 `void set_playback_mode(bool)`。
- 新增 `void push_playback_frame(const QString & topic_key, std::shared_ptr<const QImage> image)`:
  无视 `playback_mode_`,写入帧仓库、自增该 key 的 seq、`emit frameReady(topic_key, seq)`。
- 现有 `push_frame(...)` 与 `push_stats(...)`:在 `playback_mode_` 为真时**早退**(丢弃实时数据)。

> 这样所有"实时 vs 回放"的门控集中在一处。`AppController::onFrameReady/onStatsUpdated` 无需改动:
> 回放模式下它们只会收到播放器推送帧产生的 `frameReady`,实时帧在 `LiveBridge` 处已被丢弃。

### 6.2 `EventMarkerModel`(`ui_models.hpp` / `ui_models.cpp`)

- 新增 `void setInstances(const std::vector<AnnotationRecord> & annotations)`:先 `clearInstances()`,
  再把每条标注按 `shortcut`(主键)匹配到对应 marker 行,`point` 建一个 `start==t` 的实例,
  `range` 建 `start`/`end` 实例(沿用内部 `EventInstance`、`next_instance_id`)。用于历史标注只读展示。
  marker 行来自配置,历史标注理应都能匹配;**无匹配行的标注直接忽略**(不动态新增行)。

### 6.3 `TagListModel`(`ui_models.hpp` / `ui_models.cpp`)

- 新增 `void setSelectedTags(const std::vector<TagRecord> & tags)`:将会话里出现的标签标记为选中态
  展示。当前 `IsSelectedRole` 由单选 `selected_row_` 决定;为支持会话的**多个**选中标签,新增成员
  `std::vector<std::string> session_selected_names_`,`setSelectedTags` 填充它并 `dataChanged` 全行;
  `data(IsSelectedRole)` 返回 `selected_row_ == row || name ∈ session_selected_names_`。
  `clearSelection()` 同时清空 `selected_row_` 与 `session_selected_names_`。不破坏现有实时单选行为。

### 6.4 `AppController`(`app_controller.hpp` / `app_controller.cpp`)

新增成员:

- `SessionPlayer * player_{nullptr}` + `QThread * player_thread_{nullptr}`(构造时创建并启动)。
- `std::vector<SessionRecord> scanned_sessions_`:缓存最近一次 `scan()` 的完整记录(供按 row 取)。
- `std::vector<TopicEntry> live_topics_`:构造时保存 `config.topics`,返回在线时恢复。
- `bool playing_{false}`(镜像播放器状态)。

新增属性/方法:

- `Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)`。
- `Q_INVOKABLE void togglePlayback()` → `player_->togglePlay()`。

`refreshSessions()`:在 `setSessions(...)` 同时把完整 `std::vector<SessionRecord>` 存入
`scanned_sessions_`。

`selectHistorySession(int row)`(在现有翻转标志 + 状态栏基础上扩展):

1. 现有守卫保留:`recording_ || row 越界` 直接返回。
2. 取 `scanned_sessions_[row]`(校验与 `recording_session_model_` 行一致)。
3. `topic_model_.set_topics(map(session.topics))`:`backend=="video"` → `ui_category=CameraPreview`,
   否则 `NumericTrack`(见 §7);触发相机网格重建为会话相机。
4. `event_marker_model_.setInstances(session.annotations)`。
5. `tag_model_.setSelectedTags(session.tags)`。
6. `playhead_seconds_ = 0`;发 `playheadSecondsChanged`、`timelineDurationSecondsChanged`。
7. `bridge_->set_playback_mode(true)`。
8. `player_->load(session)`(队列调用;暂停态 + 推首帧)。
9. 维持现有 `dataSourceChanged`/`canRecordChanged`/`modeTextChanged`/`statusTextChanged` 发射逻辑。

`selectOnlineData()`(在现有基础上扩展):

1. `bridge_->set_playback_mode(false)`;`player_->stop()`。
2. `topic_model_.set_topics(live_topics_)` 恢复实时话题。
3. `event_marker_model_.clearInstances()`;`tag_model_.clearSelection()`。
4. `playhead_seconds_ = 0`;`playing_ = false`。
5. 维持现有信号发射。

`timelineDurationSeconds()`:`history_mode_` 时返回当前会话 `duration_seconds`(从
`scanned_sessions_[selected_session_row_]` 取);否则维持现有
`max(live_edge, playhead, kDefaultTimelineSpanSeconds)`。

`setPlayheadSeconds(double t)`:`history_mode_` 时改为 `player_->seek(t)`(并由播放器
`playheadAdvanced` 回填 `playhead_seconds_`);非历史维持现有逻辑。

播放器信号接线:`player_->playheadAdvanced` → 更新 `playhead_seconds_` + `emit playheadSecondsChanged`;
`player_->playingChanged` → 更新 `playing_` + `emit playingChanged`。

构造时若 `bridge_ == nullptr`(测试场景),`player_` 仍可创建但 reader 无帧可推;历史切换的状态机
逻辑与播放器解码解耦,便于单测。

析构:停止并 `quit()`/`wait()` `player_thread_`。

## 7. topic_key 一致性

`CameraGridModel` 用 `key_of(topic, backend) = topic + "|" + backend` 作相机身份键;
`CameraImageProvider` 用 `image://camera/<topicKey>`,而 `CameraGridModel` 的 `TopicKeyRole`
当前返回 `c.topic_name`(见 Explore 报告)。`SessionPlayer` 推帧用的 key 必须与
`CameraImageProvider::requestImage` 解析、并与 `CameraGridModel.updateFrameSeq` 匹配的 key 完全一致。

实现时以"现有实时链路中 ROS 引擎 `push_frame` 用的 topic_key"为准:`SessionPlayer` 对每个 video
话题采用相同的 key 约定(实现阶段核对 `RecorderEngine`/`LiveBridge` 实际使用的键),保证回放帧命中
相机瓦片。该一致性在 §9 测试中以 `CameraGridModel.updateFrameSeq` 命中验证。

## 8. UI 改造

### 8.1 主操作按钮迁移到时间轴信息列顶部

位置:`qml/components/TrackInfoColumn.qml` 顶部 30px header 行(行 32-63),播放头时间标签右侧
(现已有"回到实时"按钮)。在该行放一个随模式切换的**上下文主操作按钮**:

| 模式 | 按钮文案 | 动作 | 可见/可用 |
|------|---------|------|----------|
| 在线(未录制) | `录制` | `controller.toggleRecording()` | 可见;`enabled = controller.canRecord` |
| 录制中 | `停止` | `controller.toggleRecording()` | 可见;"回到实时"在脱离实时端时并列 |
| 历史回放 | `播放`/`暂停` | `controller.togglePlayback()` | 文案随 `controller.playing` 切换 |

按钮选择逻辑由 `controller.historyMode` / `controller.recording` 决定。`TrackInfoColumn` 已持有
`controller`,直接绑定。

### 8.2 底部状态栏移除录制按钮

`qml/components/StatusBar.qml`:删除 `recordButton`,保留状态文字 + 磁盘占位。录制按钮原有的
文案(录制↔停止)与 `enabled = canRecord` 逻辑迁移到 §8.1 的红框按钮。

### 8.3 录制中禁止点击历史数据

`qml/components/RecordingSessionsPanel.qml` 历史会话 delegate:`controller.recording` 为真时
**灰显且不可点**(delegate `enabled: !(controller && controller.recording)`,并降低不透明度作视觉提示)。
后端 `selectHistorySession` 已有 `recording_` 守卫,UI 与之同步。

## 9. 测试

沿用现有 gtest + QML 测试结构(`test/`),新增/扩展:

- `VideoClipReader` 解码冒烟测试:用仓库现有 `recordings/<id>/video/*.mp4 + *.csv`,验证
  `open` 成功、给定若干 `t` 返回非空且尺寸与 CSV/流分辨率一致的 `QImage`;`t` 超出末尾被钳;
  重复同 t 命中缓存。
- `EventMarkerModel::setInstances`:给定 point/range 标注,验证对应行实例数、起止时间正确;
  `clearInstances` 复位。
- `TagListModel::setSelectedTags`:验证会话标签被标为选中。
- `AppController` 历史切换状态机:`selectHistorySession`/`selectOnlineData` 切换后
  `historyMode`/`canRecord`/`timelineDurationSeconds`/`playing` 及相应信号正确(播放器解码可不触及,
  聚焦状态与信号)。
- `CameraGridModel` 命中:验证 `SessionPlayer` 选用的 key 经 `updateFrameSeq` 能命中可见相机行
  (key 一致性,§7)。
- `LiveBridge`:`playback_mode_` 为真时 `push_frame`/`push_stats` 被丢弃,`push_playback_frame`
  仍生效并发 `frameReady`。

构建:`CMakeLists.txt` 把 `video_clip_reader.{hpp,cpp}`、`session_player.{hpp,cpp}` 加入
`data_recorder_core`(libav 已通过 `PkgConfig::LIBAV` 链接);新增测试目标按现有 `test_*` 模式登记。

## 10. 风险与注意

- **解码性能**:多路 H.264 在播放器线程解码 + sws 转换,1x、~30fps 应可接受;若卡顿,可按帧间隔
  自适应丢帧(取最接近 t 的帧而非逐帧)。已通过"按 t 取帧 + 缓存"天然支持丢帧。
- **seek 精度**:依赖关键帧间隔(`VideoRecorder` 写入 `gop_size=60`),向后 seek 需解码到目标帧,
  跨大跨度 seek 可能有短暂延迟;可接受。
- **topic_key 一致性**(§7)是回放能否上屏的关键,实现阶段需以实际实时链路键为准核对。
- **`ament` 风格**:本包 C++ 为 ament 风格,LLVM `.clang-format` 会误导,不要据其格式化
  (见项目记忆 `dont-run-clang-format-ament-style`)。
