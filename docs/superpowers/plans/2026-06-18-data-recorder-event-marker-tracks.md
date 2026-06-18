# Data Recorder Event Marker Tracks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move event markers into the Timeline as editable event tracks with point/range creation, dragging, range edge resizing, and deletion.

**Architecture:** Extend `EventMarkerModel` so it owns event definitions plus in-memory instances and pending range state. `AppController` routes marker shortcuts to the model at the current playhead time. `TimelinePanel.qml` renders event rows above topic rows using new focused QML components while sharing the existing viewport, ruler, playhead, zoom, pan, and range bar.

**Tech Stack:** ROS 2 `ament_cmake`, C++17, Qt 6 Core/QML/Quick/QuickControls2, QAbstractListModel, QML, gtest, Qt smoke tests, `qmllint`, `colcon`.

---

### File Structure

**Modify:**

- `include/data_recorder/ui_models.hpp`: replace event marker selection state with track roles, instance operations, and in-memory instance structs.
- `src/ui_models.cpp`: implement event marker instance creation, pending range state, movement, resizing, deletion, shortcut lookup, and role exposure.
- `include/data_recorder/app_controller.hpp`: remove selected marker shortcut API and keep shortcut triggering as an action API.
- `src/app_controller.cpp`: call `EventMarkerModel::triggerShortcut(shortcut, playheadSeconds)` instead of selecting a marker definition.
- `qml/Main.qml`: remove the standalone `EventMarkersPanel` region and pass `eventMarkerModel` into `TimelinePanel`.
- `qml/components/TimelinePanel.qml`: render event rows above topic rows in both the left information pane and right timeline content area.
- `test/test_ui_models.cpp`: replace selection tests with event instance and shortcut action tests.
- `test/test_qml_structure.cpp`: add structure tests for event tracks and new QML components.
- `test/test_qml_smoke.cpp`: update smoke tests to find event track action buttons and verify shortcut/button actions.
- `docs/ui_terminology.md`: add event marker track terminology.

**Create:**

- `qml/components/EventTrackInfoRow.qml`: compact left-side event row with color swatch, name/count text, and action button.
- `qml/components/EventTrackRow.qml`: right-side event timeline row with point diamonds, range bars, pending range preview, drag/resize handlers, and delete menu.

**Delete:**

- `qml/components/EventMarkersPanel.qml`: the standalone event marker panel is removed.

**Do not modify:**

- `install/data_recorder/...`: generated install tree.
- Backend recorder behavior, ROS subscriptions, rosbag writing, video writing, or config loading semantics.

---

### Task 1: EventMarkerModel Tests For Track State

**Files:**

- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: Add failing EventMarkerModel tests**

Add `#include <QVariantList>` and `#include <QVariantMap>` near the existing Qt includes:

```cpp
#include <QVariantList>
#include <QVariantMap>
```

Replace the existing `EventMarkerModel` tests:

```cpp
TEST(EventMarkerModel, ExposesShortcutAndKind)
TEST(EventMarkerModel, StartsWithNoSelection)
```

with these tests:

```cpp
TEST(EventMarkerModel, ExposesTrackRoles)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({
    {"1", "拿起水杯", "point", "#1763c9"},
    {"2", "倒水", "range", "#2f9e44"},
  });

  ASSERT_EQ(model.rowCount(), 2);

  const auto point = model.index(0, 0);
  EXPECT_EQ(
    model.data(point, data_recorder::EventMarkerModel::ShortcutRole).toString().toStdString(),
    "1");
  EXPECT_EQ(
    model.data(point, data_recorder::EventMarkerModel::NameRole).toString().toStdString(),
    "拿起水杯");
  EXPECT_EQ(
    model.data(point, data_recorder::EventMarkerModel::KindRole).toString().toStdString(),
    "point");
  EXPECT_EQ(
    model.data(point, data_recorder::EventMarkerModel::ColorRole).toString().toStdString(),
    "#1763c9");
  EXPECT_EQ(model.data(point, data_recorder::EventMarkerModel::CountRole).toInt(), 0);
  EXPECT_EQ(
    model.data(point, data_recorder::EventMarkerModel::ActionTextRole).toString().toStdString(),
    "添加 (1)");
  EXPECT_FALSE(
    model.data(point, data_recorder::EventMarkerModel::HasPendingRangeStartRole).toBool());
  EXPECT_TRUE(
    model.data(point, data_recorder::EventMarkerModel::InstancesRole).toList().isEmpty());

  const auto range = model.index(1, 0);
  EXPECT_EQ(
    model.data(range, data_recorder::EventMarkerModel::ActionTextRole).toString().toStdString(),
    "添加起点 (2)");
}

TEST(EventMarkerModel, AddsMovesAndDeletesPointInstances)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({{"1", "拿起水杯", "point", "#1763c9"}});

  ASSERT_TRUE(model.triggerRowAction(0, 3.25));

  const auto row = model.index(0, 0);
  EXPECT_EQ(model.data(row, data_recorder::EventMarkerModel::CountRole).toInt(), 1);
  QVariantList instances =
    model.data(row, data_recorder::EventMarkerModel::InstancesRole).toList();
  ASSERT_EQ(instances.size(), 1);
  QVariantMap instance = instances.at(0).toMap();
  const int instance_id = instance.value(QStringLiteral("id")).toInt();
  EXPECT_GT(instance_id, 0);
  EXPECT_EQ(instance.value(QStringLiteral("kind")).toString().toStdString(), "point");
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("startSeconds")).toDouble(), 3.25);
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("endSeconds")).toDouble(), 3.25);
  EXPECT_EQ(instance.value(QStringLiteral("color")).toString().toStdString(), "#1763c9");

  ASSERT_TRUE(model.movePoint(0, instance_id, 4.5));
  instances = model.data(row, data_recorder::EventMarkerModel::InstancesRole).toList();
  instance = instances.at(0).toMap();
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("startSeconds")).toDouble(), 4.5);
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("endSeconds")).toDouble(), 4.5);

  ASSERT_TRUE(model.deleteInstance(0, instance_id));
  EXPECT_EQ(model.data(row, data_recorder::EventMarkerModel::CountRole).toInt(), 0);
  EXPECT_TRUE(model.data(row, data_recorder::EventMarkerModel::InstancesRole).toList().isEmpty());
}

TEST(EventMarkerModel, CreatesPendingAndCompletedRangeInstances)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({{"2", "倒水", "range", "#2f9e44"}});

  ASSERT_TRUE(model.triggerRowAction(0, 8.0));

  const auto row = model.index(0, 0);
  EXPECT_TRUE(
    model.data(row, data_recorder::EventMarkerModel::HasPendingRangeStartRole).toBool());
  EXPECT_DOUBLE_EQ(
    model.data(row, data_recorder::EventMarkerModel::PendingStartSecondsRole).toDouble(), 8.0);
  EXPECT_EQ(model.data(row, data_recorder::EventMarkerModel::CountRole).toInt(), 0);
  EXPECT_EQ(
    model.data(row, data_recorder::EventMarkerModel::ActionTextRole).toString().toStdString(),
    "设置终点 (2)");

  ASSERT_TRUE(model.triggerRowAction(0, 6.5));

  EXPECT_FALSE(
    model.data(row, data_recorder::EventMarkerModel::HasPendingRangeStartRole).toBool());
  EXPECT_EQ(model.data(row, data_recorder::EventMarkerModel::CountRole).toInt(), 1);
  EXPECT_EQ(
    model.data(row, data_recorder::EventMarkerModel::ActionTextRole).toString().toStdString(),
    "添加起点 (2)");

  QVariantList instances =
    model.data(row, data_recorder::EventMarkerModel::InstancesRole).toList();
  ASSERT_EQ(instances.size(), 1);
  QVariantMap instance = instances.at(0).toMap();
  const int instance_id = instance.value(QStringLiteral("id")).toInt();
  EXPECT_EQ(instance.value(QStringLiteral("kind")).toString().toStdString(), "range");
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("startSeconds")).toDouble(), 6.5);
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("endSeconds")).toDouble(), 8.0);

  ASSERT_TRUE(model.moveRange(0, instance_id, 1.25, 2.75));
  instances = model.data(row, data_recorder::EventMarkerModel::InstancesRole).toList();
  instance = instances.at(0).toMap();
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("startSeconds")).toDouble(), 1.25);
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("endSeconds")).toDouble(), 2.75);
}

TEST(EventMarkerModel, TriggerShortcutIsCaseInsensitive)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({
    {"1", "拿起水杯", "point", "#1763c9"},
    {"c", "碰撞", "point", "#e03131"},
  });

  EXPECT_TRUE(model.triggerShortcut(QStringLiteral("C"), 5.0));
  EXPECT_EQ(model.data(model.index(1, 0), data_recorder::EventMarkerModel::CountRole).toInt(), 1);
  EXPECT_FALSE(model.triggerShortcut(QStringLiteral("missing"), 9.0));
  EXPECT_EQ(model.data(model.index(0, 0), data_recorder::EventMarkerModel::CountRole).toInt(), 0);
}
```

