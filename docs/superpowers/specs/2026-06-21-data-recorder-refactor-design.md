# data_recorder 正式实现前重构设计

状态：草案，待评审
日期：2026-06-21

## 背景与目标

`data_recorder` 处于 UI 设计阶段，经多轮迭代 UI 大体定型。在开始正式后端实现前，
对前端/控制层做一次结构性重构，消除迭代遗留的混乱，使后端能接入到干净的边界上。

本次重构遵循三条原则：

1. **全程 TDD、每阶段先绿灯再进下一阶段**（沿用本项目既有纪律）。
2. **默认行为不变**——除两处明确例外：颜色集中化时允许合并近似色（视觉可略变）、
   `toggleRecording` 的信号发射收敛（修正多余信号，对外可见状态不变）。
3. **为正式实现铺路，但不在本次实现后端**——只把占位数据源与展示模型之间的缝留清晰。

## 现状审阅（问题清单）

### A. 死代码（UI 已不使用，仅被测试维持）
- `AppController::cameraModel` / `trackModel` 两个 `TopicListModel` 实例及其属性/访问器/成员。
  `Main.qml` 只用 `topicModel`，QML 全库零引用。
- `ConfigData::camera_topics` / `track_topics`：与 `topics` + 每条 `ui_category` 完全冗余。
- 3 个孤儿 QML 组件（全库零引用）：`CameraPreviewPanel.qml`、`TopicListPanel.qml`、`TopicTrack.qml`。
- 仅以下测试在维持它们：`test_config_model`、`test_ui_models`、`test_qml_smoke`。

### B. 命名（"曲线区"已名不副实）
右侧区域现在渲染 事件标记轨道 + 话题轨道（camera/numeric/empty）+ 时间尺 + 播放头，
"曲线"只是 numeric 轨道的一种画法。但 `curve` 在 `TimelinePanel.qml` 出现 29 处
（`curveList`/`curveColumn`/`curveViewport`/`seekFromCurveX`/`syncCurveToInfo`/
`timelineCurveMouseArea`/`timelineCurvePlayhead`…）+ `TimelineCurveRow.qml` 文件名。
`docs/ui_terminology.md` 的 `CurveArea` 词条、以及可能过时的 `TopicList`/`TopicTrack` 词条需同步。

**已定命名方案：轨道区 / Track Area。**

### C. 过大 / 职责混杂的文件
- `CameraGridPanel.qml`（589 行）：① 手写的"可见相机过滤 + 拖拽重排"代理模型
  （`sourceCameras`/`cameraProxy`/`Instantiator`/`rebuildVisibleCameras`/`findSourceIndex…`，
  本质是在 JS 里重写 `QSortFilterProxyModel`）；② 网格布局算法（`chooseLayout` 打分）；
  ③ 拖放状态机。
- `EventTrackRow.qml`（436 行）：4 个几乎重复的 MouseArea（point / range 体 / 左右把手），
  各自重复 wheel 缩放 + 右键删除 + 拖拽。
- `TimelinePanel.qml`（368 行）：信息列、时间尺、轨道列、播放头、滚动同步混在一起。

### D. C++/QML 边界
- `ui_models.cpp` 把展示模型与硬编码占位数据混在一起：合成正弦序列（L47-70）、
  按行号奇偶伪造帧率/分辨率（L77-85）、写死 3 条历史 session（L634-642）。
- 相机过滤+重排逻辑在 QML/JS，正式实现（真实图像帧）时应在 C++。

### E. 信号 bug（TODO #3）
`AppController::toggleRecording()`（L134-153）无条件 emit 全部 5 个信号；
而同文件 `selectOnlineData` / `setPlayheadSeconds` 等已做"值变才发"守卫。

### F. 颜色散乱
全库 45 种不同颜色、约 130 处字面量，散在几乎每个 QML 文件；大量是近似重复的 slate 灰阶。

### G. 测试脆弱性
`test_qml_structure.cpp` 是白盒变更探测器，直接断言 QML 源码文本
（文件路径、`id:` 名、函数名、字符串字面量、像素尺寸、颜色 hex）。
重命名/拆分/颜色集中都会成片打破它。

## 相机过滤+重排：目标架构（scope C 核心）

这块逻辑分两部分，**均与"真实图像帧怎么来"无关**，故现在即可安全下沉，不会返工：
- 过滤：只显示 `isCamera && isVisible` 的相机。
- 重排：用户拖拽得到的可见顺序。

