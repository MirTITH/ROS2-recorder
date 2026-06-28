#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rclcpp/serialized_message.hpp>

namespace data_recorder
{

// 一条标量样本：series_key 为稳定标识（如 "pos/joint_a"），value 为数值。
struct ExtractedSample
{
  std::string series_key;
  double value{0.0};
};

// 把某类型的序列化消息反序列化并提取若干标量样本。每种支持的类型一个实现。
class ValueExtractor
{
public:
  virtual ~ValueExtractor() = default;
  virtual std::vector<ExtractedSample> extract(
    const rclcpp::SerializedMessage & serialized) const = 0;
};

// 按 ROS 类型字符串分发到 ValueExtractor。日后通用内省也作为一个 extractor 注册进来。
class ValueExtractorRegistry
{
public:
  void register_extractor(const std::string & type, std::unique_ptr<ValueExtractor> extractor);
  bool has(const std::string & type) const;
  const ValueExtractor * get(const std::string & type) const;  // 类型未注册时返回 nullptr

private:
  std::map<std::string, std::unique_ptr<ValueExtractor>> extractors_;
};

// 注册 v1 内置类型：JointState / WrenchStamped / JointTrajectory。
void register_builtin_extractors(ValueExtractorRegistry & registry);

}  // namespace data_recorder
