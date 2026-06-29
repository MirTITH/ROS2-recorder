#include <gtest/gtest.h>

#include <QApplication>
#include <QJSValue>
#include <QMetaObject>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QList>
#include <QVariantMap>
#include <QWindow>

#include <memory>
#include <vector>

#include "data_recorder/app_controller.hpp"

namespace
{

data_recorder::ConfigData make_config_fixture()
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

  data_recorder::ConfigData config;
  config.config_path = "/tmp/data_recorder_qml_smoke.yaml";
  config.output_dir = "/tmp/recordings";
  config.topics = {tf_topic, joint_topic, camera_topic};
  config.tags = {{"成功", "#2f9e44"}};
  config.event_markers = {
    {"1", "拿起水杯", "point", "#1763c9"},
    {"2", "倒水", "range", "#2f9e44"},
    {"c", "碰撞", "point", "#e03131"},
  };
  return config;
}

// 录制会话模型不再带占位数据（见 Task 12），QML 历史行需先注入会话才能出现。
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

QObject * find_by_object_name(QObject * object, const QString & object_name)
{
  if (object == nullptr) {
    return nullptr;
  }
  if (object->objectName() == object_name) {
    return object;
  }

  if (auto * item = qobject_cast<QQuickItem *>(object)) {
    for (QQuickItem * child_item : item->childItems()) {
      if (auto * found = find_by_object_name(child_item, object_name)) {
        return found;
      }
    }
  }

  for (QObject * child : object->children()) {
    if (auto * found = find_by_object_name(child, object_name)) {
      return found;
    }
  }
  return nullptr;
}

QQuickItem * find_camera_preview_tile(QObject * object, const QString & topic_name)
{
  if (object == nullptr) {
    return nullptr;
  }

  const QVariant object_topic = object->property("topicName");
  const QVariant resolution_text = object->property("resolutionText");
  if (object_topic.isValid() && object_topic.toString() == topic_name &&
    resolution_text.isValid())
  {
    if (auto * item = qobject_cast<QQuickItem *>(object)) {
      return item;
    }
  }

  if (auto * item = qobject_cast<QQuickItem *>(object)) {
    for (QQuickItem * child_item : item->childItems()) {
      if (auto * found = find_camera_preview_tile(child_item, topic_name)) {
        return found;
      }
    }
  }

  for (QObject * child : object->children()) {
    if (auto * found = find_camera_preview_tile(child, topic_name)) {
      return found;
    }
  }
  return nullptr;
}

QObject * find_required(QObject * root, const QString & object_name)
{
  QObject * object = nullptr;
  for (int attempt = 0; attempt < 20 && object == nullptr; ++attempt) {
    QCoreApplication::processEvents();
    object = find_by_object_name(root, object_name);
    if (object == nullptr) {
      QTest::qWait(25);
    }
  }
  EXPECT_NE(object, nullptr) << object_name.toStdString();
  return object;
}

QQuickItem * find_required_camera_preview_tile(QObject * root, const QString & topic_name)
{
  QQuickItem * item = nullptr;
  for (int attempt = 0; attempt < 20 && item == nullptr; ++attempt) {
    QCoreApplication::processEvents();
    item = find_camera_preview_tile(root, topic_name);
    if (item == nullptr) {
      QTest::qWait(25);
    }
  }
  EXPECT_NE(item, nullptr) << topic_name.toStdString();
  return item;
}

int find_topic_row(data_recorder::TopicListModel * model, const QString & topic_name)
{
  if (model == nullptr) {
    return -1;
  }
  for (int row = 0; row < model->rowCount(); ++row) {
    if (model->data(model->index(row, 0), data_recorder::TopicListModel::TopicNameRole)
        .toString() == topic_name)
    {
      return row;
    }
  }
  return -1;
}

bool series_visible(data_recorder::TopicListModel * model, int topic_row, const QString & series_key)
{
  const auto series_list =
    model->data(model->index(topic_row, 0), data_recorder::TopicListModel::SeriesListRole)
      .toList();
  for (const auto & value : series_list) {
    const auto entry = value.toMap();
    if (entry.value("key").toString() == series_key) {
      return entry.value("visible").toBool();
    }
  }
  ADD_FAILURE() << "Series not found: " << series_key.toStdString();
  return false;
}

class QmlSmokeTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    static int argc = 1;
    static char app_name[] = "test_qml_smoke";
    static char * argv[] = {app_name, nullptr};
    if (QApplication::instance() == nullptr) {
      app_ = std::make_unique<QApplication>(argc, argv);
    }
  }

  static void TearDownTestSuite()
  {
    app_.reset();
  }

  void SetUp() override
  {
    controller_ = std::make_unique<data_recorder::AppController>(make_config_fixture());
    seed_history_sessions(*controller_);
    app_->installEventFilter(controller_.get());
    engine_ = std::make_unique<QQmlApplicationEngine>();
    engine_->rootContext()->setContextProperty("appController", controller_.get());
    engine_->addImportPath(QStringLiteral(DATA_RECORDER_QML_DIR));
    engine_->load(QUrl::fromLocalFile(QStringLiteral(DATA_RECORDER_QML_DIR) + "/Main.qml"));
    ASSERT_FALSE(engine_->rootObjects().isEmpty());
    root_ = engine_->rootObjects().front();
    ASSERT_NE(root_, nullptr);
    window_ = qobject_cast<QWindow *>(root_);
    ASSERT_NE(window_, nullptr);
    ASSERT_TRUE(QTest::qWaitForWindowExposed(window_));
    window_->requestActivate();
    QCoreApplication::processEvents();
    QObject * window_root = find_required(root_, "windowRoot");
    ASSERT_TRUE(QMetaObject::invokeMethod(window_root, "forceActiveFocus"));
    QCoreApplication::processEvents();
    QTest::qWait(25);
    auto * window_root_item = qobject_cast<QQuickItem *>(window_root);
    ASSERT_NE(window_root_item, nullptr);
    ASSERT_TRUE(window_root_item->hasActiveFocus());
  }

  void TearDown() override
  {
    if (controller_) {
      app_->removeEventFilter(controller_.get());
    }
    root_ = nullptr;
    engine_.reset();
    controller_.reset();
  }

  static std::unique_ptr<QApplication> app_;
  std::unique_ptr<data_recorder::AppController> controller_;
  std::unique_ptr<QQmlApplicationEngine> engine_;
  QObject * root_{nullptr};
  QWindow * window_{nullptr};
};

std::unique_ptr<QApplication> QmlSmokeTest::app_;

}  // namespace

TEST_F(QmlSmokeTest, LoadsMainWindowAndInteractiveControls)
{
  ASSERT_NE(find_required(root_, "primaryActionButton"), nullptr);
  ASSERT_NE(find_required(root_, "eventMarkerActionButton_c"), nullptr);
}

