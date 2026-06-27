#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QImage>
#include <QKeyEvent>
#include <QModelIndex>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "data_recorder/live_bridge.hpp"

#include "data_recorder/app_controller.hpp"
#include "data_recorder/ui_models.hpp"

namespace
{

data_recorder::ConfigData make_config_fixture()
{
  data_recorder::TopicEntry numeric_topic;
  numeric_topic.topic_name = "/joint_states";
  numeric_topic.backend_name = "rosbag";
  numeric_topic.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::TopicEntry camera_topic;
  camera_topic.topic_name = "/camera/image_raw";
  camera_topic.backend_name = "video";
  camera_topic.ui_category = data_recorder::TopicUiCategory::CameraPreview;

  data_recorder::ConfigData config;
  config.config_path = "/tmp/data_recorder_test.yaml";
  config.output_dir = "/tmp/recordings";
  config.topics = {numeric_topic, camera_topic};
  config.tags = {{"成功", "#2f9e44"}, {"失败", "#e03131"}};
  config.event_markers = {
    {"1", "拿起水杯", "point", "#1763c9"},
    {"2", "倒水", "range", "#2f9e44"},
    {"c", "碰撞", "point", "#e03131"},
  };
  return config;
}

// 录制会话模型不再带占位数据（见 Task 12），凡是测历史选择行为的用例需先注入会话。
void seed_history_sessions(data_recorder::AppController & controller)
{
  data_recorder::SessionRecord first;
  first.session_id = "2026-05-31_07-46-20";
  first.directory = "/tmp/recordings/2026-05-31_07-46-20";
  first.duration_seconds = 24.123;
  first.size_bytes = 1288490188;  // ~1.2 GiB
  first.tags = {{"成功", "#2f9e44"}};

  data_recorder::SessionRecord second;
  second.session_id = "2026-05-31_07-47-06";
  second.directory = "/tmp/recordings/2026-05-31_07-47-06";
  second.duration_seconds = 755.0;
  second.size_bytes = 901775360;  // ~860 MiB
  second.tags = {{"力控", "#7c4dff"}};

  controller.recordingSessionModel()->setSessions({first, second});
}

}  // namespace

TEST(TopicListModel, ExposesTopicRoles)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";
  topic.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  ASSERT_EQ(model.rowCount(), 1);
  const auto index = model.index(0, 0);
  EXPECT_EQ(
    model.data(index, data_recorder::TopicListModel::TopicNameRole).toString().toStdString(),
    "/joint_states");
  EXPECT_EQ(
    model.data(index, data_recorder::TopicListModel::BackendNameRole).toString().toStdString(),
    "rosbag");
  EXPECT_TRUE(model.data(index, data_recorder::TopicListModel::IsVisibleRole).toBool());
}

TEST(TopicListModel, TogglesVisibility)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/camera/image_raw";
  topic.backend_name = "video";
  topic.ui_category = data_recorder::TopicUiCategory::CameraPreview;

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  model.toggleVisible(0);
  EXPECT_FALSE(
    model.data(model.index(0, 0), data_recorder::TopicListModel::IsVisibleRole).toBool());
}

TEST(TopicListModel, ToggleVisibleEmitsDataChangedForVisibilityRole)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";
  topic.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  int signal_count = 0;
  QModelIndex changed_top_left;
  QModelIndex changed_bottom_right;
  QVector<int> changed_roles;
  QObject::connect(
    &model,
    &QAbstractItemModel::dataChanged,
    [&signal_count, &changed_top_left, &changed_bottom_right, &changed_roles](
      const QModelIndex & top_left,
      const QModelIndex & bottom_right,
      const QList<int> & roles) {
      ++signal_count;
      changed_top_left = top_left;
      changed_bottom_right = bottom_right;
      changed_roles = roles;
    });

  model.toggleVisible(0);

  EXPECT_EQ(signal_count, 1);
  EXPECT_EQ(changed_top_left.row(), 0);
  EXPECT_EQ(changed_bottom_right.row(), 0);
  EXPECT_TRUE(changed_roles.contains(data_recorder::TopicListModel::IsVisibleRole));
}

TEST(TopicListModel, FlagsAreNotEditable)
{
  data_recorder::TopicEntry topic;
  topic.topic_name = "/joint_states";
  topic.backend_name = "rosbag";
  topic.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::TopicListModel model;
  model.set_topics({topic});

  EXPECT_FALSE(model.flags(model.index(0, 0)) & Qt::ItemIsEditable);
}

