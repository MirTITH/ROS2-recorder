#include "data_recorder/ui_models.hpp"

#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace data_recorder
{

namespace
{

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

// ---- Placeholder/demo data generators (remove when backend provides real data) ----
// Everything in this block produces synthetic display values only. The real backend
// will supply frequency from actual topic Hz, resolution from actual image dimensions,
// and series from actual subscribed numeric data. kSeriesColors below is the categorical
// palette used as default styling.

constexpr std::array<const char *, 6> kSeriesColors{
  "#2563eb",
  "#16a34a",
  "#dc2626",
  "#9333ea",
  "#0891b2",
  "#ca8a04",
};

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

// ---- End placeholder/demo data generators ----

bool valid_row(int row, int size)
{
  return row >= 0 && row < size;
}

double non_negative_seconds(double seconds)
{
  return std::max(0.0, seconds);
}

void normalize_range(double & start_seconds, double & end_seconds)
{
  start_seconds = non_negative_seconds(start_seconds);
  end_seconds = non_negative_seconds(end_seconds);
  if (end_seconds < start_seconds) {
    std::swap(start_seconds, end_seconds);
  }
}

bool same_time(double lhs, double rhs)
{
  return lhs == rhs;
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
    case IsVisibleRole:
      return row.is_visible;
    case FrequencyTextRole:
      return row.frequency_text;
    case SeriesColorRole:
      return row.series_color;
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
    {IsVisibleRole, "isVisible"},
    {FrequencyTextRole, "frequencyText"},
    {SeriesColorRole, "seriesColor"},
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

// PLACEHOLDER DATA SEAM: synthetic per-topic display values (frequency, resolution, series).
// Replace with real values from ROS subscriptions when the backend lands. The make_*() helpers
// above generate demo data only. Real config-derived fields (topic, track_kind, is_camera,
// is_drawable) are set in set_topics, not here.
void TopicListModel::populate_placeholder_fields(TopicRow & row, int index)
{
  row.frequency_text = make_frequency_text(index, row.topic.ui_category);
  row.series_color = QString::fromLatin1(kSeriesColors[
    static_cast<std::size_t>(index) % kSeriesColors.size()]);
  row.resolution_text = row.is_camera ? make_resolution_text(index) : QString();
  row.series_list = make_series_list(index, row.track_kind);
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
    row.track_kind = track_kind_for_topic(row.topic);
    row.is_camera = row.track_kind == QStringLiteral("camera");
    row.is_drawable = row.track_kind == QStringLiteral("numeric");
    populate_placeholder_fields(row, static_cast<int>(i));
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

  const auto & row = markers_.at(static_cast<std::size_t>(index.row()));
  switch (role) {
    case ShortcutRole:
      return QString::fromStdString(row.marker.shortcut);
    case NameRole:
      return QString::fromStdString(row.marker.name);
    case KindRole:
      return QString::fromStdString(row.marker.kind);
    case ColorRole:
      return QString::fromStdString(row.marker.color);
    case CountRole:
      return row.instances.size();
    case ActionTextRole:
      if (row.marker.kind == "range") {
        return row.has_pending_range_start ?
          QStringLiteral("设置终点 (%1)").arg(QString::fromStdString(row.marker.shortcut)) :
          QStringLiteral("添加起点 (%1)").arg(QString::fromStdString(row.marker.shortcut));
      }
      return QStringLiteral("添加 (%1)").arg(QString::fromStdString(row.marker.shortcut));
    case HasPendingRangeStartRole:
      return row.has_pending_range_start;
    case PendingStartSecondsRole:
      return row.pending_start_seconds;
    case InstancesRole: {
      QVariantList instances;
      instances.reserve(row.instances.size());
      for (const auto & instance : row.instances) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), instance.id);
        map.insert(QStringLiteral("kind"), instance.kind);
        map.insert(QStringLiteral("startSeconds"), instance.start_seconds);
        map.insert(QStringLiteral("endSeconds"), instance.end_seconds);
        map.insert(QStringLiteral("color"), QString::fromStdString(row.marker.color));
        instances.push_back(map);
      }
      return instances;
    }
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
    {CountRole, "count"},
    {ActionTextRole, "actionText"},
    {HasPendingRangeStartRole, "hasPendingRangeStart"},
    {PendingStartSecondsRole, "pendingStartSeconds"},
    {InstancesRole, "instances"},
  };
}

