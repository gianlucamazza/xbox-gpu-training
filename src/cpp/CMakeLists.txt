add_executable(xbox_gpu_host
  main.cpp
)

target_include_directories(xbox_gpu_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Windows MSVC: UTF-8 source, no extra DirectX 12 link yet (Fase 0).
if(MSVC)
  target_compile_options(xbox_gpu_host PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(xbox_gpu_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS xbox_gpu_host RUNTIME DESTINATION bin)
