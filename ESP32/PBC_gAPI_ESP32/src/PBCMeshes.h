// PBC-gAPI — Copyright (c) Pablo B. C. All rights reserved.
#pragma once
#include "PBCgAPI.h"

namespace pbc {
constexpr size_t SphereVertices=17*17;
constexpr size_t SphereIndices=16*15*6; // 480 real triangles, no degenerate pole triangles.
inline void makeChecker8x8(uint16_t (&pixels)[64]) {
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x)
        pixels[y*8+x]=((x+y)&1)?0x0000:0xFFFF;
}
// 16 longitude sectors around Y, 16 latitude intervals pole-to-pole.
// Duplicate the u=0/1 seam and pole UVs; radial normals remain identical.
inline void makeSphere16(Vertex (&vertices)[SphereVertices],
                         uint16_t (&indices)[SphereIndices]) {
    for(unsigned y=0;y<=16;++y) {
        const float phi=kPi*y/16;
        const float sy=(y==0||y==16)?0.0f:std::sin(phi);
        for(unsigned x=0;x<=16;++x) {
            const float theta=2*kPi*(x==16?0:x)/16;
            const Vec3 n={sy*std::cos(theta),std::cos(phi),sy*std::sin(theta)};
            vertices[y*17+x]={n,n,{x/16.0f,y/16.0f}};
        }
    }
    size_t k=0;
    for(unsigned y=0;y<16;++y) for(unsigned x=0;x<16;++x) {
        const uint16_t a=y*17+x,b=a+1,c=a+17,d=c+1;
        if(y!=0) {indices[k++]=a;indices[k++]=b;indices[k++]=c;}
        if(y!=15) {indices[k++]=b;indices[k++]=d;indices[k++]=c;}
    }
}
inline void makeCube(Vertex (&v)[24],uint16_t (&indices)[36]) {
    const Vec3 n[6]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    const Vec3 right[6]={{1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0}};
    const Vec3 up[6]={{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,0,-1},{0,0,1}};
    const Vec2 uv[4]={{0,1},{1,1},{1,0},{0,0}};
    const float u[4]={-1,1,1,-1},w[4]={-1,-1,1,1};
    for(unsigned f=0;f<6;++f) {
        for(unsigned j=0;j<4;++j)
            v[f*4+j]={(n[f]+right[f]*u[j]+up[f]*w[j])*0.72f,n[f],uv[j]};
        const uint16_t a=f*4;
        indices[f*6]=a;indices[f*6+1]=a+1;indices[f*6+2]=a+2;
        indices[f*6+3]=a;indices[f*6+4]=a+2;indices[f*6+5]=a+3;
    }
}
} // namespace pbc
