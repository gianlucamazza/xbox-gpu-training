#include "DashboardView.h"
#include "../src/cpp/e0/model.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <limits>

namespace xgpu {
namespace {
namespace X = winrt::Windows::UI::Xaml;
namespace C = winrt::Windows::UI::Xaml::Controls;
namespace M = winrt::Windows::UI::Xaml::Media;
namespace S = winrt::Windows::UI::Xaml::Shapes;

// Chart box in effective pixels; the whole page fits 960x540 at 200% scale.
constexpr float kChartW = 560, kChartH = 220;
// run_job publishes status every 64 optimizer steps (trunk and cooldowns),
// well under a minute at measured throughput; ten silent minutes are stale.
constexpr double kStaleSeconds = 600;

// Dark palette inside the TV-safe 16-235 range.
struct Rgb {
  uint8_t r, g, b;
};
constexpr Rgb kBackground{0x1C, 0x1C, 0x20}, kSurface{0x2A, 0x2A, 0x30},
    kText{0xEB, 0xEB, 0xEB}, kMuted{0xA8, 0xA8, 0xB0}, kGrid{0x5A, 0x5A, 0x64},
    kAccent{0x3A, 0x96, 0xDD}, kGreen{0x5C, 0xB8, 0x5C},
    kAmber{0xE0, 0xA0, 0x30}, kRed{0xE0, 0x55, 0x55};

M::SolidColorBrush brush(Rgb c) {
  return M::SolidColorBrush(
      winrt::Windows::UI::ColorHelper::FromArgb(255, c.r, c.g, c.b));
}
C::TextBlock text(double size, Rgb color = kText) {
  C::TextBlock t;
  t.FontSize(size);
  t.Foreground(brush(color));
  t.TextTrimming(X::TextTrimming::CharacterEllipsis);
  return t;
}
winrt::hstring h(const std::string &s) { return winrt::to_hstring(s); }
std::string fixed(double value, int digits) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.*f", digits, value);
  return buffer;
}
C::RowDefinition row(X::GridLength height) {
  C::RowDefinition r;
  r.Height(height);
  return r;
}
C::ColumnDefinition column(X::GridLength width) {
  C::ColumnDefinition c;
  c.Width(width);
  return c;
}
X::GridLength star() {
  return X::GridLengthHelper::FromValueAndType(1, X::GridUnitType::Star);
}
// Label above a large value, the 10-foot pattern for a single metric.
C::TextBlock metric(C::StackPanel const &panel, wchar_t const *label) {
  auto caption = text(15, kMuted);
  caption.Text(label);
  auto value = text(24);
  value.Text(L"—");
  value.Margin({0, 0, 0, 10});
  panel.Children().Append(caption);
  panel.Children().Append(value);
  return value;
}
} // namespace