TEST(TopicListModel, ClassifiesCameraNumericAndEmptyTracks)
{
  data_recorder::TopicEntry tf_topic;
  tf_topic.topic_name = "/tf";
  tf_topic.backend_name = "rosbag";
  tf_topic.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::TopicEntry joint_topic;
  joint_topic.topic_name = "/joint_states";
  joint_topic.backend_name = "rosbag";
  joint_topic.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::TopicEntry camera_topic;
  camera_topic.topic_name = "/camera/image_raw";
  camera_topic.backend_name = "video";
  camera_topic.ui_category = data_recorder::TopicUiCategory::CameraPreview;

  data_recorder::TopicListModel model;
  model.set_topics({tf_topic, joint_topic, camera_topic});

  EXPECT_EQ(model.data(model.index(0, 0), data_recorder::TopicListModel::TrackKindRole).toString().toStdString(), "empty");
  EXPECT_FALSE(model.data(model.index(0, 0), data_recorder::TopicListModel::IsDrawableRole).toBool());
  EXPECT_TRUE(model.data(model.index(0, 0), data_recorder::TopicListModel::SeriesListRole).toList().isEmpty());

  EXPECT_EQ(model.data(model.index(1, 0), data_recorder::TopicListModel::TrackKindRole).toString().toStdString(), "numeric");
  EXPECT_TRUE(model.data(model.index(1, 0), data_recorder::TopicListModel::IsDrawableRole).toBool());

  EXPECT_EQ(model.data(model.index(2, 0), data_recorder::TopicListModel::TrackKindRole).toString().toStdString(), "camera");
  EXPECT_TRUE(model.data(model.index(2, 0), data_recorder::TopicListModel::IsCameraRole).toBool());
  EXPECT_FALSE(model.data(model.index(2, 0), data_recorder::TopicListModel::IsDrawableRole).toBool());
}

TEST(TopicListModel, UpdateStatsBackfillsFrequencyAndResolution)
{
  data_recorder::TopicListModel model;
  model.set_topics({
    []{ data_recorder::TopicEntry t; t.topic_name="/camera/image_raw"; t.backend_name="video";
        t.ui_category=data_recorder::TopicUiCategory::CameraPreview; return t; }(),
  });
  // 初始 frequency_text 为空（无占位）
  const auto idx = model.index(0, 0);
  // 注入 stats
  model.updateStats("/camera/image_raw", 22.0, 848, 480);
  EXPECT_EQ(model.data(idx, data_recorder::TopicListModel::FrequencyTextRole).toString().toStdString(), "22 fps");
  EXPECT_EQ(model.data(idx, data_recorder::TopicListModel::ResolutionTextRole).toString().toStdString(), "848x480");
}

TEST(TopicListModel, NumericTopicShowsHzNotFps)
{
  data_recorder::TopicListModel model;
  model.set_topics({
    []{ data_recorder::TopicEntry t; t.topic_name="/joint_states"; t.backend_name="rosbag";
        t.ui_category=data_recorder::TopicUiCategory::NumericTrack; return t; }(),
  });
  model.updateStats("/joint_states", 400.0, 0, 0);
  EXPECT_EQ(model.data(model.index(0,0), data_recorder::TopicListModel::FrequencyTextRole).toString().toStdString(), "400 Hz");
}

TEST(TopicListModel, CountsVisibleCameraRows)
{
  data_recorder::TopicEntry camera_topic;
  camera_topic.topic_name = "/camera/image_raw";
  camera_topic.backend_name = "video";
  camera_topic.ui_category = data_recorder::TopicUiCategory::CameraPreview;

  data_recorder::TopicEntry other_camera_topic = camera_topic;
  other_camera_topic.topic_name = "/left_camera/image_raw";

  data_recorder::TopicListModel model;
  model.set_topics({camera_topic, other_camera_topic});

  EXPECT_EQ(model.visibleCameraCount(), 2);
  model.toggleVisible(0);
  EXPECT_EQ(model.visibleCameraCount(), 1);
  model.toggleVisible(1);
  EXPECT_EQ(model.visibleCameraCount(), 0);
}

