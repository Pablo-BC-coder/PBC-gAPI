<!-- PBC-gAPI — property and copyright authority: Pablo B. C. -->
# Building on supported platforms

The CPU backend remains available on every target. Native builds must still be tested on the actual board/GPU/display. No GPU, desktop session, Android device or serial board is emulated by a successful cross-compilation.

## ESP32-WROOM-32 and ESP32-S3

Use ESP-IDF 5.4.2 and its official installed toolchain/Python requirements:

```sh
. /path/to/esp-idf/export.sh
cd examples/esp32
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

For S3, use a separate build/configuration so a previous ESP32 configuration is not reused:

```sh
idf.py -B build-s3 -D SDKCONFIG=sdkconfig.s3 set-target esp32s3
idf.py -B build-s3 -D SDKCONFIG=sdkconfig.s3 build
idf.py -B build-s3 -D SDKCONFIG=sdkconfig.s3 -p PORT flash monitor
```

The program renders a textured triangle into static 64x64 color/depth buffers and reports the nonbackground pixel count over the serial log. It selects FreeRTOS workers using the configured number of cores. Change `options.threads` to 1 for serial rendering. No PSRAM, LCD, Wi-Fi or GPU is required. Display drivers can consume `frame.view()`; no particular panel or pinout is assumed. If using `loadObjFile`, first mount a supported filesystem through ESP-IDF VFS. Use `loadObj` for in-memory OBJ data, with enough RAM for the parsed mesh.

The CMake component can be added to an existing ESP-IDF application's `EXTRA_COMPONENT_DIRS`. External framebuffer storage can be allocated in PSRAM by the application where available. WROOM-32 has no general-purpose graphics GPU; S3's vector instructions do not make it a CUDA/GLES GPU.

## Raspbian / Raspberry Pi OS and VideoCore VI

Install the C++17/CMake tools, `libegl-dev`, `libgles-dev`, `libsdl2-dev` and the normal Mesa/V3D graphics stack. Enable the supported vc4 KMS display configuration for the OS release. The user needs access to the normal DRM render device/display session.

```sh
cmake -S . -B build-pi -DPBC_ENABLE_GLES=ON -DPBC_ENABLE_SDL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-pi --parallel 2
ctest --test-dir build-pi --output-on-failure
./build-pi/pbc_gles PBC_GAPI_CUDA/suzanne.obj build-pi/suzanne.ppm
./build-pi/pbc_window PBC_GAPI_CUDA/suzanne.obj
```

The GPU example prints the renderer identity: expect the installed V3D driver for VideoCore acceleration, not llvmpipe. SDL2 chooses the distribution's X11/Wayland/KMSDRM window integration and presents rendered CPU images; it does not select the rasterization backend. `EglHeadless` creates a pbuffer; on a headless Mesa system, `EGL_PLATFORM=surfaceless` may be appropriate. Do not force software rendering when checking actual GPU acceleration. Normal application-owned ES contexts can be supplied directly to `GlesRenderer` without using the helper.

## Intel GPU: custom OpenCL compute rasterizer

Install a native Intel GPU OpenCL runtime supported by the card and OS, the OpenCL ICD loader and development headers (`intel-opencl-icd`, `ocl-icd-opencl-dev`, and `clinfo` on applicable Debian-based systems). Linux users need access to the kernel driver's DRM render device; Windows users need the matching Intel driver/runtime. Then:

```sh
cmake -S . -B build-intel -DPBC_ENABLE_OPENCL=ON -DPBC_ENABLE_GLES=OFF -DPBC_ENABLE_CUDA=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-intel --parallel 2
./build-intel/pbc_opencl_example PBC_GAPI_CUDA/suzanne.obj build-intel/intel.ppm
./build-intel/pbc_opencl_tests --intel-gpu
```

This path performs rasterization in the library's OpenCL C kernel without OpenGL, EGL or Vulkan. Intel GPU selection is strict; the presence of only an Intel CPU device is an initialization error, not a reason to fall back. A GPU generation unsupported by its installed native OpenCL driver is also a blocker. [OpenCL instructions](opencl.md) describe shaders, discovery, buffers and explicitly opted-in PoCL testing. The previous GLES backend and CPU rendering remain available independently.

## NVIDIA, Intel and AMD: retained paths

Use the portable CPU backend with no graphics SDK. The retained GLES hardware path requires a driver exposing OpenGL ES 3 with EGL and `PBC_ENABLE_GLES`. On Linux, Mesa Iris/RadeonSI/V3D or NVIDIA's EGL stack must match the hardware. On Windows, an ES/EGL implementation must be installed separately; HDC presentation itself only needs Win32/GDI.

NVIDIA can additionally enable `PBC_ENABLE_CUDA` with CUDA Toolkit 12.6+ and a toolkit-supported host compiler/OS. Select `CMAKE_CUDA_ARCHITECTURES` for the deployed GPUs; runtime launch geometry is independently chosen by occupancy queries. Building for an old architecture removed from a newer toolkit is not supported by the toolkit. CUDA 12.6's Linux host/glibc support predates Debian 13; use a supported OS/compiler or a newer toolkit rather than patching SDK headers or disabling compiler checks.

CPU/GLSL function pointers cannot be interchanged. A custom CUDA function pointer must be a device symbol in the final linked CUDA program. A GLES shader is a linked GLSL program, with an explicit uniform callback if needed.

## Windows / HDC

Visual Studio 2022 with C++ desktop development and CMake can build the CPU library and HDC example without CUDA:

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --parallel 2
ctest --test-dir build-win -C Release --output-on-failure
.\build-win\Release\pbc_window.exe PBC_GAPI_CUDA/suzanne.obj
```

`HdcWindow` owns HWND/DC lifetime, message polling and GDI presentation. Rendering functions do not include Win32 headers. The legacy solution's project now invokes CMake to build `pbc_cuda_example` and `pbc_window`; it therefore also needs the selected CUDA toolkit. Existing binary artifacts from the old application are not regenerated in their historical paths.

## LineageOS / Android

The native example uses Android's NDK `ANativeWindow` adapter, which is also used by LineageOS. The included NativeActivity project requests Android API 26+, compile SDK 35, NDK 27.2.12479018, CMake 3.22.1 and Android Gradle Plugin 8.7.3 (use Gradle 8.9 with JDK 17). Android Studio can install these versions. No Java application code is needed.

```sh
cd examples/android
gradle assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

No Gradle wrapper binary is committed; use the specified local Gradle/Android Studio toolchain. The application creates/recreates its surface when NativeActivity supplies a window and releases the native reference when finished. It tries the GLES backend when enabled and logs a reason before falling back to CPU. Change `PBC_ENABLE_GLES=OFF` in the Gradle CMake arguments for a CPU-only build.

For compilation of the native code without APK packaging:

```sh
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DPBC_BUILD_TESTS=OFF -DPBC_ENABLE_GLES=ON
cmake --build build-android --parallel 2
```

Use `armeabi-v7a` in a separate build directory for 32-bit ARM. Native linking does not build an APK or validate Android lifecycle/display behavior on a device.
