# 时间轴曲线扁平数组改造（第二轮）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把时间轴曲线点的跨 QVariant 表示从 `points:[{x,y}]` 改为两个并列 `QList<double>` 数组 `xs[]/ys[]`，消除 GUI 线程上每点一个 `QVariantMap` 的封送（实测 `onCurvesUpdated` 171ms 的主因）。

**Architecture:** 一次性把整条契约链改为 xs/ys：生产端（curve_payload.cpp 实时 + history_curve_loader.cpp 历史）写 `QList<double>`；消费端 `onCurvesUpdated` 读 xs/ys 配对、`updateSeries` 写 xs/ys；QML `curve_plot.js` 的 `buildSeriesCache` 直接消费 xs/ys 填 Float64Array。C++ 内部 `SeriesSnapshot.points`（`vector<pair<double,double>>`）不变——只改跨 QVariant 边界的表示。

**Tech Stack:** C++17 / Qt 6 (QVariant, QList<double>→JS Array 已验证可索引) / QML Canvas / ament_cmake / gtest。

**约定：** `WS=/home/nros/Documents/Woosh/ros2_recorder_ws`；环境 `source ~/.local/ros2_rc && rr`；包 `$WS/src/data_recorder`；测试/基准二进制 `$WS/build/data_recorder/`。分支 `feature/timeline-numeric-curves`（勿新建）。
构建（含测试）：`source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths $WS --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON`（首条命令把 `$WS` 写全路径）。

**关键事实（已验证）：** `QVariant::fromValue(QList<double>{...})` 经 `toScriptValue` 在 QML/JS 中 `isArray===true`、`.length` 与 `[i]` 可用。故 QML 端 `entry.xs[i]` 直接可索引。

---

## File Structure
- **Modify** `src/curve_payload.cpp` — 生产端（实时）：series 写 `xs`/`ys`。
- **Modify** `include/data_recorder/curve_payload.hpp` — 契约注释更新。
- **Modify** `src/history_curve_loader.cpp` — 生产端（历史）：series 写 `xs`/`ys`。
- **Modify** `src/app_controller.cpp` — `onCurvesUpdated` 读 `xs`/`ys` 配对填 `SeriesSnapshot`。
- **Modify** `src/ui_models.cpp` — `updateSeries` 写 `xs`/`ys`。
- **Modify** `qml/components/curve_plot.js` — `buildSeriesCache` 消费 `xs`/`ys`。
- **Modify** 测试：`test/test_curve_payload.cpp`、`test/test_history_curve_loader.cpp`、`test/test_ui_models.cpp`、`test/test_curve_plot_js.cpp`、`test/test_qml_smoke.cpp`、`test/bench_timeline_curves.cpp`。

执行顺序：先改两端 C++（Task 1 生产 + Task 2 消费/模型，配套单测同改，保证 C++ 链自洽）→ Task 3 改 QML 库 + 冒烟 → Task 4 全量构建/全测试 + 基准复测。

---

## Task 1: 生产端写 xs/ys（curve_payload + history_curve_loader）

**Files:**
- Modify: `src/curve_payload.cpp`, `include/data_recorder/curve_payload.hpp`, `src/history_curve_loader.cpp`
- Test: `test/test_curve_payload.cpp`, `test/test_history_curve_loader.cpp`

- [ ] **Step 1: 改 test_curve_payload.cpp（先红）**

在 `test/test_curve_payload.cpp` 的 `ExpandedTopicCarriesSeries` 中，把：
```cpp
  const auto arr = js.value("series").toList();
  ASSERT_EQ(arr.size(), 1);
  EXPECT_EQ(arr[0].toMap().value("key").toString().toStdString(), "pos/a");
```
替换为：
```cpp
  const auto arr = js.value("series").toList();
  ASSERT_EQ(arr.size(), 1);
  const auto entry = arr[0].toMap();
  EXPECT_EQ(entry.value("key").toString().toStdString(), "pos/a");
  EXPECT_FALSE(entry.contains("points"));  // 旧逐点表示已移除
  const auto xs = entry.value("xs").toList();
  const auto ys = entry.value("ys").toList();
  ASSERT_EQ(xs.size(), ys.size());
  ASSERT_FALSE(xs.isEmpty());
  EXPECT_DOUBLE_EQ(xs.front().toDouble(), 0.0);   // 首点 t=0
  EXPECT_DOUBLE_EQ(ys.front().toDouble(), 0.0);   // 首点 v=0
```

