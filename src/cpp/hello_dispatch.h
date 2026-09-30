#pragma once

#include <filesystem>
#include <string>

enum class RunStatus {
  Ok,       // dispatched and verified on a D3D12 device
  Blocked,  // no device or no shader — honest blocker, not a GPU success
  Failed,   // a device existed but the pipeline, dispatch, or verify failed
};

struct RunReport {
  RunStatus status = RunStatus::Blocked;
  std::string line;
  std::string detail;
};

// Create a DirectX 12 device, load or compile hello_compute, dispatch once, read back.
// shader_hint may be a .cso, a .hlsl, or empty (search next to the exe and the repo tree).
RunReport RunHelloCompute(const std::filesystem::path& shader_hint = {});
