#include "data_recorder/curve_payload.hpp"

#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace data_recorder
{

QVariantMap snap_to_series_map(const TopicSeries::SeriesSnapshot & snap)
{
  QList<double> xs;
  QList<double> ys;
  xs.reserve(static_cast<qsizetype>(snap.points.size()));
  ys.reserve(static_cast<qsizetype>(snap.points.size()));
  for (const auto & p : snap.points) {
    xs.push_back(p.first);
    ys.push_back(p.second);
  }
  QVariantMap series_map;
  series_map.insert("key", QString::fromStdString(snap.key));
  series_map.insert("xs", QVariant::fromValue(xs));
  series_map.insert("ys", QVariant::fromValue(ys));
  return series_map;
}

QVariantList build_curve_payload(
  const std::map<std::string, TopicSeries> & series,
  const std::set<std::string> & expanded_topics,
  std::size_t dot_budget,
  std::size_t series_budget)
{
  QVariantList topics;
  for (const auto & [topic, buffer] : series) {
    QVariantMap topic_map;
    topic_map.insert("topicKey", QString::fromStdString(topic));

    QVariantList dots;
    for (const double t : buffer.message_times(dot_budget)) { dots.push_back(t); }
    topic_map.insert("messageDots", dots);

    // 重的曲线数组只发给已展开的 topic：折叠态 GUI 只画折叠点，无需 series。
    QVariantList series_arr;
    if (expanded_topics.count(topic) != 0) {
      for (const auto & snap : buffer.snapshot(series_budget)) {
        series_arr.push_back(snap_to_series_map(snap));
      }
    }
    topic_map.insert("series", series_arr);
    topics.push_back(topic_map);
  }
  return topics;
}

}  // namespace data_recorder
