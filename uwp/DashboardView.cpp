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
using winrt::Windows::Foundation::IUnknown;

// Plot box in effective pixels, plus the loss-label column on its left; with
// the 2x4 metric tiles the page fits 960x540 at 200% scale.
constexpr float kChartW = 540, kChartH = 280, kAxisW = 44;
// Marker label rows at the top of the chart, and the clear gap below them
// before the plotted curve starts.
constexpr float kLabelTop = 2, kLabelRow = 16, kLabelGap = 8;
// run_job publishes status every 64 optimizer steps (trunk and cooldowns),
// well under a minute at measured throughput; ten silent minutes are stale.
constexpr double kStaleSeconds = 600;

// Dark palette (0xRRGGBB) inside the TV-safe 16-235 range.
constexpr uint32_t kBackground = 0x1C1C20, kSurface = 0x2A2A30,
                   kText = 0xEBEBEB, kMuted = 0xA8A8B0, kGrid = 0x5A5A64,
                   kGridFaint = 0x3C3C44, kAccent = 0x3A96DD, kGreen = 0x5CB85C,
                   kAmber = 0xE0A030, kRed = 0xE05555;

uint32_t state_color(const std::string &state) {
  return state == "running"       ? kAccent
         : state == "completed"   ? kGreen
         : state == "interrupted" ? kAmber
         : state.empty()          ? kMuted
                                  : kRed;
}
winrt::hstring h(const std::string &s) { return winrt::to_hstring(s); }
std::string fixed(double value, int digits) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.*f", digits, value);
  return buffer;
}
// Assignments that change nothing still invalidate rendering; skip them.
void set_text(C::TextBlock const &t, winrt::hstring const &s) {
  if (t.Text() != s)
    t.Text(s);
}
void set_text(C::TextBlock const &t, const std::string &s) {
  set_text(t, h(s));
}
template <class Element> void recolor(Element const &e, M::Brush const &b) {
  const auto current = e.Foreground();
  if (!current || current.as<IUnknown>() != b.as<IUnknown>())
    e.Foreground(b);
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
X::GridLength pixels(double value) {
  return X::GridLengthHelper::FromPixels(value);
}
M::DoubleCollection dashes(double on, double off) {
  M::DoubleCollection d;
  d.Append(on);
  d.Append(off);
  return d;
}
} // namespace

M::SolidColorBrush DashboardView::paint(uint32_t rgb) {
  if (const auto it = brushes_.find(rgb); it != brushes_.end())
    return it->second;
  M::SolidColorBrush b(winrt::Windows::UI::ColorHelper::FromArgb(
      255, uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)));
  brushes_.emplace(rgb, b);
  return b;
}

C::TextBlock DashboardView::label(double size, uint32_t rgb) {
  C::TextBlock t;
  t.FontSize(size);
  t.Foreground(paint(rgb));
  t.TextTrimming(X::TextTrimming::CharacterEllipsis);
  return t;
}

// Caption above a large value, the 10-foot pattern for a single metric.
DashboardView::Tile DashboardView::tile(C::Grid const &grid, int index,
                                        wchar_t const *caption) {
  C::StackPanel panel;
  panel.Margin({0, 0, 12, 12});
  auto title = label(13, kMuted);
  title.Text(caption);
  Tile t{label(24, kText), label(12, kMuted)};
  t.value.Text(L"—");
  panel.Children().Append(title);
  panel.Children().Append(t.value);
  panel.Children().Append(t.note);
  C::Grid::SetRow(panel, index / 2);
  C::Grid::SetColumn(panel, index % 2);
  grid.Children().Append(panel);
  return t;
}

