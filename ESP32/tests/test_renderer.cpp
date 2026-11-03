// PBC-gAPI — Copyright (c) Pablo B. C. All rights reserved.
#include <PBCgAPI.h>
#include <PBCMeshes.h>
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace pbc;

struct Solid {
    Color c;
    Color operator()(const VaryData&,const Framebuffer&) const {return c;}
};
static VertexOut screen(float x,float y,float w=1,float depth=0) {
    return {{x*w,y*w,depth*w,w},{{0,0,1,0,0,-2,0,0}}};
}
static void test_meshes_and_texture() {
    Vertex sphere[SphereVertices],cube[24];
    uint16_t indices[SphereIndices],ci[36],checker[64];
    makeSphere16(sphere,indices);makeCube(cube,ci);makeChecker8x8(checker);
    assert(sizeof(checker)==128 && SphereVertices==289 && SphereIndices/3==480);
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x)
        assert(checker[y*8+x]==(((x+y)&1)?0:0xffff));
    const Texture565 tex={checker,8,8};
    assert(tex.sample(0,0).r==1 && tex.sample(0.125f,0).r==0);
    assert(tex.sample(-0.875f,2).r==0);
    for(size_t i=0;i<SphereVertices;++i) {
        assert(std::abs(dot(sphere[i].normal,sphere[i].normal)-1)<1e-5f);
        assert(dot(sphere[i].normal-sphere[i].position,sphere[i].normal-sphere[i].position)==0);
    }
    for(unsigned y=0;y<=16;++y) {
        assert(sphere[y*17].position.x==sphere[y*17+16].position.x);
        assert(sphere[y*17].position.z==sphere[y*17+16].position.z);
        assert(sphere[y*17].uv.u==0 && sphere[y*17+16].uv.u==1);
    }
    for(size_t i=0;i<SphereIndices;i+=3) {
        assert(indices[i]<SphereVertices && indices[i+1]<SphereVertices && indices[i+2]<SphereVertices);
        const Vec3 a=sphere[indices[i]].position,b=sphere[indices[i+1]].position,c=sphere[indices[i+2]].position;
        assert(dot(cross(b-a,c-a),a+b+c)>1e-5f); // outward, no degenerate poles
    }
    for(size_t i=0;i<36;i+=3) {
        const Vertex a=cube[ci[i]],b=cube[ci[i+1]],c=cube[ci[i+2]];
        assert(dot(cross(b.position-a.position,c.position-a.position),a.normal)>0);
    }
    assert(rgb565({1,1,0,0})==0xf800 && rgb565({1,0,1,0})==0x07e0);
    assert(rgb565({1,0,0,1})==0x001f);
}
static void test_top_left_and_winding() {
    uint16_t color[16]={};float depth[16];
    Framebuffer fb(4,4,4,color,depth);
    const VertexOut a=screen(-1,1),b=screen(1,1),c=screen(1,-1),d=screen(-1,-1);
    int calls=0;
    auto count=[&](const VaryData&,const Framebuffer&){++calls;return Color{1,1,0,0};};
    fb.beginBand(0,4,0);
    ShaderTri(a,b,c,fb,count);
    // Reset depth only: a shared edge would call count twice if both triangles own it.
    std::fill(depth,depth+16,1.0f);
    ShaderTri(a,c,d,fb,count);
    assert(calls==16);
    for(uint16_t p:color) assert(p==0xf800);
    fb.beginBand(0,4,0); calls=0;
    ShaderTri(c,b,a,fb,count);std::fill(depth,depth+16,1.0f);
    ShaderTri(d,c,a,fb,count);assert(calls==16);
    fb.beginBand(0,4,0);calls=0;
    ShaderTri(a,b,c,fb,count,true);assert(calls==0);
    ShaderTri(c,b,a,fb,count,true);assert(calls>0);
}
static void test_perspective_and_depth() {
    uint16_t color[16];float depth[16];Framebuffer fb(4,4,4,color,depth);
    VertexOut a=screen(-1,1,1),b=screen(1,1,2),c=screen(-1,-1,4);
    a.vary.v[6]=0;b.vary.v[6]=1;c.vary.v[6]=0;
    float first=-1;
    auto capture=[&](const VaryData& v,const Framebuffer&){if(first<0)first=v.v[6];return Color{1,0,1,0};};
    fb.beginBand(0,4);ShaderTri(a,b,c,fb,capture);
    // Pixel (.5,.5): affine barycentrics .75,.125,.125, w = 1,2,4.
    assert(std::abs(first-(0.125f/2)/(0.75f+0.125f/2+0.125f/4))<1e-6f);
    assert(std::abs(first-0.125f)>0.04f);
    fb.beginBand(0,4);
    ShaderTri(screen(-1,1,1,.5f),screen(1,1,1,.5f),screen(-1,-1,1,.5f),fb,Solid{{1,1,0,0}});
    ShaderTri(screen(-1,1,1,-.5f),screen(1,1,1,-.5f),screen(-1,-1,1,-.5f),fb,Solid{{1,0,1,0}});
    ShaderTri(screen(-1,1,1,.8f),screen(1,1,1,.8f),screen(-1,-1,1,.8f),fb,Solid{{1,0,0,1}});
    assert(color[0]==0x07e0 && std::abs(depth[0]-.25f)<1e-6f);
}
static void test_clipping_and_guards() {
    uint16_t color[18];float depth[18];
    std::fill(color,color+18,0x1234);std::fill(depth,depth+18,1234);
    Framebuffer fb(4,4,4,color+1,depth+1);
    assert(fb.beginBand(0,4));
    int calls=0;
    auto count=[&](const VaryData&,const Framebuffer&){++calls;return Color{1,0,0,0};};
    // Cross near plane and both sides, instead of throwing the whole triangle away.
    ShaderTri(screen(-3,1,1,-2),screen(3,1),screen(0,-2),fb,count);
    assert(calls>0);assert(color[0]==0x1234&&color[17]==0x1234);
    assert(depth[0]==1234&&depth[17]==1234);
    fb.beginBand(0,4);calls=0;
    ShaderTri(screen(-1,1,1,-2),screen(1,1,1,-2),screen(0,-1,1,-2),fb,count);
    assert(calls==0);
    VertexOut bad=screen(0,0);bad.clip.x=std::numeric_limits<float>::quiet_NaN();
    ShaderTri(bad,screen(1,1),screen(0,-1),fb,count);assert(calls==0);
    // One vertex is behind the eye. Homogeneous clipping must stay finite.
    ShaderTri(screen(-.5f,.5f),screen(.5f,.5f),screen(0,0,-1),fb,count);
    assert(color[0]==0x1234&&color[17]==0x1234);
    for(int i=1;i<17;++i) assert(std::isfinite(depth[i]));
    assert(!fb.beginBand(3,2) && fb.rows==0);
    assert(!fb.beginBand(-1,1));
    VertexOut v[3]={screen(-1,1),screen(1,1),screen(0,-1)};
    uint16_t invalid[3]={0,1,3};fb.beginBand(0,4);calls=0;
    assert(!drawIndexed(v,3,invalid,3,fb,count) && calls==0);
}
static void test_blinn_phong() {
    uint16_t white=0xffff,black=0;float depth;Framebuffer fb(1,1,1,&white,&depth);
    BlinnPhong shader;shader.texture={&white,1,1};shader.toLight={0,0,1};
    VaryData v={{0,0,1,0,0,-2,0,0}};
    const Color front=shader(v,fb);
    assert(std::abs(front.r-1.8f)<1e-6f); // .5 ambient + 1 diffuse + .3 specular
    v.v[2]=-1;assert(std::abs(shader(v,fb).r-.5f)<1e-6f);
    shader.texture={&black,1,1};v.v[2]=1;
    assert(std::abs(shader(v,fb).r-.3f)<1e-6f);
    v.v[0]=v.v[1]=v.v[2]=0;assert(std::isfinite(shader(v,fb).r));
}
static std::vector<uint16_t> render(bool sphereSelected,int bandRows) {
    constexpr int W=320,H=240;
    Vertex sphere[SphereVertices],cube[24];uint16_t si[SphereIndices],ci[36],checker[64];
    VertexOut out[SphereVertices];makeSphere16(sphere,si);makeCube(cube,ci);makeChecker8x8(checker);
    TransformShader vs;
    vs.modelView=Mat4::translation(0,0,-3.6f)*Mat4::rotationX(.35f)*Mat4::rotationY(.7f);
    vs.projection=Mat4::perspective(50*kPi/180,4.0f/3,0.1f,20);
    shadeVertices(sphereSelected?sphere:cube,out,sphereSelected?SphereVertices:24,vs);
    BlinnPhong shader;shader.texture={checker,8,8};
    std::vector<uint16_t> color(W*bandRows),frame(W*H);std::vector<float> depth(W*bandRows);
    Framebuffer fb(W,H,bandRows,color.data(),depth.data());
    for(int y=0;y<H;y+=bandRows) {
        int rows=std::min(bandRows,H-y);assert(fb.beginBand(y,rows));
        assert(drawIndexed(out,sphereSelected?SphereVertices:24,sphereSelected?si:ci,
                           sphereSelected?SphereIndices:36,fb,shader,true));
        std::copy(color.begin(),color.begin()+W*rows,frame.begin()+y*W);
    }
    return frame;
}
static void writePPM(const char* path,const std::vector<uint16_t>& frame) {
    FILE* f=std::fopen(path,"wb");assert(f);std::fprintf(f,"P6\n320 240\n255\n");
    for(uint16_t p:frame) {
        const Color c=unpack565(p);const unsigned char rgb[3]={
            static_cast<unsigned char>(c.r*255+.5f),static_cast<unsigned char>(c.g*255+.5f),
            static_cast<unsigned char>(c.b*255+.5f)};
        std::fwrite(rgb,1,3,f);
    }
    std::fclose(f);
}
int main() {
    test_meshes_and_texture();test_top_left_and_winding();test_perspective_and_depth();
    test_clipping_and_guards();test_blinn_phong();
    for(bool sphere:{false,true}) {
        const auto whole=render(sphere,240),strip=render(sphere,16),odd=render(sphere,7);
        assert(whole==strip && strip==odd);
        assert(std::count_if(strip.begin(),strip.end(),[](uint16_t c){return c!=0xffff;})>5000);
        writePPM(sphere?"sphere.ppm":"cube.ppm",strip);
    }
    std::puts("PASS: mesh, RGB565, top-left, winding, perspective, depth, clipping, guards, lighting, strip equivalence.");
}
