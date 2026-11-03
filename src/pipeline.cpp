// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/detail/pipeline.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace pbc { namespace detail {
namespace {
bool overlap(const void* a,std::size_t an,const void* b,std::size_t bn) {
    if(!a || !b || !an || !bn) return false;
    auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    return x<=y ? y-x<an : x-y<bn;
}
double plane(const Vec4& p,int n) {
    double x=p.x,y=p.y,z=p.z,w=p.w;
    switch(n) {
    case 0:return w+x;case 1:return w-x;
    case 2:return w+y;case 3:return w-y;
    case 4:return w+z;case 5:return w-z;
    default:return w-1e-6;
    }
}
VertexOutput lerp(const VertexOutput& a,const VertexOutput& b,double t) {
    VertexOutput c;
    auto mix=[&](float x,float y){return static_cast<float>((1-t)*x+t*y);};
    c.clip={mix(a.clip.x,b.clip.x),mix(a.clip.y,b.clip.y),mix(a.clip.z,b.clip.z),mix(a.clip.w,b.clip.w)};
    for(unsigned k=0;k<VaryingCount;++k)c.varying[k]=mix(a.varying[k],b.varying[k]);
    return c;
}
bool finite(const VertexOutput& v) {
    if(!std::isfinite(v.clip.x)||!std::isfinite(v.clip.y)||!std::isfinite(v.clip.z)||!std::isfinite(v.clip.w)) return false;
    for(float f:v.varying) if(!std::isfinite(f)) return false;
    return true;
}
}
Result validatePass(const RenderPass& pass,bool require_fragment) {
    if(pass.target_count==0||pass.target_count>MaxTargets)return {false,"Expected 1..4 render targets"};
    if(!pass.shaders.vertex || (require_fragment && !pass.shaders.fragment))return {false,"Missing shader pointer"};
    if(pass.resources.texture_count>MaxTextures)return {false,"Too many textures"};
    for(unsigned i=0;i<pass.resources.texture_count;++i) {
        auto image=pass.resources.textures[i];
        if(!image.pixels||image.width<=0||image.height<=0||image.width>16384||image.height>16384)
            return {false,"Invalid texture view"};
    }
    struct Range { const void* p;std::size_t n; } ranges[MaxTargets*2]{};
    for(unsigned i=0;i<pass.target_count;++i) {
        auto target=pass.targets[i];
        if(!target||!target->valid())return {false,"Invalid framebuffer"};
        if(target->width()!=pass.targets[0]->width()||target->height()!=pass.targets[0]->height())
            return {false,"Render targets must have equal dimensions"};
        auto v=target->view();auto n=static_cast<std::size_t>(v.width)*v.height;
        ranges[i*2]={v.color,n*sizeof(Color)};ranges[i*2+1]={v.depth,n*sizeof(float)};
    }
    for(unsigned i=0;i<pass.target_count*2;++i) {
        for(unsigned j=0;j<i;++j) if(overlap(ranges[i].p,ranges[i].n,ranges[j].p,ranges[j].n))
            return {false,"Output framebuffer storage overlaps"};
        for(unsigned j=0;j<pass.resources.texture_count;++j) {
            auto image=pass.resources.textures[j];
            if(overlap(ranges[i].p,ranges[i].n,image.pixels,static_cast<std::size_t>(image.width)*image.height*sizeof(Color)))
                return {false,"Texture/output feedback is unsafe; use separate ping-pong framebuffers"};
        }
    }
    return {};
}
Result buildTriangles(Geometry g,const ShaderProgram& program,int width,int height,CullMode cull,
                      std::vector<PreparedTriangle>& output,RenderStats* stats) {
    std::size_t count=g.index_count ? g.index_count : g.vertex_count;
    if(count%3)return {false,"Triangle index/vertex count must be divisible by three"};
    if(!count)return {};
    if(!g.vertices || (g.index_count&&!g.indices) || !program.vertex)return {false,"Invalid geometry or vertex shader"};
    if(g.vertex_count>std::numeric_limits<std::uint32_t>::max())return {false,"Too many vertices"};
    // Validate first: invalid index never reaches a user shader or dereferences storage.
    for(std::size_t i=0;i<g.index_count;++i)if(g.indices[i]>=g.vertex_count)return {false,"Vertex index out of range"};
    std::vector<VertexOutput> cache(g.vertex_count);
    std::vector<unsigned char> ready(g.vertex_count,0);
    if(stats)stats->input_triangles=count/3;
    for(std::size_t i=0;i<count;i+=3) {
        VertexOutput polygon[16],temporary[16];int n=3;
        for(unsigned k=0;k<3;++k) {
            auto index=g.index_count ? g.indices[i+k] : static_cast<std::uint32_t>(i+k);
            if(!ready[index]) {
                cache[index]=program.vertex(g.vertices[index],program.uniforms);ready[index]=1;
                if(!finite(cache[index]))return {false,"Vertex shader produced a non-finite output"};
            }
            polygon[k]=cache[index];
        }
        // Clip in homogeneous coordinates BEFORE perspective division, including near/far.
        for(int p=0;p<7 && n;++p) {
            int m=0;
            for(int k=0;k<n;++k) {
                const auto& a=polygon[k];const auto& b=polygon[(k+1)%n];
                double da=plane(a.clip,p),db=plane(b.clip,p);
                if(da>=0)temporary[m++]=a;
                if((da>=0)!=(db>=0))temporary[m++]=lerp(a,b,da/(da-db));
            }
            n=m;std::copy_n(temporary,n,polygon);
        }
        for(int k=1;k+1<n;++k) {
            VertexOutput clipped[3]={polygon[0],polygon[k],polygon[k+1]};
            PreparedTriangle t;
            for(int j=0;j<3;++j) {
                auto& v=t.v[j];const auto& p=clipped[j];
                v.inv_w=1/p.clip.w;v.ndc_z=std::clamp(p.clip.z/p.clip.w,-1.0f,1.0f);
                v.x=static_cast<int>(std::lround((std::clamp(p.clip.x/p.clip.w,-1.0f,1.0f)*0.5f+0.5f)*width*16));
                v.y=static_cast<int>(std::lround((0.5f-std::clamp(p.clip.y/p.clip.w,-1.0f,1.0f)*0.5f)*height*16));
                std::copy_n(p.varying,VaryingCount,v.varying);
            }
            t.area=edge(t.v[0],t.v[1],t.v[2].x,t.v[2].y);
            if(!t.area)continue;
            bool front=t.area<0;
            if((cull==CullMode::Back&&!front)||(cull==CullMode::Front&&front))continue;
            if(t.area<0){std::swap(t.v[1],t.v[2]);t.area=-t.area;}
            t.min_x=std::max(0,std::min({t.v[0].x,t.v[1].x,t.v[2].x})/16);
            t.max_x=std::min(width-1,std::max({t.v[0].x,t.v[1].x,t.v[2].x})/16);
            t.min_y=std::max(0,std::min({t.v[0].y,t.v[1].y,t.v[2].y})/16);
            t.max_y=std::min(height-1,std::max({t.v[0].y,t.v[1].y,t.v[2].y})/16);
            if(t.min_x<=t.max_x&&t.min_y<=t.max_y)output.push_back(t);
        }
    }
    if(stats)stats->clipped_triangles=output.size();
    return {};
}
} } // namespace pbc::detail