- [ ] **Step 2: 改 test_history_curve_loader.cpp（先红）**

替换三处对 `points` 的断言。
(a) `ExtractTopicEmitsSeries`（约 142-149 行）：
```cpp
  const auto pts = pos_a.value("points").toList();
  EXPECT_EQ(pts.size(), 5);  // 5 条消息，未超抽稀预算
  // pos/a 末值 = 4（第 5 条 position[0]）
  EXPECT_DOUBLE_EQ(pts.last().toMap().value("y").toDouble(), 4.0);

  const auto pos_b = find_series(series, "pos/b");
  ASSERT_FALSE(pos_b.isEmpty());
  EXPECT_DOUBLE_EQ(pos_b.value("points").toList().last().toMap().value("y").toDouble(), 8.0);
```
改为：
```cpp
  const auto pos_a_ys = pos_a.value("ys").toList();
  EXPECT_EQ(pos_a.value("xs").toList().size(), 5);  // 5 条消息，未超抽稀预算
  ASSERT_EQ(pos_a_ys.size(), 5);
  EXPECT_DOUBLE_EQ(pos_a_ys.last().toDouble(), 4.0);  // pos/a 末值 = 4

  const auto pos_b = find_series(series, "pos/b");
  ASSERT_FALSE(pos_b.isEmpty());
  EXPECT_DOUBLE_EQ(pos_b.value("ys").toList().last().toDouble(), 8.0);
```
(b) `LongTopicDownsamplingKeepsFullSessionSpan`（约 116-119 行）：
```cpp
  const auto pts = pos_a.value("points").toList();
  ASSERT_FALSE(pts.isEmpty());
  EXPECT_DOUBLE_EQ(pts.front().toMap().value("x").toDouble(), 0.0);
  EXPECT_NEAR(pts.back().toMap().value("x").toDouble(), kLastSeconds, 1e-9);
```
改为：
```cpp
  const auto pts_xs = pos_a.value("xs").toList();
  ASSERT_FALSE(pts_xs.isEmpty());
  EXPECT_DOUBLE_EQ(pts_xs.front().toDouble(), 0.0);
  EXPECT_NEAR(pts_xs.back().toDouble(), kLastSeconds, 1e-9);
```
(c) `ExtractTopicDownsamplesSeriesToBudget`（约 170-175 行）：
```cpp
  const auto pts = pos_a.value("points").toList();
  EXPECT_LE(pts.size(), data_recorder::kHistorySeriesBudget);  // 抽稀到历史预算以内
  // 端点（首尾时间）必须保留：bag 第 i 条消息相对起点 t = i*0.001s，共 1300 条。
  ASSERT_FALSE(pts.isEmpty());
  EXPECT_DOUBLE_EQ(pts.front().toMap().value("x").toDouble(), 0.0);
  EXPECT_NEAR(pts.back().toMap().value("x").toDouble(), 1.299, 1e-9);
```
改为：
```cpp
  const auto pts_xs = pos_a.value("xs").toList();
  const auto pts_ys = pos_a.value("ys").toList();
  ASSERT_EQ(pts_xs.size(), pts_ys.size());
  EXPECT_LE(pts_xs.size(), data_recorder::kHistorySeriesBudget);  // 抽稀到历史预算以内
  // 端点（首尾时间）必须保留：bag 第 i 条消息相对起点 t = i*0.001s，共 1300 条。
  ASSERT_FALSE(pts_xs.isEmpty());
  EXPECT_DOUBLE_EQ(pts_xs.front().toDouble(), 0.0);
  EXPECT_NEAR(pts_xs.back().toDouble(), 1.299, 1e-9);
```

- [ ] **Step 3: 构建并运行，确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_curve_payload
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_history_curve_loader
```
Expected: FAIL —`xs`/`ys` 字段不存在（仍发 `points`），新断言取到空。

- [ ] **Step 4: 改生产端 curve_payload.cpp**

在 `src/curve_payload.cpp` 把构建 series 的内层（原 `points` 块）：
```cpp
        QVariantList points;
        for (const auto & p : snap.points) {
          QVariantMap pt;
          pt.insert("x", p.first);
          pt.insert("y", p.second);
          points.push_back(pt);
        }
        series_map.insert("points", points);