DashboardView::DashboardView(std::filesystem::path local,
                             std::function<std::filesystem::path()> active_job)
    : local_(std::move(local)), active_job_(std::move(active_job)) {
  root_ = C::Grid();
  root_.RequestedTheme(X::ElementTheme::Dark);
  root_.Background(brush(kBackground));
  // Default window bounds already exclude the TV-unsafe border.
  root_.Padding({24, 16, 24, 16});
  root_.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));
  root_.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));
  root_.RowDefinitions().Append(row(star()));
  root_.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));

  C::Grid header;
  header.ColumnDefinitions().Append(column(star()));
  header.ColumnDefinitions().Append(column(X::GridLengthHelper::Auto()));
  job_ = text(24);
  job_.Text(L"FloppyLM E0");
  header.Children().Append(job_);
  C::StackPanel status;
  status.HorizontalAlignment(X::HorizontalAlignment::Right);
  state_ = text(24);
  state_.HorizontalAlignment(X::HorizontalAlignment::Right);
  fresh_ = text(15, kMuted);
  fresh_.HorizontalAlignment(X::HorizontalAlignment::Right);
  status.Children().Append(state_);
  status.Children().Append(fresh_);
  C::Grid::SetColumn(status, 1);
  header.Children().Append(status);
  root_.Children().Append(header);

  C::StackPanel progress;
  progress.Margin({0, 12, 0, 12});
  progress_ = C::ProgressBar();
  progress_.Minimum(0);
  progress_.Maximum(100);
  progress_.Height(8);
  progress_.Foreground(brush(kAccent));
  progress_.Background(brush(kSurface));
  phase_ = text(18);
  progress_text_ = text(15, kMuted);
  progress.Children().Append(progress_);
  progress.Children().Append(phase_);
  progress.Children().Append(progress_text_);
  C::Grid::SetRow(progress, 1);
  root_.Children().Append(progress);

  C::Grid body;
  body.ColumnDefinitions().Append(column(X::GridLengthHelper::Auto()));
  body.ColumnDefinitions().Append(column(star()));
  C::StackPanel chart_area;
  loss_hi_ = text(12, kMuted);
  chart_ = C::Canvas();
  chart_.Width(kChartW);
  chart_.Height(kChartH);
  chart_.Background(brush(kSurface));
  raw_ = S::Polyline();
  raw_.Stroke(brush(kGrid));
  raw_.StrokeThickness(1.5);
  smooth_ = S::Polyline();
  smooth_.Stroke(brush(kAccent));
  smooth_.StrokeThickness(3);
  loss_lo_ = text(12, kMuted);
  axis_ = text(12, kMuted);
  axis_.Text(L"trunk training loss · x = step over the "
             L"full schedule");
  chart_area.Children().Append(loss_hi_);
  chart_area.Children().Append(chart_);
  chart_area.Children().Append(loss_lo_);
  chart_area.Children().Append(axis_);
  body.Children().Append(chart_area);
  C::StackPanel metrics;
  metrics.Margin({32, 0, 0, 0});
  loss_ = metric(metrics, L"trunk loss");
  rate_ = metric(metrics, L"tokens / s");
  tokens_ = metric(metrics, L"tokens processed");
  gpu_ = metric(metrics, L"GPU time / wall time");
  memory_ = metric(metrics, L"peak memory");
  eta_ = metric(metrics, L"remaining (estimate)");
  C::Grid::SetColumn(metrics, 1);
  body.Children().Append(metrics);
  C::Grid::SetRow(body, 2);
  root_.Children().Append(body);

  device_ = text(12, kMuted);
  device_.Text(L"Starting GPU worker… Jobs are controlled from the host.");
  C::Grid::SetRow(device_, 3);
  root_.Children().Append(device_);

  show_idle();
  timer_ = X::DispatcherTimer();
  timer_.Interval(std::chrono::seconds(1));
  timer_.Tick([this](auto &&, auto &&) { refresh(); });
  timer_.Start();
}

void DashboardView::refresh() {
  try {
    if (!device_shown_)
      show_device();
    const auto job = active_job_();
    if (job.empty()) {
      if (!current_.empty() && training_job_) {
        // The worker clears the active job right after the final status.
        const auto final_status = current_.parent_path() / L"results" /
                                  current_.stem().stem() / L"status.json";
        if (std::filesystem::exists(final_status))
          show_status(e0::read_json(final_status), 0);
      }
      show_idle();
      return;
    }
    if (job != current_)
      show_job(job);
    if (!training_job_)
      return;
    // inbox/<id>.job.json -> inbox/results/<id>/status.json
    const auto status =
        job.parent_path() / L"results" / job.stem().stem() / L"status.json";
    if (!std::filesystem::exists(status))
      return;
    const double age = std::chrono::duration<double>(
                           std::filesystem::file_time_type::clock::now() -
                           std::filesystem::last_write_time(status))
                           .count();
    show_status(e0::read_json(status), age);
  } catch (...) {
    // A status file replaced mid-read is retried on the next tick.
  }
}

void DashboardView::show_job(const std::filesystem::path &job) {
  history_.clear();
  previous_ = e0ui::Json::object();
  tokens_per_second_ = 0;
  schedule_.reset();
  keep_display(true);
  const auto content = e0::read_json(job);
  current_ = job;
  const std::string id =
      content.value("job_id", e0::path_text(job.stem().stem()));
  job_.Text(h("FloppyLM E0 · " + id));
  state_.Text(L"RUNNING");
  state_.Foreground(brush(kAccent));
  fresh_.Text(L"");
  progress_text_.Text(L"");
  progress_.IsIndeterminate(true);
  chart_.Children().Clear();
  training_job_ = content.value("schema", "") == "floppylm.e0.job.v1";
  // Acceptance fixtures report through .actual.json, not status.json.
  phase_.Text(training_job_
                  ? winrt::hstring(L"waiting for the first status")
                  : h("acceptance fixture · " +
                      content.value("schema", std::string("unknown"))));
}

