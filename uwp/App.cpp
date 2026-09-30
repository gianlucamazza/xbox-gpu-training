#include "App.h"
#include "../src/cpp/e0/model.h"
#include "App.g.cpp"
#include "pch.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#ifndef XGPU_COMMIT
#define XGPU_COMMIT "unknown"
#endif
namespace {
std::filesystem::path inbox;
std::mutex active_mutex;
std::filesystem::path active_job;
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
          if (content.value("schema", "") == "floppylm.e0.fixture.v1")
            e0::atomic_json(inbox / (stem + L".actual.json"),
                            e0::fixture_report(content, *kernel));
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
  Suspending([](auto const &, auto const &) {
    std::lock_guard<std::mutex> lock(active_mutex);
    if (!active_job.empty()) {
      std::ofstream file(active_job.parent_path() /
                         (active_job.stem().stem().wstring() + L".cancel"));
      file << "suspend";
    }
  });
}
void App::OnLaunched(
    Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const &) {
  auto window = Windows::UI::Xaml::Window::Current();
  if (!window.Content()) {
    Windows::UI::Xaml::Controls::TextBlock status;
    status.Text(L"FloppyLM E0 GPU trainer\nKeep this app open during "
                L"training.\nJobs and progress are controlled from the host.");
    status.FontSize(24);
    status.Margin({40, 40, 40, 40});
    window.Content(status);
    std::thread(worker).detach();
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
