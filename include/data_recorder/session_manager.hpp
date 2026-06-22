#pragma once

#include <string>
#include <vector>

#include "data_recorder/recorder_types.hpp"

namespace data_recorder
{

// 建会话目录、写/扫描 session.yaml。纯文件 I/O，无 Qt/ROS。线程无关——由调用方在非 GUI 线程使用。
class SessionManager
{
public:
  // 在 output_dir 下建 <session_id>/ 子目录，返回其绝对路径。
  std::string create_session_directory(
    const std::string & output_dir, const std::string & session_id) const;

  // 把 record 序列化为 <record.directory>/session.yaml。
  void write_session_yaml(const SessionRecord & record) const;

  // 扫描 output_dir 各子目录，读 session.yaml（无则跳过），现算 size_bytes；按 session_id 降序（新在前）。
  std::vector<SessionRecord> scan(const std::string & output_dir) const;

private:
  static uint64_t directory_size(const std::string & dir);
};

}  // namespace data_recorder
