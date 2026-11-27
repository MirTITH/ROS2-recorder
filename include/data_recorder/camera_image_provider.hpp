#pragma once

#include <QQuickImageProvider>

namespace data_recorder
{

class LiveBridge;

// 按 <topic_key>?seq=N 返回对应来源的最新帧。seq 仅用于让 QML 失效缓存。
class CameraImageProvider : public QQuickImageProvider
{
public:
  enum class Source { Preview, LiveOverlay };

  explicit CameraImageProvider(LiveBridge * bridge, Source source = Source::Preview);

  QImage requestImage(const QString & id, QSize * size, const QSize & requestedSize) override;

private:
  LiveBridge * bridge_;  // 不拥有
  Source source_;
};

}  // namespace data_recorder
