#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "data_recorder/recorder_types.hpp"

namespace data_recorder
{

// 线程安全的"每路最新帧"仓 + 把引擎事件 marshal 到 GUI 线程的信号。
// 引擎在 ROS 线程调 push_frame/push_stats/advance_live_edge；信号经 QueuedConnection 到 GUI。
class LiveBridge : public QObject
{
  Q_OBJECT

public:
  explicit LiveBridge(QObject * parent = nullptr);

  // —— ROS 线程调用（线程安全）——
  void push_frame(const QString & topic_key, std::shared_ptr<const QImage> image);
  void push_stats(const std::vector<TopicStats> & stats);
  void set_live_edge(double seconds);
  void push_playback_frame(const QString & topic_key, std::shared_ptr<const QImage> image);
  void set_playback_mode(bool on);
  void push_curves(const QVariantList & topics);
  void push_topic_types(const QVariantList & types);

  // —— GUI 线程调用（image provider）——
  std::shared_ptr<const QImage> latest_frame(const QString & topic_key) const;

signals:
  void frameReady(const QString & topic_key, int seq);   // QueuedConnection
  void statsUpdated(const QVariantList & stats);          // QueuedConnection
  void liveEdgeChanged(double seconds);                   // QueuedConnection
  void curvesUpdated(const QVariantList & topics);        // QueuedConnection
  void topicTypesUpdated(const QVariantList & types);     // QueuedConnection

private:
  mutable std::mutex mutex_;
  std::map<QString, std::shared_ptr<const QImage>> frames_;
  std::map<QString, int> seqs_;
  std::atomic<bool> playback_mode_{false};
};

}  // namespace data_recorder
