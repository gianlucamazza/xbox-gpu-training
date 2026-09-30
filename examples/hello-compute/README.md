# Hello compute shader (stub)

Fase 0 will add a DirectX 12 host that:

1. Creates a device (public GDK / Windows SDK — not GDKX/ID@Xbox).
2. Compiles or loads `src/hlsl/hello_compute.hlsl`.
3. Dispatches one compute shader thread group.
4. Optionally captures a PIX frame.

Until then this directory is documentation only. The HLSL source already lives at [`src/hlsl/hello_compute.hlsl`](../../src/hlsl/hello_compute.hlsl). The C++ smoke host is [`src/cpp/main.cpp`](../../src/cpp/main.cpp) and does **not** create a GPU device yet.