DashboardView::DashboardView(std::filesystem::path local,
                             std::function<std::filesystem::path()> active_job)
    : local_(std::move(local)), active_job_(std::move(active_job)) {
  root_ = C::Grid();
  root_.RequestedTheme(X::ElementTheme::Dark);
  root_.Background(paint(kBackground));
  // Default window bounds already exclude the TV-unsafe border.
  root_.Padding({24, 16, 24, 16});
  root_.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));
  root_.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));
  root_.RowDefinitions().Append(row(star()));
  root_.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));

  // Header: job and state, then the model summary and the update age.
  C::Grid header;
  header.ColumnDefinitions().Append(column(star()));
  header.ColumnDefinitions().Append(column(X::GridLengthHelper::Auto()));
  header.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));
  header.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));
  header.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));
  job_ = label(24, kText);
  job_.Text(L"FloppyLM E0");
  state_ = label(24, kMuted);
  state_.HorizontalAlignment(X::HorizontalAlignment::Right);
  C::Grid::SetColumn(state_, 1);
  config_ = label(15, kMuted);
  C::Grid::SetRow(config_, 1);
  fresh_ = label(15, kMuted);
  fresh_.HorizontalAlignment(X::HorizontalAlignment::Right);
  fresh_.Margin({16, 0, 0, 0});
  C::Grid::SetRow(fresh_, 1);
  C::Grid::SetColumn(fresh_, 1);
  live_ = label(15, kMuted);
  live_.Margin({0, 6, 0, 0});
  C::Grid::SetRow(live_, 2);
  C::Grid::SetColumnSpan(live_, 2);
  for (auto const &t : {job_, state_, config_, fresh_, live_})
    header.Children().Append(t);
  root_.Children().Append(header);

  // Progress: bar, phase and step count, then the branch strip.
  C::StackPanel progress;
  progress.Margin({0, 12, 0, 12});
  progress_ = C::ProgressBar();
  progress_.Minimum(0);
  progress_.Maximum(100);
  progress_.Height(8);
  progress_.Foreground(paint(kAccent));
  progress_.Background(paint(kSurface));
  C::Grid line;
  line.Margin({0, 6, 0, 0});
  line.ColumnDefinitions().Append(column(star()));
  line.ColumnDefinitions().Append(column(X::GridLengthHelper::Auto()));
  phase_ = label(18, kText);
  progress_text_ = label(15, kMuted);
  progress_text_.Margin({16, 0, 0, 0});
  progress_text_.VerticalAlignment(X::VerticalAlignment::Center);
  C::Grid::SetColumn(progress_text_, 1);
  line.Children().Append(phase_);
  line.Children().Append(progress_text_);
  C::StackPanel strip;
  strip.Orientation(C::Orientation::Horizontal);
  strip.Margin({0, 4, 0, 0});
  for (auto &b : branch_) {
    b = label(15, kMuted);
    b.Margin({0, 0, 28, 0});
    strip.Children().Append(b);
  }
  progress.Children().Append(progress_);
  progress.Children().Append(line);
  progress.Children().Append(strip);
  C::Grid::SetRow(progress, 1);
  root_.Children().Append(progress);

  // Body: loss chart with its axes, metric tiles on the right.
  C::Grid body;
  body.ColumnDefinitions().Append(column(X::GridLengthHelper::Auto()));
  body.ColumnDefinitions().Append(column(star()));
  C::Grid plot;
  plot.ColumnDefinitions().Append(column(pixels(kAxisW)));
  plot.ColumnDefinitions().Append(column(X::GridLengthHelper::Auto()));
  for (int i = 0; i < 3; ++i)
    plot.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));
  y_labels_ = C::Canvas();
  y_labels_.Width(kAxisW);
  y_labels_.Height(kChartH);
  chart_ = C::Canvas();
  chart_.Width(kChartW);
  chart_.Height(kChartH);
  chart_.Background(paint(kSurface));
  C::Grid::SetColumn(chart_, 1);
  idle_msg_ = label(16, kMuted);
  idle_msg_.Text(L"No job yet · waiting for a job from the host");
  idle_msg_.TextAlignment(X::TextAlignment::Center);
  idle_msg_.HorizontalAlignment(X::HorizontalAlignment::Center);
  idle_msg_.VerticalAlignment(X::VerticalAlignment::Center);
  idle_msg_.Width(kChartW);
  C::Grid::SetColumn(idle_msg_, 1);
  grid_ = C::Canvas();
  now_ = S::Line();
  now_.Y1(0);
  now_.Y2(kChartH);
  now_.Stroke(paint(kAccent));
  now_.StrokeThickness(1.5);
  now_.StrokeDashArray(dashes(2, 3));
  now_.Visibility(X::Visibility::Collapsed);
  raw_ = S::Polyline();
  raw_.Stroke(paint(kGrid));
  raw_.StrokeThickness(1.5);
  smooth_ = S::Polyline();
  smooth_.Stroke(paint(kAccent));
  smooth_.StrokeThickness(3);
  C::Grid x_axis;
  x_axis.Width(kChartW);
  x0_ = label(12, kMuted);
  x_end_ = label(12, kMuted);
  x_end_.HorizontalAlignment(X::HorizontalAlignment::Right);
  x_axis.Children().Append(x0_);
  x_axis.Children().Append(x_end_);
  C::Grid::SetRow(x_axis, 1);
  C::Grid::SetColumn(x_axis, 1);
  axis_ = label(12, kMuted);
  axis_.Text(L"trunk training loss · x = step over the full schedule");
  C::Grid::SetRow(axis_, 2);
  C::Grid::SetColumn(axis_, 1);
  plot.Children().Append(y_labels_);
  plot.Children().Append(chart_);
  plot.Children().Append(idle_msg_);
  plot.Children().Append(x_axis);
  plot.Children().Append(axis_);
  body.Children().Append(plot);

  C::StackPanel side;
  side.Margin({24, 0, 0, 0});
  section_ = label(12, kMuted);
  section_.Margin({0, 0, 0, 4});
  section_.Visibility(X::Visibility::Collapsed);
  C::Grid tiles;
  tiles.ColumnDefinitions().Append(column(star()));
  tiles.ColumnDefinitions().Append(column(star()));
  for (int i = 0; i < 4; ++i)
    tiles.RowDefinitions().Append(row(X::GridLengthHelper::Auto()));
  loss_ = tile(tiles, 0, L"trunk loss");
  rate_ = tile(tiles, 1, L"tokens / s");
  tokens_ = tile(tiles, 2, L"tokens processed");
  elapsed_ = tile(tiles, 3, L"elapsed");
  eta_ = tile(tiles, 4, L"remaining");
  gpu_ = tile(tiles, 5, L"GPU / wall time");
  memory_ = tile(tiles, 6, L"peak memory");
  checkpoint_ = tile(tiles, 7, L"checkpoint");
  side.Children().Append(section_);
  side.Children().Append(tiles);
  C::Grid::SetColumn(side, 1);
  body.Children().Append(side);
  C::Grid::SetRow(body, 2);
  root_.Children().Append(body);

  device_ = label(12, kMuted);
  device_.Text(L"Starting GPU worker… Jobs are controlled from the host.");
  C::Grid::SetRow(device_, 3);
  root_.Children().Append(device_);

  show_idle();
  show_worker();
  timer_ = X::DispatcherTimer();
  timer_.Interval(std::chrono::seconds(1));
  timer_.Tick([this](auto &&, auto &&) { refresh(); });
  timer_.Start();
  // Idle used to release this; Xbox then suspended during job gaps.
  keep_display(true);
}

