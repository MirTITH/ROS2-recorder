# /joint_states 时间轴曲线性能优化 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除历史回看下展开 `/joint_states`（99 序列 × 大量点）时的 UI 卡顿，且行为基本不变。

**Architecture:** 两处外科手术式改动 —（A）把 QML 曲线的「数值转换 + 求纵轴极值」从「每次重绘」挪到「仅 seriesList 变化时一次」，重绘只在缓存好的 `Float64Array` 上、用二分查找只遍历可见区间；为可测性把纯逻辑抽到 `curve_plot.js`。（B）历史曲线点预算 2000→600。

**Tech Stack:** C++17 / Qt 6 (QML Canvas, QJSEngine) / ROS 2 (rosbag2) / ament_cmake / gtest。

**约定（命令里使用）：** 工作空间根 `WS=/home/nros/Documents/Woosh/ros2_recorder_ws`；环境 `source ~/.local/ros2_rc && rr`；包源码在 `$WS/src/data_recorder`；测试二进制在 `$WS/build/data_recorder/`。

---

## File Structure

- **Modify** `src/data_recorder/src/history_curve_loader.cpp` — 历史序列预算 2000→600（具名常量）。〔改动 B〕
- **Modify** `src/data_recorder/test/test_history_curve_loader.cpp` — 新增「超预算抽稀到 ≤600」断言。
- **Create** `src/data_recorder/qml/components/curve_plot.js` — 纯函数库：缓存构建 / 可见区间二分 / 视口裁剪折线。〔改动 A 基础〕
- **Create** `src/data_recorder/test/test_curve_plot_js.cpp` — 用 QJSEngine 单测 `curve_plot.js`。
- **Modify** `src/data_recorder/CMakeLists.txt` — 注册新测试目标 `test_curve_plot_js`。
- **Modify** `src/data_recorder/qml/components/TimelineTrackRow.qml` — 接入缓存 + 二分重绘。〔改动 A 应用〕
- **Modify** `src/data_recorder/test/test_qml_structure.cpp` — 结构断言同步到新结构。

---

## Task 1: 历史曲线点预算 2000 → 600〔改动 B〕

**Files:**
- Modify: `src/data_recorder/src/history_curve_loader.cpp`
- Test: `src/data_recorder/test/test_history_curve_loader.cpp`

- [ ] **Step 1: 写失败测试**

在 `src/data_recorder/test/test_history_curve_loader.cpp` 末尾（最后一个 `}` 之后、文件结尾处）追加：

```cpp
TEST(HistoryCurveLoader, ExtractTopicDownsamplesSeriesToBudget)
{
  const auto tmp = fs::temp_directory_path() / "dr_history_budget_test";
  constexpr int kMessageCount = 1300;  // > 历史序列预算(600)
  const std::string session_dir = write_joint_state_bag(tmp, kMessageCount);

  data_recorder::HistoryCurveLoader loader;
  QSignalSpy curves_spy(&loader, &data_recorder::HistoryCurveLoader::curvesReady);
  loader.extractTopic(QString::fromStdString(session_dir), "/joint_states");

  ASSERT_EQ(curves_spy.count(), 1);
  const auto topics = curves_spy.at(0).at(0).toList();
  const auto topic = find_topic(topics, "/joint_states");
  ASSERT_FALSE(topic.isEmpty());
  const auto pos_a = find_series(topic.value("series").toList(), "pos/a");
  ASSERT_FALSE(pos_a.isEmpty());
  const auto pts = pos_a.value("points").toList();
  EXPECT_GT(pts.size(), 0);
  EXPECT_LE(pts.size(), 600);  // 抽稀到历史预算以内
  fs::remove_all(tmp);
}
```

