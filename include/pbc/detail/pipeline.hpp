// PBC-gAPI — property and copyright authority: Pablo B. C.
#pragma once
#include "pbc/pbc.hpp"
#include <cstdint>

namespace pbc { namespace detail {
struct ScreenVertex { int x=0,y=0; float inv_w=0,ndc_z=0; float varying[VaryingCount]{}; };
struct PreparedTriangle {
    ScreenVertex v[3];
    int min_x=0,min_y=0,max_x=-1,max_y=-1;
    std::int64_t area=0;
};
Result validatePass(const RenderPass&, bool require_fragment=true);
Result buildTriangles(Geometry,const ShaderProgram&,int width,int height,CullMode,
                      std::vector<PreparedTriangle>&,RenderStats*);
PBC_HD inline std::int64_t edge(const ScreenVertex& a,const ScreenVertex& b,int x,int y) {
    return static_cast<std::int64_t>(b.x-a.x)*(y-a.y)-static_cast<std::int64_t>(b.y-a.y)*(x-a.x);
}
PBC_HD inline bool topLeft(const ScreenVertex& a,const ScreenVertex& b) {
    return b.y<a.y || (b.y==a.y && b.x>a.x);
}
PBC_HD inline bool fragment(const PreparedTriangle& t,int x,int y,FragmentInput& in) {
    int px=x*16+8,py=y*16+8;
    std::int64_t e[3]={edge(t.v[1],t.v[2],px,py),edge(t.v[2],t.v[0],px,py),edge(t.v[0],t.v[1],px,py)};
    for(int i=0;i<3;++i) if(e[i]<0 || (e[i]==0 && !topLeft(t.v[(i+1)%3],t.v[(i+2)%3]))) return false;
    float b[3]={static_cast<float>(e[0])/t.area,static_cast<float>(e[1])/t.area,static_cast<float>(e[2])/t.area};
    // NDC depth is affine in screen space; attributes require reciprocal-w correction.
    in.depth=(b[0]*t.v[0].ndc_z+b[1]*t.v[1].ndc_z+b[2]*t.v[2].ndc_z)*0.5f+0.5f;
    if(!(in.depth>=0 && in.depth<=1)) return false;
    float denominator=b[0]*t.v[0].inv_w+b[1]*t.v[1].inv_w+b[2]*t.v[2].inv_w;
    if(!(denominator>0)) return false;
    for(unsigned k=0;k<VaryingCount;++k) {
        in.varying[k]=0;
        for(int j=0;j<3;++j) in.varying[k]+=b[j]*t.v[j].inv_w*t.v[j].varying[k]/denominator;
    }
    in.x=x; in.y=y;
    return true;
}
} } // namespace pbc::detail