TEST(TagListModel, StartsWithNoSelection)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}});

  EXPECT_FALSE(model.data(model.index(0, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
  EXPECT_FALSE(model.data(model.index(1, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
}

TEST(TagListModel, SelectsOneTag)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}});

  model.select(1);

  EXPECT_FALSE(model.data(model.index(0, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
  EXPECT_TRUE(model.data(model.index(1, 0), data_recorder::TagListModel::IsSelectedRole).toBool());
}

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

TEST(EventMarkerModel, IgnoresDuplicatePointAndRangeInstances)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({
    {"1", "拿起水杯", "point", "#1763c9"},
    {"2", "倒水", "range", "#2f9e44"},
  });

  ASSERT_TRUE(model.addPoint(0, 3.25));
  ASSERT_TRUE(model.addPoint(0, 3.25));

  const auto point_row = model.index(0, 0);
  EXPECT_EQ(model.data(point_row, data_recorder::EventMarkerModel::CountRole).toInt(), 1);
  EXPECT_EQ(
    model.data(point_row, data_recorder::EventMarkerModel::InstancesRole).toList().size(), 1);

  ASSERT_TRUE(model.toggleRange(1, 9.0));
  ASSERT_TRUE(model.toggleRange(1, 4.0));
  ASSERT_TRUE(model.toggleRange(1, 4.0));
  ASSERT_TRUE(model.toggleRange(1, 9.0));

  const auto range_row = model.index(1, 0);
  EXPECT_FALSE(
    model.data(range_row, data_recorder::EventMarkerModel::HasPendingRangeStartRole).toBool());
  EXPECT_EQ(model.data(range_row, data_recorder::EventMarkerModel::CountRole).toInt(), 1);

  const QVariantList ranges =
    model.data(range_row, data_recorder::EventMarkerModel::InstancesRole).toList();
  ASSERT_EQ(ranges.size(), 1);
  EXPECT_DOUBLE_EQ(
    ranges.at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 4.0);
  EXPECT_DOUBLE_EQ(ranges.at(0).toMap().value(QStringLiteral("endSeconds")).toDouble(), 9.0);
}

TEST(EventMarkerModel, DeleteAllInstancesClearsInstancesAndPendingRangeStart)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({{"2", "倒水", "range", "#2f9e44"}});

  ASSERT_TRUE(model.toggleRange(0, 2.0));
  ASSERT_TRUE(model.toggleRange(0, 5.0));
  ASSERT_TRUE(model.toggleRange(0, 7.0));

  const auto row = model.index(0, 0);
  EXPECT_TRUE(
    model.data(row, data_recorder::EventMarkerModel::HasPendingRangeStartRole).toBool());
  EXPECT_EQ(model.data(row, data_recorder::EventMarkerModel::CountRole).toInt(), 1);

  ASSERT_TRUE(model.deleteAllInstances(0));

  EXPECT_FALSE(
    model.data(row, data_recorder::EventMarkerModel::HasPendingRangeStartRole).toBool());
  EXPECT_DOUBLE_EQ(
    model.data(row, data_recorder::EventMarkerModel::PendingStartSecondsRole).toDouble(), 0.0);
  EXPECT_EQ(model.data(row, data_recorder::EventMarkerModel::CountRole).toInt(), 0);
  EXPECT_TRUE(model.data(row, data_recorder::EventMarkerModel::InstancesRole).toList().isEmpty());
  EXPECT_EQ(
    model.data(row, data_recorder::EventMarkerModel::ActionTextRole).toString().toStdString(),
    "添加起点 (2)");
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

TEST(EventMarkerModel, ExportsAnnotationSnapshotWithMultipleSameName)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({
    {"c", "碰撞", "point", "#e03131"},
    {"2", "倒水", "range", "#2f9e44"},
  });
  model.triggerShortcut("c", 8.04);   // point 1
  model.triggerShortcut("c", 12.88);  // point 2（同名）
  model.toggleRange(1, 5.0);          // range 起
  model.toggleRange(1, 9.3);          // range 止

  const auto annotations = model.exportAnnotations();
  int collision = 0; bool has_range = false;
  for (const auto & a : annotations) {
    if (a.name == "碰撞") { ++collision; }
    if (a.kind == "range" && a.name == "倒水") { has_range = true; EXPECT_NEAR(a.end, 9.3, 1e-6); }
  }
  EXPECT_EQ(collision, 2);
  EXPECT_TRUE(has_range);
}

TEST(TagListModel, ExportsSelectedTag)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}, {"失败", "#e03131"}});
  model.select(0);
  const auto tags = model.exportSelectedTags();
  ASSERT_EQ(tags.size(), 1u);
  EXPECT_EQ(tags.front().name, "成功");
}