void DashboardView::refresh() {
  try {
    if (!device_shown_)
      show_device();
    show_worker();
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
    const auto modified = std::filesystem::last_write_time(status);
    const double age =
        std::chrono::duration<double>(
            std::filesystem::file_time_type::clock::now() - modified)
            .count();
    if (status_time_ && *status_time_ == modified) {
      show_freshness(e0ui::Json{{"state", last_state_}}, age);
      return;
    }
    const auto content = e0::read_json(status);
    show_status(content, age);
    status_time_ = modified;
  } catch (...) {
    // A status file replaced mid-read is retried on the next tick.
  }
}

void DashboardView::reset_metrics() {
  for (auto *t : {&loss_, &rate_, &tokens_, &elapsed_, &eta_, &gpu_, &memory_,
                  &checkpoint_}) {
    set_text(t->value, winrt::hstring(L"—"));
    set_text(t->note, winrt::hstring());
  }
  for (auto const &b : branch_)
    set_text(b, winrt::hstring());
  set_text(progress_text_, winrt::hstring());
  set_text(x0_, winrt::hstring());
  set_text(x_end_, winrt::hstring());
}

void DashboardView::show_job(const std::filesystem::path &job) {
  idle_ = false;
  idle_msg_.Visibility(X::Visibility::Collapsed);
  status_time_.reset();
  history_.clear();
  previous_ = e0ui::Json::object();
  segment_ = e0ui::Json::object();
  tokens_per_second_ = 0;
  schedule_.reset();
  ticks_ = {};
  now_x_ = -1;
  last_state_.clear();
  const auto content = e0::read_json(job);
  current_ = job;
  last_job_id_ = content.value("job_id", e0::path_text(job.stem().stem()));
  set_text(job_, "FloppyLM E0 · " + last_job_id_);
  set_text(state_, winrt::hstring(L"RUNNING"));
  recolor(state_, paint(kAccent));
  set_text(config_, e0ui::describe_job(content));
  set_text(fresh_, winrt::hstring());
  section_.Visibility(X::Visibility::Collapsed);
  reset_metrics();
  recolor(progress_, paint(kAccent));
  progress_.IsIndeterminate(true);
  chart_.Children().Clear();
  y_labels_.Children().Clear();
  marker_tags_.clear();
  training_job_ = content.value("schema", "") == "floppylm.e0.job.v1";
  // Acceptance fixtures report through .actual.json, not status.json.
  set_text(phase_, training_job_
                       ? winrt::hstring(L"waiting for the first status")
                       : h("acceptance fixture · " +
                           content.value("schema", std::string("unknown"))));
}