void DashboardView::draw_markers() {
  chart_.Children().Clear();
  const std::pair<uint64_t, wchar_t const *> markers[] = {
      {schedule_->warmup, L"warmup"},
      {schedule_->cooldown_starts[0], L"cooldown T"},
      {schedule_->cooldown_starts[1], L"cooldown 2T"},
      {schedule_->cooldown_starts[2], L"cooldown 4T"}};
  std::vector<float> xs, widths;
  std::vector<C::TextBlock> tags;
  for (const auto &[step, label] : markers) {
    const float x = float(double(step) / double(schedule_->ends[2]) * kChartW);
    S::Line line;
    line.X1(x);
    line.X2(x);
    line.Y1(0);
    line.Y2(kChartH);
    line.Stroke(brush(kGrid));
    line.StrokeThickness(1);
    M::DoubleCollection dash;
    dash.Append(4);
    dash.Append(4);
    line.StrokeDashArray(dash);
    chart_.Children().Append(line);
    auto tag = text(12, kMuted);
    tag.Text(label);
    tag.Measure({std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::infinity()});
    xs.push_back(x);
    widths.push_back(tag.DesiredSize().Width);
    tags.push_back(tag);
  }
  // Labels of nearby markers stack in rows and stay inside the chart.
  const auto slots = e0ui::place_labels(xs, widths, kChartW);
  for (size_t i = 0; i < tags.size(); ++i) {
    C::Canvas::SetLeft(tags[i], slots[i].left);
    C::Canvas::SetTop(tags[i], 2 + 16.0 * slots[i].row);
    chart_.Children().Append(tags[i]);
  }
  chart_.Children().Append(raw_);
  chart_.Children().Append(smooth_);
}

void DashboardView::show_status(const e0ui::Json &status, double age) {
  const std::string state = status.value("state", "unknown");
  last_state_ = state;
  std::string upper = state;
  for (auto &c : upper)
    c = char(std::toupper(static_cast<unsigned char>(c)));
  state_.Text(h(upper));
  state_.Foreground(brush(state == "running"       ? kAccent
                          : state == "completed"   ? kGreen
                          : state == "interrupted" ? kAmber
                                                   : kRed));
  keep_display(state == "running");
  if (!schedule_) {
    schedule_ = e0ui::schedule_from_status(status);
    progress_.IsIndeterminate(false);
    if (!schedule_) {
      // A failure before run_job starts carries only job_id, state and error.
      phase_.Text(
          h(status.value("error", std::string("no schedule published"))));
      return;
    }
    draw_markers();
  }
  const auto &s = *schedule_;
  const bool stale = state == "running" && age > kStaleSeconds;
  fresh_.Text(h((stale ? "no update for " : "updated ") +
                e0ui::format_duration(age) + (stale ? "" : " ago")));
  fresh_.Foreground(brush(stale ? kAmber : kMuted));
  phase_.Text(h(state == "failed" ? status.value("error", std::string("failed"))
                                  : e0ui::phase(status, s)));
  const uint64_t done = e0ui::executed_steps(status, s),
                 total = s.total_steps();
  const size_t branches =
      status.contains("branches") ? status.at("branches").size() : 0;
  progress_.Value(100.0 * double(std::min(done, total)) / double(total));
  progress_text_.Text(h("step " + e0ui::format_count(done) + " / " +
                        e0ui::format_count(total) + " · branches " +
                        std::to_string(branches) + " / 3"));

  if (status.contains("last_loss") && status.at("last_loss").is_number()) {
    const double loss = status.at("last_loss").get<double>();
    history_.add(status.value("trunk_step", uint64_t(0)), loss);
    loss_.Text(h(fixed(loss, 4)));
  }
  if (previous_.empty() || done != e0ui::executed_steps(previous_, s)) {
    const auto r = e0ui::rate(previous_, status, s);
    if (r.valid)
      tokens_per_second_ = r.tokens_per_second;
    previous_ = status;
  }
  rate_.Text(tokens_per_second_ > 0
                 ? h(e0ui::format_count(uint64_t(tokens_per_second_)))
                 : winrt::hstring(L"—"));
  tokens_.Text(h(e0ui::format_count(done * s.tokens_per_step)));
  const double wall = status.value("wall_seconds", 0.0);
  gpu_.Text(
      wall > 0
          ? h(fixed(100.0 * status.value("gpu_seconds", 0.0) / wall, 0) + " %")
          : winrt::hstring(L"—"));
  memory_.Text(h(
      e0ui::format_megabytes(status.value("peak_memory_bytes", uint64_t(0)))));
  const double eta = e0ui::eta_seconds(status, s, tokens_per_second_);
  eta_.Text(eta >= 0 ? h("~" + e0ui::format_duration(eta))
                     : winrt::hstring(L"—"));
  draw_chart();
}

