#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace data_recorder
{

// 一个时间点的值：(秒, 值)。
using SeriesPoint = std::pair<double, double>;

// 把点序列抽稀到至多 budget 个点（输入须按时间 first 升序）。
// size<=budget 原样返回；否则分 budget/2 个桶，每桶输出桶内 min 与 max
// （按时间先后），保留尖峰；结果按时间升序。budget<2 视为 2。
// 注意：实际上限为 (budget/2)*2，故奇数 budget 实际最多 budget-1 个点。
std::vector<SeriesPoint> decimate(const std::vector<SeriesPoint> & points, std::size_t budget);

// 每个 topic 的时间序列缓冲：消息时间戳（折叠点）+ 各 series 的 (t,value)（展开曲线）。
// 有界环形缓冲，超 max_points 丢最旧。线程无关——调用方加锁。
class TopicSeries
{
public:
  explicit TopicSeries(std::size_t max_points = 20000);

  void add_message_time(double t_seconds);
  void add_sample(const std::string & series_key, double t_seconds, double value);

  std::vector<double> message_times(std::size_t budget) const;  // 折叠点，均匀降采样

  struct SeriesSnapshot
  {
    std::string key;
    std::vector<SeriesPoint> points;  // 已抽稀
  };
  std::vector<SeriesSnapshot> snapshot(std::size_t budget) const;  // 按 key 升序

  void clear();

private:
  std::size_t max_points_;
  std::deque<double> message_times_;
  std::map<std::string, std::deque<SeriesPoint>> series_;
};

}  // namespace data_recorder
