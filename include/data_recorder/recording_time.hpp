#pragma once

#include <chrono>
#include <cstdint>

namespace data_recorder
{

inline int64_t steady_now_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline double relative_seconds(int64_t stamp_ns, int64_t start_ns)
{
  if (stamp_ns <= start_ns) { return 0.0; }
  return static_cast<double>(stamp_ns - start_ns) / 1e9;
}

}  // namespace data_recorder
