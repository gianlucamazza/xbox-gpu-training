#include "dashboard.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace e0ui {
namespace {
// Any non-negative JSON integer, signed or unsigned in nlohmann's storage.
uint64_t value_or(const Json &j, const char *key, uint64_t fallback = 0) {
  if (!j.contains(key) || !j.at(key).is_number_integer())
    return fallback;
  const auto &v = j.at(key);
  if (v.is_number_unsigned())
    return v.get<uint64_t>();
  return v.get<int64_t>() >= 0 ? uint64_t(v.get<int64_t>()) : fallback;
}
double real_or(const Json &j, const char *key, double fallback = 0) {
  return j.contains(key) && j.at(key).is_number() ? j.at(key).get<double>()
                                                  : fallback;
}
size_t finished_branches(const Json &status) {
  return status.contains("branches") && status.at("branches").is_array()
             ? status.at("branches").size()
             : 0;
}
// Index of the branch whose cooldown is running, if any.
std::optional<unsigned> cooling(const Json &status, const Schedule &s) {
  if (status.value("phase", "") != "cooldown")
    return std::nullopt;
  const uint64_t end = value_or(status, "cooldown_end");
  for (unsigned b = 0; b < 3; ++b)
    if (s.ends[b] == end)
      return b;
  return std::nullopt;
}
} // namespace

uint64_t Schedule::total_steps() const {
  return cooldown_starts[2] + cooldown(0) + cooldown(1) + cooldown(2);
}

std::optional<Schedule> schedule_from_status(const Json &status) {
  if (!status.contains("schedule") || !status.at("schedule").is_object())
    return std::nullopt;
  const auto &j = status.at("schedule");
  Schedule s;
  s.T = j.at("T").get<uint64_t>();
  s.warmup = j.at("warmup").get<uint64_t>();
  s.tokens_per_step = j.at("tokens_per_step").get<uint64_t>();
  for (unsigned b = 0; b < 3; ++b) {
    s.ends[b] = j.at("ends").at(b).get<uint64_t>();
    s.cooldown_starts[b] = j.at("cooldown_starts").at(b).get<uint64_t>();
  }
  return s;
}

uint64_t executed_steps(const Json &status, const Schedule &s) {
  uint64_t steps = value_or(status, "trunk_step");
  const size_t done = std::min<size_t>(finished_branches(status), 3);
  for (size_t b = 0; b < done; ++b)
    steps += s.cooldown(unsigned(b));
  if (const auto b = cooling(status, s))
    steps += value_or(status, "cooldown_step", s.cooldown_starts[*b]) -
             s.cooldown_starts[*b];
  return steps;
}

void LossHistory::add(uint64_t step, double loss) {
  if (!std::isfinite(loss))
    return;
  if (!points_.empty() && step <= points_.back().step) {
    if (step == points_.back().step)
      points_.back().loss = loss;
    return;
  }
  points_.push_back({step, loss});
  if (points_.size() > capacity_) {
    std::vector<Sample> kept;
    kept.reserve(points_.size() / 2 + 1);
    for (size_t i = 0; i < points_.size(); i += 2)
      kept.push_back(points_[i]);
    if (kept.back().step != points_.back().step)
      kept.push_back(points_.back());
    points_.swap(kept);
  }
}

std::vector<double> ema(const std::vector<Sample> &points, double alpha) {
  std::vector<double> out;
  out.reserve(points.size());
  for (const auto &p : points)
    out.push_back(out.empty() ? p.loss
                              : alpha * p.loss + (1 - alpha) * out.back());
  return out;
}

std::vector<std::pair<float, float>> plot(const std::vector<Sample> &points,
                                          const std::vector<double> &values,
                                          const Schedule &schedule, float w,
                                          float h) {
  std::vector<std::pair<float, float>> out;
  if (points.empty() || values.size() != points.size() || !schedule.ends[2])
    return out;
  double lo = points.front().loss, hi = lo;
  for (const auto &p : points) {
    lo = std::min(lo, p.loss);
    hi = std::max(hi, p.loss);
  }
  const double pad = std::max(1e-6, 0.05 * (hi - lo));
  lo -= pad;
  hi += pad;
  out.reserve(points.size());
  for (size_t i = 0; i < points.size(); ++i) {
    const double x = double(points[i].step) / double(schedule.ends[2]);
    const double y = (hi - values[i]) / (hi - lo);
    out.emplace_back(float(std::clamp(x, 0.0, 1.0) * w),
                     float(std::clamp(y, 0.0, 1.0) * h));
  }
  return out;
}

Rate rate(const Json &previous, const Json &current, const Schedule &schedule) {
  if (previous.value("job_id", "") != current.value("job_id", ""))
    return {};
  const auto s0 = executed_steps(previous, schedule),
             s1 = executed_steps(current, schedule);
  const double w0 = real_or(previous, "wall_seconds"),
               w1 = real_or(current, "wall_seconds");
  // wall_seconds restarts with each resumed segment; that is not a rate.
  if (s1 <= s0 || w1 <= w0)
    return {};
  return {double(s1 - s0) * double(schedule.tokens_per_step) / (w1 - w0), true};
}

std::string phase(const Json &status, const Schedule &schedule) {
  const std::string state = status.value("state", "unknown");
  const uint64_t step = value_or(status, "trunk_step");
  if (state == "interrupted")
    return "interrupted · checkpoint at step " + format_count(step);
  if (state != "running")
    return state;
  if (const auto b = cooling(status, schedule))
    return "cooldown · branch " + std::to_string(*b + 1) +
           " of 3, ends at step " + format_count(schedule.ends[*b]);
  const size_t done = finished_branches(status);
  if (done >= 3)
    return "finishing";
  if (step < schedule.warmup)
    return "warmup";
  return "trunk · next cooldown at step " +
         format_count(schedule.cooldown_starts[done]);
}

double eta_seconds(const Json &status, const Schedule &schedule,
                   double tokens_per_second) {
  if (tokens_per_second <= 0 || status.value("state", "") != "running")
    return -1;
  const uint64_t done = executed_steps(status, schedule),
                 total = schedule.total_steps();
  return double(total > done ? total - done : 0) *
         double(schedule.tokens_per_step) / tokens_per_second;
}

std::string format_duration(double seconds) {
  if (!(seconds >= 0))
    return "—";
  const auto total = uint64_t(std::llround(seconds));
  char text[32];
  if (total < 60)
    std::snprintf(text, sizeof text, "%llu s", (unsigned long long)total);
  else if (total < 3600)
    std::snprintf(text, sizeof text, "%llu min",
                  (unsigned long long)((total + 30) / 60));
  else
    std::snprintf(text, sizeof text, "%llu h %02llu min",
                  (unsigned long long)(total / 3600),
                  (unsigned long long)(total % 3600 / 60));
  return text;
}

std::string format_count(uint64_t value) {
  std::string digits = std::to_string(value), out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i && (digits.size() - i) % 3 == 0)
      out += ' ';
    out += digits[i];
  }
  return out;
}

std::string format_megabytes(uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof text, "%.0f MB", double(bytes) / 1e6);
  return text;
}
} // namespace e0ui
