#pragma once
// On-console E0 dashboard (10-foot XAML, built in code). Read-only: it polls
// job.json and status.json once per second on the UI thread and never touches
// the trainer. Layout targets 960x540 effective pixels inside the default
// TV-safe bounds; colours are an explicit palette within the TV-safe range.
#include "pch.h"

#include "dashboard.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace xgpu {
class DashboardView {
public:
  DashboardView(std::filesystem::path local,
                std::function<std::filesystem::path()> active_job);
  winrt::Windows::UI::Xaml::UIElement root() const { return root_; }

private:
  void refresh();
  void show_job(const std::filesystem::path &job);
  void show_status(const e0ui::Json &status, double age_seconds);
  void show_idle();
  void show_freshness(const e0ui::Json &status, double age_seconds);
  void show_device();
  void keep_display(bool on);
  void draw_markers();
  void draw_chart();

  std::filesystem::path local_, current_;
  std::function<std::filesystem::path()> active_job_;
  bool training_job_ = false, idle_ = false;
  std::optional<std::filesystem::file_time_type> status_time_;
  bool fresh_stale_ = false;
  std::optional<e0ui::Schedule> schedule_;
  e0ui::LossHistory history_;
  e0ui::Json previous_ = e0ui::Json::object();
  double tokens_per_second_ = 0;
  std::string last_state_;
  bool display_held_ = false, device_shown_ = false;

  winrt::Windows::System::Display::DisplayRequest display_{nullptr};
  winrt::Windows::UI::Xaml::DispatcherTimer timer_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock job_{nullptr}, state_{nullptr},
      fresh_{nullptr}, phase_{nullptr}, progress_text_{nullptr},
      loss_hi_{nullptr}, loss_lo_{nullptr}, device_{nullptr}, axis_{nullptr};
  winrt::Windows::UI::Xaml::Controls::ProgressBar progress_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Canvas chart_{nullptr};
  winrt::Windows::UI::Xaml::Shapes::Polyline raw_{nullptr}, smooth_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock loss_{nullptr}, rate_{nullptr},
      gpu_{nullptr}, memory_{nullptr}, eta_{nullptr}, tokens_{nullptr};
};
} // namespace xgpu
