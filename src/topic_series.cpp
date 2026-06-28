#include "data_recorder/topic_series.hpp"

#include <algorithm>

namespace data_recorder
{

std::vector<SeriesPoint> decimate(const std::vector<SeriesPoint> & points, std::size_t budget)
{
  const std::size_t effective_budget = (budget < 2) ? 2 : budget;
  if (points.size() <= effective_budget) { return points; }
  const std::size_t buckets = effective_budget / 2;
  const std::size_t n = points.size();
  std::vector<SeriesPoint> out;
  out.reserve(buckets * 2);
  for (std::size_t b = 0; b < buckets; ++b) {
    const std::size_t lo = b * n / buckets;
    const std::size_t hi = (b + 1) * n / buckets;  // 独占
    if (lo >= hi) { continue; }
    std::size_t min_i = lo, max_i = lo;
    for (std::size_t i = lo + 1; i < hi; ++i) {
      if (points[i].second < points[min_i].second) { min_i = i; }
      if (points[i].second > points[max_i].second) { max_i = i; }
    }
    const std::size_t first = std::min(min_i, max_i);
    const std::size_t second = std::max(min_i, max_i);
    out.push_back(points[first]);
    if (second != first) { out.push_back(points[second]); }
  }
  return out;
}

TopicSeries::TopicSeries(std::size_t max_points)
: max_points_(max_points == 0 ? 1 : max_points)
{
}

void TopicSeries::add_message_time(double t_seconds)
{
  message_times_.push_back(t_seconds);
  while (message_times_.size() > max_points_) { message_times_.pop_front(); }
}

void TopicSeries::add_sample(const std::string & series_key, double t_seconds, double value)
{
  auto & buf = series_[series_key];
  buf.emplace_back(t_seconds, value);
  while (buf.size() > max_points_) { buf.pop_front(); }
}

std::vector<double> TopicSeries::message_times(std::size_t budget) const
{
  const std::size_t effective_budget = (budget < 1) ? 1 : budget;
  const std::size_t n = message_times_.size();
  if (n <= effective_budget) {
    return std::vector<double>(message_times_.begin(), message_times_.end());
  }
  if (effective_budget == 1) { return {message_times_.back()}; }
  std::vector<double> out;
  out.reserve(effective_budget);
  // 闭区间均匀取样：含首末（k=0→索引0，k=eb-1→索引n-1），避免最新点被漏掉。
  for (std::size_t k = 0; k < effective_budget; ++k) {
    out.push_back(message_times_[k * (n - 1) / (effective_budget - 1)]);
  }
  return out;
}

std::vector<TopicSeries::SeriesSnapshot> TopicSeries::snapshot(std::size_t budget) const
{
  std::vector<SeriesSnapshot> out;
  for (const auto & [key, buf] : series_) {
    std::vector<SeriesPoint> pts(buf.begin(), buf.end());
    out.push_back({key, decimate(pts, budget)});
  }
  return out;
}

void TopicSeries::clear()
{
  message_times_.clear();
  series_.clear();
}

}  // namespace data_recorder
