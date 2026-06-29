// 纯函数库：时间轴数值曲线的缓存构建 / 可见区间二分 / 视口裁剪折线。
// 无 QML / Canvas 依赖，可被 TimelineTrackRow.qml 导入，也可在 QJSEngine 中直接 evaluate 单测。
// （刻意不加 .pragma library，以便测试直接对本文件文本求值。）

// 从 seriesList（[{color, visible, points:[{x,y}]}]）构建扁平数组缓存。
// 仅纳入 visible !== false 的序列；逐点过滤非有限值。
// 纵轴范围以 [-1, 1] 为基线再按数据扩展（与原 onPaint 行为一致）。
// 返回 { series:[{xs:Float64Array, ys:Float64Array, color}], minY, maxY }。
// 约定：每个 points 元素均为 {x,y} 对象（由 C++ updateSeries 保证，不含 null）。
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
            // count === n 时直接复用整段；否则 slice 复制存活部分（slice 右切新数组，
            // 不像 subarray 会把原超长 buffer 钉在缓存生命周期里）。
            xs: count === n ? xs : xs.slice(0, count),
            ys: count === n ? ys : ys.slice(0, count),
            color: entry.color
        })
    }
    // minY 起于 -1 且只减、maxY 起于 1 且只增，二者必然分离，故无需平直兜底。
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
// 要求 xs 非递减（与 buildSeriesCache 输出一致）。
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
