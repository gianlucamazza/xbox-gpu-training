#pragma once
#include "pch.h"
#include "App.g.h"
namespace winrt::Xgpu::implementation {
struct App : AppT<App> {
  App();
  void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const&);
};
}
namespace winrt::Xgpu::factory_implementation {
struct App : AppT<App,implementation::App> {};
}
