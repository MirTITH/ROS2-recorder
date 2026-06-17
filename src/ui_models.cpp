#include "data_recorder/ui_models.hpp"

#include <QVariantMap>

#include <array>
#include <cmath>
#include <utility>

namespace data_recorder
{

namespace
{

constexpr std::array<const char *, 6> kSeriesColors{
  "#2563eb",
  "#16a34a",
  "#dc2626",
  "#9333ea",
  "#0891b2",
  "#ca8a04",
};

QString topic_category_name(TopicUiCategory category)
{
  return category == TopicUiCategory::CameraPreview ? QStringLiteral("camera") : QStringLiteral("numeric");
}

QVariantList make_series(int row, TopicUiCategory category)
{
  QVariantList points;
  points.reserve(80);
  const double row_offset = static_cast<double>(row) * 0.35;
  const double amplitude = category == TopicUiCategory::CameraPreview ? 0.35 : 1.0;
  for (int i = 0; i < 80; ++i) {
    QVariantMap point;
    point.insert(QStringLiteral("x"), i);
    point.insert(
      QStringLiteral("y"),
      amplitude * std::sin((static_cast<double>(i) / 8.0) + row_offset) + row_offset);
    points.push_back(point);
  }
  return points;
}

QString make_frequency_text(int row, TopicUiCategory category)
{
  if (category == TopicUiCategory::CameraPreview) {
    return QStringLiteral("%1 fps").arg(18 + row);
  }
  return QStringLiteral("%1 Hz").arg(20 + row);
}

bool valid_row(int row, int size)
{
  return row >= 0 && row < size;
}

}  // namespace

TopicListModel::TopicListModel(QObject * parent)
: QAbstractListModel(parent)
{
}

int TopicListModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(topics_.size());
}

QVariant TopicListModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || !valid_row(index.row(), static_cast<int>(topics_.size()))) {
    return {};
  }

  const auto & row = topics_.at(static_cast<std::size_t>(index.row()));
  switch (role) {
    case TopicNameRole:
      return QString::fromStdString(row.topic.topic_name);
    case BackendNameRole:
      return QString::fromStdString(row.topic.backend_name);
    case CategoryRole:
      return topic_category_name(row.topic.ui_category);
    case IsVisibleRole:
      return row.is_visible;
    case FrequencyTextRole:
      return row.frequency_text;
    case SeriesColorRole:
      return row.series_color;
    case SeriesRole:
      return row.series;
    default:
      return {};
  }
}

bool TopicListModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
  if (!index.isValid() || !valid_row(index.row(), static_cast<int>(topics_.size())) ||
    role != IsVisibleRole)
  {
    return false;
  }

  auto & row = topics_.at(static_cast<std::size_t>(index.row()));
  const bool next_visible = value.toBool();
  if (row.is_visible == next_visible) {
    return true;
  }

  row.is_visible = next_visible;
  emit dataChanged(index, index, {IsVisibleRole});
  return true;
}

Qt::ItemFlags TopicListModel::flags(const QModelIndex & index) const
{
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QHash<int, QByteArray> TopicListModel::roleNames() const
{
  return {
    {TopicNameRole, "topicName"},
    {BackendNameRole, "backendName"},
    {CategoryRole, "category"},
    {IsVisibleRole, "isVisible"},
    {FrequencyTextRole, "frequencyText"},
    {SeriesColorRole, "seriesColor"},
    {SeriesRole, "series"},
  };
}

void TopicListModel::toggleVisible(int row)
{
  if (!valid_row(row, static_cast<int>(topics_.size()))) {
    return;
  }

  const auto model_index = index(row, 0);
  setData(model_index, !topics_.at(static_cast<std::size_t>(row)).is_visible, IsVisibleRole);
}

void TopicListModel::set_topics(std::vector<TopicEntry> topics)
{
  beginResetModel();
  topics_.clear();
  topics_.reserve(topics.size());
  for (std::size_t i = 0; i < topics.size(); ++i) {
    TopicRow row;
    row.topic = std::move(topics[i]);
    row.is_visible = true;
    row.frequency_text = make_frequency_text(static_cast<int>(i), row.topic.ui_category);
    row.series_color = QString::fromLatin1(kSeriesColors[i % kSeriesColors.size()]);
    row.series = make_series(static_cast<int>(i), row.topic.ui_category);
    topics_.push_back(std::move(row));
  }
  endResetModel();
}

TagListModel::TagListModel(QObject * parent)
: QAbstractListModel(parent)
{
}

int TagListModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(tags_.size());
}