- [ ] **Step 2: Build and run model tests to verify RED**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_ui_models --output-on-failure
```

Expected: build fails because `CountRole`, `ActionTextRole`, `HasPendingRangeStartRole`, `PendingStartSecondsRole`, `InstancesRole`, `triggerRowAction`, `movePoint`, `moveRange`, `deleteInstance`, and `triggerShortcut` are not implemented yet.

- [ ] **Step 3: Commit RED model tests**

Run:

```bash
git add test/test_ui_models.cpp
git commit -m "test: specify event marker track model behavior"
```

---

### Task 2: Implement EventMarkerModel Track State

**Files:**

- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Test: `test/test_ui_models.cpp`

- [ ] **Step 1: Replace EventMarkerModel declaration**

In `include/data_recorder/ui_models.hpp`, add this include near the existing Qt includes:

```cpp
#include <QVector>
```

In `include/data_recorder/ui_models.hpp`, replace the current `EventMarkerModel` class with:

```cpp
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
    IsSelectedRole,
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
  Q_INVOKABLE bool moveRange(int row, int instance_id, double start_seconds, double end_seconds);
  Q_INVOKABLE bool deleteInstance(int row, int instance_id);
  Q_INVOKABLE void select(int row);
  Q_INVOKABLE bool selectByShortcut(const QString & shortcut);

  void set_markers(std::vector<EventMarkerEntry> markers);

signals:
  void selectedShortcutChanged(const QString & shortcut);

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
    std::vector<EventInstance> instances;
    bool has_pending_range_start{false};
    double pending_range_start_seconds{0.0};
    int next_instance_id{1};
  };

  QString action_text_for_row(const EventMarkerRow & row) const;
  QVariantList instances_for_row(const EventMarkerRow & row) const;
  QString selectedShortcut() const;
  void emit_row_changed(int row, const QVector<int> & roles);

  std::vector<EventMarkerRow> marker_rows_;
  int selected_row_{-1};
};
```

- [ ] **Step 2: Add source helpers**

In `src/ui_models.cpp`, add this include near the existing standard library includes:

```cpp
#include <algorithm>
```

Keep the existing `valid_row()` helper. Add these helpers in the anonymous namespace after `valid_row()`:

```cpp
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
```

- [ ] **Step 3: Replace EventMarkerModel implementation**

In `src/ui_models.cpp`, replace the current `EventMarkerModel` implementation block from `EventMarkerModel::EventMarkerModel` through `EventMarkerModel::set_markers` with:

```cpp
EventMarkerModel::EventMarkerModel(QObject * parent)
: QAbstractListModel(parent)
{
}

int EventMarkerModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(marker_rows_.size());
}

QVariant EventMarkerModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || !valid_row(index.row(), static_cast<int>(marker_rows_.size()))) {
    return {};
  }

  const auto & row = marker_rows_.at(static_cast<std::size_t>(index.row()));
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
      return static_cast<int>(row.instances.size());
    case ActionTextRole:
      return action_text_for_row(row);
    case HasPendingRangeStartRole:
      return row.has_pending_range_start;
    case PendingStartSecondsRole:
      return row.pending_range_start_seconds;
    case InstancesRole:
      return instances_for_row(row);
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
    {CountRole, "count"},
    {ActionTextRole, "actionText"},
    {HasPendingRangeStartRole, "hasPendingRangeStart"},
    {PendingStartSecondsRole, "pendingStartSeconds"},
    {InstancesRole, "instances"},
    {IsSelectedRole, "isSelected"},
  };
}

bool EventMarkerModel::triggerRowAction(int row, double time_seconds)
{
  if (!valid_row(row, static_cast<int>(marker_rows_.size()))) {
    return false;
  }
  const auto kind = QString::fromStdString(marker_rows_.at(static_cast<std::size_t>(row)).marker.kind);
  if (kind == QStringLiteral("point")) {
    return addPoint(row, time_seconds);
  }
  if (kind == QStringLiteral("range")) {
    return toggleRange(row, time_seconds);
  }
  return false;
}

