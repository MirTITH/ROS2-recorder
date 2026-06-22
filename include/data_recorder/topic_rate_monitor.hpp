#pragma once

#include <cstdint>
#include <deque>

namespace data_recorder
{

// 用最近一个时间窗内的到达时刻估计频率。线程不安全——单个订阅回调线程使用。
class TopicRateMonitor
{
public:
  explicit TopicRateMonitor(double window_seconds = 1.0);

  void record(int64_t stamp_ns);
  double hz() const;

private:
  int64_t window_ns_;
  std::deque<int64_t> stamps_;
};

}  // namespace data_recorder
