#pragma once
// Read-only E0 job view model for the on-console dashboard.
// Every value comes from results/<id>/status.json (floppylm.e0.result.v1,
// including the optional schedule and phase fields published by run_job);
// nothing about the WSD schedule is re-derived here.
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace e0ui {
using Json = nlohmann::json;

struct Schedule {
  uint64_t T = 0, warmup = 0, tokens_per_step = 0;
  uint64_t ends[3] = {}, cooldown_starts[3] = {};
  uint64_t cooldown(unsigned b) const { return ends[b] - cooldown_starts[b]; }
  // Optimizer steps of the whole job: trunk plus the three cooldowns.
  uint64_t total_steps() const;
};
// The schedule published by run_job, or nothing for a status without it.
std::optional<Schedule> schedule_from_status(const Json &status);

// Optimizer steps executed so far (trunk, finished cooldowns, current one).
uint64_t executed_steps(const Json &status, const Schedule &schedule);

struct Sample {
  uint64_t step = 0;
  double loss = 0;
};

// Bounded loss history: one point per distinct trunk step, halved when full.
class LossHistory {
public:
  explicit LossHistory(size_t capacity = 512) : capacity_(capacity) {}
  void add(uint64_t step, double loss);
  void clear() { points_.clear(); }
  const std::vector<Sample> &points() const { return points_; }

private:
  size_t capacity_;
  std::vector<Sample> points_;
};

// Exponential moving average over the stored points (display smoothing only).
std::vector<double> ema(const std::vector<Sample> &points, double alpha = 0.2);

// An EMA over few points lags far behind the data, so the chart shows the
// smoothed line only once there are enough points to smooth.
inline constexpr size_t kMinSmoothedPoints = 16;
inline bool smoothed(size_t points) { return points >= kMinSmoothedPoints; }

// Marker label placement inside a chart of width `chart_w`: each label sits
// right of its line, or left of it when it would overflow, on the first row
// where it overlaps no earlier label.
struct LabelSlot {
  float left = 0;
  unsigned row = 0;
};
std::vector<LabelSlot> place_labels(const std::vector<float> &xs,
                                    const std::vector<float> &widths,
                                    float chart_w, float gap = 4);

// Map samples to a w x h box: x = trunk step over the last branch end, y =
// loss over [min, max] of the history with 5% padding; top is high loss.
std::vector<std::pair<float, float>> plot(const std::vector<Sample> &points,
                                          const std::vector<double> &values,
                                          const Schedule &schedule, float w,
                                          float h);

struct Rate {
  double tokens_per_second = 0;
  bool valid = false;
};

// Throughput between two status snapshots of the same job and run segment.
Rate rate(const Json &previous, const Json &current, const Schedule &schedule);

// Human-readable phase from the published state and phase fields.
std::string phase(const Json &status, const Schedule &schedule);

// Remaining seconds at a given token rate; negative when unknown.
double eta_seconds(const Json &status, const Schedule &schedule,
                   double tokens_per_second);

std::string format_duration(double seconds);
std::string format_count(uint64_t value);
std::string format_megabytes(uint64_t bytes);
} // namespace e0ui