void DashboardView::draw_markers() {
  chart_.Children().Clear();
  grid_.Children().Clear();
  y_labels_.Children().Clear();
  ticks_ = {};
  marker_tags_.clear();
  chart_.Children().Append(grid_);
  const std::pair<uint64_t, wchar_t const *> markers[] = {
      {schedule_->warmup, L"warmup"},
      {schedule_->cooldown_starts[0], L"cooldown T"},
      {schedule_->cooldown_starts[1], L"cooldown 2T"},
      {schedule_->cooldown_starts[2], L"cooldown 4T"}};
  std::vector<float> xs, widths;
  for (const auto &[step, text] : markers) {
    const float x = float(double(step) / double(schedule_->ends[2]) * kChartW);
    S::Line line;
    line.X1(x);
    line.X2(x);
    line.Y1(0);
    line.Y2(kChartH);
    line.Stroke(paint(kGrid));
    line.StrokeThickness(1);
    line.StrokeDashArray(dashes(4, 4));
    chart_.Children().Append(line);
    auto tag = label(12, kMuted);
    tag.Text(text);
    tag.Measure({std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::infinity()});
    xs.push_back(x);
    widths.push_back(tag.DesiredSize().Width);
    marker_tags_.push_back(tag);
  }
  // Labels of nearby markers stack in rows and stay inside the chart.
  const auto slots = e0ui::place_labels(xs, widths, kChartW);
  // The curve is plotted below the label rows so it never crosses them.
  unsigned rows = 0;
  for (const auto &slot : slots)
    rows = std::max(rows, slot.row + 1);
  band_ = kLabelTop + kLabelRow * rows + kLabelGap;
  for (size_t i = 0; i < marker_tags_.size(); ++i) {
    C::Canvas::SetLeft(marker_tags_[i], slots[i].left);
    C::Canvas::SetTop(marker_tags_[i], kLabelTop + kLabelRow * slots[i].row);
    chart_.Children().Append(marker_tags_[i]);
  }
  chart_.Children().Append(now_);
  chart_.Children().Append(raw_);
  chart_.Children().Append(smooth_);
  set_text(x0_, winrt::hstring(L"0"));
  set_text(x_end_, "step " + e0ui::format_count(schedule_->ends[2]));
}

void DashboardView::show_branches(const e0ui::Json &status) {
  static const char *const names[] = {"T", "2T", "4T"};
  const auto views = e0ui::branch_views(status, *schedule_);
  for (unsigned b = 0; b < 3; ++b) {
    const auto &v = views[b];
    std::string text = std::string("branch ") + names[b] + " · ";
    uint32_t color = kMuted;
    if (v.state == e0ui::BranchView::done) {
      text += v.seconds >= 0 ? "done in " + e0ui::format_duration(v.seconds)
                             : "done";
      color = kGreen;
    } else if (v.state == e0ui::BranchView::cooling) {
      text += "cooldown " + std::to_string(v.percent) + " %";
      color = kAccent;
    } else {
      text += "pending";
    }
    set_text(branch_[b], text);
    recolor(branch_[b], paint(color));
    // Marker 0 is warmup; markers 1-3 start the three cooldowns.
    if (b + 1 < marker_tags_.size())
      recolor(marker_tags_[b + 1], paint(color));
  }
}

