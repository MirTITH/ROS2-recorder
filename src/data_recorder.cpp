#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <string>
#include <thread>
#include <vector>

#include "data_recorder/app_controller.hpp"
#include "data_recorder/camera_image_provider.hpp"
#include "data_recorder/config_model.hpp"
#include "data_recorder/live_bridge.hpp"
#include "data_recorder/recorder_engine.hpp"
#include "data_recorder/session_manager.hpp"

namespace
{

void print_usage()
{
  std::cerr
    << "Usage:\n"
    << "  ros2 run data_recorder data_recorder --ros-args -p config_file:=<config_file_path>"
    << std::endl;
}

}  // namespace

int main(int argc, char ** argv)
{
  const auto non_ros_args = rclcpp::init_and_remove_ros_arguments(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("data_recorder_ui");
  node->declare_parameter<std::string>("config_file", "");
  const auto config_path = node->get_parameter("config_file").as_string();

  if (config_path.empty()) {
    print_usage();
    rclcpp::shutdown();
    return EXIT_FAILURE;
  }

  data_recorder::ConfigData config;
  try {
    config = data_recorder::ConfigModel().load_from_file(config_path);
  } catch (const data_recorder::ConfigError & error) {
    std::cerr << "Failed to load config: " << error.what() << "\n\n";
    print_usage();
    rclcpp::shutdown();
    return EXIT_FAILURE;
  }

  std::vector<QByteArray> qt_arg_storage;
  qt_arg_storage.reserve(non_ros_args.size());
  std::vector<char *> qt_argv;
  qt_argv.reserve(non_ros_args.size());
  for (const auto & arg : non_ros_args) {
    qt_arg_storage.push_back(QByteArray::fromStdString(arg));
    qt_argv.push_back(qt_arg_storage.back().data());
  }
  int qt_argc = static_cast<int>(qt_argv.size());
  QApplication app(qt_argc, qt_argv.data());

  data_recorder::LiveBridge bridge;
  data_recorder::SessionManager session_manager;
  data_recorder::RecorderEngine engine(node, config, &bridge, &session_manager);
  data_recorder::AppController controller(config, &bridge, &engine, &session_manager);
  app.installEventFilter(&controller);

  // 延后回复服务，让录制控制在 Qt 主线程执行，同时保持 ROS 订阅继续处理数据。
  using SetBool = std_srvs::srv::SetBool;
  const auto set_recording_service = node->create_service<SetBool>(
    "~/set_recording",
    [&controller](
      rclcpp::Service<SetBool>::SharedPtr service,
      std::shared_ptr<rmw_request_id_t> header, SetBool::Request::SharedPtr request) {
      QMetaObject::invokeMethod(&controller, [&controller, service, header, enabled = request->data]() {
        const auto error = controller.setRecording(enabled);
        SetBool::Response response;
        response.success = error.isEmpty();
        response.message = error.isEmpty() ?
          (controller.recording() ? "录制中" : "已停止录制") : error.toStdString();
        if (rclcpp::ok()) { service->send_response(*header, response); }
      }, Qt::QueuedConnection);
    });

  using Trigger = std_srvs::srv::Trigger;
  const auto toggle_recording_service = node->create_service<Trigger>(
    "~/toggle_recording",
    [&controller](
      rclcpp::Service<Trigger>::SharedPtr service,
      std::shared_ptr<rmw_request_id_t> header, Trigger::Request::SharedPtr) {
      QMetaObject::invokeMethod(&controller, [&controller, service, header]() {
        const auto error = controller.setRecording(!controller.recording());
        Trigger::Response response;
        response.success = error.isEmpty();
        response.message = error.isEmpty() ?
          (controller.recording() ? "录制中" : "已停止录制") : error.toStdString();
        if (rclcpp::ok()) { service->send_response(*header, response); }
      }, Qt::QueuedConnection);
    });

  const auto package_share =
    QString::fromStdString(ament_index_cpp::get_package_share_directory("data_recorder"));
  QObject::connect(&controller, &data_recorder::AppController::recordingChanged, &app,
    [&controller, node, package_share]() {
      const auto sound = controller.recording() ? "recording_started.wav" : "recording_stopped.wav";
      if (!QProcess::startDetached("paplay", {package_share + "/sounds/" + sound})) {
        RCLCPP_WARN(node->get_logger(), "无法播放录制提示音，请确认已安装 pulseaudio-utils");
      }
    });

  QQmlApplicationEngine qml_engine;
  qml_engine.addImageProvider(
    QStringLiteral("camera"),
    new data_recorder::CameraImageProvider(&bridge));  // 引擎接管所有权
  qml_engine.rootContext()->setContextProperty("appController", &controller);

  // 后台 ROS spin 线程
  std::atomic<bool> spin_running{true};
  std::thread spin_thread([node, &spin_running]() {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    while (spin_running.load() && rclcpp::ok()) {
      executor.spin_some(std::chrono::milliseconds(10));
    }
  });

  QTimer ros_shutdown_timer;
  QObject::connect(&ros_shutdown_timer, &QTimer::timeout, &app, [&app]() {
    if (!rclcpp::ok()) {
      app.quit();
    }
  });
  ros_shutdown_timer.start(100);
  QObject::connect(&app, &QCoreApplication::aboutToQuit,
    &controller, &data_recorder::AppController::shutdownRecording);

  const auto qml_dir = package_share + QStringLiteral("/qml");
  qml_engine.addImportPath(qml_dir);

  const QUrl main_qml = QUrl::fromLocalFile(qml_dir + QStringLiteral("/Main.qml"));
  QObject::connect(
    &qml_engine, &QQmlApplicationEngine::objectCreated, &app,
    [main_qml](QObject * object, const QUrl & object_url) {
      if (object == nullptr && object_url == main_qml) {
        QCoreApplication::exit(EXIT_FAILURE);
      }
    },
    Qt::QueuedConnection);
  qml_engine.load(main_qml);

  const int result = app.exec();

  // 先关闭写入准入并唤醒阻塞回调，再等待 ROS spin 线程退出。
  controller.shutdownRecording();
  spin_running.store(false);
  if (spin_thread.joinable()) {
    spin_thread.join();
  }
  rclcpp::shutdown();
  return result;
}