void DashboardView::show_idle() {
  current_.clear();
  keep_display(false);
  state_.Text(L"IDLE");
  state_.Foreground(brush(kMuted));
  fresh_.Text(last_state_.empty() ? winrt::hstring()
                                  : h("last job " + last_state_));
  fresh_.Foreground(brush(kMuted));
  phase_.Text(L"Waiting for a job from the host. Keep this app open during "
              L"training.");
  progress_.IsIndeterminate(false);
}

void DashboardView::show_device() {
  const auto path = local_ / L"device.json";
  if (!std::filesystem::exists(path))
    return;
  const auto d = e0::read_json(path);
  if (d.value("state", "") == "failed") {
    device_.Text(h("GPU worker failed: " +
                   d.value("error", std::string("unknown error"))));
    device_.Foreground(brush(kRed));
  } else {
    const std::string commit = d.value("commit", std::string("unknown"));
    device_.Text(h(d.value("adapter", std::string("unknown adapter")) +
                   (d.value("hardware_gpu", false) ? " · hardware GPU"
                                                   : " · NO hardware GPU") +
                   " · " + d.value("package", std::string("")) + " · " +
                   commit.substr(0, 7) +
                   " · jobs are controlled from the host"));
  }
  device_shown_ = true;
}

void DashboardView::keep_display(bool on) {
  // Keep the TV awake while a job trains; release it when idle.
  if (on == display_held_)
    return;
  if (!display_)
    display_ = winrt::Windows::System::Display::DisplayRequest();
  if (on)
    display_.RequestActive();
  else
    display_.RequestRelease();
  display_held_ = on;
}

void DashboardView::draw_chart() {
  if (!schedule_)
    return;
  const auto &points = history_.points();
  std::vector<double> raw_values;
  raw_values.reserve(points.size());
  for (const auto &p : points)
    raw_values.push_back(p.loss);
  // With few points the EMA lags far behind the data: draw the raw curve as the
  // main line until there is enough to smooth.
  const bool smoothing = e0ui::smoothed(points.size());
  const auto main_values = smoothing ? e0ui::ema(points) : raw_values;
  M::PointCollection raw, main;
  if (smoothing)
    for (auto [x, y] :
         e0ui::plot(points, raw_values, *schedule_, kChartW, kChartH))
      raw.Append({x, y});
  for (auto [x, y] :
       e0ui::plot(points, main_values, *schedule_, kChartW, kChartH))
    main.Append({x, y});
  raw_.Points(raw);
  smooth_.Points(main);
  axis_.Text(smoothing
                 ? L"trunk training loss (EMA bold, raw faint) · x = step "
                   L"over the full schedule"
                 : L"trunk training loss · x = step over the full schedule");
  if (points.empty()) {
    loss_hi_.Text(L"");
    loss_lo_.Text(L"");
    return;
  }
  const auto [lo, hi] =
      std::minmax_element(points.begin(), points.end(),
                          [](const e0ui::Sample &a, const e0ui::Sample &b) {
                            return a.loss < b.loss;
                          });
  loss_hi_.Text(h("max " + fixed(hi->loss, 3)));
  loss_lo_.Text(h("min " + fixed(lo->loss, 3)));
}
} // namespace xgpu
