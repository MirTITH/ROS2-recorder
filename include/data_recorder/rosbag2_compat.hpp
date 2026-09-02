#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <rclcpp/qos.hpp>
#include <rosbag2_storage/serialized_bag_message.hpp>
#include <rosbag2_storage/topic_metadata.hpp>
#include <yaml-cpp/yaml.h>

#ifndef DATA_RECORDER_ROSBAG2_QOS_IN_STORAGE
#if __has_include(<rosbag2_storage/qos.hpp>)
#define DATA_RECORDER_ROSBAG2_QOS_IN_STORAGE 1
#else
#define DATA_RECORDER_ROSBAG2_QOS_IN_STORAGE 0
#endif
#endif

#if DATA_RECORDER_ROSBAG2_QOS_IN_STORAGE
#include <rosbag2_storage/qos.hpp>
#else
#include <rosbag2_transport/qos.hpp>
#endif

namespace data_recorder::rosbag2_compat
{

#if DATA_RECORDER_ROSBAG2_QOS_IN_STORAGE
using Rosbag2QoS = rosbag2_storage::Rosbag2QoS;
#else
using Rosbag2QoS = rosbag2_transport::Rosbag2QoS;
#endif

inline std::vector<Rosbag2QoS> wrap_qos_profiles(const std::vector<rclcpp::QoS> & profiles)
{
  std::vector<Rosbag2QoS> wrapped;
  wrapped.reserve(profiles.size());
  for (const auto & profile : profiles) {
    wrapped.emplace_back(profile);
  }
  return wrapped;
}

inline std::string serialize_qos_profiles(const std::vector<rclcpp::QoS> & profiles)
{
  YAML::Node node;
  for (const auto & profile : profiles) {
    node.push_back(Rosbag2QoS(profile));
  }
  return YAML::Dump(node);
}

inline std::vector<Rosbag2QoS> deserialize_qos_profiles(const std::string & serialized)
{
  if (serialized.empty()) { return {}; }
  return YAML::Load(serialized).as<std::vector<Rosbag2QoS>>();
}

template<typename MetadataT>
void set_topic_qos_profiles(MetadataT & metadata, const std::string & serialized)
{
  using ProfilesT = std::decay_t<decltype(metadata.offered_qos_profiles)>;
  if constexpr (std::is_same_v<ProfilesT, std::string>) {
    metadata.offered_qos_profiles = serialized;
  } else {
    metadata.offered_qos_profiles.clear();
    for (const auto & profile : deserialize_qos_profiles(serialized)) {
      metadata.offered_qos_profiles.emplace_back(profile);
    }
  }
}

template<typename MetadataT>
std::vector<Rosbag2QoS> topic_qos_profiles(const MetadataT & metadata)
{
  using ProfilesT = std::decay_t<decltype(metadata.offered_qos_profiles)>;
  if constexpr (std::is_same_v<ProfilesT, std::string>) {
    return deserialize_qos_profiles(metadata.offered_qos_profiles);
  } else {
    return wrap_qos_profiles(metadata.offered_qos_profiles);
  }
}

template<typename MessageT, typename = void>
struct HasReceiveTimestamp : std::false_type {};

template<typename MessageT>
struct HasReceiveTimestamp<
  MessageT,
  std::void_t<
    decltype(std::declval<MessageT &>().recv_timestamp),
    decltype(std::declval<MessageT &>().send_timestamp)>> : std::true_type {};

template<typename MessageT>
void set_message_timestamp(MessageT & message, int64_t timestamp_ns)
{
  if constexpr (HasReceiveTimestamp<MessageT>::value) {
    message.recv_timestamp = timestamp_ns;
    message.send_timestamp = timestamp_ns;
  } else {
    message.time_stamp = timestamp_ns;
  }
}

template<typename MessageT>
int64_t message_timestamp(const MessageT & message)
{
  if constexpr (HasReceiveTimestamp<MessageT>::value) {
    return static_cast<int64_t>(message.recv_timestamp);
  } else {
    return static_cast<int64_t>(message.time_stamp);
  }
}

}  // namespace data_recorder::rosbag2_compat
