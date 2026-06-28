#include "data_recorder/app_controller.hpp"

#include <QKeyEvent>
#include <QStringList>

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "data_recorder/live_bridge.hpp"
#include "data_recorder/recorder_engine.hpp"
#include "data_recorder/session_manager.hpp"
#include "data_recorder/session_player.hpp"
#include "data_recorder/history_curve_loader.hpp"
#include "data_recorder/topic_series.hpp"

namespace data_recorder
{

namespace
{
// 默认标尺长度：录制时间轴在实时端/播放头尚未超过该跨度前固定显示这么长，
// 之后随实时端增长，保证播放头始终落在可寻址区间内。
constexpr double kDefaultTimelineSpanSeconds = 60.0;
}  // namespace

AppController::AppController(
  const ConfigData & config, LiveBridge * bridge, RecorderEngine * engine,
  SessionManager * session_manager, QObject * parent)
: QObject(parent),
  config_path_(QString::fromStdString(config.config_path)),
  output_directory_(QString::fromStdString(config.output_dir)),
  camera_grid_model_(&topic_model_)
{
  qRegisterMetaType<data_recorder::SessionRecord>("data_recorder::SessionRecord");

  connect(
    &camera_grid_model_,
    &CameraGridModel::countChanged,
    this,
    [this]() {
      refreshVisibleCameraCount();
    });

  topic_model_.set_topics(config.topics);
  register_builtin_extractors(extractor_registry_);
  topic_model_.set_extractor_registry(&extractor_registry_);
  live_topics_ = config.topics;
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
    connect(bridge_, &LiveBridge::curvesUpdated, this, &AppController::onCurvesUpdated);
    connect(bridge_, &LiveBridge::topicTypesUpdated, this, &AppController::onTopicTypesUpdated);
  }

  player_thread_ = new QThread(this);
  player_ = new SessionPlayer(bridge_);
  player_->moveToThread(player_thread_);
  connect(player_thread_, &QThread::finished, player_, &QObject::deleteLater);
  connect(player_, &SessionPlayer::playheadAdvanced, this,
    [this](double seconds) {
      if (!history_mode_) { return; }
      if (playhead_seconds_ != seconds) {
        playhead_seconds_ = seconds;
        emit playheadSecondsChanged();
      }
    });
  connect(player_, &SessionPlayer::playingChanged, this,
    [this](bool on) {
      if (playing_ != on) {
        playing_ = on;
        emit playingChanged();
      }
    });
  player_thread_->start();

  curve_loader_thread_ = new QThread(this);
  curve_loader_ = new HistoryCurveLoader();
  curve_loader_->moveToThread(curve_loader_thread_);
  connect(curve_loader_thread_, &QThread::finished, curve_loader_, &QObject::deleteLater);
  connect(curve_loader_, &HistoryCurveLoader::curvesReady,
    this, &AppController::onCurvesUpdated);
  connect(curve_loader_, &HistoryCurveLoader::topicTypesReady,
    this, &AppController::onTopicTypesUpdated);
  curve_loader_thread_->start();

  refreshSessions();  // 启动扫描
}

