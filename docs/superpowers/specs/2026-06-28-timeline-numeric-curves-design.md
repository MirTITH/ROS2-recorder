# 时间轴数值曲线（可展开/折叠的话题轨道）设计

日期：2026-06-28
状态：设计已确认，待写实现计划

## Context（背景与动机）

时间轴轨道区当前对 rosbag 话题只显示名称 + Hz，右侧轨道是空的。曲线**渲染层其实早已存在**——
[TimelineTrackRow.qml](../../../qml/components/TimelineTrackRow.qml) 有一套基于 Canvas 的数值曲线绘制
（网格线、折线、采样点），[TopicListModel](../../../src/ui_models.cpp) 也暴露了 `seriesList` role 和
6 色板 `kSeriesColors`——但 `seriesList` 被硬接成空（`row.series_list = QVariantList(); // v1 数值留空`）。

历来两件事被显式推迟为 post-v1（见 [2026-06-22-data-recorder-backend-design.md](2026-06-22-data-recorder-backend-design.md)）：
1. **消息内省**：反序列化任意消息、提取数值字段；
2. **历史会话数据加载**：当前回放只读 `video/*.mp4`+`*.csv`+`session.yaml`，从不读 `rosbag/`。

本特性正是补上这两块：在轨道区为**支持的 topic**绘制数值曲线，实时与历史都要。

## Goals / Non-Goals

**Goals（v1）**
- 每个话题行可**展开/折叠**：折叠=每条 msg 一个点（节奏/有无）；展开=数值曲线（仅可绘制类型）。
- 可绘制 = 该消息类型注册了 value-extractor；v1 内置 `JointState` / `WrenchStamped` / `JointTrajectory`。
- 展开态可逐条曲线**显隐**切换。
- **实时**（在线/录制）与**历史**（点开已录会话）都能画。
- 折叠/展开默认状态可在 config 配置。

**Non-Goals（YAGNI，留后续 spec）**
- 通用 rosidl 运行时内省（架构留 registry 口子，不实现）。
- YAML 里逐 series 配可见性（v1 只配 `ui_expanded`；可见性走 UI + 类型默认）。
- y 轴数值刻度/单位标注、TF/图像类曲线、展开与显隐状态跨会话持久化。

## 交互与布局

行有**折叠/展开**两种高度状态，左栏第二行加 chevron。

```
左栏 (TrackInfoColumn)            轨道区 (TrackLaneColumn)
─────────────────────────────────┼──────────────────────────────────────────
 /joint_states                   │ ·· ··· ·· ···· ·· ··· ·· ···   折叠：每 msg 一点
[>] 399 Hz · rosbag              │
─────────────────────────────────┼──────────────────────────────────────────
 /joint_states                   │   ╱╲      ╱‾‾╲                  展开：值曲线
[∨] 399 Hz · rosbag              │  ╱  ╲__╱╲╱    ╲___
 ● joint1.pos ● joint2.pos       │  （行变高，多条彩色曲线，仅可见 series）
 ● joint3.pos ○ joint4.pos       │  ○=已隐藏，点 chip 切换
─────────────────────────────────┼──────────────────────────────────────────
 /tf                             │ ··· ·· ···· ·· ··  不可绘制：只有折叠点，不能展开
[>] 19 Hz · rosbag (chevron 灰)  │
─────────────────────────────────┼──────────────────────────────────────────
 /camera/image_raw         👁     │ 视频行维持现状：fps + 眼睛预览，chevron 灰
[>] 22 fps · video               │
```

