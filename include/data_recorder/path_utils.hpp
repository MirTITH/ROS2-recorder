#pragma once
#include <string>
namespace data_recorder
{
// 把 topic 名转为录制文件基名：去前导 '/'，其余 '/'→'_'。
inline std::string file_base_for_topic(const std::string & topic)
{
  std::string s = topic;
  if (!s.empty() && s.front() == '/') { s.erase(s.begin()); }
  for (auto & c : s) { if (c == '/') { c = '_'; } }
  return s;
}
}  // namespace data_recorder
