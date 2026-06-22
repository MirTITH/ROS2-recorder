#include "data_recorder/topic_rate_monitor.hpp"

namespace data_recorder
{

TopicRateMonitor::TopicRateMonitor(double window_seconds)
: window_ns_(static_cast<int64_t>(window_seconds * 1e9))
{
}

void TopicRateMonitor::record(int64_t stamp_ns)
{
  stamps_.push_back(stamp_ns);
  const int64_t cutoff = stamp_ns - window_ns_;
  while (stamps_.size() > 1 && stamps_.front() < cutoff) {
    stamps_.pop_front();
  }
}

double TopicRateMonitor::hz() const
{
  if (stamps_.size() < 2) {
    return 0.0;
  }
  const int64_t span_ns = stamps_.back() - stamps_.front();
  if (span_ns <= 0) {
    return 0.0;
  }
  // (n-1) 个间隔 / 跨度。
  return static_cast<double>(stamps_.size() - 1) * 1e9 / static_cast<double>(span_ns);
}

}  // namespace data_recorder
