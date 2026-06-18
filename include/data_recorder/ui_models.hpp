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

#include <vector>

#include "data_recorder/config_model.hpp"

namespace data_recorder
{

class TopicListModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Roles
  {
    TopicNameRole = Qt::UserRole + 1,
    BackendNameRole,
    CategoryRole,
    IsVisibleRole,
    FrequencyTextRole,
    SeriesColorRole,
    SeriesRole,
    TrackKindRole,
    IsCameraRole,
    IsDrawableRole,
    SeriesListRole,
    ResolutionTextRole,
  };

  explicit TopicListModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex & index) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void toggleVisible(int row);
  Q_INVOKABLE int visibleCameraCount() const;

  void set_topics(std::vector<TopicEntry> topics);

private:
  struct TopicRow
  {
    TopicEntry topic;
    bool is_visible{true};
    QString frequency_text;
    QString series_color;
    QVariantList series;
    QString track_kind;
    bool is_camera{false};
    bool is_drawable{false};
    QString resolution_text;
    QVariantList series_list;
  };

  std::vector<TopicRow> topics_;
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

  void set_tags(std::vector<TagEntry> tags);

private:
  std::vector<TagEntry> tags_;
  int selected_row_{-1};
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
