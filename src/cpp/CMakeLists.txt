add_library(xgpu_dx12 STATIC
  dx12_device.cpp
  hello_dispatch.cpp
  cpu_matmul.cpp
  cpu_flp2.cpp
  fixture_loader.cpp
  matmul_dispatch.cpp
  flp2_dispatch.cpp
)

target_include_directories(xgpu_dx12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
  target_link_libraries(xgpu_dx12 PUBLIC d3d12 dxgi)
endif()

if(MSVC)
  target_compile_options(xgpu_dx12 PUBLIC /W4 /permissive- /utf-8)
else()
  target_compile_options(xgpu_dx12 PUBLIC -Wall -Wextra -Wpedantic)
endif()

add_executable(xbox_gpu_host
  main.cpp
)

target_link_libraries(xbox_gpu_host PRIVATE xgpu_dx12)

add_custom_command(TARGET xbox_gpu_host POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/src/hlsl/hello_compute.hlsl"
    "$<TARGET_FILE_DIR:xbox_gpu_host>/hello_compute.hlsl"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/src/hlsl/matmul.hlsl"
    "$<TARGET_FILE_DIR:xbox_gpu_host>/matmul.hlsl"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/src/hlsl/rmsnorm.hlsl"
    "$<TARGET_FILE_DIR:xbox_gpu_host>/rmsnorm.hlsl"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/src/hlsl/rope.hlsl"
    "$<TARGET_FILE_DIR:xbox_gpu_host>/rope.hlsl"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/src/hlsl/flp2_decode.hlsl"
    "$<TARGET_FILE_DIR:xbox_gpu_host>/flp2_decode.hlsl"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/src/hlsl/flp2_forward.hlsl"
    "$<TARGET_FILE_DIR:xbox_gpu_host>/flp2_forward.hlsl"
  COMMENT "Copy Fase 0–2 HLSL next to xbox_gpu_host"
)

install(TARGETS xbox_gpu_host RUNTIME DESTINATION bin)
