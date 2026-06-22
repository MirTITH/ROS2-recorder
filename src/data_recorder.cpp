#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <rclcpp/rclcpp.hpp>

#include "data_recorder/app_controller.hpp"
#include "data_recorder/camera_image_provider.hpp"
#include "data_recorder/config_model.hpp"
#include "data_recorder/live_bridge.hpp"
#include "data_recorder/recorder_engine.hpp"
#include "data_recorder/session_manager.hpp"

namespace
{

void print_usage(const char * program_name)
{
  std::cerr
    << "Usage:\n"
    << "  source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \\\n"
    << "    --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml\n\n"
    << "Required ROS parameter:\n"
    << "  config_file: path to the YAML recorder configuration\n\n"
    << "Program: " << program_name << std::endl;
}

}  // namespace

int main(int argc, char ** argv)
{
  const auto non_ros_args = rclcpp::init_and_remove_ros_arguments(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("data_recorder_ui");
  node->declare_parameter<std::string>("config_file", "");
  const auto config_path = node->get_parameter("config_file").as_string();

  if (config_path.empty()) {
    print_usage(argv[0]);
    rclcpp::shutdown();
    return EXIT_FAILURE;
  }

  data_recorder::ConfigData config;
  try {
    config = data_recorder::ConfigModel().load_from_file(config_path);
  } catch (const data_recorder::ConfigError & error) {
    std::cerr << "Failed to load config: " << error.what() << "\n\n";
    print_usage(argv[0]);
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

  QQmlApplicationEngine qml_engine;
  qml_engine.addImageProvider(QStringLiteral("camera"),
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

  const auto package_share = QString::fromStdString(
    ament_index_cpp::get_package_share_directory("data_recorder"));
  const auto qml_dir = package_share + QStringLiteral("/qml");
  qml_engine.addImportPath(qml_dir);

  const QUrl main_qml = QUrl::fromLocalFile(qml_dir + QStringLiteral("/Main.qml"));
  QObject::connect(
    &qml_engine,
    &QQmlApplicationEngine::objectCreated,
    &app,
    [main_qml](QObject * object, const QUrl & object_url) {
      if (object == nullptr && object_url == main_qml) {
        QCoreApplication::exit(EXIT_FAILURE);
      }
    },
    Qt::QueuedConnection);
  qml_engine.load(main_qml);

  const int result = app.exec();

  // 干净退出：停 spin、join、shutdown
  spin_running.store(false);
  if (spin_thread.joinable()) {
    spin_thread.join();
  }
  rclcpp::shutdown();
  return result;
}
