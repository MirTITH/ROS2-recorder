#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <rclcpp/rclcpp.hpp>

#include "data_recorder/app_controller.hpp"
#include "data_recorder/config_model.hpp"

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
  data_recorder::AppController controller(config);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("appController", &controller);
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
  engine.addImportPath(qml_dir);

  const QUrl main_qml = QUrl::fromLocalFile(qml_dir + QStringLiteral("/Main.qml"));
  QObject::connect(
    &engine,
    &QQmlApplicationEngine::objectCreated,
    &app,
    [main_qml](QObject * object, const QUrl & object_url) {
      if (object == nullptr && object_url == main_qml) {
        QCoreApplication::exit(EXIT_FAILURE);
      }
    },
    Qt::QueuedConnection);
  engine.load(main_qml);

  const int result = app.exec();
  rclcpp::shutdown();
  return result;
}
