#include "data_recorder/app_controller.hpp"

#include <algorithm>

namespace data_recorder
{

AppController::AppController(const ConfigData & config, QObject * parent)
: QObject(parent),
  config_path_(QString::fromStdString(config.config_path)),
  output_directory_(QString::fromStdString(config.output_dir))
{
  topic_model_.set_topics(config.topics);
  camera_model_.set_topics(config.camera_topics);
  track_model_.set_topics(config.track_topics);
  tag_model_.set_tags(config.tags);
  event_marker_model_.set_markers(config.event_markers);
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
  status_text_ = recording_ ? QStringLiteral("录制中（界面原型）") : QStringLiteral("已停止");
  emit recordingChanged();
  emit statusTextChanged();
}

void AppController::setPlayheadSeconds(double seconds)
{
  const double clamped_seconds = std::max(0.0, seconds);
  if (playhead_seconds_ == clamped_seconds) {
    return;
  }

  playhead_seconds_ = clamped_seconds;
  emit playheadSecondsChanged();
}

}  // namespace data_recorder
