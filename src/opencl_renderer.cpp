// PBC-gAPI — property and copyright authority: Pablo B. C.
// Native compute only. This translation unit has no OpenGL/Vulkan dependencies.
#include "pbc/gpu.hpp"
#include "pbc/detail/pipeline.hpp"
#include "pbc_opencl_source.hpp"
#include <CL/cl.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <limits>
#include <mutex>

namespace pbc {
namespace {
Result checked(cl_int error,const char* operation) {
    return error==CL_SUCCESS ? Result{} : Result{false,std::string(operation)+" failed (OpenCL "+std::to_string(error)+")"};
}
template<class Handle,class Param,class Function>
std::string stringInfo(Handle object,Param param,Function query) {
    std::size_t n=0;
    if(query(object,param,0,nullptr,&n)!=CL_SUCCESS||!n)return {};
    std::string value(n,'\0');
    if(query(object,param,n,value.data(),nullptr)!=CL_SUCCESS)return {};
    while(!value.empty()&&value.back()=='\0')value.pop_back();
    return value;
}
std::string lower(std::string text) {
    for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
struct Device {
    cl_platform_id platform=nullptr;
    cl_device_id id=nullptr;
    OpenClDeviceInfo info;
};
Result enumerate(std::vector<Device>& output,ComputeDeviceSelection selection) {
    cl_uint count=0;
    cl_int error=clGetPlatformIDs(0,nullptr,&count);
    if(error==-1001)return {}; // CL_PLATFORM_NOT_FOUND_KHR: installed loader, no ICD.
    if(error!=CL_SUCCESS)return checked(error,"clGetPlatformIDs");
    std::vector<cl_platform_id> platforms(count);
    if(count) {
        error=clGetPlatformIDs(count,platforms.data(),nullptr);
        if(error!=CL_SUCCESS)return checked(error,"clGetPlatformIDs");
    }
    for(auto platform:platforms) {
        auto platform_name=stringInfo(platform,CL_PLATFORM_NAME,clGetPlatformInfo);
        auto platform_vendor=stringInfo(platform,CL_PLATFORM_VENDOR,clGetPlatformInfo);
        cl_uint devices=0;
        error=clGetDeviceIDs(platform,CL_DEVICE_TYPE_ALL,0,nullptr,&devices);
        if(error==CL_DEVICE_NOT_FOUND)continue;
        if(error!=CL_SUCCESS)return checked(error,"clGetDeviceIDs");
        std::vector<cl_device_id> ids(devices);
        error=clGetDeviceIDs(platform,CL_DEVICE_TYPE_ALL,devices,ids.data(),nullptr);
        if(error!=CL_SUCCESS)return checked(error,"clGetDeviceIDs");
        for(auto id:ids) {
            cl_device_type type=0;cl_uint vendor=0,units=0,bits=0;cl_ulong memory=0;
            cl_bool available=CL_FALSE,compiler=CL_FALSE;std::size_t max_group=0;
            auto query=[&](cl_device_info key,auto& value) {
                return clGetDeviceInfo(id,key,sizeof(value),&value,nullptr);
            };
            if(query(CL_DEVICE_TYPE,type)!=CL_SUCCESS||query(CL_DEVICE_VENDOR_ID,vendor)!=CL_SUCCESS||
               query(CL_DEVICE_AVAILABLE,available)!=CL_SUCCESS||query(CL_DEVICE_COMPILER_AVAILABLE,compiler)!=CL_SUCCESS||
               query(CL_DEVICE_MAX_COMPUTE_UNITS,units)!=CL_SUCCESS||query(CL_DEVICE_ADDRESS_BITS,bits)!=CL_SUCCESS||
               query(CL_DEVICE_GLOBAL_MEM_SIZE,memory)!=CL_SUCCESS||query(CL_DEVICE_MAX_WORK_GROUP_SIZE,max_group)!=CL_SUCCESS)
                return {false,"Could not query OpenCL device capabilities"};
            if(!available||!compiler)continue;
            bool gpu=(type&CL_DEVICE_TYPE_GPU)!=0,intel=vendor==0x8086;
            auto driver=stringInfo(id,CL_DRIVER_VERSION,clGetDeviceInfo);
            auto platform_text=lower(platform_name+" "+platform_vendor+" "+driver);
            // Avoid choosing OpenCL translations implemented over a forbidden graphics API.
            bool translated=false;
            for(const char* word:{"clvk","vulkan","zink","angle","openclon12","opencl on12","d3d"})
                translated=translated||platform_text.find(word)!=std::string::npos;
            bool native_intel=lower(platform_name).find("intel")!=std::string::npos&&
                              lower(platform_vendor).find("intel")!=std::string::npos;
            if(translated)continue;
            if(selection==ComputeDeviceSelection::IntelGpu&&(!gpu||!intel||!native_intel))continue;
            if(selection==ComputeDeviceSelection::AnyGpu&&!gpu)continue;
            OpenClDeviceInfo info;
            info.gpu.backend="OpenCL compute / PBC rasterizer";
            info.gpu.name=stringInfo(id,CL_DEVICE_NAME,clGetDeviceInfo);
            info.gpu.vendor=stringInfo(id,CL_DEVICE_VENDOR,clGetDeviceInfo);
            info.gpu.compute_units=units;
            info.gpu.memory_bytes=static_cast<std::size_t>(std::min<cl_ulong>(memory,std::numeric_limits<std::size_t>::max()));
            info.opencl_version=stringInfo(id,CL_DEVICE_VERSION,clGetDeviceInfo);
            info.opencl_c_version=stringInfo(id,CL_DEVICE_OPENCL_C_VERSION,clGetDeviceInfo);
            info.profile=stringInfo(id,CL_DEVICE_PROFILE,clGetDeviceInfo);
            info.platform_name=platform_name;info.platform_vendor=platform_vendor;info.driver_version=driver;
            info.is_gpu=gpu;info.is_intel=intel;info.max_work_group_size=max_group;info.address_bits=bits;
            output.push_back({platform,id,info});
        }
    }
    return {};
}
// Explicit wire ABI. Never depend on host Color field order or C++ triangle padding.
struct alignas(16) WireTriangle {
    cl_int bounds[4],xy01[4],xy2_pad[4];
    cl_float perspective[4],ndc_depth[4],varying[6][4];
};
struct alignas(16) Float4 { float r,g,b,a; };
struct alignas(16) Int4 { cl_int offset,width,height,unused; };
static_assert(sizeof(WireTriangle)==176&&alignof(WireTriangle)==16,"OpenCL triangle ABI");
static_assert(sizeof(Float4)==16&&sizeof(Int4)==16,"OpenCL vector ABI");
std::vector<WireTriangle> pack(const std::vector<detail::PreparedTriangle>& triangles) {
    std::vector<WireTriangle> wire(triangles.size());
    for(std::size_t i=0;i<triangles.size();++i) {
        auto& dst=wire[i];const auto& src=triangles[i];
        dst.bounds[0]=src.min_x;dst.bounds[1]=src.min_y;dst.bounds[2]=src.max_x;dst.bounds[3]=src.max_y;
        dst.xy01[0]=src.v[0].x;dst.xy01[1]=src.v[0].y;dst.xy01[2]=src.v[1].x;dst.xy01[3]=src.v[1].y;
        dst.xy2_pad[0]=src.v[2].x;dst.xy2_pad[1]=src.v[2].y;
        for(unsigned v=0;v<3;++v) {
            dst.perspective[v]=src.v[v].inv_w;dst.ndc_depth[v]=src.v[v].ndc_z;
            for(unsigned k=0;k<VaryingCount;++k)dst.varying[v*2+k/4][k%4]=src.v[v].varying[k];
        }
    }
    return wire;
}
struct Program {
    std::string source;
    cl_program program=nullptr;
    cl_kernel kernel=nullptr;
    ~Program(){if(kernel)clReleaseKernel(kernel);if(program)clReleaseProgram(program);}
};
}
struct OpenClRenderer::Impl {
    ComputeDeviceSelection selection;
    unsigned device_index;
    std::mutex mutex;
    Device device;
    cl_context context=nullptr;
    cl_command_queue queue=nullptr;
    cl_ulong max_allocation=0;
    std::size_t max_group_x=0;
    struct Buffer {cl_mem memory=nullptr;std::size_t capacity=0;};
    std::array<Buffer,8> buffers{};
    std::vector<std::unique_ptr<Program>> programs;
    Impl(ComputeDeviceSelection s,unsigned i):selection(s),device_index(i){}
    ~Impl() {
        if(queue)clFinish(queue);
        programs.clear();
        for(auto b:buffers)if(b.memory)clReleaseMemObject(b.memory);
        if(queue)clReleaseCommandQueue(queue);
        if(context)clReleaseContext(context);
    }
    Result initialize() {
        if(context&&queue)return {};
        std::vector<Device> devices;auto result=enumerate(devices,selection);if(!result)return result;
        if(device_index>=devices.size())return {false,selection==ComputeDeviceSelection::IntelGpu ?
            "No selected native Intel OpenCL GPU available; install the Intel Compute Runtime/ICD (no CPU or GL/Vulkan fallback)" :
            "No selected OpenCL compute device available"};
        device=devices[device_index];
        int major=0,minor=0;
        if(device.info.profile!="FULL_PROFILE"||
           std::sscanf(device.info.opencl_c_version.c_str(),"OpenCL C %d.%d",&major,&minor)!=2||
           major<1||(major==1&&minor<2))
            return {false,"The rasterizer requires OpenCL C 1.2+ FULL_PROFILE with 64-bit integer arithmetic"};
        cl_bool device_little=CL_FALSE;const cl_uint endian_probe=1;
        cl_int endian_error=clGetDeviceInfo(device.id,CL_DEVICE_ENDIAN_LITTLE,sizeof(device_little),&device_little,nullptr);
        if(endian_error!=CL_SUCCESS)return checked(endian_error,"CL_DEVICE_ENDIAN_LITTLE");
        bool host_little=*reinterpret_cast<const unsigned char*>(&endian_probe)==1;
        if((device_little!=CL_FALSE)!=host_little)return {false,"OpenCL device/host buffer byte order differs"};
        cl_uint dimensions=0;
        cl_int error=clGetDeviceInfo(device.id,CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS,sizeof(dimensions),&dimensions,nullptr);
        if(error!=CL_SUCCESS||!dimensions)return {false,"OpenCL work-item dimensions unavailable"};
        std::vector<std::size_t> limits(dimensions);
        error=clGetDeviceInfo(device.id,CL_DEVICE_MAX_WORK_ITEM_SIZES,limits.size()*sizeof(std::size_t),limits.data(),nullptr);
        if(error!=CL_SUCCESS)return checked(error,"CL_DEVICE_MAX_WORK_ITEM_SIZES");
        max_group_x=limits[0];
        error=clGetDeviceInfo(device.id,CL_DEVICE_MAX_MEM_ALLOC_SIZE,sizeof(max_allocation),&max_allocation,nullptr);
        if(error!=CL_SUCCESS)return checked(error,"CL_DEVICE_MAX_MEM_ALLOC_SIZE");
        // Context creation failure must not leave a partially initialized instance.
        cl_context_properties properties[]={CL_CONTEXT_PLATFORM,reinterpret_cast<cl_context_properties>(device.platform),0};
        auto new_context=clCreateContext(properties,1,&device.id,nullptr,nullptr,&error);
        if(error!=CL_SUCCESS)return checked(error,"clCreateContext");
        auto new_queue=clCreateCommandQueue(new_context,device.id,0,&error);
        if(error!=CL_SUCCESS){clReleaseContext(new_context);return checked(error,"clCreateCommandQueue");}
        context=new_context;queue=new_queue;return {};
    }
    Result program(const OpenClProgram& shader,Program*& out) {
        std::string fragment;
        if(shader.fragment_source)fragment=shader.fragment_source;
        else fragment=std::string("PbcFragmentOutput pbcFragment(PbcFragmentInput in,PbcResources resources,__global const uchar* uniforms){return ")+
                      (shader.builtin==BuiltinFragment::Phong?"pbcPhong":"pbcTextured")+"(in,resources);}";
        for(auto& entry:programs)if(entry->source==fragment){out=entry.get();return {};}
        auto entry=std::make_unique<Program>();entry->source=fragment;
        const char* sources[]={detail::OpenClSource,"\n",fragment.c_str()};cl_int error=0;
        entry->program=clCreateProgramWithSource(context,3,sources,nullptr,&error);
        if(error!=CL_SUCCESS)return checked(error,"clCreateProgramWithSource");
        error=clBuildProgram(entry->program,1,&device.id,"-cl-std=CL1.2",nullptr,nullptr);
        if(error!=CL_SUCCESS)return {false,"OpenCL shader build failed: "+stringInfo(entry->program,CL_PROGRAM_BUILD_LOG,
            [&](cl_program p,cl_program_build_info key,std::size_t n,void* value,std::size_t* size){return clGetProgramBuildInfo(p,device.id,key,n,value,size);})};
        entry->kernel=clCreateKernel(entry->program,"pbcRaster",&error);
        if(error!=CL_SUCCESS)return checked(error,"clCreateKernel");
        if(programs.size()==8)programs.erase(programs.begin());
        out=entry.get();programs.push_back(std::move(entry));return {};
    }
    Result upload(unsigned slot,const void* data,std::size_t bytes) {
        auto& buffer=buffers[slot];std::size_t capacity=std::max<std::size_t>(bytes,16);
        if(capacity>max_allocation)return {false,"OpenCL buffer exceeds the device allocation limit"};
        if(!buffer.memory||buffer.capacity<capacity) {
            cl_int error=0;cl_mem memory=clCreateBuffer(context,CL_MEM_READ_WRITE,capacity,nullptr,&error);
            if(error!=CL_SUCCESS)return checked(error,"clCreateBuffer");
            if(buffer.memory)clReleaseMemObject(buffer.memory);
            buffer={memory,capacity};
        }
        if(bytes)return checked(clEnqueueWriteBuffer(queue,buffer.memory,CL_TRUE,0,bytes,data,0,nullptr,nullptr),"clEnqueueWriteBuffer");
        // Deterministic dummy contents for zero textures/geometry/uniforms.
        const unsigned char zero[16]{};
        return checked(clEnqueueWriteBuffer(queue,buffer.memory,CL_TRUE,0,sizeof(zero),zero,0,nullptr,nullptr),"clEnqueueWriteBuffer(dummy)");
    }
};

OpenClRenderer::OpenClRenderer(ComputeDeviceSelection selection,unsigned index):impl_(new Impl(selection,index)){}
OpenClRenderer::~OpenClRenderer()=default;
Result OpenClRenderer::devices(std::vector<OpenClDeviceInfo>& output,ComputeDeviceSelection selection) {
    std::vector<Device> devices;auto result=enumerate(devices,selection);if(!result)return result;
    std::vector<OpenClDeviceInfo> infos;for(const auto& device:devices)infos.push_back(device.info);
    output=std::move(infos);return {};
}
Result OpenClRenderer::deviceInfo(OpenClDeviceInfo& info) {
    std::lock_guard<std::mutex> lock(impl_->mutex);auto result=impl_->initialize();
    if(result)info=impl_->device.info;
    return result;
}
Result OpenClRenderer::render(Geometry geometry,const RenderPass& pass,OpenClProgram shader,RenderOptions options,GpuInfo* launch) {
    auto result=detail::validatePass(pass,false);if(!result)return result;
    if(!options.tile_size||options.tile_size>256)return {false,"tile_size must be 1..256"};
    if(shader.uniform_bytes&&!shader.uniforms)return {false,"OpenCL uniform buffer has a size but no data"};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    result=impl_->initialize();if(!result)return result;
    Program* program=nullptr;result=impl_->program(shader,program);if(!result)return result;
    int width=pass.targets[0]->width(),height=pass.targets[0]->height();
    std::size_t pixels=static_cast<std::size_t>(width)*height;
    std::vector<detail::PreparedTriangle> triangles;
    result=detail::buildTriangles(geometry,pass.shaders,width,height,options.cull,triangles,nullptr);if(!result)return result;
    if(triangles.size()>std::numeric_limits<cl_uint>::max())return {false,"Too many OpenCL triangles"};
    GpuInfo info=impl_->device.info.gpu;
    if(triangles.empty()){if(launch)*launch=info;return {};}
    auto wire=pack(triangles);
    unsigned columns=(width+options.tile_size-1)/options.tile_size;
    unsigned rows=(height+options.tile_size-1)/options.tile_size;
    std::size_t tiles=static_cast<std::size_t>(columns)*rows;
    std::vector<cl_ulong> offsets;std::vector<cl_uint> indices;
    if(options.bin_triangles) {
        auto bins=[&](const detail::PreparedTriangle& triangle,const auto& visitor) {
            for(unsigned y=triangle.min_y/options.tile_size;y<=triangle.max_y/options.tile_size;++y)
                for(unsigned x=triangle.min_x/options.tile_size;x<=triangle.max_x/options.tile_size;++x)
                    visitor(static_cast<std::size_t>(y)*columns+x);
        };
        offsets.resize(tiles+1,0);
        for(const auto& triangle:triangles)bins(triangle,[&](auto tile){++offsets[tile+1];});
        for(std::size_t i=1;i<offsets.size();++i)offsets[i]+=offsets[i-1];
        if(offsets.back()>std::numeric_limits<std::size_t>::max()/sizeof(cl_uint))return {false,"OpenCL tile bins exceed host address space"};
        indices.resize(static_cast<std::size_t>(offsets.back()));auto cursor=offsets;
        for(std::size_t i=0;i<triangles.size();++i)bins(triangles[i],[&](auto tile){indices[static_cast<std::size_t>(cursor[tile]++)]=static_cast<cl_uint>(i);});
    }
    std::vector<Float4> colors(pixels*pass.target_count),textures;
    std::vector<float> depth(pass.targets[0]->view().depth,pass.targets[0]->view().depth+pixels);
    for(unsigned k=0;k<pass.target_count;++k)for(std::size_t i=0;i<pixels;++i) {
        auto color=pass.targets[k]->view().color[i];colors[k*pixels+i]={color.r,color.g,color.b,color.a};
    }
    std::vector<Int4> metadata;
    for(unsigned k=0;k<pass.resources.texture_count;++k) {
        auto image=pass.resources.textures[k];std::size_t n=static_cast<std::size_t>(image.width)*image.height;
        if(textures.size()+n>static_cast<std::size_t>(std::numeric_limits<cl_int>::max()))return {false,"OpenCL texture offsets exceed INT_MAX"};
        metadata.push_back({static_cast<cl_int>(textures.size()),image.width,image.height,0});
        for(std::size_t i=0;i<n;++i){auto c=image.pixels[i];textures.push_back({c.r,c.g,c.b,c.a});}
    }
    const void* data[]={wire.data(),offsets.data(),indices.data(),colors.data(),depth.data(),textures.data(),metadata.data(),shader.uniforms};
    std::size_t sizes[]={wire.size()*sizeof(WireTriangle),offsets.size()*sizeof(cl_ulong),indices.size()*sizeof(cl_uint),
        colors.size()*sizeof(Float4),depth.size()*sizeof(float),textures.size()*sizeof(Float4),metadata.size()*sizeof(Int4),shader.uniform_bytes};
    if(impl_->device.info.address_bits==32)for(auto size:sizes)if(size>std::numeric_limits<cl_uint>::max())return {false,"OpenCL buffer exceeds 32-bit device address space"};
    for(unsigned i=0;i<8;++i){result=impl_->upload(i,data[i],sizes[i]);if(!result)return result;}
    cl_uint arg=0;cl_int error=CL_SUCCESS;
    auto set=[&](const auto& value){cl_int e=clSetKernelArg(program->kernel,arg++,sizeof(value),&value);if(error==CL_SUCCESS)error=e;};
    auto mem=[&](unsigned slot){set(impl_->buffers[slot].memory);};
    mem(0);set(static_cast<cl_uint>(triangles.size()));mem(1);mem(2);set(static_cast<cl_uint>(options.bin_triangles));
    mem(3);mem(4);set(static_cast<cl_uint>(width));set(static_cast<cl_uint>(height));mem(5);mem(6);
    set(static_cast<cl_uint>(pass.resources.texture_count));set(static_cast<cl_uint>(pass.target_count));
    set(static_cast<cl_int>(pass.resources.sampler.filter));set(static_cast<cl_int>(pass.resources.sampler.address));
    set(static_cast<cl_uint>(options.tile_size));set(static_cast<cl_uint>(columns));mem(7);
    if(error!=CL_SUCCESS)return checked(error,"clSetKernelArg");
    std::size_t kernel_max=0,preferred=1;
    error=clGetKernelWorkGroupInfo(program->kernel,impl_->device.id,CL_KERNEL_WORK_GROUP_SIZE,sizeof(kernel_max),&kernel_max,nullptr);
    if(error!=CL_SUCCESS)return checked(error,"CL_KERNEL_WORK_GROUP_SIZE");
    error=clGetKernelWorkGroupInfo(program->kernel,impl_->device.id,CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE,sizeof(preferred),&preferred,nullptr);
    if(error!=CL_SUCCESS)return checked(error,"CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE");
    std::size_t local=std::min({std::size_t(128),impl_->device.info.max_work_group_size,impl_->max_group_x,kernel_max,
                              static_cast<std::size_t>(options.tile_size)*options.tile_size});
    if(!local)return {false,"OpenCL device cannot execute the rasterizer workgroup"};
    if(preferred&&local>=preferred)local-=local%preferred;
    if(tiles>std::numeric_limits<std::size_t>::max()/local)return {false,"OpenCL dispatch exceeds host address space"};
    std::size_t global=tiles*local;
    if(impl_->device.info.address_bits==32&&global>std::numeric_limits<cl_uint>::max())return {false,"OpenCL dispatch exceeds 32-bit device address space"};
    error=clEnqueueNDRangeKernel(impl_->queue,program->kernel,1,nullptr,&global,&local,0,nullptr,nullptr);
    if(error!=CL_SUCCESS)return checked(error,"clEnqueueNDRangeKernel");
    error=clEnqueueReadBuffer(impl_->queue,impl_->buffers[3].memory,CL_TRUE,0,sizes[3],colors.data(),0,nullptr,nullptr);
    if(error!=CL_SUCCESS)return checked(error,"clEnqueueReadBuffer(colors)");
    error=clEnqueueReadBuffer(impl_->queue,impl_->buffers[4].memory,CL_TRUE,0,sizes[4],depth.data(),0,nullptr,nullptr);
    if(error!=CL_SUCCESS)return checked(error,"clEnqueueReadBuffer(depth)");
    error=clFinish(impl_->queue);if(error!=CL_SUCCESS)return checked(error,"clFinish");
    // Transactional host commit: shader/build/upload/dispatch/read errors leave all outputs intact.
    for(unsigned k=0;k<pass.target_count;++k)for(std::size_t i=0;i<pixels;++i) {
        auto c=colors[k*pixels+i];auto& output=pass.targets[k]->view().color[i];
        output.r=c.r;output.g=c.g;output.b=c.b;output.a=c.a;
        pass.targets[k]->view().depth[i]=depth[i];
    }
    info.threads_per_block=static_cast<unsigned>(local);info.blocks=static_cast<unsigned>(tiles);
    if(launch)*launch=info;
    return {};
}
} // namespace pbc
