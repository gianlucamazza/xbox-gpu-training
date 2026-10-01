cmake_minimum_required(VERSION 3.20)
project(xbox_gpu_training LANGUAGES CXX)

# Builds the E0 trainer (xgpu_e0, xgpu_e0_train, e0_tensor.cso) and the diagnostic
# host (xbox_gpu_host: DirectX 12 hello + matmul + FLP2 + STE/AdamW + stream + QAT/WSD).
# Compiles as a no-GPU smoke check on non-Windows (reports BLOCKED: no D3D12 device).
# CPU tests (--cpu-ref / --bench matmul / --forward-fixture / --grad-check /
# --train-step / --stream-stress / --qat-smoke) stay green without a GPU.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XGPU_BUILD_HOST "Build the C++ host" ON)
option(XGPU_BUILD_EXAMPLES "Build example hosts (hello_compute)" ON)

# add_subdirectory(src/cpp) otherwise places the exe under build/src/cpp/<Config>/.
# Put binaries at the build root so CI and local runs can find them under
# build/<Config>/ or (if someone drops the override) build/src/cpp/<Config>/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/${_cfg}")
endforeach()

# Do not pin -G "Visual Studio 17 2022". windows-latest may ship a newer VS.
# Local: cmake -S . -B build -A x64   (auto-detect generator)

if(WIN32)
  find_program(XGPU_DXC NAMES dxc dxc.exe)
  if(NOT XGPU_DXC)
    file(GLOB _XGPU_DXC_HITS
      "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64/dxc.exe")
    if(_XGPU_DXC_HITS)
      list(SORT _XGPU_DXC_HITS COMPARE NATURAL ORDER DESCENDING)
      list(GET _XGPU_DXC_HITS 0 XGPU_DXC)
    endif()
  endif()
  if(XGPU_DXC)
    message(STATUS "dxc: ${XGPU_DXC}")
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/e0_tensor.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain -Gis
        -Fo "${CMAKE_BINARY_DIR}/e0_tensor.cso"
        "${CMAKE_SOURCE_DIR}/src/hlsl/e0_tensor.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/e0_tensor.hlsl")
    add_custom_target(xgpu_e0_cso ALL DEPENDS "${CMAKE_BINARY_DIR}/e0_tensor.cso")
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/hello_compute.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/hello_compute.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/hello_compute.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/hello_compute.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain hello_compute.hlsl"
    )
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/matmul.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/matmul.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/matmul.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/matmul.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain matmul.hlsl"
    )
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/matmul_fp32.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMainFP32
              -Fo "${CMAKE_BINARY_DIR}/matmul_fp32.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/matmul.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/matmul.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMainFP32 matmul.hlsl"
    )
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/matmul_fp16.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMainFP16
              -Fo "${CMAKE_BINARY_DIR}/matmul_fp16.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/matmul.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/matmul.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMainFP16 matmul.hlsl"
    )
    add_custom_target(xgpu_hello_cso ALL DEPENDS "${CMAKE_BINARY_DIR}/hello_compute.cso")
    add_custom_target(xgpu_matmul_cso ALL DEPENDS
      "${CMAKE_BINARY_DIR}/matmul.cso"
      "${CMAKE_BINARY_DIR}/matmul_fp32.cso"
      "${CMAKE_BINARY_DIR}/matmul_fp16.cso")
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/rmsnorm.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/rmsnorm.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/rmsnorm.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/rmsnorm.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain rmsnorm.hlsl"
    )
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/rope.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/rope.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/rope.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/rope.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain rope.hlsl"
    )
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/flp2_decode.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/flp2_decode.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/flp2_decode.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/flp2_decode.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain flp2_decode.hlsl"
    )
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/flp2_forward.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/flp2_forward.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/flp2_forward.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/flp2_forward.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain flp2_forward.hlsl"
    )
    add_custom_target(xgpu_flp2_cso ALL DEPENDS
      "${CMAKE_BINARY_DIR}/rmsnorm.cso"
      "${CMAKE_BINARY_DIR}/rope.cso"
      "${CMAKE_BINARY_DIR}/flp2_decode.cso"
      "${CMAKE_BINARY_DIR}/flp2_forward.cso")
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/fakequant_ternary.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/fakequant_ternary.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/fakequant_ternary.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/fakequant_ternary.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain fakequant_ternary.hlsl"
    )
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/matmul_grad.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/matmul_grad.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/matmul_grad.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/matmul_grad.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain matmul_grad.hlsl"
    )
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/relu2_grad.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/relu2_grad.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/relu2_grad.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/relu2_grad.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain relu2_grad.hlsl"
    )
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/ste_backward.cso"
      COMMAND "${XGPU_DXC}" -T cs_6_0 -E CSMain
              -Fo "${CMAKE_BINARY_DIR}/ste_backward.cso"
              "${CMAKE_SOURCE_DIR}/src/hlsl/ste_backward.hlsl"
      DEPENDS "${CMAKE_SOURCE_DIR}/src/hlsl/ste_backward.hlsl"
      COMMENT "dxc -T cs_6_0 -E CSMain ste_backward.hlsl"
    )
    add_custom_target(xgpu_ste_cso ALL DEPENDS
      "${CMAKE_BINARY_DIR}/fakequant_ternary.cso"
      "${CMAKE_BINARY_DIR}/matmul_grad.cso"
      "${CMAKE_BINARY_DIR}/relu2_grad.cso"
      "${CMAKE_BINARY_DIR}/ste_backward.cso")
  else()
    message(STATUS "dxc not found — HLSL compile skipped (not a GPU result)")
  endif()
endif()

if(XGPU_BUILD_HOST)
  add_subdirectory(src/cpp)
  if(XGPU_BUILD_EXAMPLES)
    add_subdirectory(examples/hello-compute)
  endif()
endif()