TEST(TagListModel, ClearSelectionEmptiesExport)
{
  data_recorder::TagListModel model;
  model.set_tags({{"成功", "#2f9e44"}});
  model.select(0);
  ASSERT_EQ(model.exportSelectedTags().size(), 1u);
  model.clearSelection();
  EXPECT_TRUE(model.exportSelectedTags().empty());
}

TEST(AppController, ExposesInitialState)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  EXPECT_EQ(controller.configPath().toStdString(), config.config_path);
  EXPECT_EQ(controller.outputDirectory().toStdString(), config.output_dir);
  EXPECT_EQ(controller.statusText().toStdString(), "实时查看");
  EXPECT_FALSE(controller.recording());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 0.0);
}

TEST(AppController, StartsInOnlineDataSourceState)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  EXPECT_FALSE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), -1);
  EXPECT_TRUE(controller.canRecord());
  EXPECT_EQ(controller.statusText().toStdString(), "实时查看");
  EXPECT_EQ(controller.modeText().toStdString(), "实时查看");
}

TEST(AppController, SelectingHistoryDisablesRecordingAndUpdatesStatus)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);
  seed_history_sessions(controller);

  controller.selectHistorySession(0);

  EXPECT_TRUE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), 0);
  EXPECT_FALSE(controller.canRecord());
  EXPECT_EQ(controller.statusText().toStdString(), "历史查看：2026-05-31_07-46-20");
  EXPECT_EQ(controller.modeText().toStdString(), "历史查看");

  controller.toggleRecording();
  EXPECT_FALSE(controller.recording());
}

TEST(AppController, SelectingOnlineDataRestoresRecordingAvailability)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);
  seed_history_sessions(controller);

  controller.selectHistorySession(1);
  controller.selectOnlineData();

  EXPECT_FALSE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), -1);
  EXPECT_TRUE(controller.canRecord());
  EXPECT_EQ(controller.statusText().toStdString(), "实时查看");
}

