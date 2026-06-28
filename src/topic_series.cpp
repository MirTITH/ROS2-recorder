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

}  // namespace data_recorder