bool EventMarkerModel::triggerRowAction(int row, double time_seconds)
{
  if (!valid_row(row, static_cast<int>(markers_.size()))) {
    return false;
  }

  const auto & marker = markers_.at(static_cast<std::size_t>(row)).marker;
  if (marker.kind == "range") {
    return toggleRange(row, time_seconds);
  }
  return addPoint(row, time_seconds);
}

bool EventMarkerModel::triggerShortcut(const QString & shortcut, double time_seconds)
{
  const auto normalized = shortcut.toLower();
  for (int row = 0; row < static_cast<int>(markers_.size()); ++row) {
    if (QString::fromStdString(markers_.at(static_cast<std::size_t>(row)).marker.shortcut)
        .toLower() == normalized)
    {
      return triggerRowAction(row, time_seconds);
    }
  }
  return false;
}

bool EventMarkerModel::addPoint(int row, double time_seconds)
{
  if (!valid_row(row, static_cast<int>(markers_.size()))) {
    return false;
  }

  auto & marker_row = markers_.at(static_cast<std::size_t>(row));
  if (marker_row.marker.kind != "point") {
    return false;
  }

  const double normalized_seconds = non_negative_seconds(time_seconds);
  const auto duplicate = std::find_if(
    marker_row.instances.cbegin(), marker_row.instances.cend(),
    [normalized_seconds](const EventInstance & instance) {
      return instance.kind == QStringLiteral("point") &&
             same_time(instance.start_seconds, normalized_seconds) &&
             same_time(instance.end_seconds, normalized_seconds);
    });
  if (duplicate != marker_row.instances.cend()) {
    return true;
  }

  EventInstance instance;
  instance.id = marker_row.next_instance_id++;
  instance.kind = QStringLiteral("point");
  instance.start_seconds = normalized_seconds;
  instance.end_seconds = normalized_seconds;
  marker_row.instances.push_back(instance);

  const auto model_index = index(row, 0);
  emit dataChanged(model_index, model_index, {CountRole, InstancesRole});
  return true;
}

bool EventMarkerModel::toggleRange(int row, double time_seconds)
{
  if (!valid_row(row, static_cast<int>(markers_.size()))) {
    return false;
  }

  auto & marker_row = markers_.at(static_cast<std::size_t>(row));
  if (marker_row.marker.kind != "range") {
    return false;
  }

  if (!marker_row.has_pending_range_start) {
    marker_row.has_pending_range_start = true;
    marker_row.pending_start_seconds = non_negative_seconds(time_seconds);

    const auto model_index = index(row, 0);
    emit dataChanged(
      model_index, model_index,
      {ActionTextRole, HasPendingRangeStartRole, PendingStartSecondsRole});
    return true;
  }

  double start_seconds = marker_row.pending_start_seconds;
  double end_seconds = time_seconds;
  normalize_range(start_seconds, end_seconds);

  const auto duplicate = std::find_if(
    marker_row.instances.cbegin(), marker_row.instances.cend(),
    [start_seconds, end_seconds](const EventInstance & instance) {
      return instance.kind == QStringLiteral("range") &&
             same_time(instance.start_seconds, start_seconds) &&
             same_time(instance.end_seconds, end_seconds);
    });
  if (duplicate != marker_row.instances.cend()) {
    marker_row.has_pending_range_start = false;
    marker_row.pending_start_seconds = 0.0;

    const auto model_index = index(row, 0);
    emit dataChanged(
      model_index, model_index,
      {ActionTextRole, HasPendingRangeStartRole, PendingStartSecondsRole});
    return true;
  }

  EventInstance instance;
  instance.id = marker_row.next_instance_id++;
  instance.kind = QStringLiteral("range");
  instance.start_seconds = start_seconds;
  instance.end_seconds = end_seconds;
  marker_row.instances.push_back(instance);
  marker_row.has_pending_range_start = false;
  marker_row.pending_start_seconds = 0.0;

  const auto model_index = index(row, 0);
  emit dataChanged(
    model_index, model_index,
    {CountRole, ActionTextRole, HasPendingRangeStartRole, PendingStartSecondsRole, InstancesRole});
  return true;
}

