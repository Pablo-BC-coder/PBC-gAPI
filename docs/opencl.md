<!-- PBC-gAPI — property and copyright authority: Pablo B. C. -->
# Custom rasterization on Intel GPUs with OpenCL

`OpenClRenderer` executes the library's rasterization algorithm as OpenCL C 1.2 compute code. It does not call OpenGL, OpenGL ES, EGL, Vulkan, a graphics rasterizer or graphics/compute interop. CPU, CUDA, GLES and the existing presentation adapters remain separate choices.

The default `ComputeDeviceSelection::IntelGpu` is strict. It selects GPU devices with Intel's vendor ID on a native Intel OpenCL platform. Intel CPU devices, another vendor's GPU and translated graphics-based ICDs are not a fallback. If the selected Intel GPU is unavailable, initialization returns an error. `AnyGpu` is an explicit portability choice; `AnyDeviceForTesting` explicitly permits a CPU OpenCL implementation such as PoCL for functional tests. Execution on a CPU testing device never establishes execution on an Intel GPU.

## Build and run

Install C++17/CMake tools, OpenCL headers, an ICD loader and a native Intel GPU OpenCL runtime supported by the installed GPU/OS. On Debian-based systems, the relevant packages are normally `ocl-icd-opencl-dev`, `intel-opencl-icd` and optionally `clinfo`. Runtime package names and supported GPU generations vary by distribution. Installing a Level Zero loader alone is not sufficient for this OpenCL backend. On Windows, install the matching Intel GPU driver/OpenCL runtime and OpenCL development headers/loader.

On Linux the matching `i915` or `xe` kernel driver must expose the render device, and the user needs permission to access it. Check `clinfo` for an Intel **GPU**, its OpenCL compiler availability and version. An Intel CPU entry is not GPU acceleration.

```sh
cmake -S . -B build-intel -DCMAKE_BUILD_TYPE=Release \
  -DPBC_ENABLE_OPENCL=ON -DPBC_ENABLE_GLES=OFF -DPBC_ENABLE_CUDA=OFF
cmake --build build-intel --parallel 2
./build-intel/pbc_opencl_example PBC_GAPI_CUDA/suzanne.obj build-intel/intel.ppm
./build-intel/pbc_opencl_tests --intel-gpu
```

Link applications with `pbc_opencl`. CMake finds the OpenCL loader and headers; no GLES/Vulkan SDK or graphics context is required. CPU-only builds remain the default when `PBC_ENABLE_OPENCL` is off. Presentation is not required to write the example's PPM image; the resulting host framebuffer can also be passed to an existing surface adapter.

## Execution and ownership

The configurable C++ vertex shader, its indexed-vertex cache, homogeneous clipping and optional triangle-bin construction run on the CPU. Packed geometry and textures are transferred to OpenCL buffers. The compute kernel implements these stages itself:

1. Fixed-point triangle coverage with 64-bit edge products, pixel centers and the top-left rule.
2. Perspective-correct varyings, normalized UV and affine NDC depth interpolation.
3. Strict LESS depth testing and submission-order handling of equal-depth triangles.
4. The selected fragment function, texture addressing, nearest/bilinear/bicubic filtering and discard.
5. Masked writes to up to four color targets with shared depth semantics.

One work-group owns each tile. Work-items own disjoint pixels and iterate the tile's triangles in submission order; they do not race to update a packed color/depth value. No 64-bit atomic operation is needed. Work-items stride through a tile when its pixel count is larger than the chosen work-group size. Group size is bounded by the device and kernel limits; no fixed Intel core-per-unit table is used. `RenderOptions::tile_size` and `bin_triangles` select tiling/binning, while `threads` remains a CPU-renderer scheduling option.

Texture pixels, output colors and depth stay in float buffers, avoiding the GLES backend's RGBA8/24-bit-depth quantization. Textures retain the normalized top-left UV and texel-center conventions in `docs/api.md`. Inputs must not overlap outputs. The shared resource validation rejects feedback before dispatch. Correlation between the CPU and GPU results allows normal floating-point differences; exact CPU/GPU bit identity is not promised.

Render calls are synchronous and serialized per renderer instance. The OpenCL context/queue, a bounded cache of eight shader programs and device-buffer capacities are reused. Geometry, texture and output contents are uploaded for each render, then output color/depth are read back. A successful readback is required before host targets are committed. Retained buffers are a capacity cache, not automatic texture-content or geometry invalidation tracking. Use independent instances for independent contexts and serialize access to shared host output storage.

