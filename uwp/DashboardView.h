#pragma once
// On-console E0 dashboard (10-foot XAML, built in code). Read-only: it polls
// job.json and status.json once per second on the UI thread and never touches
// the trainer. Layout targets 960x540 effective pixels inside the default
// TV-safe bounds; colours are an explicit palette within the TV-safe range.
#include "pch.h"

#include "dashboard.h"
#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xgpu {
class DashboardView {
public:
  DashboardView(std::filesystem::path local,
                std::function<std::filesystem::path()> active_job);
  winrt::Windows::UI::Xaml::UIElement root() const { return root_; }

private:
  using TextBlock = winrt::Windows::UI::Xaml::Controls::TextBlock;
  // A metric: caption, large value and a muted note under it.
  struct Tile {
    TextBlock value{nullptr}, note{nullptr};
  };

  void refresh();
  void show_job(const std::filesystem::path &job);
  void show_status(const e0ui::Json &status, double age_seconds);
  void show_idle();
  void show_freshness(const e0ui::Json &status, double age_seconds);
  void show_device();
  void show_worker();
  void show_branches(const e0ui::Json &status);
  void reset_metrics();
  void keep_display(bool on);
  void draw_markers();
  void draw_chart(const e0ui::Json &status);

  // Brushes are created once per colour: a new brush invalidates rendering
  // even when its colour is unchanged.
  winrt::Windows::UI::Xaml::Media::SolidColorBrush paint(uint32_t rgb);
  TextBlock label(double size, uint32_t rgb);
  Tile tile(winrt::Windows::UI::Xaml::Controls::Grid const &grid, int index,
            wchar_t const *caption);

  std::filesystem::path local_, current_;
  std::function<std::filesystem::path()> active_job_;
  bool training_job_ = false, idle_ = false;
  std::optional<std::filesystem::file_time_type> status_time_;
  bool fresh_stale_ = false;
  std::optional<e0ui::Schedule> schedule_;
  e0ui::LossHistory history_;
  e0ui::Json previous_ = e0ui::Json::object(), segment_ = e0ui::Json::object();
  double tokens_per_second_ = 0;
  std::string last_state_, last_job_id_;
  bool display_held_ = false, device_shown_ = false;
  e0ui::Ticks ticks_;
  e0ui::LossRange range_;
  float now_x_ = -1, band_ = 0;
  std::unordered_map<uint32_t, winrt::Windows::UI::Xaml::Media::SolidColorBrush>
      brushes_;

  winrt::Windows::System::Display::DisplayRequest display_{nullptr};
  winrt::Windows::UI::Xaml::DispatcherTimer timer_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
  TextBlock job_{nullptr}, state_{nullptr}, config_{nullptr}, fresh_{nullptr},
      live_{nullptr}, idle_msg_{nullptr}, phase_{nullptr},
      progress_text_{nullptr}, device_{nullptr}, axis_{nullptr}, x0_{nullptr},
      x_end_{nullptr}, section_{nullptr};
  std::array<TextBlock, 3> branch_{nullptr, nullptr, nullptr};
  std::vector<TextBlock> marker_tags_;
  winrt::Windows::UI::Xaml::Controls::ProgressBar progress_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Canvas chart_{nullptr}, grid_{nullptr},
      y_labels_{nullptr};
  winrt::Windows::UI::Xaml::Shapes::Line now_{nullptr};
  winrt::Windows::UI::Xaml::Shapes::Polyline raw_{nullptr}, smooth_{nullptr};
  Tile loss_, rate_, tokens_, elapsed_, eta_, gpu_, memory_, checkpoint_;
};
} // namespace xgpu
