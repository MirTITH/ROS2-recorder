# UI Terminology

| 中文名称 | English Name | Code Symbol | Purpose |
| --- | --- | --- | --- |
| 相机预览 | Camera Preview | `CameraPreview` | Image topic 的画面预览区域 |
| 相机预览区 | Camera Preview Area | `CameraPreviewArea` | 多路相机预览所在的上方面板组 |
| 数据 | Data | `Data` | 在线数据源和历史采集记录所在的左侧面板 |
| 数据源选择器 | Data Source Selector | `DataSourceSelector` | 在在线数据和历史记录之间切换当前查看数据源的列表 |
| 在线数据 | Online Data | `OnlineData` | 当前连接的 ROS topic 数据源 |
| 历史查看 | Historical Review | `HistoricalReview` | 查看已完成采集记录的 UI 状态 |
| 录制中回看 | Recording Review | `RecordingReview` | 录制仍在继续但播放头脱离实时端的 UI 状态 |
| 采集记录 | Recording Sessions | `RecordingSessions` | 历史采集 session 列表 |
| 记录标签 | Recording Tags | `RecordingTags` | 成功、失败、碰撞等整段记录标签 |
| 事件标记 | Event Markers | `EventMarkers` | 快捷键触发的 point/range 时间标记 |
| 事件标记轨道 | Event Marker Track | `EventMarkerTrack` | 时间轴中每个事件标记对应的一行 |
| 事件标记实例 | Event Marker Instance | `EventMarkerInstance` | 时间轴中一个具体的点事件或区间事件 |
| 话题轨道 | Topic Track | `TimelineTrackRow` | 时间轴中每个 topic 对应的一行 |
| 时间轴 | Timeline | `Timeline` | 播放头、刻度、曲线、轨道所在区域 |
| 播放头 | Playhead | `Playhead` | 当前时间位置指示线 |
| 保存目录 | Output Directory | `OutputDirectory` | YAML 中的 `output_dir` |
| 后端 | Backend | `Backend` | `rosbag` / `video` 等录制后端 |
| 配置文件 | Config File | `ConfigFile` | 当前加载的 YAML |
| 工作区 | Workspace | `Workspace` | 整个可调整面板布局 |
| 面板 | Panel | `Panel` | 统一样式、可组合的 UI 模块 |
| 分隔条 | Splitter Handle | `SplitterHandle` | 可拖动调整面板大小的分隔控件 |
| 时间轴信息面板 | Timeline Information Pane | `TimelineInfoPane` | 时间轴左侧 topic 信息、播放头时间、可见性控制区域 |
| 轨道区 | Track Area | `TrackArea` | 时间轴右侧各类轨道、时间尺、播放头所在区域 |
| 相机网格 | Camera Grid | `CameraGrid` | 自动排列所有可见相机预览的区域 |
| 标签片 | Tag Chip | `TagChip` | 采集记录和记录标签中复用的彩色标签 |
| 实时端 | Live Edge | `LiveEdge` | 录制中模拟的最新采集时间位置 |
| 轨道类型 | Track Kind | `TrackKind` | topic 在时间轴中的 `camera`、`numeric`、`empty` 分类 |

## Naming Rules

- QML component names use PascalCase, such as `TimelineTrackRow.qml`.
- C++ type names use PascalCase, such as `TopicListModel`.
- QML properties, Qt properties, and model roles use lowerCamelCase, such as `topicName`, `backendName`, and `isVisible`.
- UI labels use the Chinese names in this document.
- Developer-facing prose may use the English names in this document.