TEST_F(QmlSmokeTest, PrimaryActionButtonAndSpaceToggleRecording)
{
  QObject * primary_button = find_required(root_, "primaryActionButton");
  ASSERT_NE(primary_button, nullptr);

  EXPECT_FALSE(controller_->recording());
  ASSERT_TRUE(QMetaObject::invokeMethod(primary_button, "clicked"));
  EXPECT_TRUE(controller_->recording());

  QSignalSpy recording_spy(controller_.get(), &data_recorder::AppController::recordingChanged);
  QKeyEvent space_event(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
  EXPECT_TRUE(QCoreApplication::sendEvent(window_, &space_event));
  EXPECT_EQ(recording_spy.count(), 1);
  EXPECT_FALSE(controller_->recording());
}

TEST_F(QmlSmokeTest, DataSourceRowsSwitchBetweenOnlineAndHistoryState)
{
  auto * online_row = qobject_cast<QQuickItem *>(find_required(root_, "onlineDataSourceRow"));
  auto * history_row = qobject_cast<QQuickItem *>(find_required(root_, "historyDataSourceRow_0"));
  auto * primary_button_item = qobject_cast<QQuickItem *>(find_required(root_, "primaryActionButton"));
  ASSERT_NE(online_row, nullptr);
  ASSERT_NE(history_row, nullptr);
  ASSERT_NE(primary_button_item, nullptr);
  ASSERT_TRUE(online_row->isVisible());
  ASSERT_TRUE(history_row->isVisible());
  ASSERT_TRUE(primary_button_item->isVisible());
  ASSERT_GT(online_row->width(), 0.0);
  ASSERT_GT(online_row->height(), 0.0);
  ASSERT_GT(history_row->width(), 0.0);
  ASSERT_GT(history_row->height(), 0.0);
  ASSERT_GT(primary_button_item->width(), 0.0);
  ASSERT_GT(primary_button_item->height(), 0.0);

  EXPECT_FALSE(controller_->historyMode());
  EXPECT_EQ(controller_->statusText().toStdString(), "实时查看");
  EXPECT_TRUE(primary_button_item->property("enabled").toBool());

  QPoint history_position =
    history_row->mapToScene(QPointF(history_row->width() / 2.0, history_row->height() / 2.0))
      .toPoint();
  QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, history_position);
  QCoreApplication::processEvents();

  EXPECT_TRUE(controller_->historyMode());
  EXPECT_EQ(controller_->selectedSessionRow(), 0);
  EXPECT_EQ(controller_->statusText().toStdString(), "历史查看：2026-05-31_07-46-20");
  // In history mode the primary button shows 播放 and is always enabled.
  EXPECT_TRUE(primary_button_item->property("enabled").toBool());

  QSignalSpy recording_spy(controller_.get(), &data_recorder::AppController::recordingChanged);
  const QPoint primary_position =
    primary_button_item
      ->mapToScene(
        QPointF(primary_button_item->width() / 2.0, primary_button_item->height() / 2.0))
      .toPoint();
  QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, primary_position);
  QCoreApplication::processEvents();
  EXPECT_EQ(recording_spy.count(), 0);
  EXPECT_FALSE(controller_->recording());

  QPoint online_position =
    online_row->mapToScene(QPointF(online_row->width() / 2.0, online_row->height() / 2.0))
      .toPoint();
  QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, online_position);
  QCoreApplication::processEvents();

  EXPECT_FALSE(controller_->historyMode());
  EXPECT_EQ(controller_->selectedSessionRow(), -1);
  EXPECT_EQ(controller_->statusText().toStdString(), "实时查看");
  EXPECT_TRUE(primary_button_item->property("enabled").toBool());
}

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

TEST_F(QmlSmokeTest, CameraVisibilityButtonTogglesPreview)
{
  QObject * camera_visibility_button =
    find_required(root_, "cameraVisibilityButton_/camera/image_raw");
  ASSERT_NE(camera_visibility_button, nullptr);

  EXPECT_EQ(controller_->visibleCameraCount(), 1);
  QSignalSpy visible_camera_spy(
    controller_.get(), &data_recorder::AppController::visibleCameraCountChanged);

  ASSERT_TRUE(QMetaObject::invokeMethod(camera_visibility_button, "clicked"));

  EXPECT_EQ(visible_camera_spy.count(), 1);
  EXPECT_EQ(controller_->visibleCameraCount(), 0);
}

TEST_F(QmlSmokeTest, ClickingSeriesLegendChipTogglesVisibilityBothWays)
{
  auto * model = controller_->topicModel();
  const int joint_row = find_topic_row(model, "/joint_states");
  ASSERT_GE(joint_row, 0);

  QVariantMap type;
  type.insert("topicKey", "/joint_states");
  type.insert("rosType", "sensor_msgs/msg/JointState");
  controller_->onTopicTypesUpdated(QVariantList{type});

  QVariantMap series;
  series.insert("key", "pos/a");
  series.insert("xs", QVariant::fromValue(QList<double>{0.0}));
  series.insert("ys", QVariant::fromValue(QList<double>{1.0}));
  QVariantMap topic;
  topic.insert("topicKey", "/joint_states");
  topic.insert("messageDots", QVariantList{0.0});
  topic.insert("series", QVariantList{series});

  controller_->setTopicExpanded("/joint_states", true);
  controller_->onCurvesUpdated(QVariantList{topic});
  QCoreApplication::processEvents();
  QTest::qWait(25);

  auto * chip =
    qobject_cast<QQuickItem *>(find_required(root_, "seriesChip_/joint_states_pos/a"));
  ASSERT_NE(chip, nullptr);
  ASSERT_GT(chip->width(), 8.0);
  ASSERT_TRUE(series_visible(model, joint_row, "pos/a"));

  QPoint chip_text_position =
    chip->mapToScene(QPointF(chip->width() - 2.0, chip->height() / 2.0)).toPoint();
  QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, chip_text_position);
  QCoreApplication::processEvents();
  EXPECT_FALSE(series_visible(model, joint_row, "pos/a"));

  chip = qobject_cast<QQuickItem *>(find_required(root_, "seriesChip_/joint_states_pos/a"));
  ASSERT_NE(chip, nullptr);
  chip_text_position =
    chip->mapToScene(QPointF(chip->width() - 2.0, chip->height() / 2.0)).toPoint();
  QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, chip_text_position);
  QCoreApplication::processEvents();
  EXPECT_TRUE(series_visible(model, joint_row, "pos/a"));
}

