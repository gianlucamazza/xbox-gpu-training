cmake_minimum_required(VERSION 3.20)
project(xbox_gpu_training LANGUAGES CXX)

# Scaffold host only. DirectX 12 device creation is Fase 0 work.
# This target must compile on Windows (MSVC) and as a no-GPU smoke check elsewhere.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XGPU_BUILD_HOST "Build the C++ host stub" ON)

# add_subdirectory(src/cpp) otherwise places the exe under build/src/cpp/<Config>/.
# Put it at the build root so CI and local runs can find xbox_gpu_host consistently.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/${_cfg}")
endforeach()

if(XGPU_BUILD_HOST)
  add_subdirectory(src/cpp)
endif()
