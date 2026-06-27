#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class QEvent;

#include "data_recorder/camera_grid_model.hpp"
#include "data_recorder/config_model.hpp"
#include "data_recorder/ui_models.hpp"

namespace data_recorder
{

class LiveBridge;
class RecorderEngine;
class SessionManager;

class AppController : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString configPath READ configPath CONSTANT)
  Q_PROPERTY(QString outputDirectory READ outputDirectory CONSTANT)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
  Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
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

  QString configPath() const;
  QString outputDirectory() const;
  QString statusText() const;
  bool recording() const;
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
  Q_INVOKABLE void selectOnlineData();
  Q_INVOKABLE void selectHistorySession(int row);
  Q_INVOKABLE void setPlayheadSeconds(double seconds);
  Q_INVOKABLE void advanceLiveEdge(double seconds);
  Q_INVOKABLE void returnToLiveEdge();
  Q_INVOKABLE void detachFromLiveEdge();
  Q_INVOKABLE bool triggerMarkerShortcut(const QString & shortcut);
  Q_INVOKABLE void toggleTopicVisible(int row);

  bool eventFilter(QObject * watched, QEvent * event) override;

signals:
  void statusTextChanged();
  void recordingChanged();
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
  CameraGridModel camera_grid_model_;
  TagListModel tag_model_;
  EventMarkerModel event_marker_model_;
  RecordingSessionModel recording_session_model_;
};

}  // namespace data_recorder
