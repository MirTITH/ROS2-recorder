#include "data_recorder/camera_image_provider.hpp"

#include "data_recorder/live_bridge.hpp"

namespace data_recorder
{

CameraImageProvider::CameraImageProvider(LiveBridge * bridge, Source source)
: QQuickImageProvider(QQuickImageProvider::Image), bridge_(bridge), source_(source)
{
}

QImage CameraImageProvider::requestImage(
  const QString & id, QSize * size, const QSize & requestedSize)
{
  // id 形如 "<topic_key>?seq=N"；去掉 query 部分取 key。
  const QString key = id.section('?', 0, 0);
  const bool overlay = source_ == Source::LiveOverlay;
  auto frame = bridge_ ? (overlay ? bridge_->latest_live_overlay_frame(key) : bridge_->latest_frame(key))
    : nullptr;
  if (!frame || frame->isNull()) {
    QImage placeholder(requestedSize.isValid() ? requestedSize : QSize(16, 16),
      overlay ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB888);
    placeholder.fill(overlay ? Qt::transparent : Qt::black);
    if (size) { *size = placeholder.size(); }
    return placeholder;
  }
  if (size) { *size = frame->size(); }
  return *frame;  // QImage 隐式共享，拷贝廉价
}

}  // namespace data_recorder