```
替换为：
```cpp
        QList<double> xs;
        QList<double> ys;
        xs.reserve(static_cast<int>(snap.points.size()));
        ys.reserve(static_cast<int>(snap.points.size()));
        for (const auto & p : snap.points) {
          xs.push_back(p.first);
          ys.push_back(p.second);
        }
        series_map.insert("xs", QVariant::fromValue(xs));
        series_map.insert("ys", QVariant::fromValue(ys));
```
确认文件顶部已有 `#include <QVariantMap>`；新增 `#include <QList>` 与 `#include <QVariant>`（若缺）。

- [ ] **Step 5: 改契约注释 curve_payload.hpp**

把 [curve_payload.hpp](../../../include/data_recorder/curve_payload.hpp) 注释里：
```
//   { "topicKey": QString, "messageDots": [double], "series": [{"key", "points":[{"x","y"}]}] }
```
改为：
```
//   { "topicKey": QString, "messageDots": [double],
//     "series": [{"key", "xs":[double], "ys":[double]}] }  // xs/ys 等长、xs 升序
```

- [ ] **Step 6: 改生产端 history_curve_loader.cpp**

在 `src/history_curve_loader.cpp` `extractTopic` 把：
```cpp
    QVariantList points;
    for (const auto & p : snap.points) {
      QVariantMap pt;
      pt.insert("x", p.first);
      pt.insert("y", p.second);
      points.push_back(pt);
    }
    series_map.insert("points", points);
```
替换为：
```cpp
    QList<double> xs;
    QList<double> ys;
    xs.reserve(static_cast<int>(snap.points.size()));
    ys.reserve(static_cast<int>(snap.points.size()));
    for (const auto & p : snap.points) {
      xs.push_back(p.first);
      ys.push_back(p.second);
    }
    series_map.insert("xs", QVariant::fromValue(xs));
    series_map.insert("ys", QVariant::fromValue(ys));
```
顶部新增 `#include <QList>`、`#include <QVariant>`（若缺；`<QVariantMap>` 已在）。

- [ ] **Step 7: 构建并运行，确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_curve_payload
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_history_curve_loader
```
Expected: 两者全 PASS。

> 注意：此时 `onCurvesUpdated`（消费端）仍读旧 `points`，但生产端已发 `xs/ys` —— 这会让 QML 暂时收不到曲线（C++ 链未端到端打通）。这是预期的中间态，Task 2 立即修复。不要在此运行 QML 冒烟测试。

- [ ] **Step 8: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add src/curve_payload.cpp include/data_recorder/curve_payload.hpp src/history_curve_loader.cpp test/test_curve_payload.cpp test/test_history_curve_loader.cpp
git commit -m "perf(curves): producers emit flat xs/ys arrays instead of per-point maps

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: 消费端读 xs/ys（onCurvesUpdated + updateSeries）

**Files:**
- Modify: `src/app_controller.cpp`（`onCurvesUpdated`）, `src/ui_models.cpp`（`updateSeries`）
- Test: `test/test_ui_models.cpp`

- [ ] **Step 1: 改 test_ui_models.cpp 的 updateSeries 断言（先红）**

在 `test/test_ui_models.cpp` `UpdateSeriesBuildsStructuredEntries` 中，把：
```cpp
  const auto pts = pos.value("points").toList();
  ASSERT_EQ(pts.size(), 2);
  EXPECT_DOUBLE_EQ(pts[0].toMap().value("x").toDouble(), 0.0);
  EXPECT_DOUBLE_EQ(pts[0].toMap().value("y").toDouble(), 1.0);
  EXPECT_DOUBLE_EQ(pts[1].toMap().value("x").toDouble(), 1.0);
  EXPECT_DOUBLE_EQ(pts[1].toMap().value("y").toDouble(), 2.0);