- [ ] **Step 2: 构建并运行，确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_history_curve_loader --gtest_filter='HistoryCurveLoader.ExtractTopicDownsamplesSeriesToBudget'
```
Expected: FAIL —`pts.size()` 为 1300（当前预算 2000，1300<2000 原样返回），`EXPECT_LE(..., 600)` 不满足。

- [ ] **Step 3: 实现最小改动**

在 `src/data_recorder/src/history_curve_loader.cpp` 的匿名 `namespace {` 内（约第 20 行 `namespace fs = std::filesystem;` 下一行）新增常量：

```cpp
// 历史曲线每序列点预算：画布约 700px，600 点已足够；与实时模式一致。
constexpr std::size_t kHistorySeriesBudget = 600;
```

并把 `extractTopic` 里的（约第 153 行）：

```cpp
  for (const auto & snap : buffer.snapshot(/*budget=*/2000)) {
```
改为：
```cpp
  for (const auto & snap : buffer.snapshot(kHistorySeriesBudget)) {
```

（`messageDots` 的两处 `message_times(/*budget=*/2000)` 保持不变。）

- [ ] **Step 4: 构建并运行，确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_history_curve_loader
```
Expected: 全部 PASS（含旧的 `LongTopicDownsamplingKeepsFullSessionSpan`、`ExtractTopicEmitsSeries`，它们断言首尾跨度/未超预算，与 600 兼容）。

- [ ] **Step 5: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add src/history_curve_loader.cpp test/test_history_curve_loader.cpp
git commit -m "perf(history): cap series downsample budget at 600 points

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: 抽出纯函数库 `curve_plot.js` + QJSEngine 单测〔改动 A 基础〕

**Files:**
- Create: `src/data_recorder/qml/components/curve_plot.js`
- Create: `src/data_recorder/test/test_curve_plot_js.cpp`
- Modify: `src/data_recorder/CMakeLists.txt`

- [ ] **Step 1: 写失败测试**

新建 `src/data_recorder/test/test_curve_plot_js.cpp`：

```cpp
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJSEngine>
#include <QJSValue>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace
{
std::filesystem::path qml_dir() { return std::filesystem::path(DATA_RECORDER_QML_DIR); }

QString read_curve_plot_js()
{
  const auto path = qml_dir() / "components" / "curve_plot.js";
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return QString::fromStdString(buffer.str());
}

class CurvePlotJs : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char name[] = "test_curve_plot_js";
      static char * argv[] = {name, nullptr};
      app_ = std::make_unique<QCoreApplication>(argc, argv);
    }
  }
  static void TearDownTestSuite() { app_.reset(); }

  void SetUp() override
  {
    const QJSValue lib = engine_.evaluate(read_curve_plot_js());
    ASSERT_FALSE(lib.isError()) << lib.toString().toStdString();
  }

  QJSValue eval(const QString & snippet) { return engine_.evaluate(snippet); }

  static std::unique_ptr<QCoreApplication> app_;
  QJSEngine engine_;
};
std::unique_ptr<QCoreApplication> CurvePlotJs::app_;
}  // namespace

TEST_F(CurvePlotJs, VisibleIndexRangeFindsInclusiveWindow)
{
  QJSValue r = eval(
    "var xs=[0,1,2,3,4,5,6,7,8,9];"
    "var g=visibleIndexRange(xs,2.5,6.5);"
    "[g.lo,g.hi];");
  ASSERT_TRUE(r.isArray());
  EXPECT_EQ(r.property(0).toInt(), 3);  // 首个 >= 2.5
  EXPECT_EQ(r.property(1).toInt(), 6);  // 末个 <= 6.5
}

TEST_F(CurvePlotJs, VisibleIndexRangeEmptyWhenWindowBetweenPoints)
{
  QJSValue r = eval(
    "var g=visibleIndexRange([0,10],3,7);"
    "[g.lo,g.hi];");
  EXPECT_GT(r.property(0).toInt(), r.property(1).toInt());  // lo > hi => 窗口内无点
}

TEST_F(CurvePlotJs, BuildSeriesCacheSkipsHiddenAndKeepsBaselineRange)
{
  QJSValue r = eval(
    "var sl=[{visible:true,color:'#111111',points:[{x:0,y:0.5},{x:1,y:0.25}]},"
    "        {visible:false,color:'#222222',points:[{x:0,y:99}]}];"
    "var c=buildSeriesCache(sl);"
    "[c.series.length,c.minY,c.maxY,c.series[0].xs.length,c.series[0].ys[0]];");
  ASSERT_TRUE(r.isArray());
  EXPECT_EQ(r.property(0).toInt(), 1);             // 隐藏序列被跳过
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), -1);  // 基线 [-1,1] 保留（0.25 > -1）
  EXPECT_DOUBLE_EQ(r.property(2).toNumber(), 1);   // 基线保留（0.5 < 1）
  EXPECT_EQ(r.property(3).toInt(), 2);
  EXPECT_DOUBLE_EQ(r.property(4).toNumber(), 0.5);
}

TEST_F(CurvePlotJs, BuildSeriesCacheExpandsRangeBeyondBaseline)
{
  QJSValue r = eval(
    "var c=buildSeriesCache([{visible:true,points:[{x:0,y:-3},{x:1,y:5}]}]);"
    "[c.minY,c.maxY];");
  EXPECT_DOUBLE_EQ(r.property(0).toNumber(), -3);
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), 5);
}

TEST_F(CurvePlotJs, DrawablePolylineInterpolatesToWindowEdges)
{
  // 点在 x=0,2,4；窗口 [1,3] -> 在 x=1、x=3 处插值补边
  QJSValue r = eval(
    "var p=drawablePolyline([0,2,4],[0,2,4],1,3);"
    "[p.xs.length,p.xs[0],p.ys[0],p.xs[p.xs.length-1],p.ys[p.ys.length-1]];");
  ASSERT_TRUE(r.isArray());
  EXPECT_EQ(r.property(0).toInt(), 3);             // 左边界(1,1)+内部(2,2)+右边界(3,3)
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), 1);
  EXPECT_DOUBLE_EQ(r.property(2).toNumber(), 1);
  EXPECT_DOUBLE_EQ(r.property(3).toNumber(), 3);
  EXPECT_DOUBLE_EQ(r.property(4).toNumber(), 3);
}

TEST_F(CurvePlotJs, DrawablePolylineSpansGapWhenNoPointInside)
{
  // 点在 x=0,10；窗口 [3,7] 内无点 -> 跨窗口两边界连线
  QJSValue r = eval(
    "var p=drawablePolyline([0,10],[0,10],3,7);"
    "[p.xs.length,p.xs[0],p.ys[0],p.xs[1],p.ys[1]];");
  EXPECT_EQ(r.property(0).toInt(), 2);
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), 3);
  EXPECT_DOUBLE_EQ(r.property(2).toNumber(), 3);
  EXPECT_DOUBLE_EQ(r.property(3).toNumber(), 7);
  EXPECT_DOUBLE_EQ(r.property(4).toNumber(), 7);
}
```

- [ ] **Step 2: 注册 CMake 测试目标**

在 `src/data_recorder/CMakeLists.txt` 中，`test_history_curve_loader` 块（约 174–176 行）之后插入：

```cmake
  ament_add_gtest(test_curve_plot_js test/test_curve_plot_js.cpp)
  target_link_libraries(test_curve_plot_js Qt6::Qml)
  target_compile_definitions(test_curve_plot_js PRIVATE
    DATA_RECORDER_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
```

- [ ] **Step 3: 构建并运行，确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_curve_plot_js
```
Expected: FAIL — `curve_plot.js` 不存在，`SetUp` 读到空串，函数未定义，断言失败（或 evaluate 报错）。

- [ ] **Step 4: 创建纯函数库**

新建 `src/data_recorder/qml/components/curve_plot.js`：

```js
// 纯函数库：时间轴数值曲线的缓存构建 / 可见区间二分 / 视口裁剪折线。
// 无 QML / Canvas 依赖，可被 TimelineTrackRow.qml 导入，也可在 QJSEngine 中直接 evaluate 单测。
// （刻意不加 .pragma library，以便测试直接对本文件文本求值。）

// 从 seriesList（[{color, visible, points:[{x,y}]}]）构建扁平数组缓存。
// 仅纳入 visible !== false 的序列；逐点过滤非有限值。
// 纵轴范围以 [-1, 1] 为基线再按数据扩展（与原 onPaint 行为一致）。
// 返回 { series:[{xs:Float64Array, ys:Float64Array, color}], minY, maxY }。
function buildSeriesCache(seriesList) {
    var entries = seriesList || []
    var out = []
    var minY = -1
    var maxY = 1
    for (var i = 0; i < entries.length; ++i) {
        var entry = entries[i] || {}
        if (entry.visible === false) {
            continue
        }
        var points = entry.points || []
        var n = points.length
        var xs = new Float64Array(n)
        var ys = new Float64Array(n)
        var count = 0
        for (var j = 0; j < n; ++j) {
            var px = Number(points[j].x)
            var py = Number(points[j].y)
            if (!isFinite(px) || !isFinite(py)) {
                continue
            }
            xs[count] = px
            ys[count] = py
            if (py < minY) { minY = py }
            if (py > maxY) { maxY = py }
            ++count
        }
        out.push({
            xs: count === n ? xs : xs.subarray(0, count),
            ys: count === n ? ys : ys.subarray(0, count),
            color: entry.color
        })
    }
    if (minY === maxY) {
        minY -= 1
        maxY += 1
    }
    return { series: out, minY: minY, maxY: maxY }
}

// xs 升序：返回首个 xs[i] >= value 的下标。
function lowerBound(xs, value) {
    var low = 0
    var high = xs.length
    while (low < high) {
        var mid = (low + high) >> 1
        if (xs[mid] < value) { low = mid + 1 } else { high = mid }
    }
    return low
}

// xs 升序：返回首个 xs[i] > value 的下标。
function upperBound(xs, value) {
    var low = 0
    var high = xs.length
    while (low < high) {
        var mid = (low + high) >> 1
        if (xs[mid] <= value) { low = mid + 1 } else { high = mid }
    }
    return low
}

// 返回落在 [start, end] 内的下标闭区间 {lo, hi}；lo > hi 表示窗口内无点。
function visibleIndexRange(xs, start, end) {
    return { lo: lowerBound(xs, start), hi: upperBound(xs, end) - 1 }
}

// 线性插值：在 (x0,y0)-(x1,y1) 上求 targetX 处的 y；跨度近 0 返回 null。
function interpolateY(x0, y0, x1, y1, targetX) {
    var span = x1 - x0
    if (!isFinite(span) || Math.abs(span) < 1e-9) {
        return null
    }
    return y0 + (y1 - y0) * (targetX - x0) / span
}

// 视口裁剪折线：取 [start,end] 内的点，并在跨越边缘处按插值补到边缘。
// 等价于原 collectDrawablePoints，但用二分只触及可见段。返回 {xs:[], ys:[]}（普通数组）。
function drawablePolyline(xs, ys, start, end) {
    var rxs = []
    var rys = []
    var n = xs.length
    if (n === 0) {
        return { xs: rxs, ys: rys }
    }
    var range = visibleIndexRange(xs, start, end)
    var lo = range.lo
    var hi = range.hi
    var hasInside = lo <= hi
    var beforeIdx = lo - 1
    var afterIdx = hi + 1
    var hasBefore = beforeIdx >= 0
    var hasAfter = afterIdx < n

    var leftSrc = hasInside ? lo : (hasAfter ? afterIdx : -1)
    if (hasBefore && leftSrc >= 0 && xs[beforeIdx] < start && xs[leftSrc] > start) {
        var ly = interpolateY(xs[beforeIdx], ys[beforeIdx], xs[leftSrc], ys[leftSrc], start)
        if (ly !== null) { rxs.push(start); rys.push(ly) }
    }
    for (var i = lo; i <= hi; ++i) {
        rxs.push(xs[i])
        rys.push(ys[i])
    }
    var rightSrc = hasInside ? hi : (hasBefore ? beforeIdx : -1)
    if (rightSrc >= 0 && hasAfter && xs[rightSrc] < end && xs[afterIdx] > end) {
        var ry = interpolateY(xs[rightSrc], ys[rightSrc], xs[afterIdx], ys[afterIdx], end)
        if (ry !== null) { rxs.push(end); rys.push(ry) }
    }
    return { xs: rxs, ys: rys }
}
```

- [ ] **Step 5: 构建并运行，确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_curve_plot_js
```
Expected: 6 个测试全部 PASS。

- [ ] **Step 6: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add qml/components/curve_plot.js test/test_curve_plot_js.cpp CMakeLists.txt
git commit -m "perf(timeline): add pure curve_plot.js (cache/binary-search/clip) with tests

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: TimelineTrackRow 接入缓存 + 二分重绘〔改动 A 应用〕

**Files:**
- Modify: `src/data_recorder/qml/components/TimelineTrackRow.qml`（整体改写渲染部分）
- Modify: `src/data_recorder/test/test_qml_structure.cpp`（结构断言同步）

- [ ] **Step 1: 改写 TimelineTrackRow.qml**

用以下内容**整体替换** `src/data_recorder/qml/components/TimelineTrackRow.qml`：

```qml
import QtQuick 2.15
import "."
import "curve_plot.js" as CurvePlot

Rectangle {
    id: root

    property string trackKind: "empty"
    property var seriesList: []
    property bool isExpanded: false
    property bool showData: true
    property var messageDots: []
    property real xMax: 60
    property real visibleStartSeconds: 0
    property real visibleDurationSeconds: 60
    property real plotTopPadding: 4
    property real plotBottomPadding: 4
    property real sampleMarkerSpacingThreshold: 12
    readonly property int collapsedHeight: 32
    readonly property int expandedHeight: 120

    // 曲线缓存：仅在 seriesList 变化时重建（见 rebuildCache）。平移/缩放只重绘、不重建。
    // _cachedSeries: [{ xs: Float64Array, ys: Float64Array, color }]（仅含可见序列）
    property var _cachedSeries: []
    property real _cachedMinY: -1
    property real _cachedMaxY: 1

    height: root.isExpanded ? root.expandedHeight : root.collapsedHeight
    color: trackKind === "empty" ? Theme.surfaceAlt : Theme.surface

    function boundedDuration() {
        return Math.max(0.001, Number(visibleDurationSeconds) || 1)
    }

    function seriesColor(value) {
        var text = String(value || "")
        return /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text) ? text : "#2563eb"
    }

    function plotTop() {
        return Math.min(height / 2, Math.max(0, plotTopPadding))
    }

    function plotHeight() {
        return Math.max(1, height - plotTop() - Math.max(0, plotBottomPadding))
    }

    function visibleEndSeconds() {
        return visibleStartSeconds + boundedDuration()
    }

    function xToPixel(xValue, widthValue) {
        return ((xValue - visibleStartSeconds) / boundedDuration()) * widthValue
    }

    function yToPixel(yValue, minY, maxY, top, plotHeightValue) {
        return top + (1 - ((yValue - minY) / Math.max(0.001, maxY - minY))) * plotHeightValue
    }

    // 把 seriesList 一次性转成扁平 Float64Array 缓存 + 全局纵轴范围。
    function rebuildCache() {
        var built = CurvePlot.buildSeriesCache(root.seriesList || [])
        root._cachedSeries = built.series
        root._cachedMinY = built.minY
        root._cachedMaxY = built.maxY
    }

    function drawSampleMarkers(ctx, xs, ys, lo, hi, color, minY, maxY, top, plotHeightValue, widthValue) {
        ctx.fillStyle = Theme.surface
        ctx.strokeStyle = color
        ctx.lineWidth = 1
        for (var index = lo; index <= hi; ++index) {
            var x = xToPixel(xs[index], widthValue)
            var y = yToPixel(ys[index], minY, maxY, top, plotHeightValue)
            ctx.beginPath()
            ctx.arc(x, y, 2, 0, Math.PI * 2)
            ctx.fill()
            ctx.stroke()
        }
    }

    Canvas {
        id: curveCanvas

        anchors.fill: parent
        visible: root.showData && root.trackKind === "numeric" && root.isExpanded
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = Theme.surface
            ctx.fillRect(0, 0, width, height)

            var top = root.plotTop()
            var plotHeight = root.plotHeight()
            var minY = root._cachedMinY
            var maxY = root._cachedMaxY

            ctx.strokeStyle = Theme.gridLine
            ctx.lineWidth = 1
            for (var gridY = 0; gridY <= 2; ++gridY) {
                var yLine = top + (gridY / 2) * plotHeight
                ctx.beginPath()
                ctx.moveTo(0, yLine)
                ctx.lineTo(width, yLine)
                ctx.stroke()
            }

            var start = root.visibleStartSeconds
            var end = root.visibleEndSeconds()
            var cache = root._cachedSeries || []
            for (var s = 0; s < cache.length; ++s) {
                var entry = cache[s]
                var xs = entry.xs
                var ys = entry.ys
                if (xs.length === 0) {
                    continue
                }
                var color = root.seriesColor(entry.color)

                var poly = CurvePlot.drawablePolyline(xs, ys, start, end)
                if (poly.xs.length >= 2) {
                    ctx.beginPath()
                    ctx.strokeStyle = color
                    ctx.lineWidth = 1.5
                    for (var p = 0; p < poly.xs.length; ++p) {
                        var lineX = root.xToPixel(poly.xs[p], width)
                        var lineY = root.yToPixel(poly.ys[p], minY, maxY, top, plotHeight)
                        if (p === 0) {
                            ctx.moveTo(lineX, lineY)
                        } else {
                            ctx.lineTo(lineX, lineY)
                        }
                    }
                    ctx.stroke()
                }

                var range = CurvePlot.visibleIndexRange(xs, start, end)
                var lo = range.lo
                var hi = range.hi
                if (hi >= lo) {
                    var visibleCount = hi - lo + 1
                    var firstPx = root.xToPixel(xs[lo], width)
                    var lastPx = root.xToPixel(xs[hi], width)
                    var avgSpacing = visibleCount < 2 ?
                        width : Math.abs(lastPx - firstPx) / Math.max(1, visibleCount - 1)
                    if (visibleCount === 1 || avgSpacing >= root.sampleMarkerSpacingThreshold) {
                        root.drawSampleMarkers(
                            ctx, xs, ys, lo, hi, color, minY, maxY, top, plotHeight, width)
                    }
                }
            }
        }
    }

    Canvas {
        id: dotsCanvas

        anchors.fill: parent
        // 折叠态：所有 rosbag 数据行（numeric/empty）都画节奏点；相机行不画。
        visible: root.showData && !root.isExpanded && root.trackKind !== "camera"
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = Theme.surface
            ctx.fillRect(0, 0, width, height)

            var dots = root.messageDots || []
            var baselineY = height / 2
            ctx.fillStyle = Theme.tickStrong
            var start = root.visibleStartSeconds
            var end = root.visibleEndSeconds()
            for (var i = 0; i < dots.length; ++i) {
                var t = Number(dots[i])
                if (!isFinite(t) || t < start || t > end) {
                    continue
                }
                var x = root.xToPixel(t, width)
                ctx.beginPath()
                ctx.arc(x, baselineY, 1.5, 0, Math.PI * 2)
                ctx.fill()
            }
        }
    }

    onTrackKindChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onSeriesListChanged: { root.rebuildCache(); curveCanvas.requestPaint() }
    onMessageDotsChanged: dotsCanvas.requestPaint()
    onIsExpandedChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onShowDataChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onVisibleStartSecondsChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onVisibleDurationSecondsChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }

    Component.onCompleted: root.rebuildCache()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.gridLine
    }
}
```

- [ ] **Step 2: 同步结构测试 — `TimelineViewportRenderingRulesAreExplicit`**

在 `src/data_recorder/test/test_qml_structure.cpp` 的 `TimelineViewportRenderingRulesAreExplicit` 测试里，**替换**这一段（原约 356–366 行，`plotTopPadding` 起到 `ctx.arc` 止）：

```cpp
  expect_contains(curve_text, "property real plotTopPadding: 4");
  expect_contains(curve_text, "property real plotBottomPadding: 4");
  expect_contains(curve_text, "property real sampleMarkerSpacingThreshold: 12");
  expect_contains(curve_text, "function collectDrawablePoints");
  expect_contains(curve_text, "function interpolateBoundaryPoint");
  expect_contains(curve_text, "function collectVisibleSamples");
  expect_contains(curve_text, "function shouldDrawSampleMarkers");
  expect_contains(curve_text, "function drawSampleMarkers");
  expect_contains(curve_text, "boundary: true");
  expect_contains(curve_text, "boundary: false");
  expect_contains(curve_text, "ctx.arc");
```
为：
```cpp
  expect_contains(curve_text, "property real plotTopPadding: 4");
  expect_contains(curve_text, "property real plotBottomPadding: 4");
  expect_contains(curve_text, "property real sampleMarkerSpacingThreshold: 12");
  expect_contains(curve_text, "import \"curve_plot.js\" as CurvePlot");
  expect_contains(curve_text, "function rebuildCache");
  expect_contains(curve_text, "CurvePlot.buildSeriesCache");
  expect_contains(curve_text, "CurvePlot.drawablePolyline");
  expect_contains(curve_text, "CurvePlot.visibleIndexRange");
  expect_contains(curve_text, "function drawSampleMarkers");
  expect_contains(curve_text, "ctx.arc");

  const std::string curveplot_text = read_text(qml_dir() / "components" / "curve_plot.js");
  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "components" / "curve_plot.js"));
  expect_contains(curveplot_text, "function buildSeriesCache");
  expect_contains(curveplot_text, "function drawablePolyline");
  expect_contains(curveplot_text, "function visibleIndexRange");
  expect_contains(curveplot_text, "entry.visible === false");
  expect_contains(curveplot_text, "Float64Array");
```

- [ ] **Step 3: 同步结构测试 — `TimelineRowsSupportExpandCollapseCurves`**

在同文件的 `TimelineRowsSupportExpandCollapseCurves` 测试里，**替换** `track` 段（原约 446–451 行）：

```cpp
  const std::string track = read_text(qml_dir() / "components" / "TimelineTrackRow.qml");
  expect_contains(track, "dotsCanvas");
  expect_contains(track, "messageDots");
  expect_contains(track, "entry.visible === false");
  expect_contains(track, "readonly property int collapsedHeight: 32");
  expect_contains(track, "readonly property int expandedHeight: 120");
```
为：
```cpp
  const std::string track = read_text(qml_dir() / "components" / "TimelineTrackRow.qml");
  expect_contains(track, "dotsCanvas");
  expect_contains(track, "messageDots");
  expect_contains(track, "onSeriesListChanged: { root.rebuildCache(); curveCanvas.requestPaint() }");
  expect_contains(track, "readonly property int collapsedHeight: 32");
  expect_contains(track, "readonly property int expandedHeight: 120");
```

- [ ] **Step 4: 构建并运行结构 + 冒烟测试**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_qml_structure
source ~/.local/ros2_rc && rr && QT_QPA_PLATFORM=offscreen /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_qml_smoke
```
Expected: 两者全部 PASS。冒烟测试 `ClickingSeriesLegendChipTogglesVisibilityBothWays` / `ClickingScrolledSeriesLegendChip...` / `ShiftWheelPansTimelineWithoutMovingPlayhead` 实际加载 QML 并触发新 onPaint，验证渲染路径无 JS 错误、平移与图例切换仍正常。

- [ ] **Step 5: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add qml/components/TimelineTrackRow.qml test/test_qml_structure.cpp
git commit -m "perf(timeline): cache curve points + binary-search visible range in onPaint

Convert visible series to Float64Array and cache global min/max only when
seriesList changes; pan/zoom repaints now read the cache and binary-search
the visible window instead of re-converting and 3x full-scanning every
frame. Behavior preserved.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: 全量构建、全测试与人工外观确认

**Files:** 无（验证任务）

- [ ] **Step 1: 全量构建（整工作空间约定参数）**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache
```
Expected: data_recorder 构建成功，无错误。

- [ ] **Step 2: 运行 data_recorder 全部测试**

```bash
source ~/.local/ros2_rc && rr && QT_QPA_PLATFORM=offscreen colcon test --packages-select data_recorder --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws
colcon test-result --all --verbose | sed -n '1,40p'
```
Expected: 全部测试 PASS（特别是 `test_history_curve_loader`、`test_curve_plot_js`、`test_qml_structure`、`test_qml_smoke`、`test_topic_series`、`test_ui_models`）。

- [ ] **Step 3: 人工外观与流畅度确认（需人参与）**

依记忆：GUI 点击自动化不可靠，`xwd`+`ffmpeg` 截图可行。步骤：
1. 启动应用（按 README/run 方式），在「数据」面板选中一个含 `/joint_states` 的历史会话。
2. 展开 `/joint_states` 轨道。
3. 观察：曲线、配色、默认仅 `pos/` 可见、纵轴稳定（平移不跳动）、采样圆点 —— 应与改动前一致。
4. 平移 / 缩放（Shift+滚轮平移、滚轮缩放）—— 应明显流畅，不再卡顿。
5. （可选）`xwd -root -silent | ... ffmpeg` 截图存档，与改动前对比折线外观。

Expected: 渲染外观与旧版一致；展开与平移/缩放流畅。

- [ ] **Step 4: （如有未提交改动）收尾提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git status
```
Expected: 工作区干净（前序任务已分别提交）。

---

## Self-Review

**Spec 覆盖：**
- 〔改动 A — QML 缓存/二分/单趟重绘〕→ Task 2（纯库 + 单测）+ Task 3（接入 + 结构测试）。✓
- 〔改动 B — 历史预算 600，具名常量，messageDots 不变〕→ Task 1。✓
- 〔行为保持：默认可见性 / 纵轴 [-1,1] 基线 / 边界插值 / 采样圆点判据〕→ `buildSeriesCache`（跳过 `visible===false`、基线 -1/1）、`drawablePolyline`（边界插值）、onPaint 采样圆点判据，均在 Task 2/3 实现并单测。✓
- 〔测试：新增超预算断言；history-loader 旧测试不变；topic_series 不变；QML 结构/冒烟回归；人工外观〕→ Task 1 Step1、Task 3 Step2-4、Task 4。✓
- 〔Non-Goals：不改 {x,y} 契约 / 不改 `onCurvesUpdated`/`updateSeries` 签名〕→ 计划未触碰这些；`Float64Array` 仅 QML 内部。✓

**占位符扫描：** 无 TBD/TODO；每个代码步骤含完整内容。✓

**类型/命名一致性：** `curve_plot.js` 导出 `buildSeriesCache` / `visibleIndexRange` / `drawablePolyline` / `interpolateY` / `lowerBound` / `upperBound`；TimelineTrackRow 以 `CurvePlot.buildSeriesCache`/`CurvePlot.drawablePolyline`/`CurvePlot.visibleIndexRange` 调用；缓存属性 `_cachedSeries`/`_cachedMinY`/`_cachedMaxY` 与 `rebuildCache()` 在 onPaint、信号处理、`Component.onCompleted` 中一致引用。常量 `kHistorySeriesBudget` 定义即使用。结构测试断言字符串与新 QML 文本逐字对应（含 `onSeriesListChanged: { root.rebuildCache(); curveCanvas.requestPaint() }`）。✓
