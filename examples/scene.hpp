// PBC-gAPI — property and copyright authority: Pablo B. C.
#pragma once
#include "pbc/pbc.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>

inline pbc::Color checker(float,float,int x,int y,void*) {
    return ((x/8+y/8)&1)?pbc::Color{.1f,.25f,.9f,1}:pbc::Color{1,.8f,.2f,1};
}
struct Scene {
    pbc::FrameBuffer frame{640,480};
    pbc::Mesh mesh;
    pbc::TransformUniforms transform;
    pbc::RenderPass pass;
    Scene() {
        mesh.vertices={{{-.8f,-.8f,0},{0,0,1},{0,1}},{{.8f,-.8f,0},{0,0,1},{1,1}},
                       {{.8f,.8f,0},{0,0,1},{1,0}},{{-.8f,.8f,0},{0,0,1},{0,0}}};
        mesh.indices={0,1,2,0,2,3};frame.generateTexture(64,64,checker);
        pass.targets[0]=&frame;pass.target_count=1;
        pass.resources.texture_count=1;pass.resources.textures[0]=frame.texture();
        pass.resources.sampler={pbc::Filter::Bicubic,pbc::AddressMode::Repeat};
        pass.shaders={pbc::transformVertex,pbc::phongFragment,&transform};
    }
    pbc::Result load(const char* path) {
        auto result=pbc::loadObjFile(path,mesh);if(!result)return result;
        if(mesh.vertices.empty())return {false,"OBJ has no faces"};
        pbc::Vec3 low=mesh.vertices[0].position,high=low;
        for(auto v:mesh.vertices) {
            low={std::min(low.x,v.position.x),std::min(low.y,v.position.y),std::min(low.z,v.position.z)};
            high={std::max(high.x,v.position.x),std::max(high.y,v.position.y),std::max(high.z,v.position.z)};
        }
        float scale=1.6f/std::max({high.x-low.x,high.y-low.y,high.z-low.z,1e-6f});
        transform.mvp[0]=scale;transform.mvp[5]=scale;transform.mvp[10]=-scale*.5f;
        transform.mvp[12]=-(low.x+high.x)*.5f*scale;transform.mvp[13]=-(low.y+high.y)*.5f*scale;
        transform.mvp[14]=(low.z+high.z)*.25f*scale;return {};
    }
    bool write(const char* path) const {
        std::ofstream file(path,std::ios::binary);if(!file)return false;
        file<<"P6\n"<<frame.width()<<' '<<frame.height()<<"\n255\n";
        auto pixels=frame.view().color;
        for(int i=0;i<frame.width()*frame.height();++i){unsigned char rgb[]={static_cast<unsigned char>(pbc::detail::saturate(pixels[i].r)*255),
            static_cast<unsigned char>(pbc::detail::saturate(pixels[i].g)*255),static_cast<unsigned char>(pbc::detail::saturate(pixels[i].b)*255)};
            file.write(reinterpret_cast<char*>(rgb),3);}
        return static_cast<bool>(file);
    }
};
inline bool report(pbc::Result result){if(!result)std::cerr<<result.message<<'\n';return result.ok;}
