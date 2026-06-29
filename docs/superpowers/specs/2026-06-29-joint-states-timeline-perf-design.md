# 展开 /joint_states 时间轴曲线的性能优化设计

日期：2026-06-29
状态：设计已确认，待写实现计划
方式：外科手术式（低风险、行为基本不变）

## Context（背景与问题）

在历史回看模式下展开 `/joint_states` 轨道后，UI 变得非常卡（展开瞬间一顿 + 之后平移/缩放持续掉帧）。

该话题是全工程最重的数值轨道：本机录制为 **33 个关节**，提取器对每个关节产出 `pos/`、`vel/`、`eff/` 三条序列 ⇒ **99 条序列**。

## 根因（已量化验证）

历史曲线回读器把每条序列抽稀到 **2000 点预算**
（[history_curve_loader.cpp](../../../src/history_curve_loader.cpp) 的 `snapshot(/*budget=*/2000)`）。
于是展开 `/joint_states` 会把 **99 × 2000 ≈ 19.8 万个点**送进管线，每个点是一个 `QVariantMap{x,y}`。三处叠加成本：

1. **展开瞬间在 GUI 线程上三次往返**：worker 线程构建 19.8 万个 `QVariantMap`（[history_curve_loader.cpp](../../../src/history_curve_loader.cpp)，合理）→ GUI 线程 `AppController::onCurvesUpdated` 把它们解析回 C++ `vector<SeriesSnapshot>`（[app_controller.cpp](../../../src/app_controller.cpp) 的 `onCurvesUpdated`）→ GUI 线程 `TopicListModel::updateSeries` 又重建 19.8 万个 `QVariantMap`（[ui_models.cpp](../../../src/ui_models.cpp) 的 `updateSeries`）。
2. **每次重绘的重 JS 处理**：[TimelineTrackRow.qml](../../../qml/components/TimelineTrackRow.qml) 的 `Canvas.onPaint` 对可见点（默认仅 `pos/` 33 条 × 2000 ≈ 6.6 万点）做 **3 趟全量 O(N) 遍历**——求 minY/maxY、`collectDrawablePoints`、`collectVisibleSamples`——且每趟都做 `Number(point.y)` 这类逐点跨语言（QVariant→JS）转换。
3. **重绘风暴**：上述重绘在 `onVisibleStartSecondsChanged` / `onVisibleDurationSecondsChanged` 时触发，即每次平移/缩放每帧都重跑，且每帧的 paint 太重以致跟不上帧率，UI 线程被打满。

旁证：曲线提取在独立 worker 线程（`curve_loader_thread_`，见 [app_controller.cpp](../../../src/app_controller.cpp)）；历史回看下视口不随播放自动滚动（仅 `followingLiveEdge` 时才 `setWindow`，见 [TimelinePanel.qml](../../../qml/components/TimelinePanel.qml)），故播放本身不触发曲线重绘——卡顿专属于"展开 + 平移/缩放"，二者同根。

## Goals / Non-Goals

**Goals**
- 展开 `/joint_states` 后平移/缩放流畅（消除每帧重 JS 处理）。
- 减轻展开瞬间的一次性卡顿。
- 行为基本不变：曲线外观、配色、默认可见性（`pos/` 可见，`vel/`、`eff/` 默认隐藏）、纵轴稳定（平移不跳动）、采样圆点规则均保持。

**Non-Goals（保持 surgical）**
- 不改 C++↔QML 的点数据契约（仍是 `series:[{key,points:[{x,y}]}]`）。
- 不改 `curvesUpdated` / `curvesReady` 契约，不改 `onCurvesUpdated` / `updateSeries` 签名。
- 不引入视口感知重采样、不引入 C++ 自绘渲染项。

## 设计

### 改动 A — QML 端缓存（主要收益，行为不变）

文件：[TimelineTrackRow.qml](../../../qml/components/TimelineTrackRow.qml)

核心：把昂贵的"数值转换 + 求极值"从"每次 paint"挪到"仅 seriesList 变化时一次"，paint 只在缓存好的扁平数组上、只遍历可见区间。

- **缓存结构**（JS，存于 root 的属性/闭包）：
  - `cachedSeries`：数组，每项 `{ xs: Float64Array, ys: Float64Array, color }`，**仅包含可见序列**（默认隐藏的 66 条 `vel/`/`eff/` 不转换、不入缓存）。
  - `cachedMinY` / `cachedMaxY`：基于全部可见序列一次性算好的全局纵轴范围（保持"平移不跳动"现有行为）。
- **`buildCache()`**：遍历 `seriesList`，跳过 `visible === false` 的项，把其 `points` 一次性转成 `Float64Array` 的 `xs`/`ys`，同时累计 min/max。`Float64Array` 较普通数组更省内存、迭代更快。
- **触发关系**：
  - `onSeriesListChanged` → `buildCache()` 后 `requestPaint()`。（可见性切换会重发 `seriesList`，自动重建缓存，正确。）
  - `onVisibleStartSecondsChanged` / `onVisibleDurationSecondsChanged` → **仅 `requestPaint()`**（不重建缓存）。
  - `onIsExpandedChanged` / `onShowDataChanged` → 必要时 `requestPaint()`（缓存随 seriesList 已就绪）。
