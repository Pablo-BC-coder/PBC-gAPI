// PBC-gAPI — property and copyright authority: Pablo B. C.
#pragma once
namespace pbc { namespace detail {
PBC_HD inline int address(int p, int size, AddressMode mode) {
    if (mode == AddressMode::Repeat) { int q = p % size; return q < 0 ? q + size : q; }
    return p < 0 ? 0 : (p >= size ? size - 1 : p);
}
PBC_HD inline Color mix(Color a, Color b, float t) {
    return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};
}
PBC_HD inline float cubic(float a, float b, float c, float d, float t) {
    return b + 0.5f*t*(c-a+t*(2*a-5*b+4*c-d+t*(3*(b-c)+d-a)));
}
PBC_HD inline Color cubic(Color a, Color b, Color c, Color d, float t) {
    return {cubic(a.r,b.r,c.r,d.r,t),cubic(a.g,b.g,c.g,d.g,t),
            cubic(a.b,b.b,c.b,d.b,t),cubic(a.a,b.a,c.a,d.a,t)};
}
PBC_HD inline float saturate(float v) { return fminf(1, fmaxf(0,v)); }
} // namespace detail
PBC_HD inline Color ImageView::texel(int x, int y, AddressMode mode) const {
    if (!pixels || width <= 0 || height <= 0) return {0,0,0,0};
    return pixels[static_cast<std::size_t>(detail::address(y,height,mode))*width + detail::address(x,width,mode)];
}
PBC_HD inline Color ImageView::sample(float u, float v, Sampler sampler) const {
    // Reject NaN/infinity before any integer conversion (also valid in CUDA device code).
    if (!pixels || width <= 0 || height <= 0 || !(u <= FLT_MAX && u >= -FLT_MAX && v <= FLT_MAX && v >= -FLT_MAX))
        return {0,0,0,0};
    if (sampler.address == AddressMode::Repeat) { u -= floorf(u); v -= floorf(v); }
    else { u = detail::saturate(u); v = detail::saturate(v); }
    if (sampler.filter == Filter::Nearest)
        return texel(static_cast<int>(floorf(u*width)),static_cast<int>(floorf(v*height)),sampler.address);
    float x = u*width-0.5f, y = v*height-0.5f;
    int ix = static_cast<int>(floorf(x)), iy = static_cast<int>(floorf(y));
    float tx = x-ix, ty = y-iy;
    if (sampler.filter == Filter::Bilinear)
        return detail::mix(detail::mix(texel(ix,iy,sampler.address),texel(ix+1,iy,sampler.address),tx),
                           detail::mix(texel(ix,iy+1,sampler.address),texel(ix+1,iy+1,sampler.address),tx),ty);
    Color rows[4];
    for (int j=0;j<4;++j) rows[j] = detail::cubic(texel(ix-1,iy+j-1,sampler.address),texel(ix,iy+j-1,sampler.address),
                                               texel(ix+1,iy+j-1,sampler.address),texel(ix+2,iy+j-1,sampler.address),tx);
    return detail::cubic(rows[0],rows[1],rows[2],rows[3],ty); // Catmull-Rom; overshoot intentionally retained
}
} // namespace pbc