## Fragment programs

`OpenClProgram::fragment_source` is a complete definition of this OpenCL C function:

```c
PbcFragmentOutput pbcFragment(PbcFragmentInput in,
                              PbcResources resources,
                              __global const uchar* uniforms);
```

The backend appends it to the library's compute source and asks the selected OpenCL driver's compiler to build the program. It does not implement a new language compiler, translate C++ automatically or send a CPU function pointer to the GPU. Build errors include the driver's compiler diagnostic. A null source selects `BuiltinFragment::Textured` or `BuiltinFragment::Phong`.

`PbcFragmentInput` contains `float varying[8]`, `float depth`, and integer `x`, `y`. `PbcResources` supplies texture/target counts, sampler settings and read-only packed texture storage. `PbcFragmentOutput` contains `float4 colors[4]`, a `uint mask` and a `uint discard`. Components of a device `float4` color are red, green, blue and alpha. Host transfers pack/unpack named channels rather than assuming that the host `Color` object has the same memory layout as OpenCL vectors.

Available helpers are `pbcOutput(float4)`, `pbcTextured(in,resources)`, `pbcPhong(in,resources)` and `pbcSample(resources,slot,u,v)`. A fragment shader can read several textures, including a previous pass's framebuffer, and write several targets. Set the write mask explicitly for every intended output:

```c
PbcFragmentOutput pbcFragment(PbcFragmentInput in,
                              PbcResources r,
                              __global const uchar* uniforms) {
    PbcFragmentOutput out = pbcOutput(
        pbcSample(r, 0, in.varying[6], in.varying[7]));
    out.colors[1] = (float4)(in.varying[0], in.varying[1],
                             in.varying[2], 1.0f);
    out.mask = 3u;
    return out;
}
```

Assign the source to `OpenClProgram::fragment_source` and pass that program to `renderer.render(geometry(mesh),pass,program)`. The ordinary `RenderPass` still supplies the CPU vertex callback and its host uniforms. A CPU fragment callback is not required by this backend.

For fragment uniforms, set `OpenClProgram::uniforms` and `uniform_bytes`; the bytes are copied into a read-only OpenCL buffer. This is separate from `pass.shaders.uniforms`, which belongs to the CPU vertex stage. Agree on a device layout and provide enough bytes for every access. Ordinary scalar values or `vload4` from a correctly aligned float array avoid host/OpenCL vector-struct padding surprises. Never place host pointer addresses inside the uniform data expecting device dereferences. Custom source is application code with direct buffer access and must honor the interface bounds.

## Discovery and validation

`OpenClRenderer::devices` enumerates the devices matching the requested selection, and the constructor's device index refers to that filtered list. `deviceInfo` reports the selected platform, driver, device identity, memory, address width and work-group limit. `GpuInfo::compute_units` comes from `CL_DEVICE_MAX_COMPUTE_UNITS`; it is not a portable physical-core count or necessarily a count of individual Intel EUs. `physical_cores` remains unknown when not authoritatively exposed. Launch `blocks` and `threads_per_block` describe work-groups/work-items used by this renderer, not physical scheduling blocks.

For a machine without an Intel GPU, install `pocl-opencl-icd` and use the explicit test mode:

```sh
cmake -S . -B build-opencl-test -DCMAKE_BUILD_TYPE=Release \
  -DPBC_ENABLE_OPENCL=ON -DPBC_OPENCL_TEST_DEVICE=ON \
  -DPBC_ENABLE_GLES=OFF -DPBC_ENABLE_CUDA=OFF
cmake --build build-opencl-test --parallel 2
./build-opencl-test/pbc_opencl_tests --require-testing-device
```

The required testing-device mode fails if no usable OpenCL test device is available; it does not convert a missing runtime into a passing test. The separate `--intel-gpu` test reports a skip when the strict Intel GPU selection is absent. PoCL executes the actual compute kernel and checks functional behavior, but says nothing about Intel GPU speed, scheduling or driver correctness. Run the strict tests on the physical GPU before claiming hardware validation.

This backend does not add a native Intel-specific assembly compiler, SYCL backend, AMD-specific GPU rasterizer or VideoCore compute implementation. Those existing platforms keep their previously implemented paths.
