cmake_minimum_required(VERSION 3.20)
project(xbox_gpu_training LANGUAGES CXX)

# Scaffold host only. DirectX 12 device creation is Fase 0 work.
# This target must compile on Windows (MSVC) and as a no-GPU smoke check elsewhere.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XGPU_BUILD_HOST "Build the C++ host stub" ON)

if(XGPU_BUILD_HOST)
  add_subdirectory(src/cpp)
endif()
