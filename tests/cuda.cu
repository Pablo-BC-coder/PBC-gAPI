// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/gpu.hpp"
#include <cuda_runtime.h>
#include <iostream>
using namespace pbc;
__device__ FragmentOutput customShader(const FragmentInput& in,const ShaderResources& r,const void*) {
    FragmentOutput out;out.mask=3;out.colors[0]=r.textures[0].sample(in.varying[6],in.varying[7],r.sampler);
    out.colors[1]={0,1,0,1};return out;
}
__device__ FragmentShader shaderPointer=customShader;
int main() {
    int count=0;auto status=cudaGetDeviceCount(&count);
    if(status!=cudaSuccess||count==0){std::cerr<<"SKIP: no usable CUDA device: "<<cudaGetErrorString(status)<<'\n';return 77;}
    FrameBuffer fb(16,16),other(16,16);fb.clear();other.clear();
    fb.generateTexture(4,4,[](float u,float v,int,int,void*){return Color{u,v,0,1};});
    Mesh mesh;mesh.vertices={{{-1,-1,0},{0,0,1},{0,1}},{{1,-1,0},{0,0,1},{1,1}},{{1,1,0},{0,0,1},{1,0}},{{-1,1,0},{0,0,1},{0,0}}};mesh.indices={0,1,2,0,2,3};
    RenderPass pass;pass.targets[0]=&fb;pass.targets[1]=&other;pass.target_count=2;pass.shaders.vertex=transformVertex;
    pass.resources.textures[0]=fb.texture();pass.resources.texture_count=1;pass.resources.sampler.filter=Filter::Bicubic;
    CudaProgram shader;if(cudaMemcpyFromSymbol(&shader.device_fragment,shaderPointer,sizeof(FragmentShader))!=cudaSuccess)return 1;
    GpuInfo launch;auto result=CudaRenderer{}.render(geometry(mesh),pass,shader,{},&launch);
    if(!result){std::cerr<<result.message<<'\n';return 1;}
    for(int i=0;i<256;++i)if(other.view().color[i].g!=1||std::abs(fb.view().depth[i]-.5f)>1e-5f)return 1;
    if(!launch.blocks||!launch.threads_per_block||!launch.compute_units)return 1;
    std::cout<<"PASS CUDA: device shader pointer, MRT, depth, occupancy selection\n";
}
