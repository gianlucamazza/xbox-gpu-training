#include "dashboard.h"
#include <algorithm>
#include <cctype>
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

void LossHistory::assign(const Json &series) {
  points_.clear();
  if (!series.is_array())
    return;
  for (const auto &item : series) {
    if (!item.is_object() || !item.contains("step") || !item.contains("loss"))
      continue;
    if (!item.at("loss").is_number())
      continue;
    add(value_or(item, "step"), item.at("loss").get<double>());
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

std::vector<LabelSlot> place_labels(const std::vector<float> &xs,
                                    const std::vector<float> &widths,
                                    float chart_w, float gap) {
  std::vector<LabelSlot> slots;
  std::vector<std::vector<std::pair<float, float>>> rows; // occupied spans
  for (size_t i = 0; i < xs.size() && i < widths.size(); ++i) {
    float left = xs[i] + gap;
    if (left + widths[i] > chart_w)
      left = std::max(0.0f, xs[i] - gap - widths[i]);
    const float right = left + widths[i];
    unsigned row = 0;
    for (;; ++row) {
      if (row == rows.size())
        rows.emplace_back();
      bool free = true;
      for (auto [a, b] : rows[row])
        if (left < b + gap && a < right + gap)
          free = false;
      if (free)
        break;
    }
    rows[row].emplace_back(left, right);
    slots.push_back({left, row});
  }
  return slots;
}

LossRange loss_range(const std::vector<Sample> &points) {
  if (points.empty())
    return {};
  double lo = points.front().loss, hi = lo;
  for (const auto &p : points) {
    lo = std::min(lo, p.loss);
    hi = std::max(hi, p.loss);
  }
  const double pad = std::max(1e-6, 0.05 * (hi - lo));
  return {lo - pad, hi + pad};
}

float loss_y(const LossRange &range, double loss, float h) {
  const double y = (range.hi - loss) / (range.hi - range.lo);
  return float(std::clamp(y, 0.0, 1.0) * h);
}

Ticks nice_ticks(double lo, double hi, unsigned n) {
  Ticks t;
  if (!(hi > lo) || !n)
    return t;
  const double raw = (hi - lo) / n,
               magnitude = std::pow(10.0, std::floor(std::log10(raw))),
               f = raw / magnitude;
  const double step = (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * magnitude;
  for (double v = std::ceil(lo / step) * step; v <= hi + 1e-9 * step; v += step)
    t.values.push_back(std::abs(v) < 1e-9 * step ? 0.0 : v);
  t.digits = std::max(0, -int(std::floor(std::log10(step) + 1e-9)));
  return t;
}

std::vector<std::pair<float, float>> plot(const std::vector<Sample> &points,
                                          const std::vector<double> &values,
                                          const Schedule &schedule, float w,
                                          float h) {
  std::vector<std::pair<float, float>> out;
  if (points.empty() || values.size() != points.size() || !schedule.ends[2])
    return out;
  const auto range = loss_range(points);
  out.reserve(points.size());
  for (size_t i = 0; i < points.size(); ++i) {
    const double x = double(points[i].step) / double(schedule.ends[2]);
    out.emplace_back(float(std::clamp(x, 0.0, 1.0) * w),
                     loss_y(range, values[i], h));
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

uint64_t chart_step(const Json &status, const Schedule &s) {
  if (const auto b = cooling(status, s))
    return value_or(status, "cooldown_step", s.cooldown_starts[*b]);
  return value_or(status, "trunk_step");
}

std::array<BranchView, 3> branch_views(const Json &status, const Schedule &s) {
  std::array<BranchView, 3> out;
  const size_t done = std::min<size_t>(finished_branches(status), 3);
  for (size_t b = 0; b < done; ++b) {
    out[b].state = BranchView::done;
    out[b].percent = 100;
    out[b].seconds =
        real_or(status.at("branches").at(b), "cooldown_seconds", -1);
  }
  if (const auto b = cooling(status, s); b && *b >= done && s.cooldown(*b)) {
    const uint64_t at =
        value_or(status, "cooldown_step", s.cooldown_starts[*b]);
    out[*b].state = BranchView::cooling;
    out[*b].percent =
        unsigned(100 *
                 (std::clamp(at, s.cooldown_starts[*b], s.ends[*b]) -
                  s.cooldown_starts[*b]) /
                 s.cooldown(*b));
  }
  return out;
}

std::string describe_job(const Json &job) {
  std::string out;
  auto add = [&](const std::string &part) {
    out += (out.empty() ? "" : " · ") + part;
  };
  auto number = [](const Json &j, const char *key) -> std::optional<uint64_t> {
    if (!j.is_object() || !j.contains(key) || !j.at(key).is_number_integer())
      return std::nullopt;
    return value_or(j, key);
  };
  auto word = [](const Json &j, const char *key) {
    return j.is_object() && j.contains(key) && j.at(key).is_string()
               ? j.at(key).get<std::string>()
               : std::string();
  };
  const Json none = Json::object();
  const auto &c = job.contains("config") ? job.at("config") : none;
  const auto &sp = job.contains("spec") ? job.at("spec") : none;
  if (auto v = number(c, "d"))
    add("d " + std::to_string(*v));
  if (auto v = number(c, "n_layers"))
    add(std::to_string(*v) + " layers");
  if (auto v = number(c, "n_heads"))
    add(std::to_string(*v) + " heads");
  if (auto v = number(c, "d_ff"))
    add("ff " + std::to_string(*v));
  if (auto v = number(c, "ctx"))
    add("ctx " + std::to_string(*v));
  const std::string fmt = word(c, "core_fmt"), mlp = word(c, "mlp");
  if (!fmt.empty() || !mlp.empty())
    add(fmt + (fmt.empty() || mlp.empty() ? "" : " / ") + mlp);
  if (sp.is_object() && sp.contains("lr") && sp.at("lr").is_number()) {
    char text[32];
    std::snprintf(text, sizeof text, "lr %g", sp.at("lr").get<double>());
    add(text);
  }
  if (auto v = number(sp, "batch"))
    add("batch " + std::to_string(*v));
  if (auto v = number(sp, "tokens"))
    // spec.tokens is the trunk budget T, not the whole schedule.
    add("T " + format_compact(double(*v)) + " tokens");
  return out;
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

WorkerView worker_from_json(const Json &worker, double age_seconds) {
  WorkerView v;
  v.age_seconds = age_seconds;
  if (!worker.is_object() ||
      worker.value("schema", "") != "floppylm.worker.v1")
    return v;
  v.present = true;
  v.state = worker.value("state", std::string());
  v.stale = age_seconds > kHeartbeatStaleSeconds;
  if (worker.contains("progress") && worker.at("progress").is_object()) {
    const auto &p = worker.at("progress");
    v.operation = p.value("operation", std::string());
    v.completed_fence = value_or(p, "completed_fence");
  }
  if (worker.contains("fault") && worker.at("fault").is_object()) {
    const auto &f = worker.at("fault");
    v.faulted = true;
    v.fault_kind = f.value("kind", std::string());
    v.requested_fence = value_or(f, "requested_fence");
    v.fault_completed_fence = value_or(f, "completed_fence");
    v.elapsed_ms = value_or(f, "elapsed_ms");
  }
  if (v.state == "failed")
    v.faulted = true;
  return v;
}

std::string worker_line(const WorkerView &v) {
  if (!v.present)
    return "WORKER · not published yet";
  auto upper = [](std::string s) {
    for (auto &c : s)
      c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
  };
  if (v.faulted) {
    std::string line = "WORKER FAILED";
    if (!v.fault_kind.empty())
      line += " · " + v.fault_kind;
    if (v.requested_fence || v.fault_completed_fence)
      line += " · requested " + format_count(v.requested_fence) +
              " completed " + format_count(v.fault_completed_fence);
    if (v.elapsed_ms)
      line += " · " + format_duration(double(v.elapsed_ms) / 1000.0);
    if (v.stale)
      line += " · no heartbeat for " + format_duration(v.age_seconds);
    else
      line += " · heartbeat " + format_duration(v.age_seconds) + " ago";
    return line;
  }
  if (v.stale)
    return "WORKER UNREACHABLE · no heartbeat for " +
           format_duration(v.age_seconds);
  std::string line = "WORKER " + upper(v.state.empty() ? "unknown" : v.state) +
                     " · heartbeat " + format_duration(v.age_seconds) + " ago";
  if (v.completed_fence || !v.operation.empty())
    line += " · fence " + format_count(v.completed_fence);
  if (!v.operation.empty())
    line += " · " + v.operation;
  return line;
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

std::string format_loss(double loss) {
  char text[32];
  std::snprintf(text, sizeof text, "%.4g", loss);
  return text;
}

std::string format_compact(double value) {
  static const char *const units[] = {"", " k", " M", " G"};
  unsigned u = 0;
  while (std::abs(value) >= 999.5 && u < 3) {
    value /= 1000;
    ++u;
  }
  char text[32];
  const int digits = !u || std::abs(value) >= 99.95 ? 0
                     : std::abs(value) >= 9.995     ? 1
                                                    : 2;
  std::snprintf(text, sizeof text, "%.*f%s", digits, value, units[u]);
  return text;
}
} // namespace e0ui
