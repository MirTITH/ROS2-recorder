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
  config.camera_topics = {camera_topic};
  config.track_topics = {tf_topic, joint_topic};
  config.tags = {{"成功", "#2f9e44"}};
  config.event_markers = {
    {"1", "拿起水杯", "point", "#1763c9"},
    {"2", "倒水", "range", "#2f9e44"},
    {"c", "碰撞", "point", "#e03131"},
  };
  return config;
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
  ASSERT_NE(find_required(root_, "recordButton"), nullptr);
  ASSERT_NE(find_required(root_, "eventMarkerActionButton_c"), nullptr);
}

TEST_F(QmlSmokeTest, RecordButtonAndSpaceToggleRecording)
{
  QObject * record_button = find_required(root_, "recordButton");
  ASSERT_NE(record_button, nullptr);

  EXPECT_FALSE(controller_->recording());
  ASSERT_TRUE(QMetaObject::invokeMethod(record_button, "clicked"));
  EXPECT_TRUE(controller_->recording());

  QSignalSpy recording_spy(controller_.get(), &data_recorder::AppController::recordingChanged);
  QKeyEvent space_event(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
  EXPECT_TRUE(QCoreApplication::sendEvent(window_, &space_event));
  EXPECT_EQ(recording_spy.count(), 1);
  EXPECT_FALSE(controller_->recording());
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
  auto * curve_mouse_area = qobject_cast<QQuickItem *>(find_required(root_, "timelineCurveMouseArea"));
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
  auto * curve_playhead = qobject_cast<QQuickItem *>(find_required(root_, "timelineCurvePlayhead"));
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
