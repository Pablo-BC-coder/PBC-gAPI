// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/pbc.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

using namespace pbc;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void success(Result result){if(!result)throw std::runtime_error(result.message);}
bool near(float a,float b,float tolerance=1e-5f){return std::abs(a-b)<tolerance;}
FragmentOutput solid(const FragmentInput&,const ShaderResources&,const void* data) {
    FragmentOutput out;out.colors[0]=data?*static_cast<const Color*>(data):Color{1,0,0,1};return out;
}
struct Fixture {
    FrameBuffer fb{32,32};Mesh mesh;RenderPass pass;
    Fixture(){mesh.vertices={{{-1,-1,0}},{{1,-1,0}},{{1,1,0}},{{-1,1,0}}};mesh.indices={0,1,2,0,2,3};
        pass.target_count=1;pass.targets[0]=&fb;pass.shaders={transformVertex,solid,nullptr};fb.clear({0,0,0,0});}
    void draw(RenderOptions options={}){success(CpuRenderer{}.render(geometry(mesh),pass,options));}
};
void texture() {
    Color pixels[]={{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,1,1}};ImageView image{pixels,2,2};
    check(image.sample(0,0).r==1&&image.sample(1,1).g==1,"UV endpoints");
    check(image.sample(.25f,.25f).r==1&&image.sample(.75f,.25f).g==1,"texel centers");
    auto middle=image.sample(.5f,.5f,{Filter::Bilinear});check(near(middle.r,.5)&&near(middle.g,.5)&&near(middle.b,.5),"bilinear average");
    for(auto filter:{Filter::Nearest,Filter::Bilinear,Filter::Bicubic}) {
        Color constant[16];std::fill_n(constant,16,Color{.2f,.3f,.4f,1});
        auto c=ImageView{constant,4,4}.sample(.37f,.83f,{filter,AddressMode::Repeat});check(near(c.r,.2)&&near(c.g,.3),"constant texture invariance");
    }
    Color ramp[16];for(int y=0;y<4;++y)for(int x=0;x<4;++x)ramp[y*4+x]={x/3.0f,y/3.0f,0,1};
    auto cubic=ImageView{ramp,4,4}.sample(.5f,.5f,{Filter::Bicubic});check(near(cubic.r,.5)&&near(cubic.g,.5),"bicubic linear reconstruction");
    check(image.texel(1,0).g==1&&image.texel(20,-1).g==1,"integer texel addressing");
    check(image.sample(1,0,{Filter::Nearest,AddressMode::Repeat}).r==1,"repeat endpoint");
    check(image.sample(-.1f,0,{Filter::Nearest,AddressMode::Repeat}).g==1,"negative repeat");
    check(image.sample(std::numeric_limits<float>::quiet_NaN(),0).a==0,"NaN UV must be safe");
    check(image.sample(std::numeric_limits<float>::infinity(),0).a==0,"infinite UV must be safe");
    FrameBuffer fb(2,2);check(fb.generateTexture(4,8,[](float u,float v,int,int,void*){return Color{u,v,0,1};}),"procedural generation");
    check(near(fb.getTexel(0,0).r,.125)&&near(fb.getTexel(3,7).g,.9375),"generator receives normalized texel centers");
    check(!fb.generateTexture(-1,4,nullptr),"invalid generator request");
    check(fb.texture().width==4,"failed generation preserves texture");
}
void obj() {
    const char* header="v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt .1 .2\nvt .3 .4\nvt .5 .6\nvt .7 .8\nvn 0 0 1\n";
    Mesh mesh;success(loadObj(std::string(header)+"f 1/3/1 2/1/1 3/2/1 4/4/1 # quad\n",mesh));
    check(mesh.indices.size()==6,"OBJ triangulates quads");check(near(mesh.vertices[0].uv.x,.5)&&near(mesh.vertices[0].uv.y,.4),"UV uses independent vt index and explicit V flip");
    success(loadObj(std::string(header)+"f -4/-1/-1 -3/-2/-1 -2/-3/-1\n",mesh,{false}));
    check(near(mesh.vertices[0].uv.x,.7)&&near(mesh.vertices[0].uv.y,.8),"negative OBJ indices");
    success(loadObj(std::string(header)+"f 1//1 2//1 3//1\n",mesh));check(mesh.vertices[0].uv.x==0&&mesh.vertices[0].normal.z==1,"v//vn support");
    success(loadObj("v 0 0 0\nv 2 0 0\nv 2 2 0\nv 1 1 0\nv 0 2 0\nf 1 2 3 4 5\n",mesh));check(mesh.indices.size()==9,"concave polygon triangulation");
    for(const auto* face:{"f 0 2 3","f 1/9 2/1 3/2","f 1/a 2 3","f 1/ 2 3","f 99 2 3","f 1 2","f 1///1 2 3"}) {
        auto count=mesh.vertices.size();auto result=loadObj(std::string(header)+face,mesh);
        check(!result&&result.message.find("OBJ line")!=std::string::npos,"malformed OBJ produces diagnostic");check(mesh.vertices.size()==count,"transactional OBJ loading");
    }
}
void raster() {
    Fixture f;f.draw();
    for(int i=0;i<32*32;++i)check(f.fb.view().color[i].r==1&&near(f.fb.view().depth[i],.5),"full-screen quad has no cracks");
    // Count ownership of the shared diagonal by rendering each triangle independently.
    FrameBuffer first(32,32),second(32,32);first.clear({0,0,0,0});second.clear({0,0,0,0});
    f.pass.targets[0]=&first;f.mesh.indices={0,1,2};f.draw();
    f.pass.targets[0]=&second;f.mesh.indices={0,2,3};f.draw();
    for(int i=0;i<32*32;++i)check(first.view().color[i].r+second.view().color[i].r==1,"shared edge must be owned exactly once");
    f.pass.targets[0]=&f.fb;f.mesh.indices={0,1,2,0,2,3};
    for(auto& v:f.mesh.vertices)v.position.z=.7f;
    f.pass.shaders.fragment=[](const FragmentInput&,const ShaderResources&,const void*){FragmentOutput o;o.colors[0]={0,1,0,1};return o;};f.draw();
    check(f.fb.view().color[300].r==1,"farther geometry occluded");
    for(auto& v:f.mesh.vertices)v.position.z=-.7f;f.draw();check(f.fb.view().color[300].g==1,"near geometry wins");
    f.pass.shaders.fragment=[](const FragmentInput&,const ShaderResources&,const void*){FragmentOutput o;o.discard=true;return o;};
    auto depth=f.fb.view().depth[300];for(auto& v:f.mesh.vertices)v.position.z=-.9f;f.draw();check(f.fb.view().depth[300]==depth,"discard does not write depth");
    f.fb.clear({0,0,0,0});f.mesh.vertices[2].position=f.mesh.vertices[1].position;f.mesh.indices={0,1,2};f.draw();
    check(f.fb.view().depth[300]==1,"degenerate triangles are skipped");
}
void clipping() {
    Fixture f;f.mesh.vertices[0].position.z=-2;f.mesh.vertices[1].position.z=-2;
    f.draw();int written=0;for(int i=0;i<1024;++i){auto d=f.fb.view().depth[i];check(std::isfinite(d)&&d>=0&&d<=1,"clipped depth bounded");written+=d<1;}
    check(written>0&&written<1024,"near-plane crossing clipped instead of clamped");
    for(auto& v:f.mesh.vertices)v.position.z=2;f.fb.clear();f.draw();for(int i=0;i<1024;++i)check(f.fb.view().depth[i]==1,"far-plane rejection");
    f.pass.shaders.vertex=[](const Vertex& v,const void*){VertexOutput o;o.clip={v.position.x,v.position.y,0,-1};return o;};f.draw();
    for(int i=0;i<1024;++i)check(f.fb.view().depth[i]==1,"behind-camera geometry rejected");
    Fixture g;RenderOptions options;options.cull=CullMode::Back;g.draw(options);check(g.fb.view().depth[500]<1,"CCW front is retained");
    options.cull=CullMode::Front;g.fb.clear();g.draw(options);check(g.fb.view().depth[500]==1,"front-face culling");
}
void interpolation() {
    Fixture f;f.mesh.vertices={{{-1,-1,0},{0,0,1},{0,0}},{{1,-1,0},{0,0,1},{1,0}},{{-1,1,0},{0,0,1},{0,1}}};f.mesh.indices={0,1,2};
    f.pass.shaders.vertex=[](const Vertex& v,const void*){auto out=transformVertex(v,nullptr);float w=v.uv.x>0?2.0f:1.0f;
        out.clip={v.position.x*w,v.position.y*w,0,w};return out;};
    f.pass.shaders.fragment=[](const FragmentInput& in,const ShaderResources&,const void*){FragmentOutput out;out.colors[0]={in.varying[6],in.varying[7],0,1};return out;};
    f.draw();int x=8,y=24;float b=(x+.5f)/32.0f,c=1-(y+.5f)/32.0f,a=1-b-c;
    auto actual=f.fb.view().color[y*32+x];check(near(actual.r,(b/2)/(a+b/2+c))&&near(actual.g,c/(a+b/2+c)),"perspective-correct UV interpolation");
}
void parallel() {
    Fixture reference;
    reference.mesh.vertices.clear();reference.mesh.indices.clear();
    std::uint32_t state=42;
    auto random=[&](){state=1664525u*state+1013904223u;return (state>>8)/16777216.0f;};
    for(int i=0;i<180;++i)reference.mesh.vertices.push_back({{random()*4-2,random()*4-2,random()*1.8f-.9f},{0,0,1},{random(),random()}});
    reference.pass.shaders.fragment=[](const FragmentInput& in,const ShaderResources&,const void*){FragmentOutput o;o.colors[0]={in.varying[6],in.varying[7],in.depth,1};return o;};
    reference.draw();
    for(unsigned threads:{1u,2u,4u})for(unsigned tile:{1u,8u,16u,31u})for(bool bin:{false,true}) {
        Fixture f;f.mesh=reference.mesh;f.pass.shaders=reference.pass.shaders;
        RenderOptions options;options.threads=threads;options.tile_size=tile;options.bin_triangles=bin;
        f.draw(options);check(std::memcmp(reference.fb.view().color,f.fb.view().color,1024*sizeof(Color))==0,"parallel color determinism");
        check(std::memcmp(reference.fb.view().depth,f.fb.view().depth,1024*sizeof(float))==0,"parallel depth determinism");
    }
}
void shaders() {
    Fixture f;int calls=0;
    struct Data{int* calls;float offset;};Data data{&calls,.25f};
    f.pass.shaders.uniforms=&data;
    f.pass.shaders.vertex=[](const Vertex& v,const void* uniform){auto& d=*static_cast<const Data*>(uniform);++*d.calls;auto o=transformVertex(v,nullptr);o.clip.x+=d.offset;return o;};
    f.pass.shaders.fragment=[](const FragmentInput&,const ShaderResources&,const void*){FragmentOutput o;o.colors[0]={0,0,1,1};return o;};f.draw();
    check(calls==4,"indexed vertex shader cache");check(f.fb.view().color[16*32+16].b==1&&f.fb.view().depth[16*32]==1,"custom vertex and fragment pointers");
    FragmentInput in;in.varying[0]=in.varying[1]=in.varying[2]=0;
    auto o=phongFragment(in,{},nullptr);check(std::isfinite(o.colors[0].r),"zero normal does not produce NaN");
}
void mrt() {
    Fixture f;FrameBuffer normal(32,32);normal.clear();f.pass.targets[1]=&normal;f.pass.target_count=2;
    f.pass.shaders.fragment=[](const FragmentInput&,const ShaderResources& r,const void*){FragmentOutput out;out.mask=3;
        out.colors[0]={1,0,0,1};out.colors[1]={0,1,0,1};if(r.target_count!=2)out.discard=true;return out;};f.draw();
    check(f.fb.view().color[500].r==1&&normal.view().color[500].g==1,"multiple framebuffer writes");
    check(f.fb.view().depth[500]==normal.view().depth[500],"shared depth semantics");
    FrameBuffer output(32,32);output.clear();RenderPass pass;pass.target_count=1;pass.targets[0]=&output;
    pass.shaders={transformVertex,texturedFragment,nullptr};pass.resources.texture_count=2;
    pass.resources.textures[0]=f.fb.view().image();pass.resources.textures[1]=normal.view().image();
    pass.shaders.fragment=[](const FragmentInput& in,const ShaderResources& r,const void*){FragmentOutput out;
        auto a=r.textures[0].sample(in.varying[6],in.varying[7]),b=r.textures[1].sample(in.varying[6],in.varying[7]);out.colors[0]={a.r,b.g,0,1};return out;};
    success(CpuRenderer{}.render(geometry(f.mesh),pass));check(output.view().color[500].r==1&&output.view().color[500].g==1,"multiple input framebuffers");
    pass.resources.textures[0]=output.view().image();check(!CpuRenderer{}.render(geometry(f.mesh),pass),"feedback loop rejected");
}
void validation() {
    Fixture f;RenderOptions options;options.threads=0;check(!CpuRenderer{}.render(geometry(f.mesh),f.pass,options),"zero thread count rejected");
    auto shaders=f.pass.shaders;f.pass.shaders.fragment=nullptr;check(!CpuRenderer{}.render(geometry(f.mesh),f.pass),"null callback rejected");f.pass.shaders=shaders;
    f.mesh.indices[0]=100;check(!CpuRenderer{}.render(geometry(f.mesh),f.pass),"invalid index rejected");f.mesh.indices[0]=0;
    f.pass.targets[1]=&f.fb;f.pass.target_count=2;check(!CpuRenderer{}.render(geometry(f.mesh),f.pass),"overlapping outputs rejected");
    f.pass.target_count=1;f.pass.shaders.vertex=[](const Vertex&,const void*){VertexOutput o;o.clip.x=std::numeric_limits<float>::infinity();return o;};
    check(!CpuRenderer{}.render(geometry(f.mesh),f.pass),"non-finite vertex rejected");
    f.pass.shaders.vertex=transformVertex;f.mesh.vertices[0].position.x=std::numeric_limits<float>::max();
    success(CpuRenderer{}.render(geometry(f.mesh),f.pass));
    Color pixels[4];float depth[4];FrameBuffer external(2,2,pixels,depth,4);check(external.valid(),"external framebuffer storage");
    FrameBuffer too_small(2,2,pixels,depth,3);check(!too_small.valid(),"external storage length checked");
    FrameBuffer invalid(-1,2);check(!invalid.valid(),"invalid dimensions");
}
void asset() {
    Mesh mesh;success(loadObjFile(PBC_ASSET_PATH,mesh));check(mesh.indices.size()>100,"real Suzanne geometry parsed");
    FrameBuffer fb(64,64);fb.clear();RenderPass pass;pass.targets[0]=&fb;pass.target_count=1;pass.shaders={transformVertex,phongFragment,nullptr};
    RenderStats stats;success(CpuRenderer{}.render(geometry(mesh),pass,{2,16,true,CullMode::None},&stats));
    int pixels=0;for(int i=0;i<4096;++i)pixels+=fb.view().depth[i]<1;check(pixels>0,"Suzanne produces actual pixels");
    std::cout<<"Suzanne: "<<mesh.vertices.size()<<" corners, "<<mesh.indices.size()/3<<" triangles, "<<pixels<<" pixels\n";
}
int main(int argc,char** argv) {
    const std::map<std::string,std::function<void()>> cases={{"texture",texture},{"obj",obj},{"raster",raster},{"clipping",clipping},
        {"interpolation",interpolation},{"parallel",parallel},{"shaders",shaders},{"mrt",mrt},{"validation",validation},{"asset",asset}};
    if(argc!=2||!cases.count(argv[1])){std::cerr<<"Select a named test case\n";return 1;}
    try{cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<argv[1]<<": "<<e.what()<<'\n';return 1;}
}
