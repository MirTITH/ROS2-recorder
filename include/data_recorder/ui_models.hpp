#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

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
  };

  explicit TopicListModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex & index) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void toggleVisible(int row);

  void set_topics(std::vector<TopicEntry> topics);

private:
  struct TopicRow
  {
    TopicEntry topic;
    bool is_visible{true};
    QString frequency_text;
    QString series_color;
    QVariantList series;
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
    IsSelectedRole,
  };

  explicit EventMarkerModel(QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void select(int row);

  void set_markers(std::vector<EventMarkerEntry> markers);

private:
  std::vector<EventMarkerEntry> markers_;
  int selected_row_{-1};
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
  };

  std::vector<RecordingSessionRow> sessions_;
};

}  // namespace data_recorder
