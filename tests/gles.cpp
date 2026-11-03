// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/gpu.hpp"
#include "pbc/surface.hpp"
#include <GLES3/gl3.h>
#include <iostream>
#include <stdexcept>
using namespace pbc;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void success(Result r){if(!r)throw std::runtime_error(r.message);}
int main() {
    EglHeadless context;auto current=context.makeCurrent();if(!current){std::cerr<<"SKIP: "<<current.message<<'\n';return 77;}
    try {
        GpuInfo info;success(GlesRenderer::deviceInfo(info));std::cout<<info.vendor<<" / "<<info.name<<'\n';
        FrameBuffer gpu(32,32),cpu(32,32),other(32,32),texture(1,1);
        texture.generateTexture(8,8,[](float u,float v,int,int,void*){return Color{u,v,.25f,1};});
        Mesh mesh;mesh.vertices={{{-1,-1,0},{0,0,1},{0,1}},{{1,-1,0},{0,0,1},{1,1}},
            {{1,1,0},{0,0,1},{1,0}},{{-1,1,0},{0,0,1},{0,0}}};mesh.indices={0,1,2,0,2,3};
        RenderPass pass;pass.targets[0]=&gpu;pass.target_count=1;pass.shaders={transformVertex,texturedFragment,nullptr};
        pass.resources.textures[0]=texture.texture();pass.resources.texture_count=1;
        glViewport(2,3,4,5);glEnable(GL_BLEND);glDepthFunc(GL_GREATER);glDepthRangef(.2f,.8f);
        glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1,1);
        for(auto filter:{Filter::Nearest,Filter::Bilinear,Filter::Bicubic}) {
            gpu.clear();cpu.clear();pass.resources.sampler={filter,AddressMode::Clamp};pass.targets[0]=&gpu;
            success(GlesRenderer{}.render(geometry(mesh),pass));
            GLint viewport[4],depth_func;glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_DEPTH_FUNC,&depth_func);
            check(viewport[0]==2&&viewport[3]==5&&glIsEnabled(GL_BLEND)&&depth_func==GL_GREATER,"GL caller state restored");
            GLfloat range[2];glGetFloatv(GL_DEPTH_RANGE,range);check(std::abs(range[0]-.2f)<1e-6f&&glIsEnabled(GL_POLYGON_OFFSET_FILL),"GL depth range and polygon offset restored");
            pass.targets[0]=&cpu;success(CpuRenderer{}.render(geometry(mesh),pass));
            for(int i=0;i<32*32;++i) {
                auto a=gpu.view().color[i],b=cpu.view().color[i];
                check(std::abs(a.r-b.r)<.012f&&std::abs(a.g-b.g)<.012f&&std::abs(a.b-b.b)<.012f,"CPU/GLES texture filtering agreement");
                check(std::abs(gpu.view().depth[i]-.5f)<2e-6f,"GPU depth readback");
            }
        }
        pass.targets[0]=&gpu;pass.targets[1]=&other;pass.target_count=2;gpu.clear();other.clear();
        auto source=GlesRenderer::fragmentPrelude()+R"GLSL(
layout(location=0) out vec4 color;layout(location=1) out vec4 normal;
void main(){color=pbcSample(0,vUV);normal=vec4(0,1,0,1);}
)GLSL";
        GlesProgram custom;custom.fragment_source=source.c_str();success(GlesRenderer{}.render(geometry(mesh),pass,custom));
        check(other.view().color[500].g==1&&gpu.view().color[500].r>0,"custom GPU fragment program and MRT");
        // A subsequent far pass must preserve previous color and depth.
        for(auto& v:mesh.vertices)v.position.z=.8f;
        source=GlesRenderer::fragmentPrelude()+"\nlayout(location=0) out vec4 a;layout(location=1) out vec4 b;void main(){a=vec4(1,0,0,1);b=vec4(1,0,0,1);}";
        custom.fragment_source=source.c_str();success(GlesRenderer{}.render(geometry(mesh),pass,custom));
        check(other.view().color[500].g==1,"GPU passes preserve depth and existing color");
        custom.fragment_source="invalid GLSL";check(!GlesRenderer{}.render(geometry(mesh),pass,custom),"shader compile errors propagate");
        check(glGetError()==GL_NO_ERROR,"no remaining GL errors");
        std::cout<<"PASS GLES: three filters, CPU parity, state restore, MRT, shader selection, depth preservation\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL GLES: "<<e.what()<<'\n';return 1;}
}
