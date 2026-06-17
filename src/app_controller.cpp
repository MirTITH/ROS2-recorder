#include "data_recorder/app_controller.hpp"

#include <QKeyEvent>

#include <algorithm>

namespace data_recorder
{

AppController::AppController(const ConfigData & config, QObject * parent)
: QObject(parent),
  config_path_(QString::fromStdString(config.config_path)),
  output_directory_(QString::fromStdString(config.output_dir))
{
  topic_model_.set_topics(config.topics);
  visible_camera_count_ = topic_model_.visibleCameraCount();
  connect(
    &topic_model_,
    &TopicListModel::dataChanged,
    this,
    [this](const QModelIndex &, const QModelIndex &, const QList<int> &) {
      refreshVisibleCameraCount();
    });
  connect(
    &topic_model_,
    &TopicListModel::modelReset,
    this,
    [this]() {
      refreshVisibleCameraCount();
    });

  camera_model_.set_topics(config.camera_topics);
  track_model_.set_topics(config.track_topics);
  tag_model_.set_tags(config.tags);
  event_marker_model_.set_markers(config.event_markers);
  connect(
    &event_marker_model_,
    &EventMarkerModel::selectedShortcutChanged,
    this,
    &AppController::updateSelectedMarkerShortcut);
}

QString AppController::configPath() const
{
  return config_path_;
}

QString AppController::outputDirectory() const
{
  return output_directory_;
}

QString AppController::statusText() const
{
  return status_text_;
}

bool AppController::recording() const
{
  return recording_;
}

double AppController::playheadSeconds() const
{
  return playhead_seconds_;
}

double AppController::liveEdgeSeconds() const
{
  return live_edge_seconds_;
}

bool AppController::followingLiveEdge() const
{
  return following_live_edge_;
}

QString AppController::modeText() const
{
  if (recording_) {
    return following_live_edge_ ? QStringLiteral("录制中") : QStringLiteral("查看");
  }
  return QStringLiteral("查看");
}

QString AppController::selectedMarkerShortcut() const
{
  return selected_marker_shortcut_;
}

int AppController::visibleCameraCount() const
{
  return visible_camera_count_;
}

TopicListModel * AppController::topicModel()
{
  return &topic_model_;
}

TopicListModel * AppController::cameraModel()
{
  return &camera_model_;
}

TopicListModel * AppController::trackModel()
{
  return &track_model_;
}

TagListModel * AppController::tagModel()
{
  return &tag_model_;
}

EventMarkerModel * AppController::eventMarkerModel()
{
  return &event_marker_model_;
}

RecordingSessionModel * AppController::recordingSessionModel()
{
  return &recording_session_model_;
}

void AppController::toggleRecording()
{
  recording_ = !recording_;
  if (recording_) {
    following_live_edge_ = true;
    playhead_seconds_ = live_edge_seconds_;
    status_text_ = QStringLiteral("录制中（界面原型）");
  } else {
    following_live_edge_ = false;
    status_text_ = QStringLiteral("已停止");
  }
  emit recordingChanged();
  emit followingLiveEdgeChanged();
  emit playheadSecondsChanged();
  emit statusTextChanged();
  emit modeTextChanged();
}

void AppController::setPlayheadSeconds(double seconds)
{
  const double clamped_seconds = std::max(0.0, seconds);
  const bool was_following = following_live_edge_;
  if (recording_) {
    following_live_edge_ = false;
  }
  if (playhead_seconds_ != clamped_seconds) {
    playhead_seconds_ = clamped_seconds;
    emit playheadSecondsChanged();
  }
  if (was_following != following_live_edge_) {
    emit followingLiveEdgeChanged();
    emit modeTextChanged();
  }
}

void AppController::advanceLiveEdge(double seconds)
{
  const double clamped_seconds = std::max(0.0, seconds);
  if (live_edge_seconds_ == clamped_seconds) {
    return;
  }
  live_edge_seconds_ = clamped_seconds;
  emit liveEdgeSecondsChanged();
  if (recording_ && following_live_edge_) {
    playhead_seconds_ = live_edge_seconds_;
    emit playheadSecondsChanged();
  }
}

void AppController::returnToLiveEdge()
{
  const bool was_following = following_live_edge_;
  following_live_edge_ = recording_;
  playhead_seconds_ = live_edge_seconds_;
  if (was_following != following_live_edge_) {
    emit followingLiveEdgeChanged();
    emit modeTextChanged();
  }
  emit playheadSecondsChanged();
}

bool AppController::triggerMarkerShortcut(const QString & shortcut)
{
  if (!event_marker_model_.selectByShortcut(shortcut)) {
    return false;
  }
  return true;
}

void AppController::toggleTopicVisible(int row)
{
  topic_model_.toggleVisible(row);
}

bool AppController::eventFilter(QObject * watched, QEvent * event)
{
  if (event->type() != QEvent::KeyPress) {
    return QObject::eventFilter(watched, event);
  }

  auto * key_event = static_cast<QKeyEvent *>(event);
  if (key_event->isAutoRepeat()) {
    return QObject::eventFilter(watched, event);
  }

  if (key_event->key() == Qt::Key_Space) {
    toggleRecording();
    event->accept();
    return true;
  }

  const QString text = key_event->text();
  if (text.size() == 1 && !text.at(0).isSpace() && triggerMarkerShortcut(text)) {
    event->accept();
    return true;
  }

  return QObject::eventFilter(watched, event);
}

void AppController::refreshVisibleCameraCount()
{
  const int next_count = topic_model_.visibleCameraCount();
  if (visible_camera_count_ != next_count) {
    visible_camera_count_ = next_count;
    emit visibleCameraCountChanged();
  }
}

void AppController::updateSelectedMarkerShortcut(const QString & shortcut)
{
  if (selected_marker_shortcut_ == shortcut) {
    return;
  }
  selected_marker_shortcut_ = shortcut;
  emit selectedMarkerShortcutChanged();
}

}  // namespace data_recorder
