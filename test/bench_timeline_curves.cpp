// 性能基准（手动运行，非 CI 测试）：在真实 QML 引擎下复现"展开 /joint_states 后曲线渲染"
// 的热路径，量化 GUI 线程 onCurvesUpdated（C++ QVariant 往返）与 QML rebuildCache / curvePaint。
//
// 用法：
//   DR_PROFILE=1 QT_QPA_PLATFORM=offscreen \
//     /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder/bench_timeline_curves [series] [points] [repaints]
// 默认 series=99 points=600 repaints=30（贴近真实 33 关节 × pos/vel/eff、历史预算 600）。
//
// 输出 [DR_PROFILE] 行：
//   onCurvesUpdated           —— C++ 把 QVariant 点解析回 C++ 并回填模型（含 updateSeries 重建 QVariant）
//   QML.rebuildCache <topic>  —— QML 把可见序列转 Float64Array + 求全局 min/max（每 seriesList 变化一次）
//   QML.curvePaint <topic>    —— 单个展开行的一次 Canvas 重绘（平移/缩放每帧触发）

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QTest>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "data_recorder/app_controller.hpp"

namespace
{

data_recorder::ConfigData make_config()
{
  data_recorder::TopicEntry joint;
  joint.topic_name = "/joint_states";
  joint.backend_name = "rosbag";
  joint.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::ConfigData config;
  config.config_path = "/tmp/bench_timeline.yaml";
  config.output_dir = "/tmp/recordings";
  config.topics = {joint};
  return config;
}

// 构造 series 条曲线、每条 points 个点的 /joint_states 负载（与 LiveBridge/HistoryLoader 契约一致）。
QVariantMap make_joint_payload(int series, int points)
{
  QVariantList series_arr;
  for (int s = 0; s < series; ++s) {
    QVariantList pts;
    for (int i = 0; i < points; ++i) {
      const double t = static_cast<double>(i) / static_cast<double>(points) * 12.0;  // 0..12s
      QVariantMap pt;
      pt.insert("x", t);
      pt.insert("y", std::sin(t + s * 0.1) * (1.0 + s % 5));
      pts.push_back(pt);
    }
    QVariantMap sm;
    // 一半 pos/（默认可见），一半 vel/（默认隐藏）—— 贴近真实可见/隐藏比例。
    const QString prefix = (s % 3 == 0) ? "pos/" : (s % 3 == 1 ? "vel/" : "eff/");
    sm.insert("key", prefix + QString("joint_%1").arg(s, 2, 10, QLatin1Char('0')));
    sm.insert("points", pts);
    series_arr.push_back(sm);
  }

  QVariantList dots;
  for (int i = 0; i < 600; ++i) { dots.push_back(static_cast<double>(i) / 600.0 * 12.0); }

  QVariantMap topic;
  topic.insert("topicKey", "/joint_states");
  topic.insert("messageDots", dots);
  topic.insert("series", series_arr);
  return topic;
}

QObject * find_by_name(QObject * obj, const QString & name)
{
  if (obj == nullptr) { return nullptr; }
  if (obj->objectName() == name) { return obj; }
  if (auto * item = qobject_cast<QQuickItem *>(obj)) {
    for (QQuickItem * c : item->childItems()) {
      if (auto * f = find_by_name(c, name)) { return f; }
    }
  }
  for (QObject * c : obj->children()) {
    if (auto * f = find_by_name(c, name)) { return f; }
  }
  return nullptr;
}

}  // namespace

int main(int argc, char ** argv)
{
  const int series = argc > 1 ? std::atoi(argv[1]) : 99;
  const int points = argc > 2 ? std::atoi(argv[2]) : 600;
  const int repaints = argc > 3 ? std::atoi(argv[3]) : 30;

  if (std::getenv("DR_PROFILE") == nullptr) {
    std::cerr << "WARNING: DR_PROFILE not set — no timing will be printed. "
                 "Re-run with DR_PROFILE=1.\n";
  }
  std::cerr << "bench_timeline_curves: series=" << series << " points=" << points
            << " repaints=" << repaints << " (total points=" << (long long)series * points << ")\n";

  QApplication app(argc, argv);
  auto controller = std::make_unique<data_recorder::AppController>(make_config());

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("appController", controller.get());
  engine.addImportPath(QStringLiteral(DATA_RECORDER_QML_DIR));
  engine.load(QUrl::fromLocalFile(QStringLiteral(DATA_RECORDER_QML_DIR) + "/Main.qml"));
  if (engine.rootObjects().isEmpty()) {
    std::cerr << "FAILED to load Main.qml\n";
    return 1;
  }
  QObject * root = engine.rootObjects().front();
  auto * window = qobject_cast<QWindow *>(root);
  if (window == nullptr || !QTest::qWaitForWindowExposed(window)) {
    std::cerr << "FAILED to expose window\n";
    return 1;
  }
  QCoreApplication::processEvents();

  // 公布类型 + 标记展开（展开行才会收 series 并渲染曲线）。
  QVariantMap type;
  type.insert("topicKey", "/joint_states");
  type.insert("rosType", "sensor_msgs/msg/JointState");
  controller->onTopicTypesUpdated(QVariantList{type});
  controller->setTopicExpanded("/joint_states", true);
  QCoreApplication::processEvents();

  const QVariantMap payload = make_joint_payload(series, points);

  // (1) 展开瞬间：onCurvesUpdated（C++ 往返）+ 触发 QML rebuildCache + 首帧 curvePaint。
  std::cerr << "--- expand: onCurvesUpdated + first paint ---\n";
  controller->onCurvesUpdated(QVariantList{payload});
  for (int i = 0; i < 10; ++i) { QCoreApplication::processEvents(); QTest::qWait(5); }

  // (2) 平移/缩放风暴：反复改 viewport，每次触发所有展开行重绘。
  std::cerr << "--- pan/zoom: " << repaints << " repaints ---\n";
  QObject * viewport = find_by_name(root, "timelineViewport");
  if (viewport != nullptr) {
    for (int i = 0; i < repaints; ++i) {
      const double start = (i % 10) * 0.5;     // 平移
      const double dur = 6.0 + (i % 5);        // 缩放
      viewport->setProperty("visibleStartSeconds", start);
      viewport->setProperty("visibleDurationSeconds", dur);
      QCoreApplication::processEvents();
      QTest::qWait(2);
    }
  } else {
    std::cerr << "WARN: timelineViewport not found; skipped pan/zoom\n";
  }

  for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); QTest::qWait(5); }
  std::cerr << "bench done.\n";
  return 0;
}
