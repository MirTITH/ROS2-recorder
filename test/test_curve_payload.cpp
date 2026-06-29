#include <gtest/gtest.h>

#include <QVariantList>
#include <QVariantMap>

#include <map>
#include <set>
#include <string>

#include "data_recorder/curve_payload.hpp"
#include "data_recorder/topic_series.hpp"

using data_recorder::TopicSeries;
using data_recorder::build_curve_payload;

namespace
{
QVariantMap find_topic(const QVariantList & topics, const QString & key)
{
  for (const auto & v : topics) {
    const auto m = v.toMap();
    if (m.value("topicKey").toString() == key) { return m; }
  }
  return {};
}
}  // namespace

// 折叠（未展开）的 topic 只发折叠点，不发重的 series 数组——避免给 GUI 灌不显示的数据。
TEST(CurvePayload, CollapsedTopicCarriesDotsButNoSeries)
{
  std::map<std::string, TopicSeries> series;
  for (int i = 0; i < 10; ++i) {
    series["/joint_states"].add_message_time(static_cast<double>(i));
    series["/joint_states"].add_sample("pos/a", static_cast<double>(i), static_cast<double>(i));
  }

  const std::set<std::string> expanded;  // 空：无展开
  const auto topics = build_curve_payload(series, expanded, /*dot_budget=*/600, /*series_budget=*/600);

  const auto js = find_topic(topics, "/joint_states");
  ASSERT_FALSE(js.isEmpty());
  EXPECT_FALSE(js.value("messageDots").toList().isEmpty());  // 折叠点照发
  EXPECT_TRUE(js.value("series").toList().isEmpty());        // 未展开：不发 series
}

// 展开的 topic 才带 series 曲线数组。
TEST(CurvePayload, ExpandedTopicCarriesSeries)
{
  std::map<std::string, TopicSeries> series;
  for (int i = 0; i < 10; ++i) {
    series["/joint_states"].add_message_time(static_cast<double>(i));
    series["/joint_states"].add_sample("pos/a", static_cast<double>(i), static_cast<double>(i));
  }

  const std::set<std::string> expanded{"/joint_states"};
  const auto topics = build_curve_payload(series, expanded, /*dot_budget=*/600, /*series_budget=*/600);

  const auto js = find_topic(topics, "/joint_states");
  ASSERT_FALSE(js.isEmpty());
  const auto arr = js.value("series").toList();
  ASSERT_EQ(arr.size(), 1);
  const auto entry = arr[0].toMap();
  EXPECT_EQ(entry.value("key").toString().toStdString(), "pos/a");
  EXPECT_FALSE(entry.contains("points"));  // 旧逐点表示已移除
  const auto xs = entry.value("xs").toList();
  const auto ys = entry.value("ys").toList();
  ASSERT_EQ(xs.size(), ys.size());
  EXPECT_EQ(xs.size(), 10);
  ASSERT_FALSE(xs.isEmpty());
  EXPECT_DOUBLE_EQ(xs.front().toDouble(), 0.0);   // 首点 t=0
  EXPECT_DOUBLE_EQ(ys.front().toDouble(), 0.0);   // 首点 v=0
}

// 折叠点受预算上限约束（即便 /tf 这类高频话题灌入大量消息，也不会发上万个点给 GUI）。
TEST(CurvePayload, MessageDotsRespectBudget)
{
  std::map<std::string, TopicSeries> series;
  for (int i = 0; i < 5000; ++i) {
    series["/tf"].add_message_time(static_cast<double>(i) * 0.001);
  }

  const std::set<std::string> expanded;
  const auto topics = build_curve_payload(series, expanded, /*dot_budget=*/600, /*series_budget=*/600);

  const auto tf = find_topic(topics, "/tf");
  ASSERT_FALSE(tf.isEmpty());
  EXPECT_LE(tf.value("messageDots").toList().size(), 600);
}
