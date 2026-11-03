// PBC-gAPI — property and copyright authority: Pablo B. C.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "pbc/gpu.hpp"
#include "pbc/detail/pipeline.hpp"
#include "pbc/detail/shaders.hpp"
#include <cuda_runtime.h>
#include <algorithm>
#ifdef PBC_HAS_NVML
#include <nvml.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace pbc {
namespace {
#ifdef PBC_HAS_NVML
struct Nvml {
#ifdef _WIN32
    HMODULE library=LoadLibraryA("nvml.dll");
    ~Nvml(){if(library)FreeLibrary(library);}
    template<class F>F symbol(const char* name){return library?reinterpret_cast<F>(GetProcAddress(library,name)):nullptr;}
#else
    void* library=dlopen("libnvidia-ml.so.1",RTLD_LAZY|RTLD_LOCAL);
    ~Nvml(){if(library)dlclose(library);}
    template<class F>F symbol(const char* name){return library?reinterpret_cast<F>(dlsym(library,name)):nullptr;}
#endif
};
unsigned physicalCores(int device) {
    Nvml api;
    auto init=api.symbol<decltype(&nvmlInit_v2)>("nvmlInit_v2");
    auto shutdown=api.symbol<decltype(&nvmlShutdown)>("nvmlShutdown");
    auto handle=api.symbol<decltype(&nvmlDeviceGetHandleByPciBusId_v2)>("nvmlDeviceGetHandleByPciBusId_v2");
    auto cores=api.symbol<decltype(&nvmlDeviceGetNumGpuCores)>("nvmlDeviceGetNumGpuCores");
    auto mig=api.symbol<decltype(&nvmlDeviceGetMigMode)>("nvmlDeviceGetMigMode");
    char pci[32]{};unsigned count=0;
    if(!init||!shutdown||!handle||!cores||cudaDeviceGetPCIBusId(pci,sizeof(pci),device)!=cudaSuccess)return 0;
    if(init()!=NVML_SUCCESS)return 0;
    nvmlDevice_t gpu;unsigned mode=0,pending=0;
    if(handle(pci,&gpu)==NVML_SUCCESS) {
        // A parent PCI handle cannot describe the active cores of a MIG slice.
        bool partitioned=mig&&mig(gpu,&mode,&pending)==NVML_SUCCESS&&mode==NVML_DEVICE_MIG_ENABLE;
        if(!partitioned&&cores(gpu,&count)!=NVML_SUCCESS)count=0;
    }
    shutdown();return count;
}
#endif
__device__ FragmentOutput textureShader(const FragmentInput& in,const ShaderResources& resources,const void*) {
    return detail::textureFragment(in,resources);
}
__device__ FragmentOutput phongShader(const FragmentInput& in,const ShaderResources& resources,const void* data) {
    (void)data;
    return detail::phong(in,resources);
}
__device__ FragmentShader texturePointer=textureShader,phongPointer=phongShader;
struct DevicePass { FrameBufferView targets[MaxTargets];ShaderResources resources;unsigned target_count;CudaProgram program; };
__global__ void raster(DevicePass pass,const detail::PreparedTriangle* triangles,const std::size_t* offsets,
                       const std::size_t* indices,std::size_t triangle_count,int tile_size,int columns) {
    int width=pass.targets[0].width,height=pass.targets[0].height;
    for(std::size_t p=static_cast<std::size_t>(blockIdx.x)*blockDim.x+threadIdx.x;p<static_cast<std::size_t>(width)*height;
        p+=static_cast<std::size_t>(gridDim.x)*blockDim.x) {
        int x=p%width,y=p/width;
        auto tile=static_cast<std::size_t>(y/tile_size)*columns+x/tile_size;
        auto begin=offsets?offsets[tile]:0,end=offsets?offsets[tile+1]:triangle_count;
        for(auto i=begin;i<end;++i) {
            const auto& t=triangles[indices?indices[i]:i];
            if(x<t.min_x||x>t.max_x||y<t.min_y||y>t.max_y)continue;
            FragmentInput in;
            if(!detail::fragment(t,x,y,in)||!(in.depth<pass.targets[0].depth[p]))continue;
            auto out=pass.program.device_fragment(in,pass.resources,pass.program.device_uniforms);
            if(out.discard)continue;
            pass.targets[0].depth[p]=in.depth;
            for(unsigned k=0;k<pass.target_count;++k)if(out.mask&(1u<<k))pass.targets[k].color[p]=out.colors[k];
        }
    }
}
Result checked(cudaError_t e) {return e==cudaSuccess?Result{}:Result{false,cudaGetErrorString(e)};}
struct Buffers {
    std::vector<void*> allocations;
    ~Buffers(){for(void* p:allocations)cudaFree(p);}
    template<class T>Result upload(const T* source,std::size_t n,T*& device) {
        if(!n){device=nullptr;return {};}
        auto e=cudaMalloc(reinterpret_cast<void**>(&device),n*sizeof(T));if(e!=cudaSuccess)return checked(e);
        allocations.push_back(device);return checked(cudaMemcpy(device,source,n*sizeof(T),cudaMemcpyHostToDevice));
    }
};
}
Result CudaRenderer::deviceInfo(GpuInfo& info,int device) {
    cudaDeviceProp p{};auto r=checked(cudaGetDeviceProperties(&p,device));if(!r)return r;
    info={};info.backend="CUDA";info.vendor="NVIDIA";info.name=p.name;
    info.compute_units=p.multiProcessorCount;info.memory_bytes=p.totalGlobalMem;
    // NVML is optional. No architecture tables or guessed core counts; MIG slices stay unknown.
#ifdef PBC_HAS_NVML
    info.physical_cores=physicalCores(device);
#endif
    return {};
}
Result CudaRenderer::builtinFragment(BuiltinFragment shader,FragmentShader& pointer) {
    return checked(shader==BuiltinFragment::Phong?cudaMemcpyFromSymbol(&pointer,phongPointer,sizeof(pointer)):
                   cudaMemcpyFromSymbol(&pointer,texturePointer,sizeof(pointer)));
}
Result CudaRenderer::render(Geometry mesh,const RenderPass& pass,CudaProgram program,RenderOptions options,GpuInfo* launch) const {
    auto r=detail::validatePass(pass,false);if(!r)return r;
    if(!program.device_fragment)return {false,"A CUDA device fragment pointer is required"};
    if(!options.tile_size||options.tile_size>256)return {false,"tile_size must be 1..256"};
    int current=0;r=checked(cudaGetDevice(&current));if(!r)return r;
    GpuInfo info;r=deviceInfo(info,current);if(!r)return r;
    int width=pass.targets[0]->width(),height=pass.targets[0]->height();std::size_t n=static_cast<std::size_t>(width)*height;
    std::vector<detail::PreparedTriangle> triangles;
    r=detail::buildTriangles(mesh,pass.shaders,width,height,options.cull,triangles,nullptr);if(!r)return r;
    if(triangles.empty()){if(launch)*launch=info;return {};}
    Buffers memory;detail::PreparedTriangle* device_triangles=nullptr;
    r=memory.upload(triangles.data(),triangles.size(),device_triangles);if(!r)return r;
    DevicePass device{};device.target_count=pass.target_count;device.resources=pass.resources;
    device.resources.target_count=pass.target_count;device.program=program;
    float* depth=nullptr;r=memory.upload(pass.targets[0]->view().depth,n,depth);if(!r)return r;
    for(unsigned i=0;i<pass.target_count;++i) {
        Color* colors=nullptr;r=memory.upload(pass.targets[i]->view().color,n,colors);if(!r)return r;
        device.targets[i]={colors,depth,width,height};
    }
    for(unsigned i=0;i<pass.resources.texture_count;++i) {
        auto image=pass.resources.textures[i];Color* colors=nullptr;
        r=memory.upload(image.pixels,static_cast<std::size_t>(image.width)*image.height,colors);if(!r)return r;
        device.resources.textures[i].pixels=colors;
    }
    int columns=(width+options.tile_size-1)/options.tile_size,rows=(height+options.tile_size-1)/options.tile_size;
    std::vector<std::size_t> offsets,indices;std::size_t *device_offsets=nullptr,*device_indices=nullptr;
    if(options.bin_triangles) {
        auto bins=[&](const detail::PreparedTriangle& t,const auto& fn) {
            for(int y=t.min_y/static_cast<int>(options.tile_size);y<=t.max_y/static_cast<int>(options.tile_size);++y)
                for(int x=t.min_x/static_cast<int>(options.tile_size);x<=t.max_x/static_cast<int>(options.tile_size);++x)
                    fn(static_cast<std::size_t>(y)*columns+x);
        };
        offsets.resize(static_cast<std::size_t>(columns)*rows+1,0);
        for(const auto& t:triangles)bins(t,[&](auto tile){++offsets[tile+1];});
        for(std::size_t i=1;i<offsets.size();++i)offsets[i]+=offsets[i-1];
        indices.resize(offsets.back());auto cursor=offsets;
        for(std::size_t i=0;i<triangles.size();++i)bins(triangles[i],[&](auto tile){indices[cursor[tile]++]=i;});
        r=memory.upload(offsets.data(),offsets.size(),device_offsets);if(!r)return r;
        r=memory.upload(indices.data(),indices.size(),device_indices);if(!r)return r;
    }
    int min_grid=0,block_size=0,active=0;
    r=checked(cudaOccupancyMaxPotentialBlockSize(&min_grid,&block_size,raster,0,0));if(!r)return r;
    r=checked(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&active,raster,block_size,0));if(!r)return r;
    int blocks=std::min(static_cast<int>((n+block_size-1)/block_size),active*static_cast<int>(info.compute_units));
    info.threads_per_block=block_size;info.blocks=std::max(1,blocks);
    raster<<<info.blocks,block_size>>>(device,device_triangles,device_offsets,device_indices,triangles.size(),options.tile_size,columns);
    r=checked(cudaGetLastError());if(!r)return r;r=checked(cudaDeviceSynchronize());if(!r)return r;
    std::vector<Color> colors[MaxTargets];std::vector<float> depths(n);
    r=checked(cudaMemcpy(depths.data(),depth,n*sizeof(float),cudaMemcpyDeviceToHost));if(!r)return r;
    for(unsigned i=0;i<pass.target_count;++i) {
        colors[i].resize(n);r=checked(cudaMemcpy(colors[i].data(),device.targets[i].color,n*sizeof(Color),cudaMemcpyDeviceToHost));if(!r)return r;
    }
    for(unsigned i=0;i<pass.target_count;++i) {
        std::copy(colors[i].begin(),colors[i].end(),pass.targets[i]->view().color);
        std::copy(depths.begin(),depths.end(),pass.targets[i]->view().depth);
    }
    if(launch)*launch=info;return {};
}
} // namespace pbc