QVariant TagListModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || !valid_row(index.row(), static_cast<int>(tags_.size()))) {
    return {};
  }

  const auto & tag = tags_.at(static_cast<std::size_t>(index.row()));
  switch (role) {
    case NameRole:
      return QString::fromStdString(tag.name);
    case ColorRole:
      return QString::fromStdString(tag.color);
    case IsSelectedRole:
      return index.row() == selected_row_;
    default:
      return {};
  }
}

QHash<int, QByteArray> TagListModel::roleNames() const
{
  return {
    {NameRole, "name"},
    {ColorRole, "color"},
    {IsSelectedRole, "isSelected"},
  };
}

void TagListModel::select(int row)
{
  if (!valid_row(row, static_cast<int>(tags_.size())) || row == selected_row_) {
    return;
  }

  const int previous = selected_row_;
  selected_row_ = row;
  if (valid_row(previous, static_cast<int>(tags_.size()))) {
    const auto previous_index = index(previous, 0);
    emit dataChanged(previous_index, previous_index, {IsSelectedRole});
  }
  const auto next_index = index(selected_row_, 0);
  emit dataChanged(next_index, next_index, {IsSelectedRole});
}

void TagListModel::set_tags(std::vector<TagEntry> tags)
{
  beginResetModel();
  tags_ = std::move(tags);
  selected_row_ = -1;
  endResetModel();
}

EventMarkerModel::EventMarkerModel(QObject * parent)
: QAbstractListModel(parent)
{
}

int EventMarkerModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(markers_.size());
}

QVariant EventMarkerModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || !valid_row(index.row(), static_cast<int>(markers_.size()))) {
    return {};
  }

  const auto & marker = markers_.at(static_cast<std::size_t>(index.row()));
  switch (role) {
    case ShortcutRole:
      return QString::fromStdString(marker.shortcut);
    case NameRole:
      return QString::fromStdString(marker.name);
    case KindRole:
      return QString::fromStdString(marker.kind);
    case ColorRole:
      return QString::fromStdString(marker.color);
    case IsSelectedRole:
      return index.row() == selected_row_;
    default:
      return {};
  }
}

QHash<int, QByteArray> EventMarkerModel::roleNames() const
{
  return {
    {ShortcutRole, "shortcut"},
    {NameRole, "name"},
    {KindRole, "kind"},
    {ColorRole, "color"},
    {IsSelectedRole, "isSelected"},
  };
}

void EventMarkerModel::select(int row)
{
  if (!valid_row(row, static_cast<int>(markers_.size())) || row == selected_row_) {
    return;
  }

  const int previous = selected_row_;
  selected_row_ = row;
  if (valid_row(previous, static_cast<int>(markers_.size()))) {
    const auto previous_index = index(previous, 0);
    emit dataChanged(previous_index, previous_index, {IsSelectedRole});
  }
  const auto next_index = index(selected_row_, 0);
  emit dataChanged(next_index, next_index, {IsSelectedRole});
}

void EventMarkerModel::set_markers(std::vector<EventMarkerEntry> markers)
{
  beginResetModel();
  markers_ = std::move(markers);
  selected_row_ = -1;
  endResetModel();
}

RecordingSessionModel::RecordingSessionModel(QObject * parent)
: QAbstractListModel(parent)
{
  sessions_ = {
    {QStringLiteral("今日采集 01"), QStringLiteral("1.2 GB"), QStringLiteral("00:18:42")},
    {QStringLiteral("夹爪标定"), QStringLiteral("860 MB"), QStringLiteral("00:12:09")},
    {QStringLiteral("倒水演示"), QStringLiteral("2.4 GB"), QStringLiteral("00:31:15")},
  };
}

int RecordingSessionModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(sessions_.size());
}

QVariant RecordingSessionModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || !valid_row(index.row(), static_cast<int>(sessions_.size()))) {
    return {};
  }

  const auto & session = sessions_.at(static_cast<std::size_t>(index.row()));
  switch (role) {
    case NameRole:
      return session.name;
    case SizeRole:
      return session.size;
    case DurationRole:
      return session.duration;
    default:
      return {};
  }
}

QHash<int, QByteArray> RecordingSessionModel::roleNames() const
{
  return {
    {NameRole, "name"},
    {SizeRole, "size"},
    {DurationRole, "duration"},
  };
}

}  // namespace data_recorder
