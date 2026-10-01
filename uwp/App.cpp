#include "App.h"
#include "../src/cpp/e0/model.h"
#include "DashboardView.h"
#include "App.g.cpp"
#include "pch.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

#ifndef XGPU_COMMIT
#define XGPU_COMMIT "unknown"
#endif
namespace {
std::filesystem::path inbox;
std::mutex active_mutex;
std::condition_variable active_changed;
std::filesystem::path active_job;
std::unique_ptr<xgpu::DashboardView> dashboard;
void worker() {
  winrt::init_apartment();
  try {
    auto local = std::filesystem::path(
        winrt::Windows::Storage::ApplicationData::Current()
            .LocalFolder()
            .Path()
            .c_str());
    inbox = local / L"inbox";
    std::filesystem::create_directories(inbox);
    auto shader = std::filesystem::path(
                      winrt::Windows::ApplicationModel::Package::Current()
                          .InstalledLocation()
                          .Path()
                          .c_str()) /
                  L"Assets" / L"e0_tensor.cso";
    auto kernel = e0::make_gpu(e0::path_text(shader));
    e0::atomic_json(
        local / L"device.json",
        {{"schema", "floppylm.device.v1"},
         {"hardware_gpu", kernel->hardware()},
         {"adapter", kernel->adapter()},
         {"package",
          winrt::to_string(winrt::Windows::ApplicationModel::Package::Current()
                               .Id()
                               .FullName())},
         {"state", "ready"},
         {"capabilities", e0::capabilities()},
         {"commit", XGPU_COMMIT}});
    while (true) {
      for (const auto &entry : std::filesystem::directory_iterator(inbox)) {
        if (entry.path().extension() != L".ready")
          continue;
        auto stem = entry.path().stem().wstring();
        auto claimed = inbox / (stem + L".claimed");
        if (!MoveFileExW(entry.path().c_str(), claimed.c_str(),
                         MOVEFILE_REPLACE_EXISTING))
          continue;
        auto job = inbox / (stem + L".job.json");
        {
          std::lock_guard<std::mutex> lock(active_mutex);
          active_job = job;
        }
        try {
          const auto content = e0::read_json(job);
          if (content.contains("job_id") &&
              content.at("job_id").get<std::string>() != winrt::to_string(stem))
            throw std::runtime_error("job_id must match job file name");
          if (content.value("schema", "") == "floppylm.e0.fixture.v1")
            e0::atomic_json(inbox / (stem + L".actual.json"),
                            e0::fixture_report(content, *kernel));
          else if (content.value("schema", "") == "floppylm.e0.kernels.v1")
            e0::atomic_json(inbox / (stem + L".actual.json"),
                            e0::kernel_fixture_report(content, *kernel));
          else if (content.value("schema", "") == "floppylm.e0.optimizer.v1")
            e0::atomic_json(inbox / (stem + L".actual.json"),
                            e0::optimizer_fixture_report(content));
          else
            e0::run_job(job, *kernel, content.value("stop_after", uint64_t(0)));
        } catch (const std::exception &error) {
          auto out = inbox / L"results" / stem;
          std::filesystem::create_directories(out);
          e0::Json report = e0::Json::object();
          try {
            report = e0::read_json(out / L"status.json");
          } catch (...) {
          }
          report["state"] = "failed";
          report["error"] = error.what();
          report["job_id"] = winrt::to_string(stem);
          e0::atomic_json(out / L"status.json", report);
        }
        {
          std::lock_guard<std::mutex> lock(active_mutex);
          active_job.clear();
        }
        active_changed.notify_all();
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  } catch (const std::exception &error) {
    auto local = std::filesystem::path(
        winrt::Windows::Storage::ApplicationData::Current()
            .LocalFolder()
            .Path()
            .c_str());
    e0::atomic_json(local / L"device.json", {{"state", "failed"},
                                             {"error", error.what()},
                                             {"hardware_gpu", false},
                                             {"commit", XGPU_COMMIT}});
  }
}
} // namespace
namespace winrt::Xgpu::implementation {
App::App() {
  Suspending([](auto const &, auto const &args) {
    auto deferral = args.SuspendingOperation().GetDeferral();
    {
      std::lock_guard<std::mutex> lock(active_mutex);
      if (!active_job.empty()) {
        std::ofstream file(active_job.parent_path() /
                           (active_job.stem().stem().wstring() + L".cancel"));
        file << "suspend";
      }
    }
    std::thread([deferral]() mutable {
      std::unique_lock<std::mutex> lock(active_mutex);
      active_changed.wait_for(lock, std::chrono::seconds(4),
                              [] { return active_job.empty(); });
      lock.unlock();
      deferral.Complete();
    }).detach();
  });
}
void App::OnLaunched(
    Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const &) {
  auto window = Windows::UI::Xaml::Window::Current();
  if (!window.Content()) {
    auto local = std::filesystem::path(
        Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str());
    e0::Json probe = e0::Json::object();
    if (std::filesystem::exists(local / L"idle-probe.json"))
      probe = e0::read_json(local / L"idle-probe.json");
    if (probe.value("blank", false)) {
      Windows::UI::Xaml::Controls::Grid blank;
      blank.Background(Windows::UI::Xaml::Media::SolidColorBrush(
          Windows::UI::ColorHelper::FromArgb(255, 28, 28, 32)));
      window.Content(blank);
    } else {
      dashboard = std::make_unique<xgpu::DashboardView>(local, [] {
        std::lock_guard<std::mutex> lock(active_mutex);
        return active_job;
      });
      dashboard->diagnostic_controls(probe.value("progress", true), probe.value("timer", true));
      window.Content(dashboard->root());
    }
    if (probe.value("worker", true)) {
      std::thread(worker).detach();
    } else {
      e0::atomic_json(local / L"device.json", {{"state", "diagnostic"},
          {"hardware_gpu", false}, {"commit", XGPU_COMMIT},
          {"package", winrt::to_string(Windows::ApplicationModel::Package::Current().Id().FullName())},
          {"probe", probe}});
    }

  }
  window.Activate();
}
} // namespace winrt::Xgpu::implementation
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  try {
    winrt::Windows::UI::Xaml::Application::Start(
        [](auto &&) { winrt::make<winrt::Xgpu::implementation::App>(); });
    return 0;
  } catch (...) {
    return 1;
  }
}
