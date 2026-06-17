#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QObject>
#include <QVector>

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
  config.camera_topics = {camera_topic};
  config.track_topics = {numeric_topic};
  config.tags = {{"成功", "#2f9e44"}, {"失败", "#e03131"}};
  config.event_markers = {{"1", "拿起水杯", "point", "#1763c9"}};
  return config;
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
  EXPECT_EQ(
    model.data(index, data_recorder::TopicListModel::CategoryRole).toString().toStdString(),
    "numeric");
  EXPECT_TRUE(model.data(index, data_recorder::TopicListModel::IsVisibleRole).toBool());
  EXPECT_FALSE(model.data(index, data_recorder::TopicListModel::SeriesRole).toList().isEmpty());
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
  EXPECT_GE(model.data(model.index(1, 0), data_recorder::TopicListModel::SeriesListRole).toList().size(), 2);

  EXPECT_EQ(model.data(model.index(2, 0), data_recorder::TopicListModel::TrackKindRole).toString().toStdString(), "camera");
  EXPECT_TRUE(model.data(model.index(2, 0), data_recorder::TopicListModel::IsCameraRole).toBool());
  EXPECT_FALSE(model.data(model.index(2, 0), data_recorder::TopicListModel::IsDrawableRole).toBool());
  EXPECT_FALSE(model.data(model.index(2, 0), data_recorder::TopicListModel::ResolutionTextRole).toString().isEmpty());
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

TEST(EventMarkerModel, ExposesShortcutAndKind)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({{"1", "拿起水杯", "point", "#1763c9"}});

  ASSERT_EQ(model.rowCount(), 1);
  EXPECT_EQ(
    model.data(model.index(0, 0), data_recorder::EventMarkerModel::ShortcutRole).toString().toStdString(),
    "1");
  EXPECT_EQ(
    model.data(model.index(0, 0), data_recorder::EventMarkerModel::KindRole).toString().toStdString(),
    "point");
}

TEST(EventMarkerModel, StartsWithNoSelection)
{
  data_recorder::EventMarkerModel model;
  model.set_markers({{"1", "拿起水杯", "point", "#1763c9"}, {"2", "倒水", "range", "#2f9e44"}});

  EXPECT_FALSE(
    model.data(model.index(0, 0), data_recorder::EventMarkerModel::IsSelectedRole).toBool());
  EXPECT_FALSE(
    model.data(model.index(1, 0), data_recorder::EventMarkerModel::IsSelectedRole).toBool());
}

TEST(AppController, ExposesInitialState)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  EXPECT_EQ(controller.configPath().toStdString(), config.config_path);
  EXPECT_EQ(controller.outputDirectory().toStdString(), config.output_dir);
  EXPECT_EQ(controller.statusText().toStdString(), "就绪");
  EXPECT_FALSE(controller.recording());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 0.0);
}

TEST(AppController, ExposesPopulatedModels)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  ASSERT_NE(controller.topicModel(), nullptr);
  ASSERT_NE(controller.cameraModel(), nullptr);
  ASSERT_NE(controller.trackModel(), nullptr);
  ASSERT_NE(controller.tagModel(), nullptr);
  ASSERT_NE(controller.eventMarkerModel(), nullptr);
  ASSERT_NE(controller.recordingSessionModel(), nullptr);

  EXPECT_EQ(controller.topicModel()->rowCount(), 2);
  EXPECT_EQ(controller.cameraModel()->rowCount(), 1);
  EXPECT_EQ(controller.trackModel()->rowCount(), 1);
  EXPECT_EQ(
    controller.trackModel()
      ->data(controller.trackModel()->index(0, 0), data_recorder::TopicListModel::TopicNameRole)
      .toString()
      .toStdString(),
    "/joint_states");
  EXPECT_EQ(controller.tagModel()->rowCount(), 2);
  EXPECT_EQ(controller.eventMarkerModel()->rowCount(), 1);
  EXPECT_GT(controller.recordingSessionModel()->rowCount(), 0);
}

TEST(RecordingSessionModel, ExposesFolderDurationSizeAndTagRoles)
{
  data_recorder::RecordingSessionModel model;

  ASSERT_GT(model.rowCount(), 0);
  const auto row = model.index(0, 0);
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::FolderNameRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::ShortDurationRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::FullDurationRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::SizeTextRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::TagNameRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::TagColorRole).toString().isEmpty());
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
  EXPECT_EQ(controller.statusText().toStdString(), "录制中（界面原型）");
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
