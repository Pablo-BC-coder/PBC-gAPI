// PBC-gAPI — property and copyright authority: Pablo B. C.
#pragma once
#include "pbc/pbc.hpp"
namespace pbc { namespace detail {
PBC_HD inline FragmentOutput textureFragment(const FragmentInput& in,const ShaderResources& resources) {
    FragmentOutput out;
    out.colors[0]=resources.texture_count?resources.textures[0].sample(in.varying[6],in.varying[7],resources.sampler):Color{1,1,1,1};
    return out;
}
PBC_HD inline FragmentOutput phong(const FragmentInput& in,const ShaderResources& resources) {
    auto out=textureFragment(in,resources);
    float nx=in.varying[0],ny=in.varying[1],nz=in.varying[2];
    float length=sqrtf(nx*nx+ny*ny+nz*nz);float diffuse=0,specular=0;
    if(length>1e-12f) {
        nx/=length;ny/=length;nz/=length;
        diffuse=fmaxf(0,0.2f*nx-0.4f*ny+0.8944272f*nz);
        float vx=-in.varying[3],vy=-in.varying[4],vz=-in.varying[5];
        float eye_length=sqrtf(vx*vx+vy*vy+vz*vz);
        if(diffuse>0&&eye_length>1e-12f) {
            float dot=((2*diffuse*nx-0.2f)*vx+(2*diffuse*ny+0.4f)*vy+(2*diffuse*nz-0.8944272f)*vz)/eye_length;
            specular=0.15f*powf(fmaxf(0,dot),32.0f);
        }
    }
    out.colors[0].r=out.colors[0].r*(.25f+.75f*diffuse)+specular;
    out.colors[0].g=out.colors[0].g*(.25f+.75f*diffuse)+specular;
    out.colors[0].b=out.colors[0].b*(.25f+.75f*diffuse)+specular;
    return out;
}
} }
