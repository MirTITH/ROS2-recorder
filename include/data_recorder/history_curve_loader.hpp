#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <string>

#include "data_recorder/value_extractor.hpp"

namespace data_recorder
{

// 历史会话数值曲线回读器。设计为可直接同步调用（测试）或 moveToThread 后经队列调用
// （AppController）。读 <session_dir>/rosbag：scanTimestamps 一遍取折叠点；
// extractTopic 懒回读单 topic 提取曲线。两者都经 curvesReady/topicTypesReady 发同 Plan 4 契约。
class HistoryCurveLoader : public QObject
{
  Q_OBJECT

public:
  explicit HistoryCurveLoader(QObject * parent = nullptr);

public slots:
  // 扫描整包：每 topic 的消息时间戳（相对 bag 起点秒）→ 折叠点；并发各 topic 类型。
  void scanTimestamps(const QString & session_dir, const QStringList & topic_names);
  // 懒回读单 topic：反序列化提取标量 → 该 topic 的曲线（含折叠点）。
  void extractTopic(const QString & session_dir, const QString & topic_name);

signals:
  void curvesReady(const QVariantList & topics);        // 同 LiveBridge::curvesUpdated 契约
  void topicTypesReady(const QVariantList & types);     // 同 LiveBridge::topicTypesUpdated 契约

private:
  ValueExtractorRegistry registry_;
};

}  // namespace data_recorder
