#include "data_recorder/value_extractor.hpp"

#include <rclcpp/serialization.hpp>

#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <trajectory_msgs/msg/joint_trajectory.hpp>

namespace data_recorder
{

namespace
{

// sensor_msgs/msg/JointState → 每关节 pos/<name>，有则 vel/<name>、eff/<name>。按关节名配对。
class JointStateExtractor : public ValueExtractor
{
public:
  std::vector<ExtractedSample> extract(const rclcpp::SerializedMessage & serialized) const override
  {
    sensor_msgs::msg::JointState msg;
    // 局部构造：deserialize_message 非 const，而 extract() 为 const。
    rclcpp::Serialization<sensor_msgs::msg::JointState> ser;
    ser.deserialize_message(&serialized, &msg);
    std::vector<ExtractedSample> out;
    for (size_t i = 0; i < msg.name.size(); ++i) {
      const std::string & jn = msg.name[i];
      if (i < msg.position.size()) { out.push_back({"pos/" + jn, msg.position[i]}); }
      if (i < msg.velocity.size()) { out.push_back({"vel/" + jn, msg.velocity[i]}); }
      if (i < msg.effort.size()) { out.push_back({"eff/" + jn, msg.effort[i]}); }
    }
    return out;
  }
};

// geometry_msgs/msg/WrenchStamped → force.x/y/z, torque.x/y/z。
class WrenchStampedExtractor : public ValueExtractor
{
public:
  std::vector<ExtractedSample> extract(const rclcpp::SerializedMessage & serialized) const override
  {
    geometry_msgs::msg::WrenchStamped msg;
    // 局部构造：deserialize_message 非 const，而 extract() 为 const。
    rclcpp::Serialization<geometry_msgs::msg::WrenchStamped> ser;
    ser.deserialize_message(&serialized, &msg);
    const auto & w = msg.wrench;
    return {
      {"force.x", w.force.x}, {"force.y", w.force.y}, {"force.z", w.force.z},
      {"torque.x", w.torque.x}, {"torque.y", w.torque.y}, {"torque.z", w.torque.z},
    };
  }
};

// trajectory_msgs/msg/JointTrajectory → 取最后一个轨迹点，每关节 cmd/<name>。
class JointTrajectoryExtractor : public ValueExtractor
{
public:
  std::vector<ExtractedSample> extract(const rclcpp::SerializedMessage & serialized) const override
  {
    trajectory_msgs::msg::JointTrajectory msg;
    // 局部构造：deserialize_message 非 const，而 extract() 为 const。
    rclcpp::Serialization<trajectory_msgs::msg::JointTrajectory> ser;
    ser.deserialize_message(&serialized, &msg);
    std::vector<ExtractedSample> out;
    if (msg.points.empty()) { return out; }
    const auto & pt = msg.points.back();
    for (size_t i = 0; i < msg.joint_names.size() && i < pt.positions.size(); ++i) {
      out.push_back({"cmd/" + msg.joint_names[i], pt.positions[i]});
    }
    return out;
  }
};

}  // namespace

void ValueExtractorRegistry::register_extractor(
  const std::string & type, std::unique_ptr<ValueExtractor> extractor)
{
  extractors_[type] = std::move(extractor);
}

bool ValueExtractorRegistry::has(const std::string & type) const
{
  return extractors_.find(type) != extractors_.end();
}

const ValueExtractor * ValueExtractorRegistry::get(const std::string & type) const
{
  auto it = extractors_.find(type);
  return it == extractors_.end() ? nullptr : it->second.get();
}

void register_builtin_extractors(ValueExtractorRegistry & registry)
{
  registry.register_extractor(
    "sensor_msgs/msg/JointState", std::make_unique<JointStateExtractor>());
  registry.register_extractor(
    "geometry_msgs/msg/WrenchStamped", std::make_unique<WrenchStampedExtractor>());
  registry.register_extractor(
    "trajectory_msgs/msg/JointTrajectory", std::make_unique<JointTrajectoryExtractor>());
}

}  // namespace data_recorder