- **chevron**：左栏**第二行**（与 `Hz · backend` 同行，置其左），`>`折叠 / `∨`展开；`enabled = isPlottable`，否则灰、不可点。默认折叠（除非 config 配 `ui_expanded: true`）。
- **折叠态**：轨道区按每条 msg 时间戳沿基线画点（密集时按像素抽稀成"节奏条"）；**所有** rosbag 数据行都有，不依赖 extractor。
- **展开态**（仅可绘制）：行高增大，复用现有 Canvas 画多条彩色曲线，只画**可见** series；y 轴按当前可见 series 自动缩放（顺带解决 force 与 torque 量级差——隐藏一组另一组自动撑满）。
- **逐曲线显隐**：左栏 chevron 行下方列彩色 chip（series 名+色），点击切 `visible`。
- 左栏与轨道区**同一行等高**，都从模型 `isExpanded` 推（折叠矮 ~32px、展开高 ~120px）。

## 架构（实时 + 历史共用一套核心）

```
              ┌──────────────────────────────────────┐
              │  ValueExtractorRegistry (按 type 分发)  │
              │   JointState / WrenchStamped /         │
              │   JointTrajectory  (v1 内置)            │
              └──────────────────────────────────────┘
                      ▲ extract(type, bytes, stamp) → [{series_key, value}]
        ┌─────────────┴─────────────┐
   实时 │                           │ 历史
   on_rosbag_message            rosbag2 Reader(按 topic 过滤)
   (spin 线程)                   (会话打开/展开时, worker 线程)
        │                           │
        ▼                           ▼
   ┌──────────────────────────────────────────────┐
   │  TopicSeries (每 topic)                         │
   │   • msg 时间戳环形缓冲 → 折叠点                   │
   │   • 每 series 的 (t,value) 环形缓冲 → 展开曲线     │
   │   • 抽稀(decimate)到点数预算；可见性/配色          │
   └──────────────────────────────────────────────┘
        │  快照(已抽稀, 仅推必要量)
        ▼
   LiveBridge → TopicListModel(seriesList + messageDots + isExpanded/isPlottable) → QML Canvas
```

- **`ValueExtractor` 接口 + registry**：`extract(type, serialized, stamp_ns) → [{series_key, value}]`。
  `series_key` 语义稳定（JointState 按**关节名**配对，乱序也不串线）。`registry.has(type)` = "是否可绘制"。
  registry 即是日后接"通用内省兜底 extractor"的扩展点。
- **`TopicSeries`**：实时与历史都写它；含 msg 时间戳缓冲（折叠点）+ 每 series 值缓冲（展开曲线）；
  环形缓冲有界 + 抽稀到点数预算（应对 ~400 Hz）。承担可见性/配色状态。
- **实时**：`on_rosbag_message(topic,type,serialized)` 记时间戳 +（可绘制则）调 registry 提取并写入
  `TopicSeries`（spin 线程，加锁）；定时器（~5–10 Hz）构建抽稀快照，经 LiveBridge 推 UI。
- **历史**：会话打开时一遍读 rosbag 只取**时间戳**画折叠点；某 topic **首次展开**时再按 topic 过滤回读、
  反序列化提取值（懒加载，避免开包即解析整包），worker 线程跑、完成后推快照。复用同一 registry/抽稀。

## 内置 extractor 与默认 series（v1）

| 类型 | series | 默认可见 |
|------|--------|----------|
| `sensor_msgs/msg/JointState` | 每关节 `position`（key `pos/<joint>`）；`velocity`/`effort` 亦生成 | position 全可见；vel/eff 隐藏 |
| `geometry_msgs/msg/WrenchStamped` | `force.x/y/z`、`torque.x/y/z` | 全可见（6 条） |
| `trajectory_msgs/msg/JointTrajectory` | 每关节目标位置（取命令**最后一个**轨迹点，按 msg 时间采样） | 全可见 |

- 配色：循环 `kSeriesColors`，按 `series_key` 稳定分配。
- JointTrajectory 取"最后点"是简化（命令是整条路径，先够用；未来可细化为按 time_from_start 展开）。

## 配置（[config_model.cpp](../../../src/config_model.cpp)）

topics 列表项 = **裸字符串**（默认折叠）**或** **单键 map**（key=topic 名、value=选项）：