void DashboardView::show_status(const e0ui::Json &status, double age) {
  const std::string state = status.value("state", "unknown");
  last_state_ = state;
  std::string upper = state;
  for (auto &c : upper)
    c = char(std::toupper(static_cast<unsigned char>(c)));
  set_text(state_, upper);
  recolor(state_, paint(state_color(state)));
  recolor(progress_, paint(state_color(state)));
  if (!schedule_) {
    schedule_ = e0ui::schedule_from_status(status);
    progress_.IsIndeterminate(false);
    if (!schedule_) {
      // A failure before run_job starts carries only job_id, state and error.
      set_text(phase_,
               status.value("error", std::string("no schedule published")));
      return;
    }
    draw_markers();
  }
  const auto &s = *schedule_;
  show_freshness(status, age);
  set_text(phase_, state == "failed"
                       ? status.value("error", std::string("failed"))
                       : e0ui::phase(status, s));
  const uint64_t done = e0ui::executed_steps(status, s),
                 total = s.total_steps();
  const double fraction = double(std::min(done, total)) / double(total);
  progress_.Value(100.0 * fraction);
  // Optimizer steps count trunk and cooldowns; checkpoints use the trunk step.
  set_text(progress_text_, e0ui::format_count(done) + " / " +
                               e0ui::format_count(total) +
                               " optimizer steps · " +
                               fixed(100.0 * fraction, 0) + " %");
  show_branches(status);

  if (status.contains("loss_series") && status.at("loss_series").is_array())
    history_.assign(status.at("loss_series"));
  if (status.contains("last_loss") && status.at("last_loss").is_number()) {
    const double loss = status.at("last_loss").get<double>();
    // Always append last_loss so a series with no usable samples, or one that
    // lags the current trunk step, still plots the published point.
    history_.add(status.value("trunk_step", uint64_t(0)), loss);
    set_text(loss_.value, e0ui::format_loss(loss));
  }
  if (previous_.empty() || done != e0ui::executed_steps(previous_, s)) {
    const auto r = e0ui::rate(previous_, status, s);
    if (r.valid)
      tokens_per_second_ = r.tokens_per_second;
    previous_ = status;
  }
  // wall_seconds restarts with each resumed segment, and so does the average.
  const double wall = status.value("wall_seconds", 0.0);
  if (segment_.empty() || wall < segment_.value("wall_seconds", 0.0))
    segment_ = status;
  const auto average = e0ui::rate(segment_, status, s);
  set_text(rate_.value, tokens_per_second_ > 0
                            ? e0ui::format_count(uint64_t(tokens_per_second_))
                            : std::string("—"));
  set_text(rate_.note, average.valid ? "avg " + e0ui::format_count(uint64_t(
                                                    average.tokens_per_second))
                                     : std::string("last 64 steps"));
  set_text(tokens_.value,
           e0ui::format_compact(double(done) * double(s.tokens_per_step)));
  set_text(tokens_.note,
           "of " +
               e0ui::format_compact(double(total) * double(s.tokens_per_step)));
  set_text(elapsed_.value,
           wall > 0 ? e0ui::format_duration(wall) : std::string("—"));
  set_text(elapsed_.note, std::string("this run segment"));
  const double gpu = status.value("gpu_seconds", 0.0);
  set_text(gpu_.value,
           wall > 0 ? fixed(100.0 * gpu / wall, 0) + " %" : std::string("—"));
  set_text(gpu_.note, "GPU " + e0ui::format_duration(gpu));
  set_text(memory_.value, e0ui::format_megabytes(
                              status.value("peak_memory_bytes", uint64_t(0))));
  const double eta = e0ui::eta_seconds(status, s, tokens_per_second_);
  set_text(eta_.value, state == "completed" ? std::string("done")
                       : eta >= 0           ? "~" + e0ui::format_duration(eta)
                                            : std::string("—"));
  set_text(eta_.note,
           state == "running" ? std::string("estimate") : std::string());
  // run_job rewrites the checkpoint at the current trunk step with each status.
  if (status.contains("checkpoint") && status.at("checkpoint").is_object()) {
    set_text(checkpoint_.value, "trunk " + e0ui::format_count(status.value(
                                              "trunk_step", uint64_t(0))));
    set_text(checkpoint_.note,
             e0ui::format_megabytes(
                 status.at("checkpoint").value("bytes", uint64_t(0))));
  }
  draw_chart(status);
}

void DashboardView::show_freshness(const e0ui::Json &status, double age) {
  const bool stale =
      status.value("state", "") == "running" && age > kStaleSeconds;
  set_text(fresh_, (stale ? "no update for " : "updated ") +
                       e0ui::format_duration(age) + (stale ? "" : " ago"));
  if (fresh_stale_ != stale) {
    recolor(fresh_, paint(stale ? kAmber : kMuted));
    fresh_stale_ = stale;
  }
}