```
替换为：
```cpp
  const auto xs = pos.value("xs").toList();
  const auto ys = pos.value("ys").toList();
  ASSERT_EQ(xs.size(), 2);
  ASSERT_EQ(ys.size(), 2);
  EXPECT_DOUBLE_EQ(xs[0].toDouble(), 0.0);
  EXPECT_DOUBLE_EQ(ys[0].toDouble(), 1.0);
  EXPECT_DOUBLE_EQ(xs[1].toDouble(), 1.0);
  EXPECT_DOUBLE_EQ(ys[1].toDouble(), 2.0);
  EXPECT_FALSE(pos.contains("points"));
```

- [ ] **Step 2: 构建并运行，确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_ui_models --gtest_filter='TopicListModel.UpdateSeriesBuildsStructuredEntries'
```
Expected: FAIL —`updateSeries` 仍写 `points`，`xs`/`ys` 取空。

- [ ] **Step 3: 改 updateSeries（ui_models.cpp）写 xs/ys**

在 `src/ui_models.cpp` `updateSeries` 把：
```cpp
      QVariantList points;
      points.reserve(static_cast<int>(s.points.size()));
      for (const auto & p : s.points) {
        QVariantMap point;
        point.insert("x", p.first);
        point.insert("y", p.second);
        points.append(point);
      }

      QVariantMap entry;
      entry.insert("key", QString::fromStdString(s.key));
      entry.insert("label", QString::fromStdString(s.key));
      entry.insert("color", color);
      entry.insert("visible", visible);
      entry.insert("points", points);
      list.append(entry);
```
替换为：
```cpp
      QList<double> xs;
      QList<double> ys;
      xs.reserve(static_cast<int>(s.points.size()));
      ys.reserve(static_cast<int>(s.points.size()));
      for (const auto & p : s.points) {
        xs.push_back(p.first);
        ys.push_back(p.second);
      }

      QVariantMap entry;
      entry.insert("key", QString::fromStdString(s.key));
      entry.insert("label", QString::fromStdString(s.key));
      entry.insert("color", color);
      entry.insert("visible", visible);
      entry.insert("xs", QVariant::fromValue(xs));
      entry.insert("ys", QVariant::fromValue(ys));
      list.append(entry);
```
确认 `src/ui_models.cpp` 顶部已包含 `<QList>`、`<QVariant>`（QVariant 类多半已间接可用；若编译报缺，显式 `#include <QList>` 与 `#include <QVariant>`）。

> 注意：`setSeriesVisible`（ui_models.cpp 约 326-336）只读写 entry 的 `visible` 字段、不碰点数据，**无需改动**（它对 `xs/ys` 透明）。实现时确认该函数不引用 `points`。

- [ ] **Step 4: 改 onCurvesUpdated（app_controller.cpp）读 xs/ys 配对**

在 `src/app_controller.cpp` `onCurvesUpdated` 把内层解析：
```cpp
      const auto sm = s.toMap();
      TopicSeries::SeriesSnapshot snap;
      snap.key = sm.value("key").toString().toStdString();
      const auto pts = sm.value("points").toList();
      snap.points.reserve(static_cast<std::size_t>(pts.size()));
      for (const auto & p : pts) {
        const auto pm = p.toMap();
        snap.points.emplace_back(pm.value("x").toDouble(), pm.value("y").toDouble());
      }
      series.push_back(std::move(snap));
```
替换为：
```cpp
      const auto sm = s.toMap();
      TopicSeries::SeriesSnapshot snap;
      snap.key = sm.value("key").toString().toStdString();
      const auto xs = sm.value("xs").toList();
      const auto ys = sm.value("ys").toList();
      const int n = std::min(xs.size(), ys.size());  // 防御：按较短截断
      snap.points.reserve(static_cast<std::size_t>(n));
      for (int i = 0; i < n; ++i) {
        snap.points.emplace_back(xs.at(i).toDouble(), ys.at(i).toDouble());
      }
      series.push_back(std::move(snap));
```
确认 `src/app_controller.cpp` 顶部已 `#include <algorithm>`（用 `std::min`）；若缺则添加。

