#include "data_recorder/live_bridge.hpp"

#include <QMetaObject>
#include <QVariantMap>

namespace data_recorder
{

LiveBridge::LiveBridge(QObject * parent)
: QObject(parent)
{
}

void LiveBridge::push_frame(const QString & topic_key, std::shared_ptr<const QImage> image)
{
  bool overlay;
  int seq;
  std::uint64_t revision;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    overlay = playback_mode_.load();
    if (overlay && !live_overlay_enabled_.load()) { return; }
    auto & target = overlay ? live_overlay_frames_ : frames_;
    target[topic_key] = std::move(image);
    seq = ++seqs_[topic_key];
    revision = preview_revision_.load();
  }
  QMetaObject::invokeMethod(this, [this, topic_key, seq, overlay, revision]() {
    if (revision != preview_revision_.load()) { return; }
    if (overlay) {
      if (live_overlay_enabled_.load()) { emit liveOverlayFrameReady(topic_key, seq); }
    } else {
      emit frameReady(topic_key, seq);
    }
  }, Qt::QueuedConnection);
}

void LiveBridge::push_stats(const std::vector<TopicStats> & stats)
{
  if (playback_mode_.load()) { return; }  // 回放模式丢弃实时统计
  QVariantList list;
  for (const auto & s : stats) {
    QVariantMap m;
    m["topicKey"] = QString::fromStdString(s.topic_key);
    m["hz"] = s.hz;
    m["width"] = s.width;
    m["height"] = s.height;
    list.push_back(m);
  }
  QMetaObject::invokeMethod(this, "statsUpdated", Qt::QueuedConnection,
    Q_ARG(QVariantList, list));
}

void LiveBridge::set_live_edge(double seconds)
{
  QMetaObject::invokeMethod(this, "liveEdgeChanged", Qt::QueuedConnection,
    Q_ARG(double, seconds));
}

bool LiveBridge::push_curves(const QVariantList & topics)
{
  if (playback_mode_.load()) { return false; }  // 回放模式由历史路径单独驱动
  QMetaObject::invokeMethod(this, "curvesUpdated", Qt::QueuedConnection,
    Q_ARG(QVariantList, topics));
  return true;
}

void LiveBridge::push_topic_types(const QVariantList & types)
{
  if (playback_mode_.load()) { return; }
  QMetaObject::invokeMethod(this, "topicTypesUpdated", Qt::QueuedConnection,
    Q_ARG(QVariantList, types));
}

std::shared_ptr<const QImage> LiveBridge::latest_frame(const QString & topic_key) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = frames_.find(topic_key);
  return it != frames_.end() ? it->second : nullptr;
}

std::shared_ptr<const QImage> LiveBridge::latest_live_overlay_frame(const QString & topic_key) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = live_overlay_frames_.find(topic_key);
  return it != live_overlay_frames_.end() ? it->second : nullptr;
}

void LiveBridge::push_playback_frame(
  const QString & topic_key, std::shared_ptr<const QImage> image, std::uint64_t preview_revision)
{
  int seq;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!playback_mode_.load() || preview_revision != preview_revision_.load()) { return; }
    frames_[topic_key] = std::move(image);
    seq = ++seqs_[topic_key];
  }
  QMetaObject::invokeMethod(this, [this, topic_key, seq, preview_revision]() {
    if (preview_revision == preview_revision_.load()) { emit frameReady(topic_key, seq); }
  }, Qt::QueuedConnection);
}

std::uint64_t LiveBridge::set_playback_mode(bool on)
{
  std::lock_guard<std::mutex> lock(mutex_);
  playback_mode_.store(on);
  frames_.clear();
  // 保持序号单调递增，使跨模式切换后 QML 能重新拉帧。
  if (!on) {
    live_overlay_enabled_.store(false);
    live_overlay_frames_.clear();
  }
  return ++preview_revision_;
}

bool LiveBridge::wants_live_frames() const
{
  return !playback_mode_.load() || live_overlay_enabled_.load();
}

void LiveBridge::set_live_overlay_enabled(bool on)
{
  std::lock_guard<std::mutex> lock(mutex_);
  live_overlay_enabled_.store(on);
  if (!on) { live_overlay_frames_.clear(); }
}

}  // namespace data_recorder