void DashboardView::show_idle() {
  // Layout changes once; worker.json still updates through show_worker.
  if (idle_)
    return;
  idle_ = true;
  current_.clear();
  set_text(job_, winrt::hstring(L"FloppyLM E0"));
  set_text(state_, winrt::hstring(L"IDLE"));
  recolor(state_, paint(kMuted));
  set_text(config_, last_job_id_.empty()
                        ? std::string()
                        : "last job " + last_job_id_ +
                              (last_state_.empty() ? "" : " · " + last_state_));
  set_text(fresh_, winrt::hstring());
  recolor(fresh_, paint(kMuted));
  fresh_stale_ = false;
  // The tiles keep the last job's values; say so.
  set_text(section_, winrt::hstring(L"last job"));
  section_.Visibility(last_job_id_.empty() ? X::Visibility::Collapsed
                                           : X::Visibility::Visible);
  set_text(phase_, winrt::hstring(L"Idle · waiting for a job from the host. "
                                  L"Keep this app open during training."));
  progress_.IsIndeterminate(false);
  if (last_state_.empty())
    progress_.Value(0);
  recolor(progress_, paint(state_color(last_state_)));
  now_.Visibility(X::Visibility::Collapsed);
  idle_msg_.Visibility(last_job_id_.empty() ? X::Visibility::Visible
                                            : X::Visibility::Collapsed);
}

void DashboardView::show_worker() {
  const auto path = local_ / L"worker.json";
  e0ui::WorkerView view;
  try {
    if (std::filesystem::exists(path)) {
      const double age =
          std::chrono::duration<double>(
              std::filesystem::file_time_type::clock::now() -
              std::filesystem::last_write_time(path))
              .count();
      view = e0ui::worker_from_json(e0::read_json(path), age);
    }
  } catch (...) {
  }
  set_text(live_, e0ui::worker_line(view));
  const uint32_t color = !view.present     ? kMuted
                         : view.faulted    ? kRed
                         : view.stale      ? kAmber
                         : view.state == "running" ? kAccent
                         : view.state == "ready"   ? kGreen
                                                   : kMuted;
  recolor(live_, paint(color));
}

void DashboardView::show_device() {
  const auto path = local_ / L"device.json";
  if (!std::filesystem::exists(path))
    return;
  const auto d = e0::read_json(path);
  if (d.value("state", "") == "failed") {
    device_.Text(h("GPU worker failed: " +
                   d.value("error", std::string("unknown error"))));
    device_.Foreground(paint(kRed));
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
  // Hold for the process lifetime. Idle gaps used to release this and Xbox
  // then suspended the UWP app; OS steal-focus still suspends.
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

void DashboardView::draw_chart(const e0ui::Json &status) {
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
         e0ui::plot(points, raw_values, *schedule_, kChartW, kChartH - band_))
      raw.Append({x, y + band_});
  for (auto [x, y] :
       e0ui::plot(points, main_values, *schedule_, kChartW, kChartH - band_))
    main.Append({x, y + band_});
  raw_.Points(raw);
  smooth_.Points(main);
  set_text(axis_, smoothing
                      ? winrt::hstring(L"trunk training loss (EMA bold, raw "
                                       L"faint) · x = step over the full "
                                       L"schedule")
                      : winrt::hstring(L"trunk training loss · x = step over "
                                       L"the full schedule"));

  // Current position on the schedule.
  const float x =
      float(std::min(1.0, double(e0ui::chart_step(status, *schedule_)) /
                              double(schedule_->ends[2])) *
            kChartW);
  if (x != now_x_) {
    now_.X1(x);
    now_.X2(x);
    now_x_ = x;
  }
  now_.Visibility(status.value("state", "") == "running"
                      ? X::Visibility::Visible
                      : X::Visibility::Collapsed);

  // Loss gridlines move only when the plotted range does.
  const auto range = e0ui::loss_range(points);
  const auto ticks =
      points.empty() ? e0ui::Ticks{} : e0ui::nice_ticks(range.lo, range.hi);
  if (ticks == ticks_ && range.lo == range_.lo && range.hi == range_.hi)
    return;
  ticks_ = ticks;
  range_ = range;
  grid_.Children().Clear();
  y_labels_.Children().Clear();
  for (double v : ticks.values) {
    const float y = band_ + e0ui::loss_y(range, v, kChartH - band_);
    S::Line line;
    line.X1(0);
    line.X2(kChartW);
    line.Y1(y);
    line.Y2(y);
    line.Stroke(paint(kGridFaint));
    line.StrokeThickness(1);
    grid_.Children().Append(line);
    auto tag = label(12, kMuted);
    tag.Text(h(fixed(v, ticks.digits)));
    tag.Width(kAxisW - 6);
    tag.TextAlignment(X::TextAlignment::Right);
    C::Canvas::SetTop(tag,
                      std::clamp(double(y) - 8, 0.0, double(kChartH) - 16));
    y_labels_.Children().Append(tag);
  }
}
} // namespace xgpu
