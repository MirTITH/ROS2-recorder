#include "data_recorder/camera_grid_model.hpp"

#include <algorithm>

namespace data_recorder
{

CameraGridModel::CameraGridModel(TopicListModel * source, QObject * parent)
: QAbstractListModel(parent), source_(source)
{
  if (source_ != nullptr) {
    connect(source_, &QAbstractItemModel::dataChanged, this, [this]() { rebuild(); });
    connect(source_, &QAbstractItemModel::rowsInserted, this, [this]() { rebuild(); });
    connect(source_, &QAbstractItemModel::rowsRemoved, this, [this]() { rebuild(); });
    connect(source_, &QAbstractItemModel::modelReset, this, [this]() { rebuild(); });
  }
  rebuild();
}

QString CameraGridModel::key_of(const QString & topic, const QString & backend) const
{
  return topic + QStringLiteral("|") + backend;
}

void CameraGridModel::rebuild()
{
  beginResetModel();

  struct SourceCamera { QString key, topic, backend, resolution, color; bool visible; };
  std::vector<SourceCamera> cameras;
  const int rows = source_ ? source_->rowCount() : 0;
  for (int row = 0; row < rows; ++row) {
    const auto idx = source_->index(row, 0);
    if (!source_->data(idx, TopicListModel::IsCameraRole).toBool()) {
      continue;
    }
    SourceCamera c;
    c.topic = source_->data(idx, TopicListModel::TopicNameRole).toString();
    c.backend = source_->data(idx, TopicListModel::BackendNameRole).toString();
    c.resolution = source_->data(idx, TopicListModel::ResolutionTextRole).toString();
    c.color = source_->data(idx, TopicListModel::SeriesColorRole).toString();
    c.visible = source_->data(idx, TopicListModel::IsVisibleRole).toBool();
    c.key = key_of(c.topic, c.backend);
    cameras.push_back(std::move(c));
  }

  // Append newly-seen cameras to order_, preserving existing remembered order.
  for (const auto & c : cameras) {
    if (std::find(order_.begin(), order_.end(), c.key) == order_.end()) {
      order_.push_back(c.key);
    }
  }
  // Drop keys whose camera no longer exists.
  order_.erase(
    std::remove_if(order_.begin(), order_.end(), [&](const QString & key) {
      return std::none_of(cameras.begin(), cameras.end(),
        [&](const SourceCamera & c) { return c.key == key; });
    }),
    order_.end());

  // Filter to visible rows, in order_ sequence.
  visible_.clear();
  for (const auto & key : order_) {
    const auto it = std::find_if(cameras.begin(), cameras.end(),
      [&](const SourceCamera & c) { return c.key == key; });
    if (it != cameras.end() && it->visible) {
      visible_.push_back(Camera{it->topic, it->backend, it->resolution, it->color});
    }
  }

  endResetModel();
  emit countChanged();
}

int CameraGridModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(visible_.size());
}

QVariant CameraGridModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() < 0 ||
    index.row() >= static_cast<int>(visible_.size()))
  {
    return {};
  }
  const auto & c = visible_.at(static_cast<std::size_t>(index.row()));
  switch (role) {
    case TopicNameRole: return c.topic_name;
    case BackendNameRole: return c.backend_name;
    case ResolutionTextRole: return c.resolution_text;
    case SeriesColorRole: return c.series_color;
    case TopicKeyRole: return c.topic_name;
    case FrameSeqRole: return c.frame_seq;
    default: return {};
  }
}

QHash<int, QByteArray> CameraGridModel::roleNames() const
{
  return {
    {TopicNameRole, "topicName"},
    {BackendNameRole, "backendName"},
    {ResolutionTextRole, "resolutionText"},
    {SeriesColorRole, "seriesColor"},
    {TopicKeyRole, "topicKey"},
    {FrameSeqRole, "frameSeq"},
  };
}

void CameraGridModel::moveCamera(int from, int to)
{
  const int n = static_cast<int>(visible_.size());
  if (from < 0 || from >= n || to < 0 || to >= n || from == to) {
    return;
  }

  // Standard list-reorder over the VISIBLE sequence: remove the dragged key,
  // then insert it at the destination index in the post-removal list. This makes
  // moveCamera(0, n-1) land the item last (drag-to-last-slot puts it last), which
  // the "insert before to_key" approach would not.
  std::vector<QString> visible_keys;
  visible_keys.reserve(visible_.size());
  for (const auto & c : visible_) {
    visible_keys.push_back(key_of(c.topic_name, c.backend_name));
  }
  const QString moved = visible_keys[static_cast<std::size_t>(from)];
  visible_keys.erase(visible_keys.begin() + from);
  visible_keys.insert(visible_keys.begin() + to, moved);

  // Rebuild order_ so the visible cameras follow the new visible sequence while
  // hidden cameras keep their existing relative positions (preserving remembered
  // order across visibility toggles). Walk the old order_, emitting hidden keys
  // in place and pulling the next visible key from visible_keys whenever the old
  // slot held a visible camera.
  std::vector<QString> visible_set(visible_keys.begin(), visible_keys.end());
  std::vector<QString> next_order;
  next_order.reserve(order_.size());
  std::size_t visible_cursor = 0;
  for (const auto & key : order_) {
    const bool is_visible =
      std::find(visible_set.begin(), visible_set.end(), key) != visible_set.end();
    if (is_visible) {
      if (visible_cursor < visible_keys.size()) {
        next_order.push_back(visible_keys[visible_cursor++]);
      }
    } else {
      next_order.push_back(key);
    }
  }
  // Append any visible keys not yet placed (defensive; should not normally trigger).
  for (; visible_cursor < visible_keys.size(); ++visible_cursor) {
    next_order.push_back(visible_keys[visible_cursor]);
  }
  order_ = std::move(next_order);

  rebuild();  // reset-style; matches existing QML drop-commit behavior
}

void CameraGridModel::updateFrameSeq(const QString & topic_key, int seq)
{
  for (std::size_t i = 0; i < visible_.size(); ++i) {
    if (visible_[i].topic_name != topic_key) { continue; }
    visible_[i].frame_seq = seq;
    const auto idx = index(static_cast<int>(i), 0);
    emit dataChanged(idx, idx, {FrameSeqRole});
    return;
  }
}

}  // namespace data_recorder
