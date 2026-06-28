cmake_minimum_required(VERSION 3.16)
project(data_recorder)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_gtest REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(rosbag2_storage REQUIRED)
find_package(rosbag2_transport REQUIRED)
find_package(pluginlib REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(trajectory_msgs REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
  libavcodec libavformat libavutil libswscale)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Charts Widgets Test)

set(DATA_RECORDER_HEADERS
  include/data_recorder/app_controller.hpp
  include/data_recorder/camera_grid_model.hpp
  include/data_recorder/camera_image_provider.hpp
  include/data_recorder/config_model.hpp
  include/data_recorder/curve_payload.hpp
  include/data_recorder/history_curve_loader.hpp
  include/data_recorder/live_bridge.hpp
  include/data_recorder/path_utils.hpp
  include/data_recorder/recording_time.hpp
  include/data_recorder/recorder_engine.hpp
  include/data_recorder/recorder_types.hpp
  include/data_recorder/rosbag_writer.hpp
  include/data_recorder/session_manager.hpp
  include/data_recorder/session_player.hpp
  include/data_recorder/topic_rate_monitor.hpp
  include/data_recorder/topic_series.hpp
  include/data_recorder/ui_models.hpp
  include/data_recorder/value_extractor.hpp
  include/data_recorder/video_clip_reader.hpp
  include/data_recorder/video_recorder.hpp
  include/data_recorder/writer_queue.hpp
)

set(DATA_RECORDER_SOURCES
  src/app_controller.cpp
  src/camera_grid_model.cpp
  src/camera_image_provider.cpp
  src/config_model.cpp
  src/curve_payload.cpp
  src/history_curve_loader.cpp
  src/live_bridge.cpp
  src/recorder_engine.cpp
  src/rosbag_writer.cpp
  src/session_manager.cpp
  src/session_player.cpp
  src/topic_rate_monitor.cpp
  src/topic_series.cpp
  src/ui_models.cpp
  src/value_extractor.cpp
  src/video_clip_reader.cpp
  src/video_recorder.cpp
  src/writer_queue.cpp
)

add_library(data_recorder_core
  ${DATA_RECORDER_HEADERS}
  ${DATA_RECORDER_SOURCES}
)
target_include_directories(data_recorder_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(data_recorder_core
  yaml-cpp
  PkgConfig::LIBAV
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Charts
)
ament_target_dependencies(data_recorder_core rclcpp ament_index_cpp rosbag2_cpp rosbag2_storage pluginlib rosbag2_transport sensor_msgs geometry_msgs trajectory_msgs)

add_executable(data_recorder src/data_recorder.cpp)
target_link_libraries(data_recorder data_recorder_core)
target_link_libraries(data_recorder Qt6::Widgets)
ament_target_dependencies(data_recorder rclcpp ament_index_cpp)

install(TARGETS data_recorder data_recorder_core
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/
  DESTINATION include
)
install(DIRECTORY qml
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY docs
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  ament_add_gtest(test_config_model test/test_config_model.cpp)
  target_link_libraries(test_config_model data_recorder_core)

  ament_add_gtest(test_usage_help test/test_usage_help.cpp)
  target_compile_definitions(test_usage_help PRIVATE
    DATA_RECORDER_EXECUTABLE_PATH="$<TARGET_FILE:data_recorder>"
  )

  ament_add_gtest(test_qml_smoke test/test_qml_smoke.cpp
    ENV "QT_QPA_PLATFORM=offscreen"
  )
  target_link_libraries(test_qml_smoke data_recorder_core Qt6::Widgets Qt6::Test)
  target_compile_definitions(test_qml_smoke PRIVATE
    DATA_RECORDER_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
  )

  ament_add_gtest(test_qml_structure test/test_qml_structure.cpp)
  target_compile_definitions(test_qml_structure PRIVATE
    DATA_RECORDER_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    DATA_RECORDER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )

  ament_add_gtest(test_ui_models test/test_ui_models.cpp)
  target_link_libraries(test_ui_models data_recorder_core)

  ament_add_gtest(test_camera_grid_model test/test_camera_grid_model.cpp)
  target_link_libraries(test_camera_grid_model data_recorder_core)

  ament_add_gtest(test_topic_rate_monitor test/test_topic_rate_monitor.cpp)
  target_link_libraries(test_topic_rate_monitor data_recorder_core)

  ament_add_gtest(test_recording_time test/test_recording_time.cpp)
  target_link_libraries(test_recording_time data_recorder_core)

  ament_add_gtest(test_writer_queue test/test_writer_queue.cpp)
  target_link_libraries(test_writer_queue data_recorder_core)

  ament_add_gtest(test_session_manager test/test_session_manager.cpp)
  target_link_libraries(test_session_manager data_recorder_core)

  ament_add_gtest(test_video_recorder test/test_video_recorder.cpp)
  target_link_libraries(test_video_recorder data_recorder_core PkgConfig::LIBAV)

  ament_add_gtest(test_video_clip_reader test/test_video_clip_reader.cpp)
  target_link_libraries(test_video_clip_reader data_recorder_core PkgConfig::LIBAV)

  ament_add_gtest(test_session_player test/test_session_player.cpp
    ENV "QT_QPA_PLATFORM=offscreen"
  )
  target_link_libraries(test_session_player data_recorder_core PkgConfig::LIBAV Qt6::Test)

  find_package(std_msgs REQUIRED)
  ament_add_gtest(test_rosbag_writer test/test_rosbag_writer.cpp)
  target_link_libraries(test_rosbag_writer data_recorder_core)
  ament_target_dependencies(test_rosbag_writer rclcpp rosbag2_cpp std_msgs)

  ament_add_gtest(test_history_curve_loader test/test_history_curve_loader.cpp)
  target_link_libraries(test_history_curve_loader data_recorder_core Qt6::Test)
  ament_target_dependencies(test_history_curve_loader rclcpp rosbag2_cpp sensor_msgs)

  ament_add_gtest(test_value_extractor test/test_value_extractor.cpp)
  target_link_libraries(test_value_extractor data_recorder_core)
  ament_target_dependencies(test_value_extractor rclcpp sensor_msgs geometry_msgs trajectory_msgs)

  ament_add_gtest(test_topic_series test/test_topic_series.cpp)
  target_link_libraries(test_topic_series data_recorder_core)

  ament_add_gtest(test_curve_payload test/test_curve_payload.cpp)
  target_link_libraries(test_curve_payload data_recorder_core Qt6::Core)

  ament_add_gtest(test_recorder_engine_subscription test/test_recorder_engine_subscription.cpp)
  target_link_libraries(test_recorder_engine_subscription data_recorder_core)
  ament_target_dependencies(test_recorder_engine_subscription rclcpp std_msgs)
endif()

ament_package()
