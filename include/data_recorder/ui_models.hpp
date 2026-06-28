#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVector>

#include <map>
#include <string>
#include <vector>

#include "data_recorder/config_model.hpp"
#include "data_recorder/recorder_types.hpp"
#include "data_recorder/topic_series.hpp"

namespace data_recorder
{

class ValueExtractorRegistry;

class TopicListModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Roles
  {
    TopicNameRole = Qt::UserRole + 1,
    BackendNameRole,
    IsVisibleRole,
    FrequencyTextRole,
    SeriesColorRole,
    TrackKindRole,
    IsCameraRole,
    IsDrawableRole,
    SeriesListRole,
    ResolutionTextRole,
    FrameSeqRole,
    IsExpandedRole,
    IsPlottableRole,
    MessageDotsRole,
  };

  explicit TopicListModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex & index) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void toggleVisible(int row);
  Q_INVOKABLE int visibleCameraCount() const;

  // 由引擎经 LiveBridge 实时回填（GUI 线程调）。
  void updateStats(const QString & topic_key, double hz, int width, int height);
  void updateFrameSeq(const QString & topic_key, int seq);

  void set_topics(std::vector<TopicEntry> topics);

  void setExpanded(const QString & topic_key, bool expanded);
  void set_extractor_registry(const ValueExtractorRegistry * registry);
  void updateTopicType(const QString & topic_key, const QString & ros_type);
  void updateMessageDots(const QString & topic_key, const std::vector<double> & seconds);
  void updateSeries(
    const QString & topic_key,
    const std::vector<TopicSeries::SeriesSnapshot> & series);
  void setSeriesVisible(const QString & topic_key, const QString & series_key, bool visible);
  // 清空所有行的折叠点/曲线与每行可见性/配色覆盖（开始新录制时调，避免上次会话残留叠加）。
  void clearCurves();

private:
  struct TopicRow
  {
    TopicEntry topic;
    bool is_visible{true};
    QString frequency_text;
    QString series_color;
    QString track_kind;
    bool is_camera{false};
    bool is_drawable{false};
    QString resolution_text;
    QVariantList series_list;
    int frame_seq{0};
    bool is_expanded{false};
    bool is_plottable{false};
    QVariantList message_dots;
    // 按 series_key 稳定保留的 UI 状态，跨 updateSeries 刷新不丢。
    std::map<std::string, bool> series_visible_override;
    std::map<std::string, int> series_color_index;
    int next_color_index{0};
  };

  std::vector<TopicRow> topics_;
  const ValueExtractorRegistry * registry_{nullptr};
};

class TagListModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Roles
  {
    NameRole = Qt::UserRole + 1,
    ColorRole,
    IsSelectedRole,
  };

  explicit TagListModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void select(int row);

  std::vector<TagRecord> exportSelectedTags() const;
  void setSelectedTags(const std::vector<TagRecord> & tags);
  void clearSelection();  // startSession 时清空（select(-1) 当前是 no-op，故需独立方法）

  void set_tags(std::vector<TagEntry> tags);

private:
  std::vector<TagEntry> tags_;
  int selected_row_{-1};
  std::vector<std::string> session_selected_names_;
};

class EventMarkerModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Roles
  {
    ShortcutRole = Qt::UserRole + 1,
    NameRole,
    KindRole,
    ColorRole,
    CountRole,
    ActionTextRole,
    HasPendingRangeStartRole,
    PendingStartSecondsRole,
    InstancesRole,
  };

  explicit EventMarkerModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE bool triggerRowAction(int row, double time_seconds);
  Q_INVOKABLE bool triggerShortcut(const QString & shortcut, double time_seconds);
  Q_INVOKABLE bool addPoint(int row, double time_seconds);
  Q_INVOKABLE bool toggleRange(int row, double time_seconds);
  Q_INVOKABLE bool movePoint(int row, int instance_id, double time_seconds);
  Q_INVOKABLE bool moveRange(
    int row, int instance_id, double start_seconds, double end_seconds);
  Q_INVOKABLE bool deleteInstance(int row, int instance_id);
  Q_INVOKABLE bool deleteAllInstances(int row);

  std::vector<AnnotationRecord> exportAnnotations() const;
  void clearInstances();  // startSession 时清空
  void setInstances(const std::vector<AnnotationRecord> & annotations);

  void set_markers(std::vector<EventMarkerEntry> markers);

private:
  struct EventInstance
  {
    int id{0};
    QString kind;
    double start_seconds{0.0};
    double end_seconds{0.0};
  };

  struct EventMarkerRow
  {
    EventMarkerEntry marker;
    QVector<EventInstance> instances;
    bool has_pending_range_start{false};
    double pending_start_seconds{0.0};
    int next_instance_id{1};
  };

  void resetAllRows();

  std::vector<EventMarkerRow> markers_;
};

class RecordingSessionModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Roles
  {
    NameRole = Qt::UserRole + 1,
    SizeRole,
    DurationRole,
    FolderNameRole,
    ShortDurationRole,
    FullDurationRole,
    SizeTextRole,
    TagNameRole,
    TagColorRole,
  };

  explicit RecordingSessionModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setSessions(const std::vector<SessionRecord> & sessions);

private:
  struct RecordingSessionRow
  {
    QString name;
    QString size;
    QString duration;
    QString folder_name;
    QString short_duration;
    QString full_duration;
    QString size_text;
    QString tag_name;
    QString tag_color;
  };

  std::vector<RecordingSessionRow> sessions_;
};

}  // namespace data_recorder