TEST(AppController, DataSourceSelectionEmitsOnlyChangedPropertySignals)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);
  seed_history_sessions(controller);

  int data_source_changed_count = 0;
  int can_record_changed_count = 0;
  int recording_changed_count = 0;
  int status_text_changed_count = 0;
  int mode_text_changed_count = 0;
  int following_live_edge_changed_count = 0;
  QObject::connect(
    &controller,
    &data_recorder::AppController::dataSourceChanged,
    [&data_source_changed_count]() {
      ++data_source_changed_count;
    });
  QObject::connect(
    &controller,
    &data_recorder::AppController::canRecordChanged,
    [&can_record_changed_count]() {
      ++can_record_changed_count;
    });
  QObject::connect(
    &controller,
    &data_recorder::AppController::recordingChanged,
    [&recording_changed_count]() {
      ++recording_changed_count;
    });
  QObject::connect(
    &controller,
    &data_recorder::AppController::statusTextChanged,
    [&status_text_changed_count]() {
      ++status_text_changed_count;
    });
  QObject::connect(
    &controller,
    &data_recorder::AppController::modeTextChanged,
    [&mode_text_changed_count]() {
      ++mode_text_changed_count;
    });
  QObject::connect(
    &controller,
    &data_recorder::AppController::followingLiveEdgeChanged,
    [&following_live_edge_changed_count]() {
      ++following_live_edge_changed_count;
    });

  controller.selectHistorySession(0);
  EXPECT_TRUE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), 0);
  EXPECT_FALSE(controller.canRecord());
  EXPECT_EQ(data_source_changed_count, 1);
  EXPECT_EQ(can_record_changed_count, 1);
  EXPECT_EQ(status_text_changed_count, 1);
  EXPECT_EQ(mode_text_changed_count, 1);
  EXPECT_EQ(following_live_edge_changed_count, 0);

  controller.toggleRecording();
  EXPECT_FALSE(controller.recording());
  EXPECT_EQ(data_source_changed_count, 1);
  EXPECT_EQ(can_record_changed_count, 1);
  EXPECT_EQ(recording_changed_count, 0);
  EXPECT_EQ(status_text_changed_count, 1);
  EXPECT_EQ(mode_text_changed_count, 1);
  EXPECT_EQ(following_live_edge_changed_count, 0);

  controller.selectHistorySession(0);
  controller.selectHistorySession(-1);
  controller.selectHistorySession(controller.recordingSessionModel()->rowCount());
  EXPECT_EQ(controller.selectedSessionRow(), 0);
  EXPECT_EQ(data_source_changed_count, 1);
  EXPECT_EQ(can_record_changed_count, 1);
  EXPECT_EQ(status_text_changed_count, 1);
  EXPECT_EQ(mode_text_changed_count, 1);
  EXPECT_EQ(following_live_edge_changed_count, 0);

  controller.selectHistorySession(1);
  EXPECT_TRUE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), 1);
  EXPECT_EQ(controller.statusText().toStdString(), "历史查看：2026-05-31_07-47-06");
  EXPECT_EQ(data_source_changed_count, 2);
  EXPECT_EQ(can_record_changed_count, 1);
  EXPECT_EQ(status_text_changed_count, 2);
  EXPECT_EQ(mode_text_changed_count, 1);
  EXPECT_EQ(following_live_edge_changed_count, 0);

  controller.selectOnlineData();
  EXPECT_FALSE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), -1);
  EXPECT_TRUE(controller.canRecord());
  EXPECT_EQ(controller.statusText().toStdString(), "实时查看");
  EXPECT_EQ(data_source_changed_count, 3);
  EXPECT_EQ(can_record_changed_count, 2);
  EXPECT_EQ(status_text_changed_count, 3);
  EXPECT_EQ(mode_text_changed_count, 2);
  EXPECT_EQ(following_live_edge_changed_count, 0);

  controller.selectOnlineData();
  EXPECT_EQ(data_source_changed_count, 3);
  EXPECT_EQ(can_record_changed_count, 2);
  EXPECT_EQ(status_text_changed_count, 3);
  EXPECT_EQ(mode_text_changed_count, 2);
  EXPECT_EQ(following_live_edge_changed_count, 0);
}

TEST(AppController, HistorySelectionNoOpsWhileRecording)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  int data_source_changed_count = 0;
  int can_record_changed_count = 0;
  int status_text_changed_count = 0;
  int mode_text_changed_count = 0;
  QObject::connect(
    &controller,
    &data_recorder::AppController::dataSourceChanged,
    [&data_source_changed_count]() {
      ++data_source_changed_count;
    });
  QObject::connect(
    &controller,
    &data_recorder::AppController::canRecordChanged,
    [&can_record_changed_count]() {
      ++can_record_changed_count;
    });
  QObject::connect(
    &controller,
    &data_recorder::AppController::statusTextChanged,
    [&status_text_changed_count]() {
      ++status_text_changed_count;
    });
  QObject::connect(
    &controller,
    &data_recorder::AppController::modeTextChanged,
    [&mode_text_changed_count]() {
      ++mode_text_changed_count;
    });

  controller.toggleRecording();
  EXPECT_TRUE(controller.recording());
  EXPECT_TRUE(controller.canRecord());
  EXPECT_EQ(status_text_changed_count, 1);
  EXPECT_EQ(mode_text_changed_count, 1);

  controller.selectHistorySession(0);

  EXPECT_FALSE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), -1);
  EXPECT_TRUE(controller.canRecord());
  EXPECT_TRUE(controller.recording());
  EXPECT_EQ(data_source_changed_count, 0);
  EXPECT_EQ(can_record_changed_count, 0);
  EXPECT_EQ(status_text_changed_count, 1);
  EXPECT_EQ(mode_text_changed_count, 1);
}

TEST(AppController, RecordingReviewStateHasDistinctStatusText)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.toggleRecording();
  controller.advanceLiveEdge(10.0);
  EXPECT_EQ(controller.statusText().toStdString(), "录制中");
  EXPECT_EQ(controller.modeText().toStdString(), "录制中");

  controller.setPlayheadSeconds(4.0);
  EXPECT_TRUE(controller.recording());
  EXPECT_FALSE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 4.0);
  EXPECT_EQ(controller.statusText().toStdString(), "录制中回看");
  EXPECT_EQ(controller.modeText().toStdString(), "录制中回看");

  controller.returnToLiveEdge();
  EXPECT_TRUE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 10.0);
  EXPECT_EQ(controller.statusText().toStdString(), "录制中");
}

