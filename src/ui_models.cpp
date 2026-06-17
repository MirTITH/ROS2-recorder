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

QString track_kind_for_topic(const TopicEntry & topic)
{
  if (topic.ui_category == TopicUiCategory::CameraPreview) {
    return QStringLiteral("camera");
  }
  if (topic.topic_name == "/tf" || topic.topic_name == "/tf_static") {
    return QStringLiteral("empty");
  }
  if (topic.topic_name == "/joint_states") {
    return QStringLiteral("numeric");
  }
  return QStringLiteral("numeric");
}

QVariantList make_series_list(int row, const QString & track_kind)
{
  QVariantList series_list;
  if (track_kind != QStringLiteral("numeric")) {
    return series_list;
  }

  const int series_count = row == 0 ? 2 : 3;
  for (int series_index = 0; series_index < series_count; ++series_index) {
    QVariantMap series;
    series.insert(QStringLiteral("name"), QStringLiteral("series_%1").arg(series_index + 1));
    series.insert(QStringLiteral("color"), QString::fromLatin1(kSeriesColors[
      static_cast<std::size_t>((row + series_index) % kSeriesColors.size())]));

    QVariantList points;
    points.reserve(80);
    for (int i = 0; i < 80; ++i) {
      QVariantMap point;
      point.insert(QStringLiteral("x"), i);
      point.insert(
        QStringLiteral("y"),
        std::sin((static_cast<double>(i) / 8.0) + series_index) +
          static_cast<double>(series_index) * 0.4);
      points.push_back(point);
    }
    series.insert(QStringLiteral("points"), points);
    series_list.push_back(series);
  }
  return series_list;
}

QString make_resolution_text(int row)
{
  return row % 2 == 0 ? QStringLiteral("1280x720") : QStringLiteral("1920x1080");
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
    case TrackKindRole:
      return row.track_kind;
    case IsCameraRole:
      return row.is_camera;
    case IsDrawableRole:
      return row.is_drawable;
    case SeriesListRole:
      return row.series_list;
    case ResolutionTextRole:
      return row.resolution_text;
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
    {TrackKindRole, "trackKind"},
    {IsCameraRole, "isCamera"},
    {IsDrawableRole, "isDrawable"},
    {SeriesListRole, "seriesList"},
    {ResolutionTextRole, "resolutionText"},
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

int TopicListModel::visibleCameraCount() const
{
  int count = 0;
  for (const auto & row : topics_) {
    if (row.is_camera && row.is_visible) {
      ++count;
    }
  }
  return count;
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
    row.track_kind = track_kind_for_topic(row.topic);
    row.is_camera = row.track_kind == QStringLiteral("camera");
    row.is_drawable = row.track_kind == QStringLiteral("numeric");
    row.resolution_text = row.is_camera ? make_resolution_text(static_cast<int>(i)) : QString();
    row.series_list = make_series_list(static_cast<int>(i), row.track_kind);
    row.series = row.series_list.isEmpty() ?
      QVariantList{} :
      row.series_list.first().toMap().value(QStringLiteral("points")).toList();
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
  emit selectedShortcutChanged(selectedShortcut());
}

bool EventMarkerModel::selectByShortcut(const QString & shortcut)
{
  const auto normalized = shortcut.toLower();
  for (int row = 0; row < static_cast<int>(markers_.size()); ++row) {
    if (QString::fromStdString(markers_.at(static_cast<std::size_t>(row)).shortcut).toLower() ==
      normalized)
    {
      select(row);
      return true;
    }
  }
  return false;
}

QString EventMarkerModel::selectedShortcut() const
{
  if (!valid_row(selected_row_, static_cast<int>(markers_.size()))) {
    return {};
  }
  return QString::fromStdString(markers_.at(static_cast<std::size_t>(selected_row_)).shortcut)
    .toLower();
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
    {QStringLiteral("2026-05-31_07-46-20"), QStringLiteral("1.2 GB"), QStringLiteral("00:00:24.123"),
      QStringLiteral("2026-05-31_07-46-20"), QStringLiteral("24s"), QStringLiteral("00:00:24.123"),
      QStringLiteral("1.2 GB"), QStringLiteral("成功"), QStringLiteral("#2f9e44")},
    {QStringLiteral("2026-05-31_07-47-06"), QStringLiteral("860 MB"), QStringLiteral("00:12:35.000"),
      QStringLiteral("2026-05-31_07-47-06"), QStringLiteral("12m35s"), QStringLiteral("00:12:35.000"),
      QStringLiteral("860 MB"), QStringLiteral("力控"), QStringLiteral("#7c4dff")},
    {QStringLiteral("用户自己改的名称"), QStringLiteral("2.4 GB"), QStringLiteral("02:34:35.500"),
      QStringLiteral("用户自己改的名称"), QStringLiteral("154m35s"), QStringLiteral("02:34:35.500"),
      QStringLiteral("2.4 GB"), QStringLiteral("失败"), QStringLiteral("#e03131")},
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
    case FolderNameRole:
      return session.folder_name;
    case ShortDurationRole:
      return session.short_duration;
    case FullDurationRole:
      return session.full_duration;
    case SizeTextRole:
      return session.size_text;
    case TagNameRole:
      return session.tag_name;
    case TagColorRole:
      return session.tag_color;
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
    {FolderNameRole, "folderName"},
    {ShortDurationRole, "shortDuration"},
    {FullDurationRole, "fullDuration"},
    {SizeTextRole, "sizeText"},
    {TagNameRole, "tagName"},
    {TagColorRole, "tagColor"},
  };
}

}  // namespace data_recorder
