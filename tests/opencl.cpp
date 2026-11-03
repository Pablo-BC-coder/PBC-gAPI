// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/gpu.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace pbc;
namespace {
constexpr int Width=37,Height=29; // deliberately partial edge tiles
constexpr std::size_t Pixels=Width*Height;
void check(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}
void success(Result result) {check(result.ok,result.message);}
VertexOutput vertex(const Vertex& in,const void*) {return transformVertex(in,nullptr);}
Mesh quad() {
    Mesh mesh;
    mesh.vertices={{{-1,-1,0},{0,0,1},{-.25f,1.3f}},{{1,-1,0},{0,0,1},{1.25f,1.3f}},
                   {{1,1,0},{0,0,1},{1.25f,-.1f}},{{-1,1,0},{0,0,1},{-.25f,-.1f}}};
    mesh.indices={0,1,2,0,2,3};return mesh;
}
struct Fixture {
    FrameBuffer actual{Width,Height},expected{Width,Height},texture{1,1};
    Mesh mesh=quad();
    RenderPass pass;
    Fixture() {
        texture.generateTexture(7,5,[](float u,float v,int x,int y,void*) {
            return Color{u,v,(x+2*y)/14.0f,.3f+.4f*u};
        });
        pass.targets[0]=&actual;pass.target_count=1;
        pass.shaders={vertex,texturedFragment,nullptr};
        pass.resources.texture_count=1;pass.resources.textures[0]=texture.texture();
        clear();
    }
    void clear() {actual.clear({.07f,.11f,.19f,.23f});expected.clear({.07f,.11f,.19f,.23f});}
    void cpu(RenderOptions options={}) {
        auto cpu_pass=pass;cpu_pass.targets[0]=&expected;
        success(CpuRenderer{}.render(geometry(mesh),cpu_pass,options));
    }
};
void compare(const FrameBuffer& actual,const FrameBuffer& expected,const std::string& context,float tolerance=5e-5f) {
    check(actual.width()==expected.width()&&actual.height()==expected.height(),context+": dimensions");
    auto a=actual.view(),b=expected.view();int written=0;
    for(int i=0;i<a.width*a.height;++i) {
        const auto& x=a.color[i];const auto& y=b.color[i];
        bool color=std::abs(x.r-y.r)<tolerance&&std::abs(x.g-y.g)<tolerance&&
                   std::abs(x.b-y.b)<tolerance&&std::abs(x.a-y.a)<tolerance;
        if(!color||!(std::abs(a.depth[i]-b.depth[i])<tolerance))
            throw std::runtime_error(context+": mismatch at pixel "+std::to_string(i)+
                " actual RGBA="+std::to_string(x.r)+","+std::to_string(x.g)+","+std::to_string(x.b)+","+std::to_string(x.a)+
                " expected="+std::to_string(y.r)+","+std::to_string(y.g)+","+std::to_string(y.b)+","+std::to_string(y.a));
        written+=a.depth[i]<1;
    }
    check(written>0,context+": the test must render pixels");
}
void filtersAndTiles(OpenClRenderer& renderer) {
    Fixture fixture;
    for(auto address:{AddressMode::Clamp,AddressMode::Repeat})
        for(auto filter:{Filter::Nearest,Filter::Bilinear,Filter::Bicubic}) {
            fixture.pass.resources.sampler={filter,address};
            for(bool bins:{false,true})for(unsigned tile:{1u,8u,16u,31u}) {
                fixture.clear();RenderOptions options;options.tile_size=tile;options.bin_triangles=bins;
                GpuInfo launch;
                success(renderer.render(geometry(fixture.mesh),fixture.pass,{},options,&launch));
                check(launch.blocks>0&&launch.threads_per_block>0,"OpenCL launch dimensions must be reported");
                fixture.cpu(options);compare(fixture.actual,fixture.expected,
                    "filter="+std::to_string(static_cast<int>(filter))+" address="+std::to_string(static_cast<int>(address))+
                    " tile="+std::to_string(tile)+" bins="+std::to_string(bins));
            }
        }
    // Built-in selection must affect the actual device program, not just host metadata.
    fixture.clear();fixture.pass.shaders.fragment=phongFragment;
    OpenClProgram program;program.builtin=BuiltinFragment::Phong;
    success(renderer.render(geometry(fixture.mesh),fixture.pass,program));fixture.cpu();
    compare(fixture.actual,fixture.expected,"Phong shader selection");
}
void clippingAndPerspective(OpenClRenderer& renderer) {
    Fixture fixture;fixture.mesh.vertices[0].position.z=-2;fixture.mesh.vertices[1].position.z=-2;
    fixture.pass.resources.sampler={Filter::Bilinear,AddressMode::Clamp};
    success(renderer.render(geometry(fixture.mesh),fixture.pass));fixture.cpu();
    compare(fixture.actual,fixture.expected,"near-plane clipping");
    int count=0;for(std::size_t i=0;i<Pixels;++i)count+=fixture.actual.view().depth[i]<1;
    check(count>0&&count<static_cast<int>(Pixels),"near-plane crossing must be partially clipped");

    fixture.clear();fixture.mesh=quad();
    fixture.pass.shaders.vertex=[](const Vertex& v,const void*) {
        auto out=transformVertex(v,nullptr);float w=v.position.x>0?2.0f:1.0f;
        out.clip={v.position.x*w,v.position.y*w,0,w};return out;
    };
    success(renderer.render(geometry(fixture.mesh),fixture.pass));fixture.cpu();
    compare(fixture.actual,fixture.expected,"reciprocal-w interpolation");

    // Overlapping differently shaded primitives exercise ordered depth, not just a flat quad.
    fixture.clear();fixture.mesh.vertices.clear();fixture.mesh.indices.clear();
    fixture.pass.shaders.vertex=vertex;std::uint32_t state=77;
    auto random=[&](){state=1664525u*state+1013904223u;return (state>>8)/16777216.0f;};
    for(int i=0;i<90;++i)fixture.mesh.vertices.push_back({{random()*3-1.5f,random()*3-1.5f,random()*1.8f-.9f},
        {0,0,1},{random(),random()}});
    RenderOptions options;options.tile_size=7;options.bin_triangles=true;
    success(renderer.render(geometry(fixture.mesh),fixture.pass,{},options));fixture.cpu(options);
    compare(fixture.actual,fixture.expected,"overlap/clipping/depth order");
}
const char* UniformShader=R"CLC(
PbcFragmentOutput pbcFragment(PbcFragmentInput in,PbcResources resources,__global const uchar* uniforms) {
    PbcFragmentOutput out=pbcTextured(in,resources);
    out.colors[0] *= vload4(0,(__global const float*)uniforms);
    return out;
}
)CLC";
void customShadersAndMrt(OpenClRenderer& renderer) {
    Fixture fixture;
    fixture.pass.shaders.fragment=[](const FragmentInput& in,const ShaderResources& r,const void* user) {
        auto out=texturedFragment(in,r,nullptr);auto tint=*static_cast<const Color*>(user);
        auto& c=out.colors[0];c={c.r*tint.r,c.g*tint.g,c.b*tint.b,c.a*tint.a};return out;
    };
    Color tint{.2f,.4f,.7f,.8f};OpenClProgram custom;
    custom.fragment_source=UniformShader;custom.uniforms=&tint;custom.uniform_bytes=sizeof(tint);
    fixture.pass.shaders.uniforms=&tint;fixture.pass.resources.sampler.filter=Filter::Bilinear;
    success(renderer.render(geometry(fixture.mesh),fixture.pass,custom));fixture.cpu();
    compare(fixture.actual,fixture.expected,"custom shader and uniform buffer");
    // Same source, changed uniforms/geometry: cached programs must not cache mutable draw data.
    tint={.9f,.3f,.1f,.6f};fixture.clear();fixture.mesh.vertices[2].uv={.7f,.15f};
    success(renderer.render(geometry(fixture.mesh),fixture.pass,custom));fixture.cpu();
    compare(fixture.actual,fixture.expected,"cached program updated uniforms and geometry");

    FrameBuffer second(Width,Height),second_expected(Width,Height),texture2(1,1);
    texture2.generateTexture(3,9,[](float u,float v,int,int,void*){return Color{v,.25f,u,.9f};});
    fixture.clear();second.clear();second_expected.clear();
    fixture.pass.target_count=2;fixture.pass.targets[1]=&second;
    fixture.pass.resources.texture_count=2;fixture.pass.resources.textures[1]=texture2.texture();
    fixture.pass.shaders.uniforms=nullptr;
    fixture.pass.shaders.fragment=[](const FragmentInput& in,const ShaderResources& r,const void*) {
        FragmentOutput out;out.mask=3;
        out.colors[0]=r.textures[0].sample(in.varying[6],in.varying[7],r.sampler);
        out.colors[1]=r.textures[1].sample(in.varying[6],in.varying[7],r.sampler);return out;
    };
    custom={};custom.fragment_source=R"CLC(
PbcFragmentOutput pbcFragment(PbcFragmentInput in,PbcResources resources,__global const uchar* uniforms) {
    PbcFragmentOutput out=pbcTextured(in,resources);out.mask=3;
    out.colors[1]=pbcSample(resources,1,in.varying[6],in.varying[7]);return out;
}
)CLC";
    success(renderer.render(geometry(fixture.mesh),fixture.pass,custom));
    auto expected_pass=fixture.pass;expected_pass.targets[0]=&fixture.expected;expected_pass.targets[1]=&second_expected;
    success(CpuRenderer{}.render(geometry(fixture.mesh),expected_pass));
    compare(fixture.actual,fixture.expected,"MRT first output");compare(second,second_expected,"MRT second output");
    for(std::size_t i=0;i<Pixels;++i)check(second.view().depth[i]==fixture.actual.view().depth[i],"MRT shared depth");

    std::vector<Color> retained(fixture.actual.view().color,fixture.actual.view().color+Pixels);
    for(auto& v:fixture.mesh.vertices)v.position.z=-.6f;
    custom.fragment_source=R"CLC(
