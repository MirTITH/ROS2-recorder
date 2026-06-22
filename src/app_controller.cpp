#include "data_recorder/app_controller.hpp"

#include <QKeyEvent>

#include <algorithm>
#include <filesystem>

#include "data_recorder/live_bridge.hpp"
#include "data_recorder/recorder_engine.hpp"
#include "data_recorder/session_manager.hpp"

namespace data_recorder
{

AppController::AppController(
  const ConfigData & config, LiveBridge * bridge, RecorderEngine * engine,
  SessionManager * session_manager, QObject * parent)
: QObject(parent),
  config_path_(QString::fromStdString(config.config_path)),
  output_directory_(QString::fromStdString(config.output_dir)),
  camera_grid_model_(&topic_model_)
{
  connect(
    &camera_grid_model_,
    &CameraGridModel::countChanged,
    this,
    [this]() {
      refreshVisibleCameraCount();
    });

  topic_model_.set_topics(config.topics);
  visible_camera_count_ = camera_grid_model_.rowCount();

  tag_model_.set_tags(config.tags);
  event_marker_model_.set_markers(config.event_markers);

  bridge_ = bridge;
  engine_ = engine;
  session_manager_ = session_manager;

  if (bridge_) {
    connect(bridge_, &LiveBridge::statsUpdated, this, &AppController::onStatsUpdated);
    connect(bridge_, &LiveBridge::frameReady, this, &AppController::onFrameReady);
    connect(bridge_, &LiveBridge::liveEdgeChanged, this, &AppController::onLiveEdge);
  }
  refreshSessions();  // 启动扫描
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

bool AppController::historyMode() const
{
  return history_mode_;
}

int AppController::selectedSessionRow() const
{
  return selected_session_row_;
}

bool AppController::canRecord() const
{
  return !history_mode_;
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
  if (history_mode_) {
    return QStringLiteral("历史查看");
  }
  if (recording_) {
    return following_live_edge_ ? QStringLiteral("录制中") : QStringLiteral("录制中回看");
  }
  return QStringLiteral("实时查看");
}

int AppController::visibleCameraCount() const
{
  return visible_camera_count_;
}

TopicListModel * AppController::topicModel()
{
  return &topic_model_;
}

CameraGridModel * AppController::cameraGridModel()
{
  return &camera_grid_model_;
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
  if (!canRecord()) {
    return;
  }

  const bool prev_recording = recording_;
  const bool prev_following = following_live_edge_;
  const double prev_playhead = playhead_seconds_;
  const QString prev_status = status_text_;
  const QString prev_mode = modeText();

  if (!recording_) {
    // 开始
    event_marker_model_.clearInstances();
    tag_model_.clearSelection();  // 清标签选择，每次录制从干净状态开始
    if (engine_) {
      const std::string id = engine_->start_session();
      if (id.empty()) {
        status_text_ = QStringLiteral("录制启动失败");
        emit statusTextChanged();
        return;
      }
    }
    recording_ = true;
  } else {
    // 停止
    if (engine_) {
      engine_->stop_session(
        event_marker_model_.exportAnnotations(), tag_model_.exportSelectedTags());
    }
    recording_ = false;
    refreshSessions();
  }

  if (recording_) {
    following_live_edge_ = true;
    playhead_seconds_ = live_edge_seconds_;
  } else {
    following_live_edge_ = false;
  }
  status_text_ = statusTextForCurrentState();

  if (prev_recording != recording_) {
    emit recordingChanged();
  }
  if (prev_following != following_live_edge_) {
    emit followingLiveEdgeChanged();
  }
  if (prev_playhead != playhead_seconds_) {
    emit playheadSecondsChanged();
  }
  if (prev_status != status_text_) {
    emit statusTextChanged();
  }
  if (prev_mode != modeText()) {
    emit modeTextChanged();
  }
}

void AppController::selectOnlineData()
{
  if (!history_mode_ && selected_session_row_ == -1) {
    return;
  }

  const QString previous_status = status_text_;
  const QString previous_mode = modeText();
  const bool previous_can_record = canRecord();
  history_mode_ = false;
  selected_session_row_ = -1;
  status_text_ = statusTextForCurrentState();
  emit dataSourceChanged();
  if (previous_can_record != canRecord()) {
    emit canRecordChanged();
  }
  if (previous_status != status_text_) {
    emit statusTextChanged();
  }
  if (previous_mode != modeText()) {
    emit modeTextChanged();
  }
}

void AppController::selectHistorySession(int row)
{
  if (recording_ || row < 0 || row >= recording_session_model_.rowCount()) {
    return;
  }

  const auto session_index = recording_session_model_.index(row, 0);
  const QString folder_name =
    recording_session_model_.data(session_index, RecordingSessionModel::FolderNameRole).toString();
  if (folder_name.isEmpty()) {
    return;
  }

  const QString previous_status = status_text_;
  const QString previous_mode = modeText();
  const bool previous_can_record = canRecord();
  const bool previous_history_mode = history_mode_;
  const int previous_selected_row = selected_session_row_;
  const bool previous_following = following_live_edge_;
  history_mode_ = true;
  selected_session_row_ = row;
  following_live_edge_ = false;
  status_text_ = statusTextForCurrentState();
  if (previous_history_mode != history_mode_ || previous_selected_row != selected_session_row_) {
    emit dataSourceChanged();
  }
  if (previous_can_record != canRecord()) {
    emit canRecordChanged();
  }
  if (previous_following != following_live_edge_) {
    emit followingLiveEdgeChanged();
  }
  if (previous_status != status_text_) {
    emit statusTextChanged();
  }
  if (previous_mode != modeText()) {
    emit modeTextChanged();
  }
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
    refreshStatusText();
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
    refreshStatusText();
    emit followingLiveEdgeChanged();
    emit modeTextChanged();
  }
  emit playheadSecondsChanged();
}

bool AppController::triggerMarkerShortcut(const QString & shortcut)
{
  return event_marker_model_.triggerShortcut(shortcut, playhead_seconds_);
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

  const Qt::KeyboardModifiers modifiers = key_event->modifiers();
  const bool unmodified = modifiers == Qt::NoModifier;
  const bool marker_modifiers = unmodified || modifiers == Qt::ShiftModifier;

  if (key_event->key() == Qt::Key_Space) {
    if (!unmodified) {
      return QObject::eventFilter(watched, event);
    }
    toggleRecording();
    event->accept();
    return true;
  }

  const QString text = key_event->text();
  if (marker_modifiers && text.size() == 1 && !text.at(0).isSpace() &&
    triggerMarkerShortcut(text))
  {
    event->accept();
    return true;
  }

  return QObject::eventFilter(watched, event);
}

void AppController::refreshVisibleCameraCount()
{
  const int next_count = camera_grid_model_.rowCount();
  if (visible_camera_count_ != next_count) {
    visible_camera_count_ = next_count;
    emit visibleCameraCountChanged();
  }
}

QString AppController::statusTextForCurrentState() const
{
  if (history_mode_) {
    const auto session_index = recording_session_model_.index(selected_session_row_, 0);
    const QString folder_name =
      recording_session_model_.data(session_index, RecordingSessionModel::FolderNameRole).toString();
    return QStringLiteral("历史查看：%1").arg(folder_name);
  }
  return modeText();
}

void AppController::refreshStatusText()
{
  const QString next_status_text = statusTextForCurrentState();
  if (status_text_ == next_status_text) {
    return;
  }

  status_text_ = next_status_text;
  emit statusTextChanged();
}

void AppController::onStatsUpdated(const QVariantList & stats)
{
  for (const auto & v : stats) {
    const auto m = v.toMap();
    const QString key = m.value("topicKey").toString();
    topic_model_.updateStats(key, m.value("hz").toDouble(),
      m.value("width").toInt(), m.value("height").toInt());
  }
}

void AppController::onFrameReady(const QString & key, int seq)
{
  topic_model_.updateFrameSeq(key, seq);
  camera_grid_model_.updateFrameSeq(key, seq);
}

void AppController::onLiveEdge(double seconds)
{
  advanceLiveEdge(seconds);  // 复用现有方法
}

void AppController::refreshSessions()
{
  if (!session_manager_) { return; }
  const std::string dir = std::filesystem::absolute(output_directory_.toStdString()).string();
  recording_session_model_.setSessions(session_manager_->scan(dir));
}

}  // namespace data_recorder
