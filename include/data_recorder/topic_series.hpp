#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace data_recorder
{

// 一个时间点的值：(秒, 值)。
using SeriesPoint = std::pair<double, double>;

// 把点序列抽稀到至多 budget 个点（输入须按时间 first 升序）。
// size<=budget 原样返回；否则分 budget/2 个桶，每桶输出桶内 min 与 max
// （按时间先后），保留尖峰；结果按时间升序。budget<2 视为 2。
// 注意：实际上限为 (budget/2)*2，故奇数 budget 实际最多 budget-1 个点。
std::vector<SeriesPoint> decimate(const std::vector<SeriesPoint> & points, std::size_t budget);

}  // namespace data_recorder
