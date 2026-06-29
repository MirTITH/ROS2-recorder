# 时间轴曲线性能优化（第二轮）：扁平 double 数组消除逐点 QVariantMap 封送

日期：2026-06-29
状态：设计已确认（两数组 xs/ys），待写实现计划
方式：数据契约改造（有测量背书）

## Context（背景与测量结论）

第一轮（预算 2000→600 + QML Float64Array 缓存 + 二分可见区间）已合入但**未消除卡顿**。
按用户要求加了 `DR_PROFILE` 探针与无头基准 `bench_timeline_curves`，量化三阶段（99 序列 × 600 点）：

| 阶段 | 耗时 | 频率 |
|---|---|---|
| **`onCurvesUpdated`**（GUI 线程 QVariant 往返） | **171 ms** | 展开一次；**实时录制时每 200ms 一次** |
| `QML.rebuildCache` | 3 ms | 每 seriesList 变化一次 |
| `QML.curvePaint`（每行） | 5–8 ms | 每平移/缩放帧 |

**主因**：每个点被表示为 `QVariantMap{"x","y"}`。worker 线程构建 → `AppController::onCurvesUpdated` 在 GUI 线程**逐点拆**（`pm.value("x").toDouble()`）→ `TopicListModel::updateSeries` 又**逐点重建** `QVariantMap`（[ui_models.cpp:291-296](../../../src/ui_models.cpp)）。99×600=59,400 点 × 两侧 = ~12 万次 `QVariantMap` 构造/析构 + 字符串键查找，全在 GUI 线程。

**实时尤甚**：`curves_timer_`（[recorder_engine.cpp:94](../../../src/recorder_engine.cpp)）每 200ms 推一帧；展开 `/joint_states` 时每帧都走这条 171ms 的槽 → UI 线程约 85% 被占（用户所述"渐冻/很卡"）。背压只防彻底冻死，挡不住持续卡顿。

第一轮改在渲染层——**找错了主导项所在的层**。

## Goals / Non-Goals

**Goals**
- 把 `onCurvesUpdated` 的 171ms 降一个数量级（目标 < ~20ms）。
- 实时与历史、展开瞬间与持续推送都受益。
- 行为不变：曲线外观、配色、默认可见性、纵轴、采样点、折叠点。

**Non-Goals（保持聚焦）**
- 不改 `curvesUpdated`/`curvesReady` 的**信号签名**（仍是 `QVariantList`）；只改其中**点的表示**。
- 不引入视口感知重采样。
- 不动折叠点 `messageDots`（已是 `[double]`，本就轻）。

## 设计：点表示 `[{x,y}]` → 两个并列 double 数组 `xs[] / ys[]`

把 series 项里的 `points:[{x,y}]` 换成两个并列数组：

```
旧： { "key", "label", "color", "visible", "points": [ {"x":t0,"y":v0}, {"x":t1,"y":v1}, ... ] }
新： { "key", "label", "color", "visible", "xs": [t0,t1,...], "ys": [v0,v1,...] }
```

C++ 侧用 `QList<double>`（QVariant 原生支持，封送为 JS number 数组，**无逐点 QVariantMap**）。
两数组等长；`xs` 升序（与现契约一致）。

### 改动点（全契约面）

**生产端（worker 线程，构建负载）**
1. [curve_payload.cpp](../../../src/curve_payload.cpp) `build_curve_payload`：每条 series 写 `xs`/`ys`（`QList<double>`）替代 `points`。
2. [history_curve_loader.cpp](../../../src/history_curve_loader.cpp) `extractTopic`：同上。
3. [curve_payload.hpp](../../../include/data_recorder/curve_payload.hpp) 顶部契约注释更新为 `xs/ys`。

**消费端（GUI 线程）**
4. [app_controller.cpp](../../../src/app_controller.cpp) `onCurvesUpdated`：读 `xs`/`ys` 两个 `QVariantList`，等长配对填 `SeriesSnapshot.points`（C++ 内部仍用 `vector<pair<double,double>>`，不外泄表示）。用 `QVariantList` 还是更快的 `value<QList<double>>()` 由实现择优；至少避免逐点 `toMap()`。
5. [ui_models.cpp](../../../src/ui_models.cpp) `updateSeries`：把 `SeriesSnapshot.points` 写成 `xs`/`ys` 两个 `QVariantList<double>`（替代逐点 `QVariantMap`）。