```yaml
groups:
  - topics:
      - /tf                                                     # 默认折叠
      - /joint_states: { ui_expanded: true }                    # 默认展开
      - /left_force_torque_sensor_broadcaster/wrench: { ui_expanded: true }
    backend: rosbag
```

- 解析：`node.IsScalar()` → 名 + 默认；`node.IsMap()`（单键）→ key=名、value 读 `ui_expanded`。
- `TopicEntry` 加 `bool default_expanded{false}`。组级 `backend`/`params` 不受影响。
- 选项键统一 `ui_*` 前缀（明确 UI 关注点，留位将来加别的）。

## 模型与 QML 改动

**[TopicListModel](../../../src/ui_models.cpp) 新增 role / 方法**
- `isPlottable`（= `registry.has(type)`）→ 控 chevron 可点/灰。
- `isExpanded`（读写）→ 控行高与折叠/展开渲染；初值取 `default_expanded`。
- `messageDots`（抽稀后的 msg 时间戳数组）→ 折叠点。
- `seriesList` 扩展：每条 `{key, label, color, visible, points:[{x,y}]}`。
- 方法（经 AppController 暴露 QML）：`setExpanded(topicKey,bool)`、`setSeriesVisible(topicKey,seriesKey,bool)`。

**QML**
- [TimelineInfoRow.qml](../../../qml/components/TimelineInfoRow.qml)（左栏）：第二行加 chevron（`enabled: isPlottable`）；
  展开时下方 Repeater 列 chip（点击切 `visible`）；行高随 `isExpanded`。
- [TimelineTrackRow.qml](../../../qml/components/TimelineTrackRow.qml)（轨道区）：折叠→画 `messageDots`（沿基线 `viewport.xAtTime(t)`）；
  展开→复用现有曲线 Canvas，只画可见 series，y 轴按可见 series 自动缩放；行高随 `isExpanded`。
- 行高同步：左右栏同行都读模型 `isExpanded`。

**联动**：QML 点 chevron → AppController → 模型置位 → `dataChanged` 重绘；历史模式「展开」额外触发该 topic 懒回读。

## 数据量与保留

- 每 series 推 UI 前抽稀到点数预算（约 2000）；QML 再按视口裁剪。
- 实时为有界环形缓冲（超量丢最旧）。
- 折叠点同样抽稀（密集 msg 按像素合并）。

## 实现分步（同一 spec、同一核心）

1. **核心 + 实时**：`ValueExtractor`/registry/内置 extractor、`TopicSeries`、config 解析、模型新 role、
   QML chevron/折叠点/展开曲线/显隐、实时数据流。
2. **历史**：会话打开扫时间戳、展开懒回读 rosbag、worker 线程加载。

## 测试

- **单元**：各 extractor（喂一条已知 msg，断言 series_key/value，按关节名配对）；`TopicSeries` 环形缓冲+抽稀
  （点数预算、时序、降采样正确）；config 解析 scalar vs 单键 map（`ui_expanded` true/缺省）；registry
  可绘制判定；`TopicListModel` 新 role + `setExpanded`/`setSeriesVisible` 触发 `dataChanged`（仿现有
  [test_ui_models.cpp](../../../test/test_ui_models.cpp)）。
- **历史 round-trip**：用 rosbag2 写一段含 JointState 的小包 → 走回读路径 → 断言提取出曲线
  （仿 [test_rosbag_writer.cpp](../../../test/test_rosbag_writer.cpp) 链接 sensor_msgs 等）。
- **人工**：带 GUI 跑，展开 `/joint_states` 看曲线、切 chip 显隐、力矩行展开（参照既有 xwd+ffmpeg 截图核验法）。

## 注意
- 本仓 C++ 是 ament 风格，仓内 `.clang-format` 是 LLVM、会误导——不要跑 clang-format，手动匹配周边风格。
- extractor 需链接对应消息包（sensor_msgs / geometry_msgs / trajectory_msgs），CMakeLists 加依赖。
