// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/pbc.hpp"
#include "pbc/detail/shaders.hpp"
#include <algorithm>
#include <limits>

namespace pbc {
namespace {
std::size_t count(int w,int h) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return 0;
    return static_cast<std::size_t>(w)*h;
}
}
FrameBuffer::FrameBuffer(int w,int h) {
    auto n=count(w,h);
    if (!n) return;
    owned_color_.resize(n); owned_depth_.resize(n,1);
    view_={owned_color_.data(),owned_depth_.data(),w,h};
}
FrameBuffer::FrameBuffer(int w,int h,Color* c,float* d,std::size_t n) {
    if (c && d && count(w,h) && n>=count(w,h)) view_={c,d,w,h};
}
bool FrameBuffer::valid() const { return view_.color && view_.depth && count(width(),height()); }
void FrameBuffer::clear(Color c,float d) {
    if (!valid()) return;
    std::fill_n(view_.color,count(width(),height()),c);
    std::fill_n(view_.depth,count(width(),height()),d);
}
bool FrameBuffer::generateTexture(int w,int h,TextureGenerator generate,void* user) {
    if (!count(w,h) || !generate) return false;
    std::vector<Color> pixels(count(w,h));
    for (int y=0;y<h;++y) for(int x=0;x<w;++x)
        pixels[static_cast<std::size_t>(y)*w+x]=generate((x+0.5f)/w,(y+0.5f)/h,x,y,user);
    texture_.swap(pixels); texture_width_=w; texture_height_=h;
    return true;
}
ImageView FrameBuffer::texture() const { return {texture_.data(),texture_width_,texture_height_}; }
Color FrameBuffer::getTexturePixel(float u,float v,Sampler s) const { return texture().sample(u,v,s); }
Color FrameBuffer::getTexel(int x,int y,AddressMode m) const { return texture().texel(x,y,m); }
VertexOutput transformVertex(const Vertex& v,const void* data) {
    TransformUniforms identity;
    const auto& t=data ? *static_cast<const TransformUniforms*>(data) : identity;
    const float* m=t.mvp;
    VertexOutput out;
    out.clip={m[0]*v.position.x+m[4]*v.position.y+m[8]*v.position.z+m[12],
              m[1]*v.position.x+m[5]*v.position.y+m[9]*v.position.z+m[13],
              m[2]*v.position.x+m[6]*v.position.y+m[10]*v.position.z+m[14],
              m[3]*v.position.x+m[7]*v.position.y+m[11]*v.position.z+m[15]};
    for(int i=0;i<3;++i) out.varying[i]=t.normal[i]*v.normal.x+t.normal[i+3]*v.normal.y+t.normal[i+6]*v.normal.z;
    const auto* mv=t.model_view;
    for(int i=0;i<3;++i)out.varying[i+3]=mv[i]*v.position.x+mv[i+4]*v.position.y+mv[i+8]*v.position.z+mv[i+12];
    out.varying[6]=v.uv.x; out.varying[7]=v.uv.y;
    return out;
}
FragmentOutput texturedFragment(const FragmentInput& in,const ShaderResources& resources,const void*) {
    return detail::textureFragment(in,resources);
}
FragmentOutput phongFragment(const FragmentInput& in,const ShaderResources& resources,const void*) {
    return detail::phong(in,resources);
}
} // namespace pbc