**决策：专用 C++ 模型 `CameraGridModel`（`QAbstractListModel`）。**
- 观察 `topic_model_`，过滤出可见相机；用 `beginMoveRows` 维护**显式顺序**；
  暴露 `Q_INVOKABLE void moveCamera(int from, int to)`；以 `appController.cameraGridModel` 暴露。
- 可见性单一真相源仍在 `topic_model_`（由时间轴信息行 `toggleTopicVisible` 切换）；
  `CameraGridModel` 是其派生消费者。`visibleCameraCount` 由该模型派生。
- 否决 `QSortFilterProxyModel`：任意拖拽顺序不是排序谓词，且不适合将来挂载每路实时帧。

**拖拽实时"挤开"效果保留**：挤开是视图层效果，由临时的 `dropInsertIndex` 驱动
（`insertIndexAtPoint` → `previewSequence` → `previewLayoutForKey` → 格子重算槽位 + `dropPlaceholder`），
**全程不碰模型**；只在落点 `onReleased` 调一次 `moveCamera`，模型落到正好是预览已显示的顺序，无跳变。
拖时持续改模型反而更差（模型抖动、将来搬动带帧数据的重行）——"拖时预览、落时提交"是正确且高性能的做法。

`CameraGridPanel` 按三职责拆开：**模型 → C++ `CameraGridModel`**；**布局几何 → QML 助手**
（`chooseLayout`/`layoutForIndex`/`cameraAspectRatio`）；**拖放交互 → QML 交互件**。

## 颜色集中化

### 方案
QML 单例 `qml/Theme.qml`（`pragma Singleton`）+ `qmldir` 声明，组件以 `Theme.accent` 等引用。
仅集中**主题色（chrome）**；数据色（config 的 tag/marker 色、numeric 分类色板）留在数据侧。

### 精简调色板（~14 token 覆盖 45 色；允许合并近似色）

| token | 取值 | 用途 |
| --- | --- | --- |
| `surface` | `#ffffff` | 面板/内容背景 |
| `surfaceAlt` | `#f1f5f9` | 次级背景（信息列、空轨道）；合并 #f8fafc/#f6f8fb/#eef2f7 |
| `windowBg` | `#e9edf3` | 窗口底色 |
| `rowSelected` | `#e8f1ff` | 选中行；合并 #eef5ff |
| `border` | `#cbd5e1` | 面板/单元边框；合并 #dbe3ef/#d7dde5/#c6d0dc/#b8c2cf |
| `gridLine` | `#e2e8f0` | 图表网格线 / 行分隔；合并 #e5e7eb |
| `tickStrong` | `#94a3b8` | 强调刻度 |
| `textPrimary` | `#111827` | 主文字；合并 #0f172a/#1f2937 |
| `textSecondary` | `#475569` | 次文字；合并 #334155 |
| `textMuted` | `#64748b` | 标签/刻度文字；合并 #718096 |
| `accent` | `#2563eb` | 主强调（选中框、拖拽高亮、默认序列色） |
| `accentSoft` | `#dbeafe` | 弱强调填充 |
| `danger` | `#dc2626` | 播放头 / 停止 / 错误 |
| `cameraTileBg` | `#162033` | 相机瓦片暗底；合并 #0f172a/#020617 |

（最终 token 集在实现时按实际引用微调；目标是简洁可读。）

### 数据色（保持为数据，不进 Theme）
- `kSeriesColors`（6 色，`ui_models.cpp`）：numeric 曲线分类色板，保持为 C++ 单一来源。
- config 的 tag/marker 颜色及默认值（`#8a94a6`/`#3b82f6`）：留在 `config_model.cpp`。
- 占位 session 的标签色：随真实后端消失。

## 分阶段计划（每阶段先绿灯）

### Phase 0 — 基线
构建 + 跑全部 5 个测试目标，记录全绿。重构只从绿灯出发。

### Phase 1 — 删死代码（行为不变）
- 删 `cameraModel`/`trackModel`（属性 L30-31 / 访问器 L52-53 / 成员 L96-97 / 实现 L109-117 / 填充 L32-33）。
- 删 `ConfigData::camera_topics`/`track_topics`（声明 L47-48 / 填充 config_model.cpp L107-111）。
- 删 3 个孤儿 QML：`CameraPreviewPanel.qml`、`TopicListPanel.qml`、`TopicTrack.qml`。
- 改测试：`test_config_model` L52-57、`test_ui_models` L33-34 & L636-647、`test_qml_smoke` L46-47。
- 审计疑似未用的 `TopicListModel` role（`SeriesRole`/`SeriesColorRole`/`CategoryRole`），
  对照 QML+测试确认后删除真未用者。

