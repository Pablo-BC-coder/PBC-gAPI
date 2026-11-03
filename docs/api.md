<!-- PBC-gAPI — property and copyright authority: Pablo B. C. -->
# API and rendering contracts

Include `pbc/pbc.hpp`, link `pbc`, and keep all input/output storage alive until the synchronous render call returns. GPU backends are explicit optional builds. The core has no operating-system window handles.

## Framebuffers, textures and sampling

`FrameBuffer(w,h)` owns float RGBA and float depth storage. The external-storage constructor additionally takes `Color*`, `float*` and the available pixel count. External buffers must be distinct, correctly aligned and large enough. Framebuffers cannot be copied or moved, so stored views do not silently dangle. Dimensions must be 1..16384; practical limits are set by available memory and GPU texture limits.

`frame.generateTexture(w,h,generator,user)` evaluates a function pointer for each texel. The generator receives normalized **texel-center** UV, integer x/y and the caller's context. Texture storage is separate from the color/depth render storage. Generation replaces the texture only after filling the new image. Any old `ImageView` must be refreshed after regeneration; do not regenerate while a render is using it.

- `frame.getTexturePixel(u,v,sampler)` and `ImageView::sample(u,v,sampler)` use normalized UV.
- `frame.getTexel(x,y)` and `ImageView::texel(x,y)` address integer texels in the actual image dimensions.
- All images use a top-left origin. Texel `(x,y)` is centered at `((x+.5)/width,(y+.5)/height)`.
- Nearest uses `floor(uv*size)`. Bilinear and bicubic use `uv*size-.5`, including samples across repeat seams.
- Clamp extends the border texels; Repeat wraps each axis, including negative coordinates.
- Bicubic is a separable 16-tap Catmull-Rom reconstruction. It may overshoot; CPU/CUDA/OpenCL preserve float output and presentation clamps it. GLES uses RGBA8 textures/targets and 24-bit depth, so it has quantization and clamps color output.
- NaN/infinite UV is rejected by CPU/CUDA/OpenCL sampling and produces transparent black. An empty image also returns transparent black.

Host color/depth images require 20 bytes per pixel. A 64x64 output needs 80 KiB; a 640x480 output needs about 5.86 MiB. Textures, geometry, clipped triangles, shader caches and optional bins use additional memory. Keep resolutions and geometry small on ESP32, disable bins when appropriate, and use caller-provided RAM/PSRAM for outputs. Allocations follow the C++/ESP-IDF allocation policy; `Result` is not an out-of-memory recovery mechanism.

## Shader execution

`ShaderProgram` holds a `VertexShader` pointer, a `FragmentShader` pointer and a shared immutable uniform/context pointer. User callbacks must not throw. Vertex callbacks execute serially and are cached per referenced vertex index during the draw. Fragment callbacks may execute concurrently; they must be reentrant and must not mutate shared textures, uniforms or external state without their own synchronization. Render calls sharing output storage must be serialized by the caller.

The initial vertex stage returns homogeneous `clip` position plus eight float varyings: normal xyz, view-space position xyz, UV xy. The default `transformVertex` uses column-major `mvp`, an affine `model_view` matrix and a caller-supplied inverse-transpose 3x3 normal matrix. Update all three consistently for nontrivial transforms. Custom vertex shaders can define their own eight-varying convention.

`FragmentInput` contains perspective-correct varyings, integer pixel coordinates and normalized depth. `FragmentOutput` supplies up to four colors, a write mask and a discard flag. `texturedFragment` and `phongFragment` are selectable library callbacks, never hardwired into rasterization. Phong normalizes normals safely and uses a view-dependent reflection term; a zero normal or eye vector does not divide by zero.

The renderer clips triangles against all six homogeneous clip planes and a small positive-w guard. It divides by w only after clipping. Rasterization uses 1/16-pixel fixed coordinates, 64-bit edge products, sample centers and the top-left ownership rule. Both windings work; optional culling treats counterclockwise NDC triangles as front faces. Depth is `ndc_z*.5+.5`, interpolated affinely in screen space. Varyings use reciprocal-w interpolation.

Depth comparison is strict LESS; clear depth to 1. The first submitted primitive wins exact depth ties. Discard writes neither depth nor color. There is no implicit alpha blending, multisampling, sRGB conversion or mipmapping.

## Multiple outputs and input framebuffers

`RenderPass::targets` accepts 1..4 equal-sized framebuffers. All outputs share the test against target 0's depth. A successful fragment synchronizes the depth value to every target, even for color channels excluded by the write mask. Existing color/depth survives subsequent draws unless explicitly cleared.

`ShaderResources::textures` accepts up to eight immutable `ImageView`s. Any previous pass's framebuffer can be used via `frame.view().image()`. The fragment shader can sample several images and write several targets in the same draw. Generated texture storage on an output framebuffer is also safe because it is distinct from the output image.

