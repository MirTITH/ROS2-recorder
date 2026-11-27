#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <cstdint>
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
  void push_playback_frame(
    const QString & topic_key, std::shared_ptr<const QImage> image, std::uint64_t preview_revision);
  bool wants_live_frames() const;
  // 推曲线负载到 GUI；回放模式下被抑制并返回 false（调用方据此不武装背压）。
  bool push_curves(const QVariantList & topics);
  void push_topic_types(const QVariantList & types);

  // —— GUI 线程调用 ——
  // 每次切换数据源后，迟到的旧会话帧会因版本不匹配而丢弃。
  std::uint64_t set_playback_mode(bool on);
  void set_live_overlay_enabled(bool on);
  std::shared_ptr<const QImage> latest_frame(const QString & topic_key) const;
  std::shared_ptr<const QImage> latest_live_overlay_frame(const QString & topic_key) const;

signals:
  void frameReady(const QString & topic_key, int seq);   // QueuedConnection
  void liveOverlayFrameReady(const QString & topic_key, int seq);
  void statsUpdated(const QVariantList & stats);          // QueuedConnection
  void liveEdgeChanged(double seconds);                   // QueuedConnection
  void curvesUpdated(const QVariantList & topics);        // QueuedConnection
  void topicTypesUpdated(const QVariantList & types);     // QueuedConnection

private:
  mutable std::mutex mutex_;
  std::map<QString, std::shared_ptr<const QImage>> frames_;
  std::map<QString, std::shared_ptr<const QImage>> live_overlay_frames_;
  std::map<QString, int> seqs_;
  std::atomic<bool> playback_mode_{false};
  std::atomic<bool> live_overlay_enabled_{false};
  std::atomic<std::uint64_t> preview_revision_{0};
};

}  // namespace data_recorder