PbcFragmentOutput pbcFragment(PbcFragmentInput in,PbcResources resources,__global const uchar* uniforms) {
    PbcFragmentOutput out=pbcOutput((float4)(1,0,0,1));out.mask=2;out.colors[1]=(float4)(0,0,1,1);return out;
}
)CLC";
    success(renderer.render(geometry(fixture.mesh),fixture.pass,custom));
    check(!std::memcmp(retained.data(),fixture.actual.view().color,Pixels*sizeof(Color)),"MRT mask preserves excluded color output");
    for(std::size_t i=0;i<Pixels;++i)check(second.view().color[i].b==1&&std::abs(second.view().depth[i]-.2f)<1e-5f&&
        second.view().depth[i]==fixture.actual.view().depth[i],"MRT color mask still synchronizes depth");

    // Previous render outputs are valid simultaneous input framebuffer images in the next pass.
    FrameBuffer combined(Width,Height);combined.clear();
    auto read_pass=fixture.pass;read_pass.targets[0]=&combined;read_pass.targets[1]=nullptr;read_pass.target_count=1;
    read_pass.resources.textures[0]=fixture.actual.view().image();read_pass.resources.textures[1]=second.view().image();
    custom.fragment_source=R"CLC(
PbcFragmentOutput pbcFragment(PbcFragmentInput in,PbcResources resources,__global const uchar* uniforms) {
    float u=(in.x+.5f)/37.0f,v=(in.y+.5f)/29.0f;
    return pbcOutput((pbcSample(resources,0,u,v)+pbcSample(resources,1,u,v))*.5f);
}
)CLC";
    auto full_quad=quad();success(renderer.render(geometry(full_quad),read_pass,custom));
    for(std::size_t i=0;i<Pixels;++i)check(std::abs(combined.view().color[i].r-(fixture.actual.view().color[i].r+second.view().color[i].r)*.5f)<5e-5f,
        "simultaneous input framebuffers");
}
void depthDiscardAndErrors(OpenClRenderer& renderer) {
    Fixture fixture;fixture.pass.resources.texture_count=0;
    OpenClProgram program;program.fragment_source=R"CLC(
PbcFragmentOutput pbcFragment(PbcFragmentInput in,PbcResources resources,__global const uchar* uniforms) {
    return pbcOutput(vload4(0,(__global const float*)uniforms));
}
)CLC";
    Color color{1,0,0,1};program.uniforms=&color;program.uniform_bytes=sizeof(color);
    success(renderer.render(geometry(fixture.mesh),fixture.pass,program));
    color={0,1,0,1};success(renderer.render(geometry(fixture.mesh),fixture.pass,program));
    for(std::size_t i=0;i<Pixels;++i)check(fixture.actual.view().color[i].r==1,"strict LESS preserves first equal-depth fragment");
    for(auto& v:fixture.mesh.vertices)v.position.z=.8f;
    success(renderer.render(geometry(fixture.mesh),fixture.pass,program));
    check(fixture.actual.view().color[500].r==1,"later far pass preserves color");
    for(auto& v:fixture.mesh.vertices)v.position.z=-.8f;
    program.fragment_source=R"CLC(