Aliased output buffers, mismatched dimensions and texture/output feedback are rejected before rendering. Use two buffers in alternating passes for feedback effects. This prevents undefined read/write ordering and CPU data races.

## CPU threads and tiles

Set `RenderOptions::threads=1` for serial execution; zero is an error. A larger request is capped by the available logical CPU count and tile count. Linux/Android honors the current affinity mask; desktop fallback uses `hardware_concurrency`; ESP uses the FreeRTOS core configuration. FreeRTOS workers are joined through a completion semaphore before returning.

Tile size is 1..256, default 16. With bins enabled, compact prefix-sum bins retain triangle submission order. Without bins, each tile scans all triangles, saving bin storage. Each tile is assigned to exactly one worker, so the rasterizer requires no per-pixel locks or atomics. Geometry and shader outputs must not be modified during a draw. Pure shaders produce the same CPU output across thread counts/bin settings/tile sizes.

## GPU shaders and discovery

CUDA accepts `CudaProgram::device_fragment`, a **device function pointer**, and optional device-resident uniforms. Use `CudaRenderer::builtinFragment` to obtain a built-in pointer. For custom shaders, compile a `.cu` defining a `__device__` function with the `FragmentShader` signature, copy its symbol's pointer with `cudaMemcpyFromSymbol`, and link all device code in the final application's separable device-link step. Keep `CUDA_SEPARABLE_COMPILATION=ON`; do not pre-resolve the static library separately when using application-defined device shaders. Host shader pointers are not device addresses. The vertex stage currently runs on the CPU and its clipped/cached outputs are transferred to CUDA.

CUDA queries the current device's SM count, obtains a block size with `cudaOccupancyMaxPotentialBlockSize`, queries active blocks per SM, and chooses a bounded grid with a grid-stride pixel loop. Optional runtime-loaded NVML supplies the actual GPU-core count when supported. MIG slices, unavailable NVML and unsupported queries leave the count at zero. Zero means unknown, not a zero-core GPU. There are no guessed architecture tables.

OpenCL provides a separate custom compute rasterizer through `OpenClRenderer`; applications link `pbc_opencl`. Its strict default accepts a native Intel GPU OpenCL platform, with no CPU or graphics-API fallback. The CPU vertex/cache/clipping stages are reused; the library's OpenCL C kernel owns triangle coverage, interpolation, depth, filtering and color writes. `OpenClProgram` selects a built-in or a source definition of `pbcFragment`, with a separate read-only fragment-uniform buffer. The driver's OpenCL compiler builds the program; ordinary C++ pointers are not device functions. Context/queue, up to eight cached programs and device-buffer capacities are retained across synchronous calls. `CL_DEVICE_MAX_COMPUTE_UNITS` and kernel/device work-group limits are queried, without inventing a physical-core count. Explicit `AnyDeviceForTesting` permits PoCL tests and does not prove Intel GPU execution. See [OpenCL contracts and examples](opencl.md).

GLES accepts a complete fragment shader source and an optional uniform-binding callback. `GlesRenderer::fragmentPrelude()` provides `vNormal`, `vPosition`, `vUV`, `uTextures`, `uTextureCount`, `uFilter`, `uAddress`, `pbcTexel` and `pbcSample` for custom programs. Declare color outputs using `layout(location=N)`. Multiple targets require a custom fragment program that writes each intended output. Use GLSL `discard` to discard a fragment. CPU color masks are not GLSL programs.

The initial GLES vertex shader passes through the CPU vertex-stage outputs; GPU hardware performs perspective interpolation, depth testing, fragment execution and texture fetches. A current ES3 context on the calling thread is required. The uniform callback may set uniforms but must not change framebuffer bindings, programs, buffers or raster state. The renderer restores the context state it changes; callers must not have active transform feedback or concurrent use of the same context. `GpuInfo` reports the actual GL vendor/renderer. ES has no standard physical-core or SM-count query, so those fields remain unknown. Software renderers such as llvmpipe are reported by name and are not hardware-acceleration proof.

## OBJ loading

`loadObj(text,mesh,options)` parses text, and `loadObjFile(path,mesh,options)` uses the platform filesystem/VFS. Both are transactional: failures leave the previous mesh intact and report the failing line. Independent position/UV/normal corner indices prevent UV seams from being overwritten. Positive and relative negative indices, `v/vt/vn`, `v/vt`, `v//vn` and position-only faces are supported. Missing UV defaults to `(0,0)` and missing normals use the face normal. The default `flip_v=true` converts OBJ's lower-left UV origin to this library's top-left image origin; set false if assets already use image coordinates.

Simple nondegenerate polygon faces are triangulated by ear clipping, including concave polygons; individual faces are limited to 4096 corners. Malformed, zero and out-of-range indices are errors. Material/MTL loading, automatic texture-file loading, smoothing groups, free-form surfaces, holes, self-intersecting faces and nonplanar polygon semantics are outside this initial OBJ interface. UVs outside [0,1] remain intact for repeat addressing.
