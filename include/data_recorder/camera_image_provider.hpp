#pragma once

#include <QQuickImageProvider>

namespace data_recorder
{

class LiveBridge;

// 按 image://camera/<topic_key>?seq=N 返回最新帧。seq 仅用于让 QML 失效缓存。
class CameraImageProvider : public QQuickImageProvider
{
public:
  explicit CameraImageProvider(LiveBridge * bridge);

  QImage requestImage(const QString & id, QSize * size, const QSize & requestedSize) override;

private:
  LiveBridge * bridge_;  // 不拥有
};

}  // namespace data_recorder