AppController::~AppController()
{
  if (player_thread_) {
    player_thread_->quit();
    player_thread_->wait();
  }
  if (curve_loader_thread_) {
    curve_loader_thread_->quit();
    curve_loader_thread_->wait();
  }
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

bool AppController::playing() const
{
  return playing_;
}

void AppController::togglePlayback()
{
  if (!history_mode_ || !player_) { return; }
  QMetaObject::invokeMethod(player_, [p = player_] { p->togglePlay(); }, Qt::QueuedConnection);
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

double AppController::timelineDurationSeconds() const
{
  if (history_mode_ && selected_session_row_ >= 0 &&
    selected_session_row_ < static_cast<int>(scanned_sessions_.size()))
  {
    const double d = scanned_sessions_[static_cast<std::size_t>(selected_session_row_)]
      .duration_seconds;
    if (d > 0.0) { return d; }
  }
  return std::max({live_edge_seconds_, playhead_seconds_, kDefaultTimelineSpanSeconds});
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

  if (bridge_) { bridge_->set_playback_mode(false); }
  if (player_) { QMetaObject::invokeMethod(player_, [p = player_] { p->stop(); }, Qt::QueuedConnection); }
  topic_model_.set_topics(live_topics_);
  event_marker_model_.clearInstances();
  tag_model_.clearSelection();
  playhead_seconds_ = 0.0;
  emit playheadSecondsChanged();
  emit timelineDurationSecondsChanged();
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

  if (row >= 0 && row < static_cast<int>(scanned_sessions_.size())) {
    const SessionRecord & session = scanned_sessions_[static_cast<std::size_t>(row)];

    std::vector<TopicEntry> session_topics;
    for (const auto & tref : session.topics) {
      TopicEntry e;
      e.topic_name = tref.name;
      e.backend_name = tref.backend;
      // 与实时配置的相机判定保持一致（ConfigModel::is_camera_topic）：
      // backend 为 video，或话题名（小写）含 "image" 即视为相机。
      auto lower_name = tref.name;
      std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      const bool is_camera =
        (tref.backend == "video") || lower_name.find("image") != std::string::npos;
      e.ui_category = is_camera ? TopicUiCategory::CameraPreview : TopicUiCategory::NumericTrack;
      session_topics.push_back(e);
    }
    topic_model_.set_topics(session_topics);
    event_marker_model_.setInstances(session.annotations);
    tag_model_.setSelectedTags(session.tags);

    playhead_seconds_ = 0.0;
    emit playheadSecondsChanged();
    emit timelineDurationSecondsChanged();

    if (bridge_) { bridge_->set_playback_mode(true); }
    if (player_) {
      QMetaObject::invokeMethod(player_, [p = player_, session] { p->load(session); },
        Qt::QueuedConnection);
    }
    if (curve_loader_) {
      QStringList topic_names;
      for (const auto & e : session_topics) {
        topic_names.push_back(QString::fromStdString(e.topic_name));
      }
      const QString dir = QString::fromStdString(session.directory);
      QMetaObject::invokeMethod(curve_loader_,
        [loader = curve_loader_, dir, topic_names] {
          loader->scanTimestamps(dir, topic_names);
        },
        Qt::QueuedConnection);
    }
  }
}

void AppController::setPlayheadSeconds(double seconds)
{
  if (history_mode_) {
    const double clamped = std::max(0.0, seconds);
    if (playhead_seconds_ != clamped) {
      playhead_seconds_ = clamped;
      emit playheadSecondsChanged();
    }
    if (player_) {
      QMetaObject::invokeMethod(player_, [p = player_, clamped] { p->seek(clamped); },
        Qt::QueuedConnection);
    }
    return;
  }
  const double clamped_seconds = std::max(0.0, seconds);
  const bool was_following = following_live_edge_;
  const double previous_timeline_duration = timelineDurationSeconds();
  if (recording_) {
    following_live_edge_ = false;
  }
  if (playhead_seconds_ != clamped_seconds) {
    playhead_seconds_ = clamped_seconds;
    emit playheadSecondsChanged();
  }
  if (timelineDurationSeconds() != previous_timeline_duration) {
    emit timelineDurationSecondsChanged();
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
  const double previous_timeline_duration = timelineDurationSeconds();
  live_edge_seconds_ = clamped_seconds;
  emit liveEdgeSecondsChanged();
  if (recording_ && following_live_edge_) {
    playhead_seconds_ = live_edge_seconds_;
    emit playheadSecondsChanged();
  }
  if (timelineDurationSeconds() != previous_timeline_duration) {
    emit timelineDurationSecondsChanged();
  }
}

void AppController::returnToLiveEdge()
{
  const bool was_following = following_live_edge_;
  const double previous_timeline_duration = timelineDurationSeconds();
  following_live_edge_ = recording_;
  playhead_seconds_ = live_edge_seconds_;
  if (was_following != following_live_edge_) {
    refreshStatusText();
    emit followingLiveEdgeChanged();
    emit modeTextChanged();
  }
  emit playheadSecondsChanged();
  if (timelineDurationSeconds() != previous_timeline_duration) {
    emit timelineDurationSecondsChanged();
  }
}

void AppController::detachFromLiveEdge()
{
  // 仅在录制且当前正跟随实时端时生效：脱离实时端进入“录制中回看”，播放头保持原位。
  if (!recording_ || !following_live_edge_) {
    return;
  }
  following_live_edge_ = false;
  refreshStatusText();
  emit followingLiveEdgeChanged();
  emit modeTextChanged();
}

bool AppController::triggerMarkerShortcut(const QString & shortcut)
{
  if (history_mode_) { return false; }
  return event_marker_model_.triggerShortcut(shortcut, playhead_seconds_);
}

void AppController::toggleTopicVisible(int row)
{
  topic_model_.toggleVisible(row);
}

void AppController::setTopicExpanded(const QString & topic_key, bool expanded)
{
  topic_model_.setExpanded(topic_key, expanded);

  // 维护展开集合并转发给引擎：仅展开的 topic 才接收重的 series 曲线负载（实时卡顿修复）。
  if (expanded) {
    expanded_topics_.insert(topic_key.toStdString());
  } else {
    expanded_topics_.erase(topic_key.toStdString());
  }
  if (engine_) { engine_->set_expanded_topics(expanded_topics_); }

  if (expanded && history_mode_ && curve_loader_ &&
    selected_session_row_ >= 0 &&
    selected_session_row_ < static_cast<int>(scanned_sessions_.size()))
  {
    const QString dir = QString::fromStdString(
      scanned_sessions_[static_cast<std::size_t>(selected_session_row_)].directory);
    QMetaObject::invokeMethod(curve_loader_,
      [loader = curve_loader_, dir, topic_key] {
        loader->extractTopic(dir, topic_key);
      },
      Qt::QueuedConnection);
  }
}

void AppController::setSeriesVisible(
  const QString & topic_key, const QString & series_key, bool visible)
{
  topic_model_.setSeriesVisible(topic_key, series_key, visible);
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

void AppController::onTopicTypesUpdated(const QVariantList & types)
{
  for (const auto & v : types) {
    const auto m = v.toMap();
    topic_model_.updateTopicType(
      m.value("topicKey").toString(), m.value("rosType").toString());
  }
}

void AppController::onCurvesUpdated(const QVariantList & topics)
{
  for (const auto & v : topics) {
    const auto m = v.toMap();
    const QString topic_key = m.value("topicKey").toString();

    std::vector<double> dots;
    const auto dot_list = m.value("messageDots").toList();
    dots.reserve(static_cast<std::size_t>(dot_list.size()));
    for (const auto & d : dot_list) { dots.push_back(d.toDouble()); }
    topic_model_.updateMessageDots(topic_key, dots);

    std::vector<TopicSeries::SeriesSnapshot> series;
    const auto series_list = m.value("series").toList();
    series.reserve(static_cast<std::size_t>(series_list.size()));
    for (const auto & s : series_list) {
      const auto sm = s.toMap();
      TopicSeries::SeriesSnapshot snap;
      snap.key = sm.value("key").toString().toStdString();
      const auto pts = sm.value("points").toList();
      snap.points.reserve(static_cast<std::size_t>(pts.size()));
      for (const auto & p : pts) {
        const auto pm = p.toMap();
        snap.points.emplace_back(pm.value("x").toDouble(), pm.value("y").toDouble());
      }
      series.push_back(std::move(snap));
    }
    topic_model_.updateSeries(topic_key, series);
  }
  // 背压复位：本帧曲线已消费，允许引擎推下一帧。
  if (engine_) { engine_->notify_curves_consumed(); }
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
  scanned_sessions_ = session_manager_->scan(dir);
  recording_session_model_.setSessions(scanned_sessions_);
}

}  // namespace data_recorder