- [ ] **Step 5: 构建并运行 C++ 链相关测试，确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_ui_models
```
Expected: 全 PASS（65+ 用例）。

> 注意：QML 冒烟测试此时仍会失败（其负载与 curve_plot.js 仍是旧 `points` 形态）—— Task 3 修复。暂不跑冒烟。

- [ ] **Step 6: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add src/app_controller.cpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "perf(curves): consume flat xs/ys in onCurvesUpdated and emit them from updateSeries

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: QML 端消费 xs/ys（curve_plot.js + 冒烟/基准负载）

**Files:**
- Modify: `qml/components/curve_plot.js`
- Test: `test/test_curve_plot_js.cpp`, `test/test_qml_smoke.cpp`, `test/bench_timeline_curves.cpp`

- [ ] **Step 1: 改 test_curve_plot_js.cpp（先红）**

在 `test/test_curve_plot_js.cpp`，把 `BuildSeriesCacheSkipsHiddenAndKeepsBaselineRange` 与 `BuildSeriesCacheExpandsRangeBeyondBaseline` 两个用例里 `points:[{x,y}]` 形态的输入改为 `xs/ys`，并新增不等长用例：
替换 `BuildSeriesCacheSkipsHiddenAndKeepsBaselineRange`：
```cpp
TEST_F(CurvePlotJs, BuildSeriesCacheSkipsHiddenAndKeepsBaselineRange)
{
  QJSValue r = eval(
    "var sl=[{visible:true,color:'#111111',xs:[0,1],ys:[0.5,0.25]},"
    "        {visible:false,color:'#222222',xs:[0],ys:[99]}];"
    "var c=buildSeriesCache(sl);"
    "[c.series.length,c.minY,c.maxY,c.series[0].xs.length,c.series[0].ys[0]];");
  ASSERT_TRUE(r.isArray());
  EXPECT_EQ(r.property(0).toInt(), 1);
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), -1);
  EXPECT_DOUBLE_EQ(r.property(2).toNumber(), 1);
  EXPECT_EQ(r.property(3).toInt(), 2);
  EXPECT_DOUBLE_EQ(r.property(4).toNumber(), 0.5);
}
```
替换 `BuildSeriesCacheExpandsRangeBeyondBaseline`：
```cpp
TEST_F(CurvePlotJs, BuildSeriesCacheExpandsRangeBeyondBaseline)
{
  QJSValue r = eval(
    "var c=buildSeriesCache([{visible:true,xs:[0,1],ys:[-3,5]}]);"
    "[c.minY,c.maxY];");
  EXPECT_DOUBLE_EQ(r.property(0).toNumber(), -3);
  EXPECT_DOUBLE_EQ(r.property(1).toNumber(), 5);
}
```
追加新用例（不等长按较短截断不崩）：
```cpp
TEST_F(CurvePlotJs, BuildSeriesCacheTruncatesMismatchedLengths)
{
  QJSValue r = eval(
    "var c=buildSeriesCache([{visible:true,xs:[0,1,2],ys:[10,20]}]);"
    "[c.series[0].xs.length,c.series[0].ys.length];");
  ASSERT_TRUE(r.isArray());
  EXPECT_EQ(r.property(0).toInt(), 2);  // 按 ys 较短截断
  EXPECT_EQ(r.property(1).toInt(), 2);
}
```

- [ ] **Step 2: 构建并运行，确认失败**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_curve_plot_js
```
Expected: FAIL —`buildSeriesCache` 仍读 `entry.points`，从 `xs/ys` 取不到点（cache 为空）。

- [ ] **Step 3: 改 buildSeriesCache 消费 xs/ys（curve_plot.js）**

在 `qml/components/curve_plot.js`，把 `buildSeriesCache` 的头注释与循环体改为消费 `xs`/`ys`：
将函数头注释行：
```js
// 从 seriesList（[{color, visible, points:[{x,y}]}]）构建扁平数组缓存。
// 仅纳入 visible !== false 的序列；逐点过滤非有限值。
// 纵轴范围以 [-1, 1] 为基线再按数据扩展（与原 onPaint 行为一致）。
// 返回 { series:[{xs:Float64Array, ys:Float64Array, color}], minY, maxY }。
// 约定：每个 points 元素均为 {x,y} 对象（由 C++ updateSeries 保证，不含 null）。
```
改为：
```js
// 从 seriesList（[{color, visible, xs:[double], ys:[double]}]）构建扁平数组缓存。
// 仅纳入 visible !== false 的序列；逐点过滤非有限值；xs/ys 不等长时按较短截断。
// 纵轴范围以 [-1, 1] 为基线再按数据扩展（与原 onPaint 行为一致）。
// 返回 { series:[{xs:Float64Array, ys:Float64Array, color}], minY, maxY }。
// 约定：xs/ys 为并列数值数组（由 C++ updateSeries 保证）。
```
并把循环体：
```js
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
```
替换为：
```js
        var srcXs = entry.xs || []
        var srcYs = entry.ys || []
        var n = Math.min(srcXs.length, srcYs.length)  // 不等长按较短截断
        var xs = new Float64Array(n)
        var ys = new Float64Array(n)
        var count = 0
        for (var j = 0; j < n; ++j) {
            var px = Number(srcXs[j])
            var py = Number(srcYs[j])
            if (!isFinite(px) || !isFinite(py)) {
                continue
            }
            xs[count] = px
            ys[count] = py
            if (py < minY) { minY = py }
            if (py > maxY) { maxY = py }
            ++count
        }
```
（`out.push({ xs: ..., ys: ..., color: entry.color })` 及后续不变。）

