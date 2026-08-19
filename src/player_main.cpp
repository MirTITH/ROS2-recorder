#include <rclcpp/rclcpp.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <thread>

#include "data_recorder/player.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    auto node = std::make_shared<data_recorder::PlayerNode>();
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    while (rclcpp::ok() && !node->finished()) {
      executor.spin_some();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    executor.remove_node(node);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(rclcpp::get_logger("player"), "%s", error.what());
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}
