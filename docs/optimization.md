<!-- PBC-gAPI — property and copyright authority: Pablo B. C. -->
# Tile and cache decisions

The initial implementation uses a 16x16 tile default and optional triangle bins. The bin layout is CSR: tile offsets followed by contiguous triangle indices in draw order. Workers acquire whole tiles through one atomic counter, then update color/depth without pixel locks. This preserves equal-depth ordering and improves spatial locality. CUDA and OpenCL similarly give each pixel one owner and use the bins to reduce candidates. The old CUDA packed-color `atomicMin` tie-break is no longer used.

Indexed vertex outputs are cached once per referenced vertex per draw. The cache is local to a synchronous render call, so changing vertex uniforms on the next draw cannot accidentally reuse stale transforms. CPU textures/framebuffers remain row-major. The GPU's native texture caches are used for GLES; explicit CPU texture-cache blocks have not been added without evidence that they improve this access pattern.

Run a reproducible comparison using the same geometry and image:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/pbc_benchmark PBC_GAPI_CUDA/suzanne.obj > build/benchmark.csv
```

The benchmark covers 1/2/4 requested workers, tile sizes 8/16/32 and bins disabled/enabled. It warms each configuration once, averages five subsequent renders, includes transform/clipping/binning/scheduling, excludes image writes, and checks bit-identical CPU color/depth output across configurations. It fails if scheduling changes the result. It is not a portable performance promise, and timings are deliberately not test assertions.

Bins consume `(tile_count+1 + triangle_tile_overlap_count)*sizeof(size_t)` plus temporary construction storage. Very large screen-covering triangles can make overlap storage expensive. Disable bins for small meshes/tight RAM (the ESP example does so), or choose larger tiles after measurement. One-frame CSR bins are rebuilt when geometry changes; there is no persistent geometry cache to invalidate.

GLES currently compiles/links programs and creates/uploads staging buffers for each render, then reads color and depth back. This makes it useful as an initial hardware backend and a correctness reference, but can dominate small draws. A future retained GPU resource API should cache programs/textures/vertex buffers, use dirty/version tracking and present without readback. CUDA likewise allocates/uploads per draw; persistent device buffers and asynchronous transfers should be measured on actual GPUs before being selected as defaults.

The Intel OpenCL compute backend keeps its context/queue and a bounded eight-program cache. Device buffers retain capacity between calls, so unchanged capacity does not require a new allocation. It still uploads current draw/image contents and reads results back to preserve the host framebuffer API; allocation reuse is not a dirty-resource tracker. One work-group owns each tile, and work-items stride over its pixels. Group size is selected within both device and kernel work-group limits rather than treating a tile's area or an Intel compute-unit count as a physical core count. CPU binning and vertex caching remain per draw. PoCL tests validate the actual compute kernel on CPU when explicitly requested; they are not GPU performance measurements. Profile actual Intel GPUs before choosing vendor-specific tile sizes or claiming a speedup.

Further candidates are incremental edge stepping, coarse tile rejection, packet/SIMD sampling, per-worker scratch reuse and a persistent worker pool. They are not claimed as implemented. Mipmaps/anisotropic sampling and a fully GPU-resident custom vertex stage are also separate extensions. Device-specific tile sizes or CUDA core-per-SM tables are not hardcoded as hardware facts.