- [ ] **Step 4: 改 test_qml_smoke.cpp 两处负载**

在 `test/test_qml_smoke.cpp` `ClickingSeriesLegendChipTogglesVisibilityBothWays`，把：
```cpp
  QVariantMap point;
  point.insert("x", 0.0);
  point.insert("y", 1.0);
  QVariantMap series;
  series.insert("key", "pos/a");
  series.insert("points", QVariantList{point});
```
替换为：
```cpp
  QVariantMap series;
  series.insert("key", "pos/a");
  series.insert("xs", QVariant::fromValue(QList<double>{0.0}));
  series.insert("ys", QVariant::fromValue(QList<double>{1.0}));
```
在 `ClickingScrolledSeriesLegendChipTogglesVisibility` 的 30 序列循环里，把：
```cpp
    QVariantMap point;
    point.insert("x", 0.0);
    point.insert("y", double(index));

    QVariantMap series;
    series.insert("key", QString("pos/joint_%1").arg(index, 2, 10, QLatin1Char('0')));
    series.insert("points", QVariantList{point});
    series_list.push_back(series);
```
替换为：
```cpp
    QVariantMap series;
    series.insert("key", QString("pos/joint_%1").arg(index, 2, 10, QLatin1Char('0')));
    series.insert("xs", QVariant::fromValue(QList<double>{0.0}));
    series.insert("ys", QVariant::fromValue(QList<double>{double(index)}));
    series_list.push_back(series);
```
确认 `test/test_qml_smoke.cpp` 顶部含 `#include <QList>`（若缺则加）。

- [ ] **Step 5: 改 bench_timeline_curves.cpp 负载**

在 `test/bench_timeline_curves.cpp` `make_joint_payload` 把内层：
```cpp
    QVariantList pts;
    for (int i = 0; i < points; ++i) {
      const double t = static_cast<double>(i) / static_cast<double>(points) * 12.0;  // 0..12s
      QVariantMap pt;
      pt.insert("x", t);
      pt.insert("y", std::sin(t + s * 0.1) * (1.0 + s % 5));
      pts.push_back(pt);
    }
    QVariantMap sm;
    // 一半 pos/（默认可见），一半 vel/（默认隐藏）—— 贴近真实可见/隐藏比例。
    const QString prefix = (s % 3 == 0) ? "pos/" : (s % 3 == 1 ? "vel/" : "eff/");
    sm.insert("key", prefix + QString("joint_%1").arg(s, 2, 10, QLatin1Char('0')));
    sm.insert("points", pts);
    series_arr.push_back(sm);
```
替换为：
```cpp
    QList<double> xs;
    QList<double> ys;
    xs.reserve(points);
    ys.reserve(points);
    for (int i = 0; i < points; ++i) {
      const double t = static_cast<double>(i) / static_cast<double>(points) * 12.0;  // 0..12s
      xs.push_back(t);
      ys.push_back(std::sin(t + s * 0.1) * (1.0 + s % 5));
    }
    QVariantMap sm;
    // 三类前缀：pos/（默认可见）、vel/ 与 eff/（默认隐藏）—— 贴近真实可见/隐藏比例。
    const QString prefix = (s % 3 == 0) ? "pos/" : (s % 3 == 1 ? "vel/" : "eff/");
    sm.insert("key", prefix + QString("joint_%1").arg(s, 2, 10, QLatin1Char('0')));
    sm.insert("xs", QVariant::fromValue(xs));
    sm.insert("ys", QVariant::fromValue(ys));
    series_arr.push_back(sm);
```
确认 `test/bench_timeline_curves.cpp` 顶部含 `#include <QList>`（若缺则加）。

