#pragma once

#include <QObject>
#include <QString>

#include "data_recorder/config_model.hpp"
#include "data_recorder/ui_models.hpp"

namespace data_recorder
{

class AppController : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString configPath READ configPath CONSTANT)
  Q_PROPERTY(QString outputDirectory READ outputDirectory CONSTANT)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
  Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
  Q_PROPERTY(double playheadSeconds READ playheadSeconds NOTIFY playheadSecondsChanged)
  Q_PROPERTY(double liveEdgeSeconds READ liveEdgeSeconds NOTIFY liveEdgeSecondsChanged)
  Q_PROPERTY(bool followingLiveEdge READ followingLiveEdge NOTIFY followingLiveEdgeChanged)
  Q_PROPERTY(QString modeText READ modeText NOTIFY modeTextChanged)
  Q_PROPERTY(QString selectedMarkerShortcut READ selectedMarkerShortcut NOTIFY selectedMarkerShortcutChanged)
  Q_PROPERTY(int visibleCameraCount READ visibleCameraCount NOTIFY visibleCameraCountChanged)
  Q_PROPERTY(TopicListModel * topicModel READ topicModel CONSTANT)
  Q_PROPERTY(TopicListModel * cameraModel READ cameraModel CONSTANT)
  Q_PROPERTY(TopicListModel * trackModel READ trackModel CONSTANT)
  Q_PROPERTY(TagListModel * tagModel READ tagModel CONSTANT)
  Q_PROPERTY(EventMarkerModel * eventMarkerModel READ eventMarkerModel CONSTANT)
  Q_PROPERTY(RecordingSessionModel * recordingSessionModel READ recordingSessionModel CONSTANT)

public:
  explicit AppController(const ConfigData & config, QObject * parent = nullptr);

  QString configPath() const;
  QString outputDirectory() const;
  QString statusText() const;
  bool recording() const;
  double playheadSeconds() const;
  double liveEdgeSeconds() const;
  bool followingLiveEdge() const;
  QString modeText() const;
  QString selectedMarkerShortcut() const;
  int visibleCameraCount() const;
  TopicListModel * topicModel();
  TopicListModel * cameraModel();
  TopicListModel * trackModel();
  TagListModel * tagModel();
  EventMarkerModel * eventMarkerModel();
  RecordingSessionModel * recordingSessionModel();

  Q_INVOKABLE void toggleRecording();
  Q_INVOKABLE void setPlayheadSeconds(double seconds);
  Q_INVOKABLE void advanceLiveEdge(double seconds);
  Q_INVOKABLE void returnToLiveEdge();
  Q_INVOKABLE bool triggerMarkerShortcut(const QString & shortcut);
  Q_INVOKABLE void toggleTopicVisible(int row);

signals:
  void statusTextChanged();
  void recordingChanged();
  void playheadSecondsChanged();
  void liveEdgeSecondsChanged();
  void followingLiveEdgeChanged();
  void modeTextChanged();
  void selectedMarkerShortcutChanged();
  void visibleCameraCountChanged();

private:
  QString config_path_;
  QString output_directory_;
  QString status_text_{"就绪"};
  bool recording_{false};
  double playhead_seconds_{0.0};
  double live_edge_seconds_{0.0};
  bool following_live_edge_{false};
  QString selected_marker_shortcut_;
  TopicListModel topic_model_;
  TopicListModel camera_model_;
  TopicListModel track_model_;
  TagListModel tag_model_;
  EventMarkerModel event_marker_model_;
  RecordingSessionModel recording_session_model_;
};

}  // namespace data_recorder