**QML 渲染端**
6. [curve_plot.js](../../../qml/components/curve_plot.js) `buildSeriesCache`：入参从 `entry.points[{x,y}]` 改为 `entry.xs`/`entry.ys`，直接灌 `Float64Array`（更省：无逐点 `.x/.y` 属性读）。其余（min/max、过滤非有限、slice）不变。
   - `drawablePolyline`/`visibleIndexRange` 等已基于 Float64Array，不受影响。

### C++ 内部表示不变
`TopicSeries::SeriesSnapshot.points` 仍是 `vector<pair<double,double>>`。改的只是**跨 QVariant 边界的表示**。`decimate`/`snapshot` 不动。

## 数据流（改动后）

```
worker: snapshot → series{xs:QList<double>, ys:QList<double>}   [无 QVariantMap/点]
  GUI : onCurvesUpdated 读 xs/ys 等长配对 → vector<pair>        [无逐点 toMap]
  GUI : updateSeries 写 xs/ys QVariantList<double>              [无逐点 QVariantMap]
  QML : buildSeriesCache(entry.xs, entry.ys) → Float64Array     [无逐点 .x/.y]
  QML : paint 读缓存 + 二分（不变）
```

## 行为保持核对
- 配色/可见性/label：entry 其余字段不变。
- 纵轴 min/max：`buildSeriesCache` 仍按可见序列扩展 [-1,1] 基线（逻辑不变，只换取点方式）。
- 非有限点过滤：保留（`isFinite(xs[i]) && isFinite(ys[i])`）。
- 折叠点：`messageDots` 不变。
- 边界插值、采样点：不变。

## 测试与验证
- **C++ 单测**（改断言到 xs/ys）：
  - [test_curve_payload.cpp](../../../test/test_curve_payload.cpp)：`ExpandedTopicCarriesSeries` 等——series 项现含 `xs`/`ys`；加断言：`xs.size()==ys.size()` 且首值正确。
  - [test_history_curve_loader.cpp](../../../test/test_history_curve_loader.cpp)：4 处 `points...toMap().value("x"/"y")` 改读 `xs`/`ys` 列表（首尾值、预算 ≤600、端点保留）。
  - [test_ui_models.cpp](../../../test/test_ui_models.cpp) `UpdateSeriesBuildsStructuredEntries`：断言改为 `xs=[0,1], ys=[1,2]`。
- **往返单测（新增，锁定契约）**：构造含 `xs/ys` 的负载喂 `onCurvesUpdated`，再经 `SeriesListRole` 读回，校验 `xs/ys` 原样透传（覆盖生产→消费→模型整链）。
- **QML 纯逻辑**：[test_curve_plot_js.cpp](../../../test/test_curve_plot_js.cpp) `buildSeriesCache` 用例从 `points:[{x,y}]` 改为 `xs/ys`；新增"xs/ys 不等长时按较短截断不崩"。
- **QML 冒烟**：[test_qml_smoke.cpp](../../../test/test_qml_smoke.cpp) 两处构造负载（`ClickingSeriesLegendChip*`）改用 `xs/ys`。
- **基准复测**：`bench_timeline_curves.cpp` 负载改 `xs/ys`；`DR_PROFILE=1` 重跑，确认 `onCurvesUpdated` 从 ~171ms 降到目标量级；数字记录到本 spec 的"结果"小节（实现后补）。
- **回归**：全 184+ 测试绿。

## 风险
- **契约面广**：生产/消费/模型/QML/6+ 测试需同步改，任一漏改即断链——往返单测兜底。
- **xs/ys 等长假设**：消费端与 `buildSeriesCache` 都按 `min(xs.length, ys.length)` 防御，避免越界。
- **QList<double> 封送**：确认 QML 端 `entry.xs` 取到的是可索引数组（QList<double> → JS Array）；冒烟测试实测。

## 结果（实测）

基准 `bench_timeline_curves 99 600 30`（99 序列 × 600 点 = 59,400 点），同机对照：

| 阶段 | 改造前（round 1） | 改造后（round 2） | 变化 |
|---|---|---|---|
| **onCurvesUpdated**（GUI 线程 QVariant 往返） | 171.3 ms | **18.2 ms** | **9.4× 更快** |
| QML.rebuildCache | 3 ms | 4 ms | 持平 |
| QML.curvePaint（每行/帧） | 5–8 ms | 8–15 ms | 持平 |

主因 `onCurvesUpdated` 降一个数量级达成（目标 <~20ms）。实时模式下 200ms 推送周期内该槽占用从 ~85% 降到 ~9%，UI 不再被堵。全部 186 测试绿（含新增 2 个不等长截断用例）。