- [ ] **Step 6: 构建并运行 QML 测试，确认通过**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_curve_plot_js
source ~/.local/ros2_rc && rr && QT_QPA_PLATFORM=offscreen /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/test_qml_smoke
```
Expected: 两者全 PASS（冒烟尤其 `ClickingSeriesLegendChip*` 现端到端走通 xs/ys → 渲染）。

- [ ] **Step 7: 提交**

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add qml/components/curve_plot.js test/test_curve_plot_js.cpp test/test_qml_smoke.cpp test/bench_timeline_curves.cpp
git commit -m "perf(curves): QML buildSeriesCache consumes flat xs/ys arrays

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: 全量构建、全测试与基准复测

**Files:** 无（验证任务）

- [ ] **Step 1: 全量构建 + 全测试**

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --mixin release compile-commands ccache --cmake-args -DBUILD_TESTING=ON
source ~/.local/ros2_rc && rr && QT_QPA_PLATFORM=offscreen colcon test --packages-select data_recorder --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws
colcon test-result --all | tail -5
```
Expected: 全部测试 PASS（≥184 + 新增用例），0 failures。

- [ ] **Step 2: 基准复测（量化收益）**

```bash
source ~/.local/ros2_rc && rr && DR_PROFILE=1 QT_QPA_PLATFORM=offscreen /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/bench_timeline_curves 99 600 30 2>&1 | grep -E "DR_PROFILE] onCurvesUpdated|rebuildCache|--- "
```
Expected: `onCurvesUpdated` 从 ~171ms 显著下降（目标 < ~20ms）。把实测数值填入 spec 的"结果"小节。

- [ ] **Step 3: 把基准结果写回 spec**

编辑 `docs/superpowers/specs/2026-06-29-timeline-curves-flat-arrays-design.md` 的"结果（实现后填写）"小节，填入 onCurvesUpdated 改造前后的实测 ms。提交：
```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add docs/superpowers/specs/2026-06-29-timeline-curves-flat-arrays-design.md
git commit -m "docs: record flat-array benchmark result

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 4: 人工外观/流畅度确认（需人参与）**

启动应用、选含 `/joint_states` 的历史会话、展开该轨道：曲线/配色/默认可见性/纵轴/采样点与改造前一致；展开瞬间不再卡、平移/缩放流畅。（GUI 点击自动化不可靠，故人工确认。）

---

## Self-Review

**Spec 覆盖：**
- 生产端（curve_payload + history_loader）写 xs/ys → Task 1。✓
- 消费端（onCurvesUpdated + updateSeries）读/写 xs/ys → Task 2。✓
- QML buildSeriesCache 消费 xs/ys → Task 3。✓
- 全部相关测试同步 + 往返契约 + 不等长防御 → Task 1/2/3 各 Step。✓（往返由 test_qml_smoke 端到端覆盖：构造 xs/ys → onCurvesUpdated → SeriesListRole → QML 渲染。）
- 基准复测量化 → Task 4。✓
- Non-Goals（信号签名不变、messageDots 不变、C++ 内部 points 不变）→ 计划只改 QVariant 边界表示。✓

**占位符扫描：** 无 TBD；每个代码步骤含完整前后代码。✓

**类型/命名一致性：** 契约字段统一为 `xs`/`ys`（QList<double> 经 QVariant::fromValue）。生产端两文件、消费端 updateSeries、QML buildSeriesCache、所有测试断言、bench 负载全部用 `xs`/`ys`，无残留 `points`（除 C++ 内部 `SeriesSnapshot.points` 与 `EXPECT_FALSE(...contains("points"))` 的负向断言）。消费端与 QML 均按 `min(xs,ys)` 截断。`std::min`→`<algorithm>`、`QList`/`QVariant` include 已在各 Step 提示补齐。✓

**中间态提醒：** Task 1 之后、Task 2 之前 C++ 链不端到端连通（生产发 xs/ys、消费读 points），计划已显式标注"此阶段勿跑冒烟"，避免误判失败。✓