bool EventMarkerModel::triggerShortcut(const QString & shortcut, double time_seconds)
{
  const auto normalized = shortcut.toLower();
  for (int row = 0; row < static_cast<int>(marker_rows_.size()); ++row) {
    if (QString::fromStdString(marker_rows_.at(static_cast<std::size_t>(row)).marker.shortcut).toLower() ==
      normalized)
    {
      return triggerRowAction(row, time_seconds);
    }
  }
  return false;
}

bool EventMarkerModel::addPoint(int row, double time_seconds)
{
  if (!valid_row(row, static_cast<int>(marker_rows_.size()))) {
    return false;
  }
  auto & marker_row = marker_rows_.at(static_cast<std::size_t>(row));
  if (QString::fromStdString(marker_row.marker.kind) != QStringLiteral("point")) {
    return false;
  }

  const double seconds = non_negative_seconds(time_seconds);
  marker_row.instances.push_back(
    EventInstance{marker_row.next_instance_id++, QStringLiteral("point"), seconds, seconds});
  emit_row_changed(row, {CountRole, InstancesRole});
  return true;
}

bool EventMarkerModel::toggleRange(int row, double time_seconds)
{
  if (!valid_row(row, static_cast<int>(marker_rows_.size()))) {
    return false;
  }
  auto & marker_row = marker_rows_.at(static_cast<std::size_t>(row));
  if (QString::fromStdString(marker_row.marker.kind) != QStringLiteral("range")) {
    return false;
  }

  if (!marker_row.has_pending_range_start) {
    marker_row.has_pending_range_start = true;
    marker_row.pending_range_start_seconds = non_negative_seconds(time_seconds);
    emit_row_changed(row, {ActionTextRole, HasPendingRangeStartRole, PendingStartSecondsRole});
    return true;
  }

  double start_seconds = marker_row.pending_range_start_seconds;
  double end_seconds = time_seconds;
  normalize_range(start_seconds, end_seconds);
  marker_row.instances.push_back(
    EventInstance{marker_row.next_instance_id++, QStringLiteral("range"), start_seconds, end_seconds});
  marker_row.has_pending_range_start = false;
  marker_row.pending_range_start_seconds = 0.0;
  emit_row_changed(
    row, {CountRole, ActionTextRole, HasPendingRangeStartRole, PendingStartSecondsRole, InstancesRole});
  return true;
}

bool EventMarkerModel::movePoint(int row, int instance_id, double time_seconds)
{
  if (!valid_row(row, static_cast<int>(marker_rows_.size()))) {
    return false;
  }
  auto & marker_row = marker_rows_.at(static_cast<std::size_t>(row));
  for (auto & instance : marker_row.instances) {
    if (instance.id == instance_id && instance.kind == QStringLiteral("point")) {
      const double seconds = non_negative_seconds(time_seconds);
      instance.start_seconds = seconds;
      instance.end_seconds = seconds;
      emit_row_changed(row, {InstancesRole});
      return true;
    }
  }
  return false;
}

bool EventMarkerModel::moveRange(int row, int instance_id, double start_seconds, double end_seconds)
{
  if (!valid_row(row, static_cast<int>(marker_rows_.size()))) {
    return false;
  }
  auto & marker_row = marker_rows_.at(static_cast<std::size_t>(row));
  normalize_range(start_seconds, end_seconds);
  for (auto & instance : marker_row.instances) {
    if (instance.id == instance_id && instance.kind == QStringLiteral("range")) {
      instance.start_seconds = start_seconds;
      instance.end_seconds = end_seconds;
      emit_row_changed(row, {InstancesRole});
      return true;
    }
  }
  return false;
}

bool EventMarkerModel::deleteInstance(int row, int instance_id)
{
  if (!valid_row(row, static_cast<int>(marker_rows_.size()))) {
    return false;
  }
  auto & marker_row = marker_rows_.at(static_cast<std::size_t>(row));
  const auto previous_size = marker_row.instances.size();
  marker_row.instances.erase(
    std::remove_if(
      marker_row.instances.begin(),
      marker_row.instances.end(),
      [instance_id](const EventInstance & instance) { return instance.id == instance_id; }),
    marker_row.instances.end());
  if (marker_row.instances.size() == previous_size) {
    return false;
  }
  emit_row_changed(row, {CountRole, InstancesRole});
  return true;
}