TEST_F(QmlSmokeTest, ClickingScrolledSeriesLegendChipTogglesVisibility)
{
  auto * model = controller_->topicModel();
  const int joint_row = find_topic_row(model, "/joint_states");
  ASSERT_GE(joint_row, 0);

  QVariantMap type;
  type.insert("topicKey", "/joint_states");
  type.insert("rosType", "sensor_msgs/msg/JointState");
  controller_->onTopicTypesUpdated(QVariantList{type});

  QVariantList series_list;
  for (int index = 0; index < 30; ++index) {
    QVariantMap series;
    series.insert("key", QString("pos/joint_%1").arg(index, 2, 10, QLatin1Char('0')));
    series.insert("xs", QVariant::fromValue(QList<double>{0.0}));
    series.insert("ys", QVariant::fromValue(QList<double>{double(index)}));
    series_list.push_back(series);
  }

  QVariantMap topic;
  topic.insert("topicKey", "/joint_states");
  topic.insert("messageDots", QVariantList{0.0});
  topic.insert("series", series_list);

  controller_->setTopicExpanded("/joint_states", true);
  controller_->onCurvesUpdated(QVariantList{topic});
  QCoreApplication::processEvents();
  QTest::qWait(25);

  auto * chip =
    qobject_cast<QQuickItem *>(find_required(root_, "seriesChip_/joint_states_pos/joint_29"));
  ASSERT_NE(chip, nullptr);

  QQuickItem * flickable = chip;
  while (flickable != nullptr && !flickable->property("contentY").isValid()) {
    flickable = flickable->parentItem();
  }
  ASSERT_NE(flickable, nullptr);

  const double max_content_y =
    flickable->property("contentHeight").toDouble() - flickable->height();
  ASSERT_GT(max_content_y, 0.0);
  ASSERT_TRUE(flickable->setProperty("contentY", max_content_y));
  QCoreApplication::processEvents();
  QTest::qWait(25);

  ASSERT_TRUE(series_visible(model, joint_row, "pos/joint_29"));
  const QPoint chip_position =
    chip->mapToScene(QPointF(chip->width() / 2.0, chip->height() / 2.0)).toPoint();
  QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, chip_position);
  QCoreApplication::processEvents();
  EXPECT_FALSE(series_visible(model, joint_row, "pos/joint_29"));
}

TEST_F(QmlSmokeTest, PressingCameraPreviewDoesNotHideTile)
{
  QQuickItem * camera_tile = find_required_camera_preview_tile(root_, "/camera/image_raw");
  ASSERT_NE(camera_tile, nullptr);
  ASSERT_GT(camera_tile->width(), 0.0);
  ASSERT_GT(camera_tile->height(), 0.0);
  ASSERT_TRUE(camera_tile->isVisible());

  const QPoint window_position =
    camera_tile->mapToScene(QPointF(camera_tile->width() / 2.0, camera_tile->height() / 2.0))
      .toPoint();

  QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, window_position);
  QCoreApplication::processEvents();
  QTest::qWait(25);

  EXPECT_TRUE(camera_tile->isVisible());

  QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, window_position);
}