TEST(AppController, ExposesPopulatedModels)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  ASSERT_NE(controller.topicModel(), nullptr);
  ASSERT_NE(controller.tagModel(), nullptr);
  ASSERT_NE(controller.eventMarkerModel(), nullptr);
  ASSERT_NE(controller.recordingSessionModel(), nullptr);

  EXPECT_EQ(controller.topicModel()->rowCount(), 2);
  EXPECT_EQ(
    controller.topicModel()
      ->data(controller.topicModel()->index(0, 0), data_recorder::TopicListModel::TopicNameRole)
      .toString()
      .toStdString(),
    "/joint_states");
  EXPECT_EQ(controller.tagModel()->rowCount(), 2);
  EXPECT_EQ(controller.eventMarkerModel()->rowCount(), 3);
  EXPECT_EQ(controller.recordingSessionModel()->rowCount(), 0);
}

TEST(RecordingSessionModel, ExposesFolderDurationSizeAndTagRoles)
{
  data_recorder::RecordingSessionModel model;
  data_recorder::SessionRecord r;
  r.session_id = "2026-05-31_07-46-20";
  r.directory = "/tmp/x/2026-05-31_07-46-20";
  r.duration_seconds = 24.123;
  r.size_bytes = 1288490188;  // ~1.2 GiB
  r.tags = {{"成功", "#2f9e44"}};
  model.setSessions({r});

  ASSERT_GT(model.rowCount(), 0);
  const auto row = model.index(0, 0);
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::FolderNameRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::ShortDurationRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::FullDurationRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::SizeTextRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::TagNameRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::TagColorRole).toString().isEmpty());
}

TEST(RecordingSessionModel, SetSessionsPopulatesRows)
{
  data_recorder::RecordingSessionModel model;
  data_recorder::SessionRecord r;
  r.session_id = "2026-06-22_14-30-05";
  r.directory = "/tmp/x/2026-06-22_14-30-05";
  r.duration_seconds = 65.0;  // 1:05
  r.size_bytes = 256 * 1024 * 1024;  // 256 MiB
  r.tags = {{"成功", "#2f9e44"}};
  model.setSessions({r});

  ASSERT_EQ(model.rowCount(), 1);
  const auto idx = model.index(0, 0);
  EXPECT_EQ(model.data(idx, data_recorder::RecordingSessionModel::FolderNameRole).toString().toStdString(),
    "2026-06-22_14-30-05");
  EXPECT_EQ(model.data(idx, data_recorder::RecordingSessionModel::TagNameRole).toString().toStdString(), "成功");
  // 时长格式化为 mm:ss 含 1:05
  EXPECT_NE(model.data(idx, data_recorder::RecordingSessionModel::ShortDurationRole).toString().indexOf("1:05"), -1);
}

TEST(AppController, ToggleRecordingUpdatesStateAndEmitsSignals)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  int recording_changed_count = 0;
  int status_text_changed_count = 0;
  QObject::connect(
    &controller, &data_recorder::AppController::recordingChanged, [&recording_changed_count]() {
      ++recording_changed_count;
    });
  QObject::connect(
    &controller, &data_recorder::AppController::statusTextChanged, [&status_text_changed_count]() {
      ++status_text_changed_count;
    });

  controller.toggleRecording();

  EXPECT_TRUE(controller.recording());
  EXPECT_EQ(controller.statusText().toStdString(), "录制中");
  EXPECT_EQ(recording_changed_count, 1);
  EXPECT_EQ(status_text_changed_count, 1);
}

TEST(AppController, SetPlayheadSecondsUpdatesClampsAndEmits)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  int playhead_changed_count = 0;
  QObject::connect(
    &controller,
    &data_recorder::AppController::playheadSecondsChanged,
    [&playhead_changed_count]() {
      ++playhead_changed_count;
    });

  controller.setPlayheadSeconds(2.5);
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 2.5);
  EXPECT_EQ(playhead_changed_count, 1);

  controller.setPlayheadSeconds(-1.0);
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 0.0);
  EXPECT_EQ(playhead_changed_count, 2);
}

