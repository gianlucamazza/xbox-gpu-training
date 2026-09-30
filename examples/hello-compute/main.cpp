// Hello compute example host.
// Dispatches src/hlsl/hello_compute.hlsl on a Windows DirectX 12 device.
// English comments only. No tok/s. No console claim.

#include "hello_dispatch.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  std::filesystem::path shader_hint;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--shader" && i + 1 < argc) {
      shader_hint = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: hello_compute [--shader <cso-or-hlsl>]\n";
      std::cout << "Dispatches src/hlsl/hello_compute.hlsl via DirectX 12.\n";
      return 0;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 1;
    }
  }

  const RunReport report = RunHelloCompute(shader_hint);
  std::cout << report.line << "\n";
  if (!report.detail.empty()) {
    std::cout << report.detail << "\n";
  }
  return report.status == RunStatus::Failed ? 1 : 0;
}
