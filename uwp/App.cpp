#include "pch.h"
#include "App.h"
#include "App.g.cpp"
#include "../src/cpp/e0/model.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace {
std::filesystem::path inbox;
std::mutex active_mutex;
std::filesystem::path active_job;
void worker() {
  winrt::init_apartment();
  try {
    auto local=std::filesystem::path(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str());
    inbox=local/L"inbox";std::filesystem::create_directories(inbox);
    auto shader=std::filesystem::path(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str())/L"Assets"/L"e0_tensor.cso";
    auto kernel=e0::make_gpu(shader.u8string());
    e0::atomic_json(local/L"device.json",{{"schema","floppylm.device.v1"},{"hardware_gpu",kernel->hardware()},
      {"adapter",kernel->adapter()},{"package",winrt::to_string(winrt::Windows::ApplicationModel::Package::Current().Id().FullName())},
      {"state","ready"}});
    while(true) {
      for(const auto& entry:std::filesystem::directory_iterator(inbox)) {
        if(entry.path().extension()!=L".ready") continue;
        auto stem=entry.path().stem().wstring(), claimed=inbox/(stem+L".claimed");
        if(!MoveFileW(entry.path().c_str(),claimed.c_str())) continue;
        auto job=inbox/(stem+L".job.json");
        { std::lock_guard<std::mutex> lock(active_mutex);active_job=job; }
        try {
          const auto content=e0::read_json(job);
          if(content.value("schema","")=="floppylm.e0.fixture.v1")
            e0::atomic_json(inbox/(stem+L".actual.json"),e0::fixture_report(content,*kernel));
          else e0::run_job(job,*kernel);
        }
        catch(const std::exception& error) {
          auto out=inbox/L"results"/stem;std::filesystem::create_directories(out);
          e0::atomic_json(out/L"status.json",{{"state","failed"},{"error",error.what()},{"job_id",winrt::to_string(stem)}});
        }
        { std::lock_guard<std::mutex> lock(active_mutex);active_job.clear(); }
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  } catch(const std::exception& error) {
    auto local=std::filesystem::path(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str());
    e0::atomic_json(local/L"device.json",{{"state","failed"},{"error",error.what()},{"hardware_gpu",false}});
  }
}
}
namespace winrt::Xgpu::implementation {
App::App() {
  Suspending([](auto const&,auto const&) {
    std::lock_guard<std::mutex> lock(active_mutex);
    if(!active_job.empty()) { std::ofstream file(active_job.parent_path()/"cancel");file<<"suspend"; }
  });
}
void App::OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const&) {
  auto window=Windows::UI::Xaml::Window::Current();
  if(!window.Content()) {
    Windows::UI::Xaml::Controls::TextBlock status;
    status.Text(L"FloppyLM E0 GPU trainer\nKeep this app open during training.\nJobs and progress are controlled from the host.");
    status.FontSize(24);status.Margin({40,40,40,40});window.Content(status);
    std::thread(worker).detach();
  }
  window.Activate();
}
}
int __stdcall wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
  try {
    winrt::Windows::UI::Xaml::Application::Start([](auto&&){winrt::make<winrt::Xgpu::implementation::App>();});
    return 0;
  } catch(...) { return 1; }
}
