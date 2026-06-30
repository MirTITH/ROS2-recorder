#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantList>

#include <set>
#include <string>
#include <vector>

class QEvent;

#include "data_recorder/camera_grid_model.hpp"
#include "data_recorder/config_model.hpp"
#include "data_recorder/recorder_types.hpp"
#include "data_recorder/ui_models.hpp"
#include "data_recorder/value_extractor.hpp"

namespace data_recorder
{

class LiveBridge;
class RecorderEngine;
class SessionManager;
class SessionPlayer;
class HistoryCurveLoader;

class AppController : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString configPath READ configPath CONSTANT)
  Q_PROPERTY(QString outputDirectory READ outputDirectory CONSTANT)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
  Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
  Q_PROPERTY(bool historyMode READ historyMode NOTIFY dataSourceChanged)
  Q_PROPERTY(int selectedSessionRow READ selectedSessionRow NOTIFY dataSourceChanged)
  Q_PROPERTY(bool canRecord READ canRecord NOTIFY canRecordChanged)
  Q_PROPERTY(double playheadSeconds READ playheadSeconds NOTIFY playheadSecondsChanged)
  Q_PROPERTY(double liveEdgeSeconds READ liveEdgeSeconds NOTIFY liveEdgeSecondsChanged)
  Q_PROPERTY(double timelineDurationSeconds READ timelineDurationSeconds NOTIFY
    timelineDurationSecondsChanged)
  Q_PROPERTY(bool followingLiveEdge READ followingLiveEdge NOTIFY followingLiveEdgeChanged)
  Q_PROPERTY(QString modeText READ modeText NOTIFY modeTextChanged)
  Q_PROPERTY(int visibleCameraCount READ visibleCameraCount NOTIFY visibleCameraCountChanged)
  Q_PROPERTY(TopicListModel * topicModel READ topicModel CONSTANT)
  Q_PROPERTY(CameraGridModel * cameraGridModel READ cameraGridModel CONSTANT)
  Q_PROPERTY(TagListModel * tagModel READ tagModel CONSTANT)
  Q_PROPERTY(EventMarkerModel * eventMarkerModel READ eventMarkerModel CONSTANT)
  Q_PROPERTY(RecordingSessionModel * recordingSessionModel READ recordingSessionModel CONSTANT)

public:
  explicit AppController(
    const ConfigData & config, LiveBridge * bridge = nullptr, RecorderEngine * engine = nullptr,
    SessionManager * session_manager = nullptr, QObject * parent = nullptr);

  ~AppController() override;

  QString configPath() const;
  QString outputDirectory() const;
  QString statusText() const;
  bool recording() const;
  bool playing() const;
  bool historyMode() const;
  int selectedSessionRow() const;
  bool canRecord() const;
  double playheadSeconds() const;
  double liveEdgeSeconds() const;
  double timelineDurationSeconds() const;
  bool followingLiveEdge() const;
  QString modeText() const;
  int visibleCameraCount() const;
  TopicListModel * topicModel();
  CameraGridModel * cameraGridModel();
  TagListModel * tagModel();
  EventMarkerModel * eventMarkerModel();
  RecordingSessionModel * recordingSessionModel();

  Q_INVOKABLE void toggleRecording();
  Q_INVOKABLE void togglePlayback();
  Q_INVOKABLE void selectOnlineData();
  Q_INVOKABLE void selectHistorySession(int row);
  Q_INVOKABLE void setPlayheadSeconds(double seconds);
  Q_INVOKABLE void advanceLiveEdge(double seconds);
  Q_INVOKABLE void returnToLiveEdge();
  Q_INVOKABLE void detachFromLiveEdge();
  Q_INVOKABLE bool triggerMarkerShortcut(const QString & shortcut);
  Q_INVOKABLE void toggleTopicVisible(int row);
  Q_INVOKABLE void setTopicExpanded(const QString & topic_key, bool expanded);
  Q_INVOKABLE void setSeriesVisible(
    const QString & topic_key, const QString & series_key, bool visible);

  // 底部「记录标签」面板点击入口：按当前状态分派——录制中切换内存勾选（停录时随会话写入）；
  // 选中历史会话时即时切换并同步写回该会话 session.yaml + 刷新行；未录制且非历史为 no-op。
  Q_INVOKABLE void toggleTag(int tag_row);

  // 历史数据面板:右键删除某历史会话的指定标签(按下标定位)。写回 session.yaml 并刷新行;
  // 若该行正是当前载入的历史会话,同步左侧「记录标签」面板勾选高亮。录制中为 no-op。
  Q_INVOKABLE void removeSessionTag(int session_row, int tag_index);

  bool eventFilter(QObject * watched, QEvent * event) override;

  void onCurvesUpdated(const QVariantList & topics);
  void onTopicTypesUpdated(const QVariantList & types);

signals:
  void statusTextChanged();
  void recordingChanged();
  void playingChanged();
  void dataSourceChanged();
  void canRecordChanged();
  void playheadSecondsChanged();
  void liveEdgeSecondsChanged();
  void timelineDurationSecondsChanged();
  void followingLiveEdgeChanged();
  void modeTextChanged();
  void visibleCameraCountChanged();

private:
  void refreshVisibleCameraCount();
  QString statusTextForCurrentState() const;
  void refreshStatusText();
  void onStatsUpdated(const QVariantList & stats);
  void onFrameReady(const QString & key, int seq);
  void onLiveEdge(double seconds);
  void refreshSessions();

  LiveBridge * bridge_{nullptr};
  RecorderEngine * engine_{nullptr};
  SessionManager * session_manager_{nullptr};
  SessionPlayer * player_{nullptr};
  QThread * player_thread_{nullptr};
  HistoryCurveLoader * curve_loader_{nullptr};
  QThread * curve_loader_thread_{nullptr};
  std::set<std::string> expanded_topics_;  // 当前展开的 topic（转发给引擎，仅这些发 series 曲线）
  std::vector<SessionRecord> scanned_sessions_;
  std::vector<TopicEntry> live_topics_;
  bool playing_{false};

  QString config_path_;
  QString output_directory_;
  QString status_text_{"实时查看"};
  bool recording_{false};
  bool history_mode_{false};
  int selected_session_row_{-1};
  double playhead_seconds_{0.0};
  double live_edge_seconds_{0.0};
  bool following_live_edge_{false};
  int visible_camera_count_{0};
  TopicListModel topic_model_;
  ValueExtractorRegistry extractor_registry_;
  CameraGridModel camera_grid_model_;
  TagListModel tag_model_;
  EventMarkerModel event_marker_model_;
  RecordingSessionModel recording_session_model_;
};

}  // namespace data_recorder