void EventMarkerModel::select(int row)
{
  if (!valid_row(row, static_cast<int>(marker_rows_.size())) || row == selected_row_) {
    return;
  }

  const int previous = selected_row_;
  selected_row_ = row;
  if (valid_row(previous, static_cast<int>(marker_rows_.size()))) {
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
  for (int row = 0; row < static_cast<int>(marker_rows_.size()); ++row) {
    if (QString::fromStdString(marker_rows_.at(static_cast<std::size_t>(row)).marker.shortcut).toLower() ==
      normalized)
    {
      select(row);
      return true;
    }
  }
  return false;
}

void EventMarkerModel::set_markers(std::vector<EventMarkerEntry> markers)
{
  beginResetModel();
  marker_rows_.clear();
  marker_rows_.reserve(markers.size());
  selected_row_ = -1;
  for (auto & marker : markers) {
    EventMarkerRow row;
    row.marker = std::move(marker);
    marker_rows_.push_back(std::move(row));
  }
  endResetModel();
}

QString EventMarkerModel::action_text_for_row(const EventMarkerRow & row) const
{
  const auto shortcut = QString::fromStdString(row.marker.shortcut);
  const auto kind = QString::fromStdString(row.marker.kind);
  if (kind == QStringLiteral("range")) {
    return QStringLiteral("%1 (%2)")
      .arg(row.has_pending_range_start ? QStringLiteral("设置终点") : QStringLiteral("添加起点"), shortcut);
  }
  return QStringLiteral("添加 (%1)").arg(shortcut);
}

QVariantList EventMarkerModel::instances_for_row(const EventMarkerRow & row) const
{
  QVariantList instances;
  instances.reserve(static_cast<int>(row.instances.size()));
  const auto color = QString::fromStdString(row.marker.color);
  for (const auto & instance : row.instances) {
    QVariantMap value;
    value.insert(QStringLiteral("id"), instance.id);
    value.insert(QStringLiteral("kind"), instance.kind);
    value.insert(QStringLiteral("startSeconds"), instance.start_seconds);
    value.insert(QStringLiteral("endSeconds"), instance.end_seconds);
    value.insert(QStringLiteral("color"), color);
    instances.push_back(value);
  }
  return instances;
}

QString EventMarkerModel::selectedShortcut() const
{
  if (!valid_row(selected_row_, static_cast<int>(marker_rows_.size()))) {
    return {};
  }
  return QString::fromStdString(marker_rows_.at(static_cast<std::size_t>(selected_row_)).marker.shortcut)
    .toLower();
}

void EventMarkerModel::emit_row_changed(int row, const QVector<int> & roles)
{
  if (!valid_row(row, static_cast<int>(marker_rows_.size()))) {
    return;
  }
  const auto model_index = index(row, 0);
  emit dataChanged(model_index, model_index, roles);
}
```

- [ ] **Step 4: Run model tests and verify GREEN**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_ui_models --output-on-failure
```

Expected: `test_ui_models` passes. The legacy event selection API is still present as a temporary compatibility layer until the controller and QML migration are complete.

- [ ] **Step 5: Commit model implementation**

Run:

```bash
git add include/data_recorder/ui_models.hpp src/ui_models.cpp
git commit -m "Add event marker track state model"
```

---

### Task 3: Controller Tests For Shortcut Actions

**Files:**

- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: Expand the controller fixture markers**

In `make_config_fixture()`, replace the existing event marker fixture assignment with:

```cpp
  config.event_markers = {
    {"1", "拿起水杯", "point", "#1763c9"},
    {"2", "倒水", "range", "#2f9e44"},
    {"c", "碰撞", "point", "#e03131"},
  };
```

- [ ] **Step 2: Replace selection-oriented controller tests**

Delete these tests:

```cpp
TEST(AppController, TriggerMarkerShortcutSelectsMarker)
TEST(AppController, DirectEventMarkerSelectionUpdatesSelectedMarkerShortcut)
TEST(AppController, MissingMarkerShortcutPreservesSelectionAndDoesNotEmit)
```

Add these tests in their place:

```cpp
TEST(AppController, TriggerMarkerShortcutAddsPointAtPlayhead)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.setPlayheadSeconds(7.125);

  EXPECT_TRUE(controller.triggerMarkerShortcut("1"));
  const auto row = controller.eventMarkerModel()->index(0, 0);
  EXPECT_EQ(
    controller.eventMarkerModel()->data(row, data_recorder::EventMarkerModel::CountRole).toInt(),
    1);

  const QVariantList instances =
    controller.eventMarkerModel()->data(row, data_recorder::EventMarkerModel::InstancesRole).toList();
  ASSERT_EQ(instances.size(), 1);
  EXPECT_DOUBLE_EQ(
    instances.at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 7.125);

  EXPECT_FALSE(controller.triggerMarkerShortcut("missing"));
  EXPECT_EQ(
    controller.eventMarkerModel()->data(row, data_recorder::EventMarkerModel::CountRole).toInt(),
    1);
}

TEST(AppController, TriggerMarkerShortcutCompletesRangeAtPlayhead)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);
  const auto range_row = controller.eventMarkerModel()->index(1, 0);

  controller.setPlayheadSeconds(2.0);
  EXPECT_TRUE(controller.triggerMarkerShortcut("2"));
  EXPECT_TRUE(
    controller.eventMarkerModel()
      ->data(range_row, data_recorder::EventMarkerModel::HasPendingRangeStartRole)
      .toBool());
  EXPECT_EQ(
    controller.eventMarkerModel()->data(range_row, data_recorder::EventMarkerModel::CountRole).toInt(),
    0);

  controller.setPlayheadSeconds(4.5);
  EXPECT_TRUE(controller.triggerMarkerShortcut("2"));
  EXPECT_FALSE(
    controller.eventMarkerModel()
      ->data(range_row, data_recorder::EventMarkerModel::HasPendingRangeStartRole)
      .toBool());
  EXPECT_EQ(
    controller.eventMarkerModel()->data(range_row, data_recorder::EventMarkerModel::CountRole).toInt(),
    1);
}
```

- [ ] **Step 3: Update event filter shortcut expectations**

In `EventFilterHandlesRecordingAndMarkerShortcuts`, replace:

```cpp
  EXPECT_EQ(controller.selectedMarkerShortcut().toStdString(), "1");
```

with:

```cpp
  EXPECT_EQ(
    controller.eventMarkerModel()
      ->data(controller.eventMarkerModel()->index(0, 0), data_recorder::EventMarkerModel::CountRole)
      .toInt(),
    1);
```

In `EventFilterIgnoresAutoRepeatAndUnknownKeys`, replace:

```cpp
  EXPECT_TRUE(controller.selectedMarkerShortcut().isEmpty());
```

with:

```cpp
  EXPECT_EQ(
    controller.eventMarkerModel()
      ->data(controller.eventMarkerModel()->index(0, 0), data_recorder::EventMarkerModel::CountRole)
      .toInt(),
    0);
```

In `EventFilterIgnoresModifiedShortcuts`, replace:

```cpp
  EXPECT_TRUE(controller.selectedMarkerShortcut().isEmpty());
```

with:

```cpp
  EXPECT_EQ(
    controller.eventMarkerModel()
      ->data(controller.eventMarkerModel()->index(0, 0), data_recorder::EventMarkerModel::CountRole)
      .toInt(),
    0);
```

- [ ] **Step 4: Run controller tests and verify RED**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_ui_models --output-on-failure
```

Expected: tests compile and fail because `AppController::triggerMarkerShortcut()` still selects marker definitions instead of adding instances at the playhead time.

- [ ] **Step 5: Commit RED controller tests**

Run:

```bash
git add test/test_ui_models.cpp
git commit -m "test: specify marker shortcuts as timeline actions"
```

---

### Task 4: Route Controller Shortcuts To EventMarkerModel Actions

**Files:**

- Modify: `include/data_recorder/app_controller.hpp`
- Modify: `src/app_controller.cpp`
- Test: `test/test_ui_models.cpp`

- [ ] **Step 1: Remove selected marker API from AppController header**

In `include/data_recorder/app_controller.hpp`, remove:

```cpp
  Q_PROPERTY(QString selectedMarkerShortcut READ selectedMarkerShortcut NOTIFY selectedMarkerShortcutChanged)
```

Remove this public getter declaration:

```cpp
  QString selectedMarkerShortcut() const;
```

Remove this signal:

```cpp
  void selectedMarkerShortcutChanged();
```

Remove this private method:

```cpp
  void updateSelectedMarkerShortcut(const QString & shortcut);
```

Remove this private member:

```cpp
  QString selected_marker_shortcut_;
```

- [ ] **Step 2: Remove selected marker wiring from AppController source**

In `src/app_controller.cpp`, delete the constructor connection:

```cpp
  connect(
    &event_marker_model_,
    &EventMarkerModel::selectedShortcutChanged,
    this,
    &AppController::updateSelectedMarkerShortcut);
```

Delete the `selectedMarkerShortcut()` method:

```cpp
QString AppController::selectedMarkerShortcut() const
{
  return selected_marker_shortcut_;
}
```

Delete the `updateSelectedMarkerShortcut()` method at the end of the file.

- [ ] **Step 3: Change shortcut action implementation**

Replace `AppController::triggerMarkerShortcut` with:

```cpp
bool AppController::triggerMarkerShortcut(const QString & shortcut)
{
  return event_marker_model_.triggerShortcut(shortcut, playhead_seconds_);
}
```

- [ ] **Step 4: Run model/controller tests and verify GREEN**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_ui_models --output-on-failure
```

Expected: `test_ui_models` passes.

- [ ] **Step 5: Commit controller shortcut implementation**

Run:

```bash
git add include/data_recorder/app_controller.hpp src/app_controller.cpp
git commit -m "Trigger marker shortcuts at the playhead"
```

---

### Task 5: QML Structure And Smoke Tests For Event Tracks

**Files:**

- Modify: `test/test_qml_structure.cpp`
- Modify: `test/test_qml_smoke.cpp`

- [ ] **Step 1: Add QML structure test**

Add this test after `AppChromeUsesStatusBarForRecording` in `test/test_qml_structure.cpp`:

```cpp
TEST(QmlStructure, EventMarkersRenderAsTimelineTracks)
{
  const std::string main_text = read_text(qml_dir() / "Main.qml");
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");

  expect_not_contains(main_text, "EventMarkersPanel");
  EXPECT_FALSE(std::filesystem::exists(qml_dir() / "components" / "EventMarkersPanel.qml"));
  expect_contains(main_text, "eventMarkerModel: appController.eventMarkerModel");
  expect_contains(panel_text, "property var eventMarkerModel");
  expect_contains(panel_text, "model: root.eventMarkerModel");
  expect_contains(panel_text, "EventTrackInfoRow {");
  expect_contains(panel_text, "EventTrackRow {");

  const auto event_position = panel_text.find("model: root.eventMarkerModel");
  const auto topic_position = panel_text.find("model: root.model");
  ASSERT_NE(event_position, std::string::npos);
  ASSERT_NE(topic_position, std::string::npos);
  EXPECT_LT(event_position, topic_position);

  const std::filesystem::path info_row_path = qml_dir() / "components" / "EventTrackInfoRow.qml";
  const std::filesystem::path track_row_path = qml_dir() / "components" / "EventTrackRow.qml";
  EXPECT_TRUE(std::filesystem::exists(info_row_path));
  EXPECT_TRUE(std::filesystem::exists(track_row_path));

  const std::string info_text = read_text(info_row_path);
  expect_contains(info_text, "objectName: \"eventMarkerActionButton_\" + root.shortcut");
  expect_contains(info_text, "root.eventName + \"（共 \" + root.count + \" 个）\"");
  expect_contains(info_text, "signal actionRequested()");

  const std::string track_text = read_text(track_row_path);
  expect_contains(track_text, "property var viewport");
  expect_contains(track_text, "property var markerModel");
  expect_contains(track_text, "id: pendingRangePreview");
  expect_contains(track_text, "rotation: 45");
  expect_contains(track_text, "id: leftResizeHandle");
  expect_contains(track_text, "id: rightResizeHandle");
  expect_contains(track_text, "root.markerModel.movePoint");
  expect_contains(track_text, "root.markerModel.moveRange");
  expect_contains(track_text, "root.markerModel.deleteInstance");
  expect_contains(track_text, "MenuItem");
  expect_contains(track_text, "text: \"删除\"");
}
```

- [ ] **Step 2: Update QML smoke fixture markers**

In `test/test_qml_smoke.cpp`, replace the fixture `config.event_markers` with:

```cpp
  config.event_markers = {
    {"1", "拿起水杯", "point", "#1763c9"},
    {"2", "倒水", "range", "#2f9e44"},
    {"c", "碰撞", "point", "#e03131"},
  };
```

- [ ] **Step 3: Update main window smoke test**

In `LoadsMainWindowAndInteractiveControls`, replace:

```cpp
  ASSERT_NE(find_required(root_, "eventMarkerButton_c"), nullptr);
```

with:

```cpp
  ASSERT_NE(find_required(root_, "eventMarkerActionButton_c"), nullptr);
```

- [ ] **Step 4: Replace marker shortcut smoke test**

Replace `MarkerShortcutSelectsMatchingMarker` with:

```cpp
TEST_F(QmlSmokeTest, MarkerShortcutAddsPointAtPlayhead)
{
  controller_->setPlayheadSeconds(6.25);

  QKeyEvent marker_event(QEvent::KeyPress, Qt::Key_C, Qt::NoModifier, QStringLiteral("c"));
  EXPECT_TRUE(QCoreApplication::sendEvent(window_, &marker_event));

  const auto row = controller_->eventMarkerModel()->index(2, 0);
  EXPECT_EQ(
    controller_->eventMarkerModel()->data(row, data_recorder::EventMarkerModel::CountRole).toInt(),
    1);
  const QVariantList instances =
    controller_->eventMarkerModel()->data(row, data_recorder::EventMarkerModel::InstancesRole).toList();
  ASSERT_EQ(instances.size(), 1);
  EXPECT_DOUBLE_EQ(
    instances.at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 6.25);
}
```

- [ ] **Step 5: Add smoke test for range action button**

Add this test after `MarkerShortcutAddsPointAtPlayhead`:

```cpp
TEST_F(QmlSmokeTest, RangeActionButtonAddsStartAndEnd)
{
  QObject * range_button = find_required(root_, "eventMarkerActionButton_2");
  ASSERT_NE(range_button, nullptr);

  controller_->setPlayheadSeconds(1.0);
  ASSERT_TRUE(QMetaObject::invokeMethod(range_button, "clicked"));

  const auto row = controller_->eventMarkerModel()->index(1, 0);
  EXPECT_TRUE(
    controller_->eventMarkerModel()
      ->data(row, data_recorder::EventMarkerModel::HasPendingRangeStartRole)
      .toBool());
  EXPECT_EQ(
    controller_->eventMarkerModel()->data(row, data_recorder::EventMarkerModel::CountRole).toInt(),
    0);

  controller_->setPlayheadSeconds(3.0);
  ASSERT_TRUE(QMetaObject::invokeMethod(range_button, "clicked"));

  EXPECT_FALSE(
    controller_->eventMarkerModel()
      ->data(row, data_recorder::EventMarkerModel::HasPendingRangeStartRole)
      .toBool());
  EXPECT_EQ(
    controller_->eventMarkerModel()->data(row, data_recorder::EventMarkerModel::CountRole).toInt(),
    1);
}
```

- [ ] **Step 6: Run QML tests and verify RED**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R "test_qml_structure|test_qml_smoke" --output-on-failure
```

Expected: tests fail because `EventMarkersPanel.qml` still exists, `TimelinePanel.qml` does not accept `eventMarkerModel`, and event track action buttons do not exist.

- [ ] **Step 7: Commit RED QML tests**

Run:

```bash
git add test/test_qml_structure.cpp test/test_qml_smoke.cpp
git commit -m "test: specify event markers as timeline tracks"
```

---

### Task 6: Add Event Track QML Components

**Files:**

- Create: `qml/components/EventTrackInfoRow.qml`
- Create: `qml/components/EventTrackRow.qml`
- Test: `test/test_qml_structure.cpp`

- [ ] **Step 1: Create EventTrackInfoRow.qml**

Create `qml/components/EventTrackInfoRow.qml` with:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property string eventName: ""
    property string shortcut: ""
    property string color: "#2563eb"
    property int count: 0
    property string actionText: ""

    signal actionRequested()

    height: 32
    color: "#f6f8fb"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        spacing: 6

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 16
            color: root.color
        }

        Label {
            Layout.fillWidth: true
            text: root.eventName + "（共 " + root.count + " 个）"
            color: "#111827"
            font.pixelSize: 11
            font.bold: true
            elide: Text.ElideRight
        }

        Button {
            objectName: "eventMarkerActionButton_" + root.shortcut
            Layout.preferredWidth: Math.max(78, actionLabel.implicitWidth + 18)
            Layout.preferredHeight: 22
            padding: 0
            onClicked: root.actionRequested()

            contentItem: Label {
                id: actionLabel
                text: root.actionText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: "#111827"
                font.pixelSize: 10
                font.bold: true
                elide: Text.ElideRight
            }

            background: Rectangle {
                color: parent.hovered ? "#e2e8f0" : "#ffffff"
                border.color: "#cbd5e1"
                border.width: 1
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }
}
```

- [ ] **Step 2: Create EventTrackRow.qml**

Create `qml/components/EventTrackRow.qml` with:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    property int rowIndex: -1
    property string kind: "point"
    property string color: "#2563eb"
    property var instances: []
    property bool hasPendingRangeStart: false
    property real pendingStartSeconds: 0
    property real playheadSeconds: 0
    property var viewport
    property var markerModel

    property int contextInstanceId: -1

    height: 32
    color: "#fbfdff"

    function xAtTime(seconds) {
        if (viewport && viewport.xAtTime) {
            return viewport.xAtTime(seconds, width)
        }
        return 0
    }

    function timeAtX(xPosition) {
        if (viewport && viewport.timeAtX) {
            return viewport.timeAtX(xPosition, width)
        }
        return Math.max(0, Number(xPosition || 0))
    }

    function localXFromMouse(item, mouse) {
        return item.mapToItem(root, mouse.x, mouse.y).x
    }

    function zoomAtLocalX(localX, wheel) {
        if (!root.viewport) {
            return
        }
        if (wheel.modifiers & Qt.ShiftModifier) {
            root.viewport.panByWheel(wheel.angleDelta.y)
        } else {
            root.viewport.zoomAt(localX, root.width, wheel.angleDelta.y)
        }
        wheel.accepted = true
    }

    function normalizedStart(instance) {
        return Math.min(Number(instance.startSeconds || 0), Number(instance.endSeconds || 0))
    }

    function normalizedEnd(instance) {
        return Math.max(Number(instance.startSeconds || 0), Number(instance.endSeconds || 0))
    }

    function requestDelete(instanceId) {
        contextInstanceId = instanceId
        deleteMenu.popup()
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }

    Rectangle {
        id: pendingRangePreview

        visible: root.kind === "range" && root.hasPendingRangeStart
        x: Math.min(root.xAtTime(root.pendingStartSeconds), root.xAtTime(root.playheadSeconds))
        y: 11
        width: Math.max(
            3,
            Math.abs(root.xAtTime(root.playheadSeconds) - root.xAtTime(root.pendingStartSeconds)))
        height: 10
        color: root.color
        opacity: 0.32
        border.color: root.color
        border.width: 1
    }

    Repeater {
        model: root.instances

        delegate: Item {
            id: instanceDelegate

            required property var modelData

            readonly property int instanceId: Number(modelData.id)
            readonly property string instanceKind: String(modelData.kind || root.kind)
            readonly property real instanceStart: Number(modelData.startSeconds || 0)
            readonly property real instanceEnd: Number(modelData.endSeconds || instanceStart)
            readonly property string instanceColor: String(modelData.color || root.color)

            x: instanceKind === "point" ?
                root.xAtTime(instanceStart) - width / 2 :
                root.xAtTime(root.normalizedStart(modelData))
            y: 0
            width: instanceKind === "point" ?
                16 :
                Math.max(14, root.xAtTime(root.normalizedEnd(modelData)) - root.xAtTime(root.normalizedStart(modelData)))
            height: root.height
            z: 5

            Rectangle {
                visible: instanceDelegate.instanceKind === "point"
                anchors.centerIn: parent
                width: 8
                height: 8
                rotation: 45
                color: instanceDelegate.instanceColor
                border.color: "#ffffff"
                border.width: 1
            }

            MouseArea {
                id: pointMouseArea

                visible: instanceDelegate.instanceKind === "point"
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(pointMouseArea, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(instanceDelegate.instanceId)
                    }
                }

                onPositionChanged: function(mouse) {
                    if ((pressedButtons & Qt.LeftButton) && root.markerModel && root.markerModel.movePoint) {
                        root.markerModel.movePoint(
                            root.rowIndex,
                            instanceDelegate.instanceId,
                            root.timeAtX(root.localXFromMouse(pointMouseArea, mouse)))
                    }
                }
            }

            Rectangle {
                id: rangeBody

                visible: instanceDelegate.instanceKind === "range"
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                height: 12
                color: instanceDelegate.instanceColor
                opacity: 0.78
                border.color: instanceDelegate.instanceColor
                border.width: 1
            }

            MouseArea {
                id: rangeBodyMouseArea

                visible: instanceDelegate.instanceKind === "range"
                anchors.fill: rangeBody
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(rangeBodyMouseArea, wheel), wheel)
                }

                property real pressTrackX: 0
                property real pressStartSeconds: 0
                property real pressEndSeconds: 0

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(instanceDelegate.instanceId)
                        return
                    }
                    pressTrackX = root.localXFromMouse(rangeBodyMouseArea, mouse)
                    pressStartSeconds = root.normalizedStart(instanceDelegate.modelData)
                    pressEndSeconds = root.normalizedEnd(instanceDelegate.modelData)
                }

                onPositionChanged: function(mouse) {
                    if ((pressedButtons & Qt.LeftButton) && root.markerModel && root.markerModel.moveRange) {
                        var deltaSeconds = root.timeAtX(root.localXFromMouse(rangeBodyMouseArea, mouse)) - root.timeAtX(pressTrackX)
                        root.markerModel.moveRange(
                            root.rowIndex,
                            instanceDelegate.instanceId,
                            pressStartSeconds + deltaSeconds,
                            pressEndSeconds + deltaSeconds)
                    }
                }
            }

            MouseArea {
                id: leftResizeHandle

                visible: instanceDelegate.instanceKind === "range"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 18
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.SizeHorCursor
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(leftResizeHandle, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(instanceDelegate.instanceId)
                    }
                }

                onPositionChanged: function(mouse) {
                    if ((pressedButtons & Qt.LeftButton) && root.markerModel && root.markerModel.moveRange) {
                        root.markerModel.moveRange(
                            root.rowIndex,
                            instanceDelegate.instanceId,
                            root.timeAtX(root.localXFromMouse(leftResizeHandle, mouse)),
                            root.normalizedEnd(instanceDelegate.modelData))
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 3
                    height: 14
                    color: "#ffffff"
                    opacity: 0.9
                }
            }

            MouseArea {
                id: rightResizeHandle

                visible: instanceDelegate.instanceKind === "range"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 18
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.SizeHorCursor
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(rightResizeHandle, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(instanceDelegate.instanceId)
                    }
                }

                onPositionChanged: function(mouse) {
                    if ((pressedButtons & Qt.LeftButton) && root.markerModel && root.markerModel.moveRange) {
                        root.markerModel.moveRange(
                            root.rowIndex,
                            instanceDelegate.instanceId,
                            root.normalizedStart(instanceDelegate.modelData),
                            root.timeAtX(root.localXFromMouse(rightResizeHandle, mouse)))
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 3
                    height: 14
                    color: "#ffffff"
                    opacity: 0.9
                }
            }
        }
    }

    Menu {
        id: deleteMenu

        MenuItem {
            text: "删除"
            onTriggered: {
                if (root.markerModel && root.markerModel.deleteInstance) {
                    root.markerModel.deleteInstance(root.rowIndex, root.contextInstanceId)
                }
            }
        }
    }
}
```

- [ ] **Step 3: Run qmllint on new components**

Run:

```bash
source ~/.local/ros2_rc && rr && qmllint -I qml/components qml/components/EventTrackInfoRow.qml qml/components/EventTrackRow.qml
```

Expected: no qmllint errors.

- [ ] **Step 4: Commit new QML components**

Run:

```bash
git add qml/components/EventTrackInfoRow.qml qml/components/EventTrackRow.qml
git commit -m "Add event marker timeline row components"
```

---

### Task 7: Integrate Event Tracks Into TimelinePanel

**Files:**

- Modify: `qml/Main.qml`
- Modify: `qml/components/TimelinePanel.qml`
- Delete: `qml/components/EventMarkersPanel.qml`
- Test: `test/test_qml_structure.cpp`
- Test: `test/test_qml_smoke.cpp`

- [ ] **Step 1: Replace standalone event marker region in Main.qml**

In `qml/Main.qml`, replace the right-side vertical `SplitView` that contains `EventMarkersPanel` and `TimelinePanel` with a direct `TimelinePanel` child:

```qml
                    TimelinePanel {
                        SplitView.fillHeight: true
                        SplitView.fillWidth: true
                        controller: appController
                        model: appController.topicModel
                        eventMarkerModel: appController.eventMarkerModel
                    }