TEST_F(QmlSmokeTest, ShiftWheelPansTimelineWithoutMovingPlayhead)
{
  QObject * viewport = find_required(root_, "timelineViewport");
  auto * curve_mouse_area = qobject_cast<QQuickItem *>(find_required(root_, "timelineLaneMouseArea"));
  ASSERT_NE(curve_mouse_area, nullptr);

  QJSValue viewport_value = engine_->newQObject(viewport);
  QJSValue set_window = viewport_value.property(QStringLiteral("setWindow"));
  ASSERT_TRUE(set_window.isCallable());
  set_window.call({QJSValue(10.0), QJSValue(20.0)});

  controller_->setPlayheadSeconds(15.0);
  const double previous_playhead = controller_->playheadSeconds();
  const double previous_start = viewport->property("visibleStartSeconds").toDouble();

  const QPoint position =
    curve_mouse_area
      ->mapToScene(QPointF(curve_mouse_area->width() / 2.0, curve_mouse_area->height() / 2.0))
      .toPoint();
  QWheelEvent wheel_event(
    position,
    window_->mapToGlobal(position),
    QPoint(),
    QPoint(0, -120),
    Qt::NoButton,
    Qt::ShiftModifier,
    Qt::NoScrollPhase,
    false);
  EXPECT_TRUE(QCoreApplication::sendEvent(window_, &wheel_event));
  QCoreApplication::processEvents();

  EXPECT_DOUBLE_EQ(controller_->playheadSeconds(), previous_playhead);
  EXPECT_GT(viewport->property("visibleStartSeconds").toDouble(), previous_start);
}

TEST_F(QmlSmokeTest, PlayheadLineHidesOutsideVisibleWindow)
{
  QObject * viewport = find_required(root_, "timelineViewport");
  auto * curve_playhead = qobject_cast<QQuickItem *>(find_required(root_, "timelineLanePlayhead"));
  auto * ruler_playhead = qobject_cast<QQuickItem *>(find_required(root_, "timelineRulerPlayhead"));
  ASSERT_NE(curve_playhead, nullptr);
  ASSERT_NE(ruler_playhead, nullptr);

  QJSValue viewport_value = engine_->newQObject(viewport);
  QJSValue set_window = viewport_value.property(QStringLiteral("setWindow"));
  ASSERT_TRUE(set_window.isCallable());
  set_window.call({QJSValue(20.0), QJSValue(10.0)});

  controller_->setPlayheadSeconds(5.0);
  QCoreApplication::processEvents();

  EXPECT_FALSE(curve_playhead->isVisible());
  EXPECT_FALSE(ruler_playhead->isVisible());

  controller_->setPlayheadSeconds(25.0);
  QCoreApplication::processEvents();

  EXPECT_TRUE(curve_playhead->isVisible());
  EXPECT_TRUE(ruler_playhead->isVisible());
}

TEST_F(QmlSmokeTest, FollowingLiveEdgeKeepsPlayheadVisibleBeyondDefaultSpan)
{
  auto * curve_playhead = qobject_cast<QQuickItem *>(find_required(root_, "timelineLanePlayhead"));
  auto * ruler_playhead = qobject_cast<QQuickItem *>(find_required(root_, "timelineRulerPlayhead"));
  ASSERT_NE(curve_playhead, nullptr);
  ASSERT_NE(ruler_playhead, nullptr);

  controller_->toggleRecording();
  ASSERT_TRUE(controller_->followingLiveEdge());

  // 实时端越过默认 60 秒标尺，跟随模式应自动滚动窗口，使播放头始终可见。
  controller_->advanceLiveEdge(120.0);
  QCoreApplication::processEvents();

  EXPECT_GE(controller_->playheadSeconds(), 120.0);
  EXPECT_TRUE(curve_playhead->isVisible());
  EXPECT_TRUE(ruler_playhead->isVisible());
}

TEST_F(QmlSmokeTest, ManualPanWhileRecordingDetachesFromLiveEdge)
{
  auto * curve_mouse_area =
    qobject_cast<QQuickItem *>(find_required(root_, "timelineLaneMouseArea"));
  ASSERT_NE(curve_mouse_area, nullptr);

  controller_->toggleRecording();
  controller_->advanceLiveEdge(30.0);
  QCoreApplication::processEvents();
  ASSERT_TRUE(controller_->followingLiveEdge());

  const QPoint position =
    curve_mouse_area
      ->mapToScene(QPointF(curve_mouse_area->width() / 2.0, curve_mouse_area->height() / 2.0))
      .toPoint();
  QWheelEvent wheel_event(
    position,
    window_->mapToGlobal(position),
    QPoint(),
    QPoint(0, -120),
    Qt::NoButton,
    Qt::ShiftModifier,
    Qt::NoScrollPhase,
    false);
  EXPECT_TRUE(QCoreApplication::sendEvent(window_, &wheel_event));
  QCoreApplication::processEvents();

  // 手动平移视口即脱离实时端（进入“录制中回看”），不再被实时端拉回。
  EXPECT_FALSE(controller_->followingLiveEdge());
}
