#pragma once

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace data_recorder
{

// 轻量性能探针：仅当环境变量 DR_PROFILE 非空时输出，否则零开销（除一次 getenv 缓存）。
// 用法：
//   DR_SCOPE_TIMER("onCurvesUpdated");                 // RAII：析构时打印耗时
//   if (dr_profile_enabled()) { ... 打印自定义计数 ... }
// 输出到 stderr（应用日志已重定向 stderr 到 log/）。格式：[DR_PROFILE] <label>: <ms> ms [<extra>]

inline bool dr_profile_enabled()
{
  // 进程内只读一次环境变量。
  static const bool enabled = [] {
    const char * v = std::getenv("DR_PROFILE");
    return v != nullptr && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
  }();
  return enabled;
}

class ScopeTimer
{
public:
  explicit ScopeTimer(std::string label)
  : label_(std::move(label)), start_(std::chrono::steady_clock::now())
  {
  }

  // 附加一段可选信息（如条目数），随耗时一并打印。
  void set_extra(std::string extra) { extra_ = std::move(extra); }

  ~ScopeTimer()
  {
    if (!dr_profile_enabled()) { return; }
    const auto end = std::chrono::steady_clock::now();
    const double ms =
      std::chrono::duration<double, std::milli>(end - start_).count();
    std::cerr << "[DR_PROFILE] " << label_ << ": " << ms << " ms";
    if (!extra_.empty()) { std::cerr << " [" << extra_ << "]"; }
    std::cerr << std::endl;
  }

private:
  std::string label_;
  std::string extra_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace data_recorder

// 仅在启用时构造计时器对象（禁用时连对象都不建，彻底零开销）。
#define DR_SCOPE_TIMER(label) \
  ::data_recorder::ScopeTimer _dr_scope_timer_##__LINE__{(label)}
