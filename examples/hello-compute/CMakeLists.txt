add_executable(hello_compute
  main.cpp
)

target_link_libraries(hello_compute PRIVATE xgpu_dx12)

add_custom_command(TARGET hello_compute POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/src/hlsl/hello_compute.hlsl"
    "$<TARGET_FILE_DIR:hello_compute>/hello_compute.hlsl"
  COMMENT "Copy hello_compute.hlsl next to hello_compute"
)

install(TARGETS hello_compute RUNTIME DESTINATION bin)