PbcFragmentOutput pbcFragment(PbcFragmentInput in,PbcResources resources,__global const uchar* uniforms) {
    PbcFragmentOutput out=pbcOutput((float4)(0,0,1,1));out.discard=1;return out;
}
)CLC";
    success(renderer.render(geometry(fixture.mesh),fixture.pass,program));
    check(fixture.actual.view().color[500].r==1&&std::abs(fixture.actual.view().depth[500]-.5f)<1e-5f,"discard preserves color and depth");

    std::vector<Color> before(fixture.actual.view().color,fixture.actual.view().color+Pixels);
    std::vector<float> depths(fixture.actual.view().depth,fixture.actual.view().depth+Pixels);
    program.fragment_source="this is not a valid OpenCL fragment shader";
    auto failed=renderer.render(geometry(fixture.mesh),fixture.pass,program);
    check(!failed&& !failed.message.empty(),"invalid kernel source returns a compiler diagnostic");
    check(!std::memcmp(before.data(),fixture.actual.view().color,Pixels*sizeof(Color))&&
          !std::memcmp(depths.data(),fixture.actual.view().depth,Pixels*sizeof(float)),"compile failure leaves output unchanged");
    program={};program.uniform_bytes=16;
    check(!renderer.render(geometry(fixture.mesh),fixture.pass,program),"nonempty uniform buffer requires a host pointer");
    program={};success(renderer.render(geometry(fixture.mesh),fixture.pass,program));
    check(fixture.actual.view().color[500].r==1&&fixture.actual.view().color[500].g==1&&
        std::abs(fixture.actual.view().depth[500]-.1f)<1e-5f,"valid draw recovers after compilation/validation failures");
    fixture.pass.resources.texture_count=1;fixture.pass.resources.textures[0]=fixture.actual.view().image();
    check(!renderer.render(geometry(fixture.mesh),fixture.pass,program),"output/texture feedback rejected");
    fixture.pass.resources.texture_count=0;fixture.mesh.indices[0]=999;
    check(!renderer.render(geometry(fixture.mesh),fixture.pass,program),"invalid geometry rejected");
}
}
int main(int argc,char** argv) {
    bool testing=argc==2&&std::string(argv[1])=="--require-testing-device";
    bool intel=argc==2&&std::string(argv[1])=="--intel-gpu";
    if(!testing&&!intel){std::cerr<<"Usage: pbc_opencl_tests --intel-gpu | --require-testing-device\n";return 1;}
    auto selection=testing?ComputeDeviceSelection::AnyDeviceForTesting:ComputeDeviceSelection::IntelGpu;
    try {
        std::vector<OpenClDeviceInfo> devices;success(OpenClRenderer::devices(devices,selection));
        if(devices.empty()) {
            std::cerr<<(testing?"FAIL: required OpenCL testing device missing\n":"SKIP: no Intel OpenCL GPU\n");return testing?1:77;
        }
        OpenClRenderer renderer(selection);OpenClDeviceInfo info;success(renderer.deviceInfo(info));
        std::cout<<info.gpu.backend<<": "<<info.gpu.vendor<<" / "<<info.gpu.name<<"; "<<(info.is_gpu?"GPU":"CPU/testing device")<<'\n';
        check(testing||(info.is_gpu&&info.is_intel),"strict Intel selection must use an Intel GPU");
        check(info.max_work_group_size>0&&info.gpu.compute_units>0,"device limits must be queried");
        OpenClRenderer invalid(selection,std::numeric_limits<unsigned>::max());OpenClDeviceInfo unused;
        check(!invalid.deviceInfo(unused),"invalid device index must not fall back");
        std::vector<OpenClDeviceInfo> intel_devices;success(OpenClRenderer::devices(intel_devices));
        for(const auto& device:intel_devices)check(device.is_gpu&&device.is_intel,"Intel enumeration excludes CPU and other vendors");
        if(intel_devices.empty()) {
            OpenClRenderer strict;check(!strict.deviceInfo(unused),"default must not silently select a CPU/other-vendor device");
        }
        filtersAndTiles(renderer);clippingAndPerspective(renderer);customShadersAndMrt(renderer);depthDiscardAndErrors(renderer);
        std::cout<<"PASS OpenCL: custom rasterization, filters, CPU parity, clipping, perspective, tiles, shader uniforms, MRT, depth/discard, errors, strict device selection\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL OpenCL: "<<e.what()<<'\n';return 1;}
}