```

The parent remains the horizontal `SplitView` beside the recording/session panel. The standalone `EventMarkersPanel` block is removed.

- [ ] **Step 2: Delete EventMarkersPanel.qml**

Run:

```bash
git rm qml/components/EventMarkersPanel.qml
```

- [ ] **Step 3: Add event marker model property to TimelinePanel.qml**

Near the existing `property var model` in `qml/components/TimelinePanel.qml`, add:

```qml
    property var eventMarkerModel
```

- [ ] **Step 4: Replace the left `ListView` with a combined row `Flickable`**

In `TimelinePanel.qml`, replace the current `ListView { id: infoList ... }` block with:

```qml
            Flickable {
                id: infoList

                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: infoColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                onContentYChanged: root.syncCurveToInfo()

                Column {
                    id: infoColumn

                    width: infoList.width
                    spacing: 0

                    Repeater {
                        id: eventInfoRepeater
                        model: root.eventMarkerModel

                        delegate: EventTrackInfoRow {
                            width: infoColumn.width
                            eventName: model.name
                            shortcut: model.shortcut
                            color: model.color
                            count: model.count
                            actionText: model.actionText
                            onActionRequested: {
                                if (root.eventMarkerModel && root.eventMarkerModel.triggerRowAction) {
                                    root.eventMarkerModel.triggerRowAction(index, root.playheadSeconds)
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: infoColumn.width
                        height: eventInfoRepeater.count > 0 ? 1 : 0
                        color: "#cbd5e1"
                    }

                    Repeater {
                        model: root.model

                        delegate: TimelineInfoRow {
                            width: infoColumn.width
                            topicName: model.topicName
                            frequencyText: model.frequencyText
                            backendName: model.backendName
                            isVisible: model.isVisible
                            isCamera: model.isCamera
                            onToggleVisibleRequested: {
                                if (root.controller && root.controller.toggleTopicVisible) {
                                    root.controller.toggleTopicVisible(index)
                                }
                            }
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }
```

- [ ] **Step 5: Replace the right `ListView` with a combined row `Flickable`**

In `TimelinePanel.qml`, inside `Item { id: curveViewport ... }`, replace the current `ListView { id: curveList ... }` block with:

```qml
                MouseArea {
                    objectName: "timelineCurveMouseArea"
                    anchors.fill: parent
                    z: 0
                    acceptedButtons: Qt.LeftButton
                    onPressed: root.seekFromCurveX(mouse.x)
                    onPositionChanged: {
                        if (pressed) {
                            root.seekFromCurveX(mouse.x)
                        }
                    }
                    onWheel: function(wheel) {
                        if (wheel.modifiers & Qt.ShiftModifier) {
                            viewport.panByWheel(wheel.angleDelta.y)
                        } else {
                            viewport.zoomAt(wheel.x, curveViewport.width, wheel.angleDelta.y)
                        }
                        wheel.accepted = true
                    }
                }

                Flickable {
                    id: curveList

                    anchors.fill: parent
                    z: 1
                    contentHeight: curveColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    onContentYChanged: root.syncInfoToCurve()

                    Column {
                        id: curveColumn

                        width: curveList.width
                        spacing: 0

                        Repeater {
                            id: eventCurveRepeater
                            model: root.eventMarkerModel

                            delegate: EventTrackRow {
                                width: curveColumn.width
                                rowIndex: index
                                kind: model.kind
                                color: model.color
                                instances: model.instances
                                hasPendingRangeStart: model.hasPendingRangeStart
                                pendingStartSeconds: model.pendingStartSeconds
                                playheadSeconds: root.playheadSeconds
                                viewport: viewport
                                markerModel: root.eventMarkerModel
                            }
                        }

                        Rectangle {
                            width: curveColumn.width
                            height: eventCurveRepeater.count > 0 ? 1 : 0
                            color: "#cbd5e1"
                        }

                        Repeater {
                            model: root.model

                            delegate: TimelineCurveRow {
                                width: curveColumn.width
                                trackKind: model.trackKind
                                seriesList: model.seriesList
                                xMax: root.effectiveDurationSeconds
                                visibleStartSeconds: viewport.visibleStartSeconds
                                visibleDurationSeconds: viewport.boundedVisibleDuration
                            }
                        }
                    }
                }
```

Delete the old `MouseArea { objectName: "timelineCurveMouseArea" ... }` block that followed the `ListView`. The replacement above keeps one seek/zoom mouse area behind event marker items.

- [ ] **Step 6: Run QML lint and QML tests**

Run:

```bash
source ~/.local/ros2_rc && rr && qmllint -I qml/components qml/Main.qml qml/components/*.qml
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R "test_qml_structure|test_qml_smoke" --output-on-failure
```

Expected: `qmllint`, `test_qml_structure`, and `test_qml_smoke` pass.

- [ ] **Step 7: Commit Timeline integration**

Run:

```bash
git add qml/Main.qml qml/components/TimelinePanel.qml qml/components/EventMarkersPanel.qml
git commit -m "Render event markers as timeline tracks"
```

---

### Task 8: Update UI Terminology

**Files:**

- Modify: `docs/ui_terminology.md`

- [ ] **Step 1: Add event track terminology rows**

In `docs/ui_terminology.md`, add these rows after the existing `事件标记` row:

```markdown
| 事件标记轨道 | Event Marker Track | `EventMarkerTrack` | 时间轴中每个事件标记对应的一行 |
| 事件标记实例 | Event Marker Instance | `EventMarkerInstance` | 时间轴中一个具体的点事件或区间事件 |
```

- [ ] **Step 2: Commit terminology update**

Run:

```bash
git add docs/ui_terminology.md
git commit -m "docs: add event marker track terminology"
```

---

### Task 9: Remove Legacy Event Marker Selection API

**Files:**

- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Test: `test/test_ui_models.cpp`
- Test: `test/test_qml_structure.cpp`
- Test: `test/test_qml_smoke.cpp`

- [ ] **Step 1: Remove legacy selection declarations from EventMarkerModel**

In `include/data_recorder/ui_models.hpp`, remove `IsSelectedRole` from the `EventMarkerModel::Roles`
enum:

```cpp
    IsSelectedRole,
```

Remove these invokable methods:

```cpp
  Q_INVOKABLE void select(int row);
  Q_INVOKABLE bool selectByShortcut(const QString & shortcut);
```

Remove this signal block:

```cpp
signals:
  void selectedShortcutChanged(const QString & shortcut);
```

Remove this private helper:

```cpp
  QString selectedShortcut() const;
```

Remove this private member:

```cpp
  int selected_row_{-1};
```

- [ ] **Step 2: Remove legacy selection implementation**

In `src/ui_models.cpp`, remove this `data()` case:

```cpp
    case IsSelectedRole:
      return index.row() == selected_row_;
```

Remove this `roleNames()` entry:

```cpp
    {IsSelectedRole, "isSelected"},
```

Remove the full implementations of:

```cpp
void EventMarkerModel::select(int row)
bool EventMarkerModel::selectByShortcut(const QString & shortcut)
QString EventMarkerModel::selectedShortcut() const
```

In `EventMarkerModel::set_markers`, remove:

```cpp
  selected_row_ = -1;
```

- [ ] **Step 3: Run tests that previously depended on selection**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R "test_ui_models|test_qml_structure|test_qml_smoke" --output-on-failure
```

Expected: all three tests pass. No source or test file references `selectedMarkerShortcut`,
`selectedShortcutChanged`, `selectByShortcut`, or `EventMarkerModel::IsSelectedRole`.

- [ ] **Step 4: Commit legacy selection cleanup**

Run:

```bash
git add include/data_recorder/ui_models.hpp src/ui_models.cpp
git commit -m "Remove legacy event marker selection state"
```

---

### Task 10: Final Verification

**Files:**

- All files changed by Tasks 1-9.

- [ ] **Step 1: Run full package verification**

Run:

```bash
source ~/.local/ros2_rc && rr && qmllint -I qml/components qml/Main.qml qml/components/*.qml
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args --output-on-failure
```

Expected: `qmllint` exits successfully, `data_recorder` builds successfully, and all `data_recorder` tests pass.

- [ ] **Step 2: Inspect the UI manually**

Run the app with the package example config:

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder --ros-args -p config_path:=/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml
```

Expected UI checks:

- The standalone event marker panel is gone.
- Event marker tracks appear at the top of the Timeline.
- Event rows show `名称（共 N 个）` on the left and an action button on the right.
- Pressing a point shortcut adds a point diamond at the playhead.
- Pressing a range shortcut once shows a pending range preview and changes the button to `设置终点`.
- Pressing the same range shortcut again creates a completed range.
- Point markers drag horizontally.
- Range bodies drag horizontally.
- Range left and right edges resize independently.
- Right-clicking a marker opens a menu with `删除`.
- Topic curves still zoom, pan, scroll, and align with the ruler.

- [ ] **Step 3: Commit any verification fixes**

If verification required fixes, inspect the changed files and commit only the files related to this
feature. For example, if the fixes touched the event model and event track QML components, run:

```bash
git status --short
git add include/data_recorder/ui_models.hpp src/ui_models.cpp qml/components/EventTrackInfoRow.qml qml/components/EventTrackRow.qml qml/components/TimelinePanel.qml
git commit -m "Fix event marker track verification issues"
```

If no fixes were required, leave the worktree clean and do not create an empty commit.