### Phase 2 — 命名清扫（曲线 → 轨道区/Track Area，行为不变）
- `TimelineCurveRow.qml` → `TimelineTrackRow.qml`；更新 TimelinePanel 内的 Repeater 使用。
- TimelinePanel 内：`curveList→trackLaneList`、`curveColumn→trackLaneColumn`、
  `curveViewport→trackLaneViewport`、`seekFromCurveX→seekFromLaneX`、
  `syncCurveToInfo/syncInfoToCurve→syncLaneToInfo/syncInfoToLane`、
  objectName `timelineCurveMouseArea→timelineLaneMouseArea`、`timelineCurvePlayhead→timelineLanePlayhead`。
- 更新 `docs/ui_terminology.md`：`CurveArea→TrackArea(轨道区)`；校正过时的 `TopicList`/`TopicTrack` 词条。
- 改测试：`test_qml_structure` L282-300（含测试名 `TimelineCurveArea…`）、`test_qml_smoke` L371 & L406。

### Phase 3 — 建 Theme 单例、替换颜色字面量
- 新建 `qml/Theme.qml`（`pragma Singleton`）+ `qmldir` 声明，接通 import。
- 将各 QML 的 chrome 颜色字面量替换为 `Theme.*`（数据色不动）。
- 改测试：`test_qml_structure` 中断言 hex 字面量处改为断言 `Theme.*` 引用，或去脆化。

### Phase 4 — 拆分 TimelinePanel 与 EventTrackRow（行为不变，逐个拆、之间保持绿灯）
- TimelinePanel：拆出左"轨道信息列"与右"轨道列"（时间尺 + 轨道 + 播放头 + range bar）；
  TimelinePanel 退为编排 + 滚动同步。
- EventTrackRow：把 4 个重复 MouseArea 抽成一个复用交互件，消除 4× 重复；point/range 视觉分件。

### Phase 5 — CameraGridPanel：模型下沉 C++ + 拆分（行为不变）
相机相关工作合并为一相，且**模型先行**（避免拆"即将被替换"的 JS 代码）：
1. **先下沉模型**：新建 `CameraGridModel`（见上"目标架构"），以 `appController.cameraGridModel` 暴露；
   `visibleCameraCount` 由它派生；gtest 锁定 过滤/可见性联动/move 重排/计数。
2. 替换 JS 的 `sourceCameras`/`cameraProxy`/`Instantiator`/`rebuild` 机器——此时文件已瘦一半。
3. **再拆剩余视图层**：布局助手（`chooseLayout`/`layoutForIndex`/`cameraAspectRatio`）/ 拖放交互件；
   网格瘦身为绑定 `cameraGridModel` 的 Repeater；拖拽预览留视图层，落点调一次 `moveCamera`。

### Phase 6 — toggleRecording 信号收敛（TODO #3）
- 每个 emit 加"值变才发"守卫，与兄弟方法对齐；核对 `test_qml_smoke` recording spy 期望。

### 贯穿项
- **展示模型 / 占位数据源留缝**：把合成序列、伪帧率/分辨率、写死 session 收到一个清晰命名的
  边界（如 `populate*Placeholder()` / `DemoDataSource`），让后端接入只动这一处。仅留缝，不实现后端。
- **测试去脆化（建议，待定）**：保留行为型冒烟/模型测试；精简 `test_qml_structure` 中
  只复述实现细节的白盒源码断言（尤其颜色/像素/`id` 名），降低后续重构成本。

## 风险与缓解
- **结构测试成片变红**：这是预期的——每个 phase 把相关测试一并更新，保持每步绿灯；
  并借机推进去脆化。
- **颜色合并致视觉微变**：已获授权（"不必完全相同"）；合并以语义为界，保持对比度与可读性。
- **CameraGridModel 行为回归**：先以 gtest 锁定过滤/重排/计数，再替换 QML；
  拖拽手感由保留的视图层预览保证。

## 验收
- 每个 phase 结束：全部 5 个测试目标绿灯。
- 全部完成后：人工核对 README "UI Verification" 清单（splitter、录制切换、相机网格无滚动条、
  可见性折叠、拖拽播放头、滚轮缩放、相机拖拽挤开）。
- 颜色全部经 `Theme`（数据色除外）；`grep` 确认无遗留 chrome hex 字面量。
- `docs/ui_terminology.md` 与实际组件名一致。
