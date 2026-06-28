#pragma once

#include <QVariantList>

#include <cstddef>
#include <map>
#include <set>
#include <string>

#include "data_recorder/topic_series.hpp"

namespace data_recorder
{

// 把每 topic 的时间序列缓冲打成可跨线程传给 GUI 的 QVariant 负载。
// 折叠点（messageDots）对所有 topic 都发，但抽稀到 dot_budget 以内；
// 重的曲线数组（series）仅对 expanded 集合里的 topic 发，抽稀到 series_budget 以内。
// 这样 GUI 不会被未展开 / 高频话题的大量数据淹没（见实时卡顿修复）。
//
// 返回的每项 QVariantMap 形态（与 LiveBridge::curvesUpdated 契约一致）：
//   { "topicKey": QString, "messageDots": [double], "series": [{"key", "points":[{"x","y"}]}] }
QVariantList build_curve_payload(
  const std::map<std::string, TopicSeries> & series,
  const std::set<std::string> & expanded_topics,
  std::size_t dot_budget,
  std::size_t series_budget);

}  // namespace data_recorder