TEST(AppController, RecordingStartsFollowingLiveEdge)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.toggleRecording();
  controller.advanceLiveEdge(3.5);

  EXPECT_TRUE(controller.recording());
  EXPECT_TRUE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.liveEdgeSeconds(), 3.5);
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 3.5);
  EXPECT_EQ(controller.modeText().toStdString(), "录制中");
}

TEST(AppController, ScrubbingDuringRecordingDetachesAndCanReturnToLive)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.toggleRecording();
  controller.advanceLiveEdge(10.0);
  controller.setPlayheadSeconds(4.0);

  EXPECT_FALSE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 4.0);

  controller.advanceLiveEdge(12.0);
  EXPECT_DOUBLE_EQ(controller.liveEdgeSeconds(), 12.0);
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 4.0);

  controller.returnToLiveEdge();
  EXPECT_TRUE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 12.0);
}

TEST(AppController, TimelineDurationDefaultsToSixtySecondSpan)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  // 默认标尺长度 60 秒：录制开始前，时间轴总长固定为 60。
  EXPECT_DOUBLE_EQ(controller.timelineDurationSeconds(), 60.0);
}

TEST(AppController, TimelineDurationGrowsWithLiveEdgeAndEmitsOnlyWhenSpanChanges)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  int timeline_duration_changed_count = 0;
  QObject::connect(
    &controller,
    &data_recorder::AppController::timelineDurationSecondsChanged,
    [&timeline_duration_changed_count]() {
      ++timeline_duration_changed_count;
    });

  controller.toggleRecording();

  // 实时端仍在默认 60 秒标尺内 → 总长不变、不发信号。
  controller.advanceLiveEdge(45.0);
  EXPECT_DOUBLE_EQ(controller.timelineDurationSeconds(), 60.0);
  EXPECT_EQ(timeline_duration_changed_count, 0);

  // 实时端超过默认标尺 → 总长随之增长并发一次信号。
  controller.advanceLiveEdge(120.0);
  EXPECT_DOUBLE_EQ(controller.timelineDurationSeconds(), 120.0);
  EXPECT_EQ(timeline_duration_changed_count, 1);
}

TEST(AppController, PlayheadBeyondLiveEdgeStillExtendsTimelineDuration)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  // 即便实时端为 0，把播放头拖到远处也应让总长覆盖播放头，避免播放头脱离可寻址区间。
  controller.setPlayheadSeconds(95.0);
  EXPECT_DOUBLE_EQ(controller.timelineDurationSeconds(), 95.0);
}

TEST(AppController, DetachFromLiveEdgeStopsFollowingWithoutMovingPlayhead)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.toggleRecording();
  controller.advanceLiveEdge(40.0);
  ASSERT_TRUE(controller.followingLiveEdge());
  ASSERT_DOUBLE_EQ(controller.playheadSeconds(), 40.0);

  controller.detachFromLiveEdge();

  EXPECT_FALSE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 40.0);  // 播放头不移动，只脱离实时端
  EXPECT_EQ(controller.statusText().toStdString(), "录制中回看");

  // 已经脱离时再次调用应为无操作。
  controller.detachFromLiveEdge();
  EXPECT_FALSE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 40.0);
}

TEST(AppController, DetachFromLiveEdgeIsNoOpWhenNotRecording)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  int following_changed_count = 0;
  QObject::connect(
    &controller,
    &data_recorder::AppController::followingLiveEdgeChanged,
    [&following_changed_count]() {
      ++following_changed_count;
    });

  controller.detachFromLiveEdge();

  EXPECT_FALSE(controller.followingLiveEdge());
  EXPECT_EQ(following_changed_count, 0);
}

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
  EXPECT_DOUBLE_EQ(
    controller.eventMarkerModel()
      ->data(range_row, data_recorder::EventMarkerModel::PendingStartSecondsRole)
      .toDouble(),
    2.0);
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

  const QVariantList instances =
    controller.eventMarkerModel()
      ->data(range_row, data_recorder::EventMarkerModel::InstancesRole)
      .toList();
  ASSERT_EQ(instances.size(), 1);
  const QVariantMap instance = instances.at(0).toMap();
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("startSeconds")).toDouble(), 2.0);
  EXPECT_DOUBLE_EQ(instance.value(QStringLiteral("endSeconds")).toDouble(), 4.5);
}

