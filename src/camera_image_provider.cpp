#include "data_recorder/camera_image_provider.hpp"

#include "data_recorder/live_bridge.hpp"

namespace data_recorder
{

CameraImageProvider::CameraImageProvider(LiveBridge * bridge)
: QQuickImageProvider(QQuickImageProvider::Image), bridge_(bridge)
{
}

QImage CameraImageProvider::requestImage(
  const QString & id, QSize * size, const QSize & requestedSize)
{
  // id 形如 "<topic_key>?seq=N"；去掉 query 部分取 key。
  const QString key = id.section('?', 0, 0);
  auto frame = bridge_ ? bridge_->latest_frame(key) : nullptr;
  if (!frame || frame->isNull()) {
    QImage placeholder(requestedSize.isValid() ? requestedSize : QSize(16, 16),
      QImage::Format_RGB888);
    placeholder.fill(Qt::black);
    if (size) { *size = placeholder.size(); }
    return placeholder;
  }
  if (size) { *size = frame->size(); }
  return *frame;  // QImage 隐式共享，拷贝廉价
}

}  // namespace data_recorder