bool EventMarkerModel::movePoint(int row, int instance_id, double time_seconds)
{
  if (!valid_row(row, static_cast<int>(markers_.size()))) {
    return false;
  }

  auto & marker_row = markers_.at(static_cast<std::size_t>(row));
  if (marker_row.marker.kind != "point") {
    return false;
  }

  const double normalized_seconds = non_negative_seconds(time_seconds);
  auto instance = std::find_if(
    marker_row.instances.begin(), marker_row.instances.end(),
    [instance_id](const EventInstance & candidate) { return candidate.id == instance_id; });
  if (instance == marker_row.instances.end()) {
    return false;
  }

  instance->start_seconds = normalized_seconds;
  instance->end_seconds = normalized_seconds;

  const auto model_index = index(row, 0);
  emit dataChanged(model_index, model_index, {InstancesRole});
  return true;
}

bool EventMarkerModel::moveRange(
  int row, int instance_id, double start_seconds, double end_seconds)
{
  if (!valid_row(row, static_cast<int>(markers_.size()))) {
    return false;
  }

  auto & marker_row = markers_.at(static_cast<std::size_t>(row));
  if (marker_row.marker.kind != "range") {
    return false;
  }

  auto instance = std::find_if(
    marker_row.instances.begin(), marker_row.instances.end(),
    [instance_id](const EventInstance & candidate) { return candidate.id == instance_id; });
  if (instance == marker_row.instances.end()) {
    return false;
  }

  normalize_range(start_seconds, end_seconds);
  instance->start_seconds = start_seconds;
  instance->end_seconds = end_seconds;

  const auto model_index = index(row, 0);
  emit dataChanged(model_index, model_index, {InstancesRole});
  return true;
}

bool EventMarkerModel::deleteInstance(int row, int instance_id)
{
  if (!valid_row(row, static_cast<int>(markers_.size()))) {
    return false;
  }

  auto & marker_row = markers_.at(static_cast<std::size_t>(row));
  const auto previous_size = marker_row.instances.size();
  marker_row.instances.erase(
    std::remove_if(
      marker_row.instances.begin(), marker_row.instances.end(),
      [instance_id](const EventInstance & instance) { return instance.id == instance_id; }),
    marker_row.instances.end());
  if (marker_row.instances.size() == previous_size) {
    return false;
  }

  const auto model_index = index(row, 0);
  emit dataChanged(model_index, model_index, {CountRole, InstancesRole});
  return true;
}

bool EventMarkerModel::deleteAllInstances(int row)
{
  if (!valid_row(row, static_cast<int>(markers_.size()))) {
    return false;
  }

  auto & marker_row = markers_.at(static_cast<std::size_t>(row));
  const bool had_instances = !marker_row.instances.isEmpty();
  const bool had_pending_range_start = marker_row.has_pending_range_start;
  if (!had_instances && !had_pending_range_start) {
    return false;
  }

  marker_row.instances.clear();
  marker_row.has_pending_range_start = false;
  marker_row.pending_start_seconds = 0.0;

  const auto model_index = index(row, 0);
  emit dataChanged(
    model_index, model_index,
    {CountRole, ActionTextRole, HasPendingRangeStartRole, PendingStartSecondsRole, InstancesRole});
  return true;
}

void EventMarkerModel::set_markers(std::vector<EventMarkerEntry> markers)
{
  beginResetModel();
  markers_.clear();
  markers_.reserve(markers.size());
  for (auto & marker : markers) {
    EventMarkerRow row;
    row.marker = std::move(marker);
    markers_.push_back(std::move(row));
  }
  endResetModel();
}

RecordingSessionModel::RecordingSessionModel(QObject * parent)
: QAbstractListModel(parent)
{
  populate_placeholder_sessions();
}

// PLACEHOLDER DATA SEAM: hardcoded demo sessions. Replace with a scan of the output
// directory when the backend lands.
void RecordingSessionModel::populate_placeholder_sessions()
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
