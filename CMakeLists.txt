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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Charts Widgets Test)

set(DATA_RECORDER_HEADERS
  include/data_recorder/app_controller.hpp
  include/data_recorder/config_model.hpp
  include/data_recorder/ui_models.hpp
)

set(DATA_RECORDER_SOURCES
  src/app_controller.cpp
  src/config_model.cpp
  src/ui_models.cpp
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
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Charts
)
ament_target_dependencies(data_recorder_core rclcpp ament_index_cpp)

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

  ament_add_gtest(test_ui_models test/test_ui_models.cpp)
  target_link_libraries(test_ui_models data_recorder_core)
endif()

ament_package()