TEST(AppController, DirectTopicModelToggleEmitsVisibleCameraCountChanged)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  int signal_count = 0;
  QObject::connect(
    &controller,
    &data_recorder::AppController::visibleCameraCountChanged,
    [&signal_count]() {
      ++signal_count;
    });

  EXPECT_EQ(controller.visibleCameraCount(), 1);
  controller.topicModel()->toggleVisible(1);

  EXPECT_EQ(controller.visibleCameraCount(), 0);
  EXPECT_EQ(signal_count, 1);
}

TEST(AppController, ToggleTopicVisibleEmitsVisibleCameraCountChangedOnce)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  int signal_count = 0;
  QObject::connect(
    &controller,
    &data_recorder::AppController::visibleCameraCountChanged,
    [&signal_count]() {
      ++signal_count;
    });

  controller.toggleTopicVisible(1);

  EXPECT_EQ(controller.visibleCameraCount(), 0);
  EXPECT_EQ(signal_count, 1);
}

TEST(AppController, EventFilterHandlesRecordingAndMarkerShortcuts)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  QKeyEvent space_event(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
  EXPECT_TRUE(controller.eventFilter(nullptr, &space_event));
  EXPECT_TRUE(controller.recording());
  EXPECT_TRUE(space_event.isAccepted());

  QKeyEvent marker_event(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier, QStringLiteral("1"));
  EXPECT_TRUE(controller.eventFilter(nullptr, &marker_event));
  EXPECT_EQ(
    controller.eventMarkerModel()
      ->data(controller.eventMarkerModel()->index(0, 0), data_recorder::EventMarkerModel::CountRole)
      .toInt(),
    1);
  EXPECT_TRUE(marker_event.isAccepted());
}

TEST(AppController, EventFilterIgnoresAutoRepeatAndUnknownKeys)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  QKeyEvent repeat_space_event(
    QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), true);
  EXPECT_FALSE(controller.eventFilter(nullptr, &repeat_space_event));
  EXPECT_FALSE(controller.recording());

  QKeyEvent unknown_event(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, QStringLiteral("z"));
  EXPECT_FALSE(controller.eventFilter(nullptr, &unknown_event));
  EXPECT_EQ(
    controller.eventMarkerModel()
      ->data(controller.eventMarkerModel()->index(0, 0), data_recorder::EventMarkerModel::CountRole)
      .toInt(),
    0);
}

TEST(AppController, EventFilterIgnoresModifiedShortcuts)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  QKeyEvent ctrl_space_event(
    QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier, QStringLiteral(" "));
  EXPECT_FALSE(controller.eventFilter(nullptr, &ctrl_space_event));
  EXPECT_FALSE(controller.recording());

  QKeyEvent alt_space_event(
    QEvent::KeyPress, Qt::Key_Space, Qt::AltModifier, QStringLiteral(" "));
  EXPECT_FALSE(controller.eventFilter(nullptr, &alt_space_event));
  EXPECT_FALSE(controller.recording());

  QKeyEvent ctrl_marker_event(
    QEvent::KeyPress, Qt::Key_1, Qt::ControlModifier, QStringLiteral("1"));
  EXPECT_FALSE(controller.eventFilter(nullptr, &ctrl_marker_event));
  EXPECT_EQ(
    controller.eventMarkerModel()
      ->data(controller.eventMarkerModel()->index(0, 0), data_recorder::EventMarkerModel::CountRole)
      .toInt(),
    0);
}

TEST(LiveBridgeTest, PlaybackModeGatesLiveButAllowsPlayback)
{
  data_recorder::LiveBridge bridge;
  const QString key = "/camera/image_raw";
  auto img = std::make_shared<QImage>(4, 4, QImage::Format_RGB888);

  bridge.push_frame(key, img);
  ASSERT_NE(bridge.latest_frame(key), nullptr);

  bridge.set_playback_mode(true);
  auto live2 = std::make_shared<QImage>(8, 8, QImage::Format_RGB888);
  bridge.push_frame(key, live2);
  EXPECT_EQ(bridge.latest_frame(key)->width(), 4);  // still old frame

  auto play = std::make_shared<QImage>(16, 16, QImage::Format_RGB888);
  bridge.push_playback_frame(key, play);
  EXPECT_EQ(bridge.latest_frame(key)->width(), 16);

  bridge.set_playback_mode(false);
  bridge.push_frame(key, live2);
  EXPECT_EQ(bridge.latest_frame(key)->width(), 8);
}