- **`onPaint`（曲线）**：
  - 读 `cachedMinY/maxY` 画网格与折线，不再现算极值。
  - 对每条 `cachedSeries`：因 `xs` 按时间升序，用**二分查找**定位可见窗口的 `[lo, hi]` 索引；折线绘制取 `[lo-1 … hi+1]` 切片（含左右各一个邻点）并对越界的首尾段按现有 `interpolateBoundaryPoint` 逻辑插值到视口边缘，保持折线贯穿边缘的观感。
  - 采样圆点：沿用 `averageSampleSpacing` / `shouldDrawSampleMarkers` 判据，但只基于可见切片计算与绘制。
- 复杂度：单次 paint 从 `O(总点数 × 3 趟 × 跨语言转换)` 降到 `O(可见点数 × 1 趟 × 原生 JS)`；转换成本从"每帧"降到"每次数据变化一次"。

> 说明：`Float64Array` 缓存纯属 QML 内部表示，**不改变** C++↔QML 的 `{x,y}` 点契约。

### 改动 B — 历史曲线点预算 2000 → 600

文件：[history_curve_loader.cpp](../../../src/history_curve_loader.cpp)

- 将 `extractTopic` 中 `snapshot(/*budget=*/2000)` 改为 **600**，提为具名常量（如 `kHistorySeriesBudget`）。
- 理由：画布宽约 700px，600 点在满视图下约 1 点/px 已足够；600 亦与实时模式现用值一致（[recorder_engine.cpp](../../../src/recorder_engine.cpp) 已用 600）。
- 作用：把展开瞬间 GUI 线程上的往返从 99×2000≈19.8 万降到 99×600≈5.9 万（约 3.3× 更快）。
- 唯一可感知的行为变化：极深缩放时折线略粗——已确认接受。
- **折叠点 `messageDots` 预算保持 2000**（`scanTimestamps` / `extractTopic` 中）：折叠点轻、且只在折叠态显示，非卡顿来源。

## 数据流（改动后）

```
worker: extractTopic → snapshot(600) → QVariantList<QVariantMap> (99×600)  [emit curvesReady]
  GUI : onCurvesUpdated 解析 → vector<SeriesSnapshot> → updateSeries 重建 QVariantList  [契约不变，仅数据更小]
  QML : seriesList 变 → buildCache()（仅可见 33 条转 Float64Array + 求全局 min/max）一次
  QML : 平移/缩放 → onPaint 读缓存 + 二分可见区间，单趟绘制（不重建缓存、不再逐点转换）
```

## 行为保持核对

- 曲线/网格/配色：一致。
- 默认可见性（`pos/` 显示、`vel/`/`eff/` 隐藏）：一致（缓存按 `visible` 过滤，切换重发 seriesList → 重建缓存）。
- 纵轴范围：仍取全部可见序列的全局 min/max（缓存化，不随平移变化）。
- 采样圆点：判据与外观一致，仅改为基于可见切片。
- 边缘折线：保留边界插值。

## 测试与验证

- **C++ 单测**：
  - 现有 [test_history_curve_loader.cpp](../../../test/test_history_curve_loader.cpp) 的 `LongTopicDownsamplingKeepsFullSessionSpan`、`ExtractTopicEmitsSeries` 断言的是**首尾时间跨度**与"未超预算时原样返回"，对单调测试数据 min/max 抽稀保端点，故预算改 600 后**仍通过**，无需改动。
  - **新增**一个断言：当某序列原始点数 > 600 时，输出点数 ≤ 600（锁定新预算）。
  - [test_topic_series.cpp](../../../test/test_topic_series.cpp) 直接以显式 budget 测 `decimate`/`snapshot`，不受历史预算常量影响，**不变**。
- **QML 逻辑**：把缓存构建、二分可见区间、边界插值写成可单测的纯函数；在 [test_qml_structure.cpp](../../../test/test_qml_structure.cpp) / [test_qml_smoke.cpp](../../../test/test_qml_smoke.cpp) 思路下校验其结果与现有 `collectDrawablePoints`/`collectVisibleSamples`/min-max 在样例上一致。
- **回归**：现有 `test_qml_structure` / `test_qml_smoke` 保持通过。
- **人工外观确认**：依记忆「点击自动化不可靠、`xwd`+`ffmpeg` 截图可行、可 `QT_QPA_PLATFORM=offscreen` 起进程」，截图确认展开 `/joint_states` 后曲线渲染与改动前一致；交互流畅度以复杂度分析背书（无可靠的无头交互基准）。

## 风险

- **二分 + 边界切片的正确性**：折线在视口边缘的接续、单点/空序列、全部点都在视口外等边界情形需覆盖——以纯函数单测兜底。
- **预算降低导致深缩放变粗**：已接受；预算为具名常量，后续可调或升级为视口感知重采样（非本次范围）。
- **缓存失效遗漏**：必须确保可见性切换、清空曲线（`clearCurves`）、会话切换都会重发 `seriesList` 从而重建缓存——实现时核对各 `dataChanged(SeriesListRole)` 触发点。

## 后续（本次不做）

- 扁平数组贯通 C++↔QML 契约，彻底消除逐点封送。
- 视口感知重采样（任意缩放恒定点数、全分辨率细节）。
- 消除 GUI 线程上 `onCurvesUpdated ↔ updateSeries` 的往返（直接复用 QVariantList 透传点）。
