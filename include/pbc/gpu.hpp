// PBC-gAPI — property and copyright authority: Pablo B. C.
#pragma once
#include "pbc/pbc.hpp"
#include <memory>

namespace pbc {
struct GpuInfo {
    std::string backend,name,vendor;
    unsigned compute_units=0; // CUDA SMs or OpenCL compute units (not physical cores); 0 = unknown
    unsigned physical_cores=0; // unknown unless the API supplies an authoritative count
    unsigned threads_per_block=0,blocks=0; // selected launch, not physical core counts
    std::size_t memory_bytes=0;
};
enum class BuiltinFragment { Textured, Phong };
// These are CUDA DEVICE addresses. A host function pointer is never a GPU shader.
struct CudaProgram { FragmentShader device_fragment=nullptr; const void* device_uniforms=nullptr; };
class CudaRenderer {
public:
    static Result deviceInfo(GpuInfo&,int device=0);
    static Result builtinFragment(BuiltinFragment,FragmentShader& device_pointer);
    Result render(Geometry,const RenderPass&,CudaProgram,RenderOptions={},GpuInfo* launch=nullptr) const;
};
// An ES 3.0 context must be current on this thread. Shader code belongs to the caller;
// an empty source selects the supplied textured/Phong example program. User uniforms
// can be bound in a callback after the program is linked and before drawing.
using GlesUniformBinder = void (*)(unsigned program,const void* user);
struct GlesProgram {
    const char* fragment_source=nullptr; // complete #version 300 es fragment shader
    BuiltinFragment builtin=BuiltinFragment::Textured;
    GlesUniformBinder bind=nullptr;
    const void* user=nullptr;
};
class GlesRenderer {
public:
    static Result deviceInfo(GpuInfo&);
    // VertexShader executes on CPU (cached); perspective interpolation and fragments
    // execute on GPU. Pointer-based CPU shaders and GPU GLSL are explicit alternatives.
    Result render(Geometry,const RenderPass&,GlesProgram={},RenderOptions={}) const;
    static std::string fragmentPrelude(); // texture/filter helpers for custom GLSL
};

enum class ComputeDeviceSelection { IntelGpu, AnyGpu, AnyDeviceForTesting };
struct OpenClDeviceInfo {
    GpuInfo gpu;
    std::string opencl_version,opencl_c_version,profile,platform_name,platform_vendor,driver_version;
    bool is_gpu=false, is_intel=false;
    std::size_t max_work_group_size=0;
    unsigned address_bits=0;
};
struct OpenClProgram {
    // OpenCL C definition of pbcFragment, appended to the library's compute kernel.
    // Null selects a built-in. Host pointers are never executed on a device.
    const char* fragment_source=nullptr;
    BuiltinFragment builtin=BuiltinFragment::Textured;
    const void* uniforms=nullptr;
    std::size_t uniform_bytes=0; // copied into a read-only device buffer; separate from vertex uniforms
};
class OpenClRenderer {
public:
    // Intel GPU is strict: no CPU, other-vendor or graphics-API fallback.
    explicit OpenClRenderer(ComputeDeviceSelection=ComputeDeviceSelection::IntelGpu,unsigned device_index=0);
    ~OpenClRenderer();
    OpenClRenderer(const OpenClRenderer&)=delete;
    OpenClRenderer& operator=(const OpenClRenderer&)=delete;
    static Result devices(std::vector<OpenClDeviceInfo>&,ComputeDeviceSelection=ComputeDeviceSelection::IntelGpu);
    Result deviceInfo(OpenClDeviceInfo&);
    // Synchronous, serialized per instance. Context/queue, programs and buffer capacity are reused.
    Result render(Geometry,const RenderPass&,OpenClProgram={},RenderOptions={},GpuInfo* launch=nullptr);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace pbc
