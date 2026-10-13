#include "data_recorder/session_manager.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace data_recorder
{

std::string SessionManager::create_session_directory(
  const std::string & output_dir, const std::string & session_id) const
{
  const fs::path dir = fs::absolute(fs::path(output_dir) / session_id);
  if (!fs::create_directory(dir)) {
    throw fs::filesystem_error(
            "Session directory already exists", dir,
            std::make_error_code(std::errc::file_exists));
  }
  return dir.string();
}

bool SessionManager::write_session_yaml(const SessionRecord & record, std::string * error) const
{
  if (error) { error->clear(); }
  const auto fail = [&](const std::string & detail) {
      if (error) { *error = "Failed to save " + record.directory + "/session.yaml: " + detail; }
      return false;
    };
  try {
    YAML::Node root;
    root["session"] = record.session_id;
    if (!record.recorder_version.empty()) { root["version"] = record.recorder_version; }
    YAML::Node recorded_at;
    recorded_at["unix"] = record.unix_time;
    recorded_at["ros_time_ns"] = record.ros_time_ns;
    root["recorded_at"] = recorded_at;
    root["duration_seconds"] = record.duration_seconds;

    for (const auto & topic : record.topics) {
      YAML::Node t;
      t["name"] = topic.name;
      t["backend"] = topic.backend;
      if (!topic.offered_qos_profiles.empty()) {
        t["offered_qos_profiles"] = topic.offered_qos_profiles;
      }
      root["topics"].push_back(t);
    }
    for (const auto & tag : record.tags) {
      YAML::Node t;
      t["name"] = tag.name;
      t["color"] = tag.color;
      root["tags"].push_back(t);
    }
    for (const auto & a : record.annotations) {
      YAML::Node n;
      n["name"] = a.name;
      n["shortcut"] = a.shortcut;
      n["kind"] = a.kind;
      n["color"] = a.color;
      if (a.kind == "range") {
        n["start"] = a.t;
        n["end"] = a.end;
      } else {
        n["t"] = a.t;
      }
      root["annotations"].push_back(n);
    }

    // Serialize before opening the destination so YAML errors cannot truncate it.
    const std::string contents = YAML::Dump(root);
    const fs::path path = fs::path(record.directory) / "session.yaml";
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) { return fail("could not open file for writing"); }
    out << contents;
    if (out.fail()) { return fail("write failed"); }
    out.flush();
    if (out.fail()) { return fail("flush failed"); }
    out.close();
    if (out.fail()) { return fail("close failed"); }
    return true;
  } catch (const std::exception & e) {
    return fail(e.what());
  }
}

uint64_t SessionManager::directory_size(const std::string & dir)
{
  uint64_t total = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, ec);
    it != fs::recursive_directory_iterator(); it.increment(ec))
  {
    if (ec) { break; }
    if (it->is_regular_file(ec)) {
      total += it->file_size(ec);
    }
  }
  return total;
}

std::vector<SessionRecord> SessionManager::scan(const std::string & output_dir) const
{
  std::vector<SessionRecord> sessions;
  std::error_code ec;
  if (!fs::exists(output_dir, ec)) {
    return sessions;
  }
  for (const auto & entry : fs::directory_iterator(output_dir, ec)) {
    if (ec) { break; }
    // 用 error_code 重载，避免目录扫描中途被删/瞬时错误抛出 filesystem_error 崩溃 GUI 线程。
    ec.clear();
    if (!entry.is_directory(ec) || ec) { continue; }  // 非目录或出错则跳过
    const fs::path yaml_path = entry.path() / "session.yaml";
    if (!fs::exists(yaml_path)) { continue; }  // 静默跳过（崩溃/进行中会话）

    YAML::Node root;
    try {
      root = YAML::LoadFile(yaml_path.string());
    } catch (const YAML::Exception &) {
      continue;  // 损坏的 yaml 跳过
    }

    SessionRecord r;
    r.session_id = root["session"] ? root["session"].as<std::string>() : entry.path().filename().string();
    r.recorder_version = root["version"].as<std::string>("");
    ec.clear();
    const fs::path abs_dir = fs::absolute(entry.path(), ec);
    r.directory = ec ? entry.path().string() : abs_dir.string();
    if (root["recorded_at"]) {
      r.unix_time = root["recorded_at"]["unix"].as<double>(0.0);
      r.ros_time_ns = root["recorded_at"]["ros_time_ns"].as<int64_t>(0);
    }
    r.duration_seconds = root["duration_seconds"].as<double>(0.0);
    r.size_bytes = directory_size(entry.path().string());  // 现算，不来自 yaml

    if (root["topics"]) {
      for (const auto & t : root["topics"]) {
        r.topics.push_back({
          t["name"].as<std::string>(""),
          t["backend"].as<std::string>("rosbag"),
          t["offered_qos_profiles"].as<std::string>("")});
      }
    }
    if (root["tags"]) {
      for (const auto & t : root["tags"]) {
        r.tags.push_back({t["name"].as<std::string>(""), t["color"].as<std::string>("#8a94a6")});
      }
    }
    if (root["annotations"]) {
      for (const auto & a : root["annotations"]) {
        AnnotationRecord rec;
        rec.name = a["name"].as<std::string>("");
        rec.shortcut = a["shortcut"].as<std::string>("");
        rec.kind = a["kind"].as<std::string>("point");
        rec.color = a["color"].as<std::string>("#3b82f6");
        if (rec.kind == "range") {
          rec.t = a["start"].as<double>(0.0);
          rec.end = a["end"].as<double>(0.0);
        } else {
          rec.t = a["t"].as<double>(0.0);
        }
        r.annotations.push_back(rec);
      }
    }
    sessions.push_back(std::move(r));
  }
  // 新会话在前
  std::sort(sessions.begin(), sessions.end(),
    [](const SessionRecord & a, const SessionRecord & b) { return a.session_id > b.session_id; });
  return sessions;
}

}  // namespace data_recorder
