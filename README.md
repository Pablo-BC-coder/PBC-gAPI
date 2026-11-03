# PBC-gAPI

PBC-gAPI is a graphics library owned by **Pablo B. C.** It provides a portable CPU rasterizer, optional NVIDIA CUDA, Intel OpenCL compute and OpenGL ES 3 backends, and independent presentation adapters.

## Build and run

A C++17 compiler, CMake 3.20+ and the platform threading library are sufficient for CPU rendering. From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/pbc_cpu 1 PBC_GAPI_CUDA/suzanne.obj build/suzanne.ppm
./build/pbc_cpu 4 PBC_GAPI_CUDA/suzanne.obj build/suzanne-4.ppm
```

`pbc_cpu [threads>=1] [model.obj] [output.ppm]` produces a PPM image without a window or GPU. With no model it draws a UV-mapped procedural checkerboard. For Visual Studio generators, add `--config Release` when building, use `ctest -C Release`, and run executables from `build/Release`.

## Library and examples

- `include/pbc/pbc.hpp`: framebuffer, textures, shader pointers, render passes, geometry and OBJ API.
- `src/`: library implementations; no executable entry points.
- `include/pbc/gpu.hpp`: CUDA device shaders, OpenCL compute fragment programs and OpenGL ES programs.
- `include/pbc/surface.hpp`: HDC, SDL2/Raspbian, Android native-window and EGL adapters.
- `examples/`: CPU, CUDA, Intel OpenCL, GLES, window, benchmark, ESP-IDF and Android programs.
- `tests/`: regressions for geometry, UV, sampling, clipping, depth, concurrency and GPU integration.

Applications link `pbc`, `pbc_cuda` for CUDA, or `pbc_opencl` for OpenCL; they no longer include implementation `.cpp` files. CMake produces static libraries and separate executable examples. ESP-IDF uses `components/pbc`.

## Platforms

| Target | Rendering | Presentation / integration |
| --- | --- | --- |
| ESP32-WROOM-32, ESP32-S3 | CPU, one or multiple FreeRTOS workers | External framebuffer memory; caller supplies an LCD driver |
| Raspberry Pi 4 / VideoCore VI | CPU or GLES 3 through Mesa/V3D | SDL2 using the installed X11, Wayland or KMSDRM driver |
| NVIDIA | CPU, CUDA, or GLES with a supporting driver | HDC on Windows; SDL2 on Raspbian/Linux |
| Intel GPU | CPU, custom OpenCL compute rasterizer, or retained GLES backend | HDC on Windows; SDL2 on Raspbian/Linux |
| AMD | CPU or GLES with a supporting driver | HDC on Windows; SDL2 on Raspbian/Linux |
| LineageOS / Android | CPU or GLES 3, with CPU fallback in the example | NDK `ANativeWindow` / NativeActivity |

ESP32 variants have no desktop graphics GPU: they use the CPU backend. GLES support requires an ES 3.0-capable driver; the vendor name alone does not guarantee it. Build validation does not replace tests on the physical board or GPU. The Intel compute path requires a native Intel GPU OpenCL runtime and does not use OpenGL/Vulkan or silently fall back to CPU. See [Intel OpenCL instructions](docs/opencl.md), [platform instructions](docs/platforms.md) and [API contracts](docs/api.md).

## Optional GPU and window builds

```sh
# Intel GPU: native Intel OpenCL runtime, loader and development headers required.
cmake -S . -B build-intel -DPBC_ENABLE_OPENCL=ON -DPBC_ENABLE_GLES=OFF -DPBC_ENABLE_CUDA=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-intel --parallel 2
./build-intel/pbc_opencl_example PBC_GAPI_CUDA/suzanne.obj build-intel/intel.ppm
./build-intel/pbc_opencl_tests --intel-gpu

# Raspbian / Debian: install libegl-dev libgles-dev libsdl2-dev and a working Mesa/V3D driver.
cmake -S . -B build-gles -DPBC_ENABLE_GLES=ON -DPBC_ENABLE_SDL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-gles --parallel 2
./build-gles/pbc_gles PBC_GAPI_CUDA/suzanne.obj build-gles/suzanne.ppm
./build-gles/pbc_window PBC_GAPI_CUDA/suzanne.obj

# NVIDIA: a supported host compiler, CUDA Toolkit 12.6+ and NVIDIA driver are required.
cmake -S . -B build-cuda -DPBC_ENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=75
cmake --build build-cuda --parallel 2
./build-cuda/pbc_cuda_example PBC_GAPI_CUDA/suzanne.obj build-cuda/suzanne.ppm
```

Choose CUDA architectures for the intended devices, or `native` on a build machine with the actual GPU and CMake 3.24+. `75` is an example for Turing, not an automatic architecture choice for all NVIDIA hardware. Runtime block size and grid size are selected using CUDA occupancy queries. SM count is queried from CUDA; actual GPU core count is optionally queried from NVML. Unsupported queries return zero (unknown). GLES exposes no standard physical-core count. OpenCL reports compute units and work-group limits, which must not be relabeled as physical cores. The strict Intel selector requires a native Intel GPU ICD; PoCL CPU execution is available only through an explicit testing-device selection.

The original `.sln` now delegates to CMake through an NMake project; opening the repository directly as a CMake project in Visual Studio is also supported. Existing tracked `.vs`, `x64/Debug` and prebuilt Windows artifacts are historical and are not used by the new build.

## Optimization and migration

CPU work is partitioned into tiles with one owner per pixel. Optional compact triangle bins reduce repeated bounding-box work; transformed indexed vertices are cached once per draw. CUDA and OpenCL use the same binning/pixel-ownership strategy to avoid depth/color atomic races. OpenCL implements coverage, interpolation, depth, texture filtering and fragment execution in a library compute kernel; it does not delegate those stages to a graphics rasterizer. Its context/queue, bounded program cache and device-buffer capacities are reused. [Optimization notes](docs/optimization.md) describe measurements, memory costs and remaining GPU upload/compilation overhead.

This is a source-level API migration. Use `pbc::FrameBuffer` (`Framebuffer` is an alias), `width()`/`height()`/`view()`, RGBA `Color{r,g,b,a}`, normalized `ImageView::sample()`, integer `texel()`, and `RenderPass` shader pointers. The old `Color{a,r,g,b}`, global symbols, public framebuffer fields, `ShaderTri` entry point, and direct inclusion of `terminalPBC_API.cpp` are replaced. The obsolete `kernel.cu`, `oldKernel.ku` and `terminalPBC_API.cpp` implementations are removed in favor of the library and examples; the OBJ asset is retained.

## Authority and Copyright

This project is the sole property of **Pablo B. C.** 

The author's name and copyright authority must be explicitly preserved and inferred across all source files, documentation, and derivatives, regardless of any future modifications, refactoring, or community contributions.
