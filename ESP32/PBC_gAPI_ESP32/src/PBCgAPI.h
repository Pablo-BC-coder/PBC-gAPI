// PBC-gAPI — Copyright (c) Pablo B. C. All rights reserved.
// ESP32 CPU port derived from PBC_GAPI_CUDA/terminalPBC_API.cpp (a6b45e7).
// Standalone C++11: no CUDA, Win32, Arduino or display dependency.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace pbc {
constexpr float kPi = 3.14159265358979323846f;
struct Color { float a, r, g, b; }; // Same channel order as the original API.
struct Vec2 { float u, v; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct VaryData { float v[8]; }; // normal.xyz, viewPosition.xyz, uv.xy
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, float b) { return {a.x*b,a.y*b,a.z*b}; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline Vec3 normalize(Vec3 a) {
    const float l2=dot(a,a);
    return l2>1e-20f ? a*(1.0f/std::sqrt(l2)) : Vec3{0,0,0};
}
inline float saturate(float a) { return std::max(0.0f,std::min(1.0f,a)); }
inline uint16_t rgb565(Color c) {
    return uint16_t((uint16_t(saturate(c.r)*31.0f+0.5f)<<11) |
                    (uint16_t(saturate(c.g)*63.0f+0.5f)<<5) |
                     uint16_t(saturate(c.b)*31.0f+0.5f));
}
inline Color unpack565(uint16_t c) {
    return {1.0f,((c>>11)&31)/31.0f,((c>>5)&63)/63.0f,(c&31)/31.0f};
}
struct Texture565 {
    const uint16_t* pixels;
    uint16_t width, height;
    // Native uint16_t RGB565, nearest-neighbour, repeat; v=0 is the first row.
    Color sample(float u, float v) const {
        if (!pixels || !width || !height || !std::isfinite(u) || !std::isfinite(v))
            return {1,1,1,1};
        u-=std::floor(u); v-=std::floor(v);
        const unsigned x=std::min(unsigned(u*width),unsigned(width-1));
        const unsigned y=std::min(unsigned(v*height),unsigned(height-1));
        return unpack565(pixels[y*width+x]);
    }
};

// Column-major matrices, column vectors; right-handed view looks toward -Z.
struct Mat4 {
    float m[16];
    static Mat4 identity() {
        Mat4 r={}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1; return r;
    }
    static Mat4 translation(float x,float y,float z) {
        Mat4 r=identity(); r.m[12]=x; r.m[13]=y; r.m[14]=z; return r;
    }
    static Mat4 rotationX(float a) {
        Mat4 r=identity(); const float c=std::cos(a),s=std::sin(a);
        r.m[5]=c; r.m[6]=s; r.m[9]=-s; r.m[10]=c; return r;
    }
    static Mat4 rotationY(float a) {
        Mat4 r=identity(); const float c=std::cos(a),s=std::sin(a);
        r.m[0]=c; r.m[2]=-s; r.m[8]=s; r.m[10]=c; return r;
    }
    static Mat4 perspective(float fovY,float aspect,float nearZ,float farZ) {
        Mat4 r={};
        const float f=1.0f/std::tan(fovY*0.5f);
        r.m[0]=f/aspect; r.m[5]=f;
        r.m[10]=(farZ+nearZ)/(nearZ-farZ); r.m[11]=-1;
        r.m[14]=2*farZ*nearZ/(nearZ-farZ); return r;
    }
};
inline Vec4 operator*(const Mat4& m,Vec4 v) {
    return {m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z+m.m[12]*v.w,
            m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z+m.m[13]*v.w,
            m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z+m.m[14]*v.w,
            m.m[3]*v.x+m.m[7]*v.y+m.m[11]*v.z+m.m[15]*v.w};
}
inline Mat4 operator*(const Mat4& a,const Mat4& b) {
    Mat4 r={};
    for (int c=0;c<4;++c) for(int row=0;row<4;++row)
        for(int k=0;k<4;++k) r.m[c*4+row]+=a.m[k*4+row]*b.m[c*4+k];
    return r;
}
struct Vertex { Vec3 position,normal; Vec2 uv; };
struct VertexOut { Vec4 clip; VaryData vary; };
template<class VertexShader>
void shadeVertices(const Vertex* in,VertexOut* out,size_t count,VertexShader shader) {
    for(size_t i=0;i<count;++i) out[i]=shader(in[i]);
}
struct TransformShader {
    Mat4 modelView,projection;
    // Rigid transforms or uniform scale only. For nonuniform scale, supply
    // a custom shader with the inverse-transpose normal matrix.
    VertexOut operator()(const Vertex& v) const {
        const Vec4 p=modelView*Vec4{v.position.x,v.position.y,v.position.z,1};
        const Vec4 n=modelView*Vec4{v.normal.x,v.normal.y,v.normal.z,0};
        return {projection*p,{{n.x,n.y,n.z,p.x,p.y,p.z,v.uv.u,v.uv.v}}};
    }
};

// One screen-space band; width/height describe the entire viewport.
// Storage must contain width*capacityRows elements of each buffer.
class Framebuffer {
public:
    const int width,height,capacityRows;
    uint16_t* const color_buffer;
    float* const depth_buffer;
    int top=0,rows=0;
    Framebuffer(int w,int h,int capacity,uint16_t* color,float* depth)
        :width(w),height(h),capacityRows(capacity),color_buffer(color),depth_buffer(depth) {}
    bool beginBand(int y,int count,uint16_t background=0xFFFF) {
        rows=0;
        if (!color_buffer || !depth_buffer || width<=0 || width>1024 || height<=0 ||
            height>1024 || y<0 || y>=height || count<=0 || count>capacityRows || count>height-y)
            return false;
        top=y; rows=count;
        std::fill(color_buffer,color_buffer+width*rows,background);
        std::fill(depth_buffer,depth_buffer+width*rows,1.0f);
        return true;
    }
};
struct BlinnPhong {
    Texture565 texture;
    Vec3 toLight=normalize(Vec3{-0.4f,0.6f,1.0f}); // view space, toward source
    float directionalIntensity=1.0f,ambientIntensity=0.5f;
    float specularIntensity=0.30f,shininess=32.0f;
    Color operator()(const VaryData& v,const Framebuffer&) const {
        const Vec3 n=normalize({v.v[0],v.v[1],v.v[2]});
        const Vec3 eye=normalize({-v.v[3],-v.v[4],-v.v[5]});
        const Vec3 l=normalize(toLight),h=normalize(l+eye);
        const float ndotl=std::max(0.0f,dot(n,l));
        float spec=0;
        if(ndotl>0) {
            float s=std::max(0.0f,dot(n,h));
            if(shininess==32.0f) { s*=s; s*=s; s*=s; s*=s; s*=s; }
            else s=std::pow(s,shininess);
            spec=s*specularIntensity*directionalIntensity;
        }
        const float diffuse=ambientIntensity+directionalIntensity*ndotl;
        const Color t=texture.sample(v.v[6],v.v[7]);
        return {1,t.r*diffuse+spec,t.g*diffuse+spec,t.b*diffuse+spec};
    }
};

namespace detail {
inline bool finite(const VertexOut& v) {
    if(!std::isfinite(v.clip.x)||!std::isfinite(v.clip.y)||
       !std::isfinite(v.clip.z)||!std::isfinite(v.clip.w)) return false;
    for(float a:v.vary.v) if(!std::isfinite(a)) return false;
    return true;
}
inline float planeDistance(Vec4 p,int plane) {
    switch(plane) {
    case 0:return p.w+p.x; case 1:return p.w-p.x;
    case 2:return p.w+p.y; case 3:return p.w-p.y;
    case 4:return p.w+p.z; default:return p.w-p.z;
    }
}
inline VertexOut lerp(const VertexOut& a,const VertexOut& b,float t) {
    VertexOut r;
    r.clip={a.clip.x+(b.clip.x-a.clip.x)*t,a.clip.y+(b.clip.y-a.clip.y)*t,
            a.clip.z+(b.clip.z-a.clip.z)*t,a.clip.w+(b.clip.w-a.clip.w)*t};
    for(int i=0;i<8;++i) r.vary.v[i]=a.vary.v[i]+(b.vary.v[i]-a.vary.v[i])*t;
    return r;
}
struct ScreenVertex { int32_t x,y; float depth,invW; VaryData vary; };
inline int32_t edge(const ScreenVertex& a,const ScreenVertex& b,int32_t x,int32_t y) {
    return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
}
inline bool topLeft(const ScreenVertex& a,const ScreenVertex& b) {
    const int dx=b.x-a.x,dy=b.y-a.y; return dy<0 || (dy==0 && dx>0);
}
inline bool toScreen(const VertexOut& in,ScreenVertex& out,int width,int height) {
    if(!finite(in) || in.clip.w<=1e-7f) return false;
    out.invW=1.0f/in.clip.w;
    out.x=int32_t(std::floor((in.clip.x*out.invW*0.5f+0.5f)*width*16+0.5f));
    out.y=int32_t(std::floor((0.5f-in.clip.y*out.invW*0.5f)*height*16+0.5f));
    out.depth=in.clip.z*out.invW*0.5f+0.5f;
    out.vary=in.vary; return true;
}
template<class FragmentShader>
void rasterize(ScreenVertex a,ScreenVertex b,ScreenVertex c,Framebuffer& fb,
               const FragmentShader& shader,bool cullBackfaces) {
    int32_t area=edge(a,b,c.x,c.y);
    // CCW in NDC becomes negative area in a downward-Y framebuffer.
    if(area==0 || (cullBackfaces && area>=0)) return;
    if(area<0) { std::swap(b,c); area=-area; }
    const int minX=std::max(0,int(std::min(a.x,std::min(b.x,c.x))/16));
    const int maxX=std::min(fb.width-1,int(std::max(a.x,std::max(b.x,c.x))/16));
    const int minY=std::max(fb.top,int(std::min(a.y,std::min(b.y,c.y))/16));
    const int maxY=std::min(fb.top+fb.rows-1,int(std::max(a.y,std::max(b.y,c.y))/16));
    if(minX>maxX || minY>maxY) return;
    const bool tl0=topLeft(b,c),tl1=topLeft(c,a),tl2=topLeft(a,b);
    const float invArea=1.0f/area;
    for(int y=minY;y<=maxY;++y) {
        int32_t e0=edge(b,c,minX*16+8,y*16+8);
        int32_t e1=edge(c,a,minX*16+8,y*16+8);
        int32_t e2=edge(a,b,minX*16+8,y*16+8);
        for(int x=minX;x<=maxX;++x,e0-=(c.y-b.y)*16,
            e1-=(a.y-c.y)*16,e2-=(b.y-a.y)*16) {
            if(e0<0 || (e0==0&&!tl0) || e1<0 || (e1==0&&!tl1) ||
               e2<0 || (e2==0&&!tl2)) continue;
            const float l0=e0*invArea,l1=e1*invArea,l2=e2*invArea;
            // NDC depth is affine in screen space; attributes are not.
            const float depth=l0*a.depth+l1*b.depth+l2*c.depth;
            const int index=(y-fb.top)*fb.width+x;
            if(depth<0 || depth>1 || depth>=fb.depth_buffer[index]) continue;
            const float q0=l0*a.invW,q1=l1*b.invW,q2=l2*c.invW;
            const float sum=q0+q1+q2;
            if(sum<=0) continue;
            const float w=1.0f/sum;
            VaryData vary;
            for(int i=0;i<8;++i)
                vary.v[i]=(q0*a.vary.v[i]+q1*b.vary.v[i]+q2*c.vary.v[i])*w;
            fb.color_buffer[index]=rgb565(shader(vary,fb));
            fb.depth_buffer[index]=depth;
        }
    }
}
} // detail

// Clip all six homogeneous planes BEFORE division; preserve linear varyings
// through clipping, then apply 1/w interpolation at each covered pixel.
template<class FragmentShader>
void ShaderTri(const VertexOut& a,const VertexOut& b,const VertexOut& c,
               Framebuffer& fb,const FragmentShader& shader,bool cullBackfaces=false) {
    if(!fb.rows || !detail::finite(a)||!detail::finite(b)||!detail::finite(c)) return;
    // Band rejection is safe before clipping only for positive w vertices.
    if(a.clip.w>1e-7f && b.clip.w>1e-7f && c.clip.w>1e-7f) {
        const float ya=(0.5f-a.clip.y/a.clip.w*0.5f)*fb.height;
        const float yb=(0.5f-b.clip.y/b.clip.w*0.5f)*fb.height;
        const float yc=(0.5f-c.clip.y/c.clip.w*0.5f)*fb.height;
        if(std::max(ya,std::max(yb,yc))<fb.top-1 ||
           std::min(ya,std::min(yb,yc))>fb.top+fb.rows+1) return;
    }
    VertexOut p[12]={a,b,c},q[12];
    int count=3;
    for(int plane=0;plane<6;++plane) {
        int next=0;
        VertexOut prev=p[count-1]; float dp=detail::planeDistance(prev.clip,plane);
        for(int i=0;i<count;++i) {
            const VertexOut cur=p[i]; const float dc=detail::planeDistance(cur.clip,plane);
            if((dp>=0)!=(dc>=0)) q[next++]=detail::lerp(prev,cur,dp/(dp-dc));
            if(dc>=0) q[next++]=cur;
            prev=cur; dp=dc;
        }
        if(next<3) return;
        count=next;
        for(int i=0;i<count;++i) p[i]=q[i];
    }
    detail::ScreenVertex first;
    if(!detail::toScreen(p[0],first,fb.width,fb.height)) return;
    for(int i=1;i<count-1;++i) {
        detail::ScreenVertex v1,v2;
        if(detail::toScreen(p[i],v1,fb.width,fb.height) &&
           detail::toScreen(p[i+1],v2,fb.width,fb.height))
            detail::rasterize(first,v1,v2,fb,shader,cullBackfaces);
    }
}
template<class FragmentShader>
bool drawIndexed(const VertexOut* vertices,size_t vertexCount,const uint16_t* indices,
                 size_t indexCount,Framebuffer& fb,const FragmentShader& shader,
                 bool cullBackfaces=false) {
    if(!vertices || !indices || indexCount%3) return false;
    // Validate once before touching the framebuffer.
    for(size_t i=0;i<indexCount;++i) if(indices[i]>=vertexCount) return false;
    for(size_t i=0;i<indexCount;i+=3)
        ShaderTri(vertices[indices[i]],vertices[indices[i+1]],vertices[indices[i+2]],
                  fb,shader,cullBackfaces);
    return true;
}
} // namespace pbc
