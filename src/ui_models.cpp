#include "data_recorder/ui_models.hpp"

#include <QVariantMap>

#include <algorithm>
#include <array>
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
    case FrameSeqRole:
      return row.frame_seq;
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
    {FrameSeqRole, "frameSeq"},
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
    row.track_kind = track_kind_for_topic(row.topic);
    row.is_camera = row.track_kind == QStringLiteral("camera");
    row.is_drawable = row.track_kind == QStringLiteral("numeric");
    row.series_color = QString::fromLatin1(kSeriesColors[
      static_cast<std::size_t>(i) % kSeriesColors.size()]);
    row.frequency_text = QString();      // 等引擎回填
    row.resolution_text = QString();
    row.series_list = QVariantList();    // v1 数值留空
    topics_.push_back(std::move(row));
  }
  endResetModel();
}

void TopicListModel::updateStats(const QString & topic_key, double hz, int width, int height)
{
  for (std::size_t i = 0; i < topics_.size(); ++i) {
    auto & row = topics_[i];
    if (QString::fromStdString(row.topic.topic_name) != topic_key) { continue; }
    const QString unit = row.is_camera ? QStringLiteral("fps") : QStringLiteral("Hz");
    row.frequency_text = QStringLiteral("%1 %2").arg(qRound(hz)).arg(unit);
    if (row.is_camera && width > 0 && height > 0) {
      row.resolution_text = QStringLiteral("%1x%2").arg(width).arg(height);
    }
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {FrequencyTextRole, ResolutionTextRole});
    return;
  }
}

void TopicListModel::updateFrameSeq(const QString & topic_key, int seq)
{
  for (std::size_t i = 0; i < topics_.size(); ++i) {
    if (QString::fromStdString(topics_[i].topic.topic_name) != topic_key) { continue; }
    topics_[i].frame_seq = seq;
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {FrameSeqRole});
    return;
  }
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

std::vector<TagRecord> TagListModel::exportSelectedTags() const
{
  std::vector<TagRecord> out;
  if (selected_row_ >= 0 && selected_row_ < static_cast<int>(tags_.size())) {
    out.push_back({tags_[static_cast<std::size_t>(selected_row_)].name,
      tags_[static_cast<std::size_t>(selected_row_)].color});
  }
  return out;
}

void TagListModel::clearSelection()
{
  if (selected_row_ < 0) { return; }
  const int previous = selected_row_;
  selected_row_ = -1;
  if (valid_row(previous, static_cast<int>(tags_.size()))) {
    const auto previous_index = index(previous, 0);
    emit dataChanged(previous_index, previous_index, {IsSelectedRole});
  }
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

std::vector<AnnotationRecord> EventMarkerModel::exportAnnotations() const
{
  std::vector<AnnotationRecord> out;
  for (const auto & row : markers_) {
    for (const auto & inst : row.instances) {
      AnnotationRecord rec;
      rec.name = row.marker.name;
      rec.shortcut = row.marker.shortcut;
      rec.kind = inst.kind.toStdString();
      rec.color = row.marker.color;
      if (inst.kind == QStringLiteral("range")) {
        rec.t = inst.start_seconds;
        rec.end = inst.end_seconds;
      } else {
        rec.t = inst.start_seconds;
      }
      out.push_back(rec);
    }
  }
  std::sort(out.begin(), out.end(),
    [](const AnnotationRecord & a, const AnnotationRecord & b) { return a.t < b.t; });
  return out;
}

void EventMarkerModel::resetAllRows()
{
  for (auto & row : markers_) {
    row.instances.clear();
    row.has_pending_range_start = false;
    row.next_instance_id = 1;
  }
}

void EventMarkerModel::clearInstances()
{
  beginResetModel();
  resetAllRows();
  endResetModel();
}

void EventMarkerModel::setInstances(const std::vector<AnnotationRecord> & annotations)
{
  beginResetModel();
  resetAllRows();
  for (const auto & ann : annotations) {
    auto it = std::find_if(markers_.begin(), markers_.end(),
      [&ann](const EventMarkerRow & r) {
        return r.marker.shortcut == ann.shortcut;
      });
    if (it == markers_.end()) { continue; }
    EventInstance instance;
    instance.id = it->next_instance_id++;
    if (ann.kind == "range") {
      double start_seconds = ann.t;
      double end_seconds = ann.end;
      normalize_range(start_seconds, end_seconds);
      instance.kind = QStringLiteral("range");
      instance.start_seconds = start_seconds;
      instance.end_seconds = end_seconds;
    } else {
      instance.kind = QStringLiteral("point");
      instance.start_seconds = ann.t;
      instance.end_seconds = ann.t;
    }
    it->instances.push_back(instance);
  }
  endResetModel();
}

RecordingSessionModel::RecordingSessionModel(QObject * parent)
: QAbstractListModel(parent)
{
}

namespace
{
QString format_short_duration(double seconds)
{
  const int total = static_cast<int>(seconds);
  const int m = total / 60;
  const int s = total % 60;
  return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString format_size(uint64_t bytes)
{
  const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
  if (mb >= 1024.0) { return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1); }
  return QStringLiteral("%1 MB").arg(mb, 0, 'f', 0);
}
}  // namespace

void RecordingSessionModel::setSessions(const std::vector<SessionRecord> & sessions)
{
  beginResetModel();
  sessions_.clear();
  for (const auto & s : sessions) {
    RecordingSessionRow row;
    row.name = QString::fromStdString(s.session_id);
    row.folder_name = QString::fromStdString(s.session_id);
    row.short_duration = format_short_duration(s.duration_seconds);
    row.full_duration = row.short_duration;
    row.duration = row.short_duration;
    row.size_text = format_size(s.size_bytes);
    row.size = row.size_text;
    if (!s.tags.empty()) {
      row.tag_name = QString::fromStdString(s.tags.front().name);
      row.tag_color = QString::fromStdString(s.tags.front().color);
    }
    sessions_.push_back(std::move(row));
  }
  endResetModel();
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
