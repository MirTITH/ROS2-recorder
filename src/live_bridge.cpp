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
  if (playback_mode_.load()) { return; }  // 回放模式丢弃实时帧
  int seq = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_[topic_key] = std::move(image);
    seq = ++seqs_[topic_key];
  }
  // 跨线程发信号：排到 GUI 线程事件循环。
  QMetaObject::invokeMethod(this, "frameReady", Qt::QueuedConnection,
    Q_ARG(QString, topic_key), Q_ARG(int, seq));
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

std::shared_ptr<const QImage> LiveBridge::latest_frame(const QString & topic_key) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = frames_.find(topic_key);
  return it != frames_.end() ? it->second : nullptr;
}

void LiveBridge::push_playback_frame(const QString & topic_key, std::shared_ptr<const QImage> image)
{
  if (!playback_mode_.load()) { return; }  // 回放已结束：丢弃迟到的解码帧
  int seq = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_[topic_key] = std::move(image);
    seq = ++seqs_[topic_key];
  }
  QMetaObject::invokeMethod(this, "frameReady", Qt::QueuedConnection,
    Q_ARG(QString, topic_key), Q_ARG(int, seq));
}

void LiveBridge::set_playback_mode(bool on)
{
  playback_mode_.store(on);
  if (!on) {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.clear();
    // 注意：不清 seqs_，保持 seq 单调递增，避免 image provider 缓存键回退
  }
}

}  // namespace data_recorder
