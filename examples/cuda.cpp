// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "scene.hpp"
#include "pbc/gpu.hpp"
int main(int argc,char** argv) {
    Scene scene;if(argc>1&&!report(scene.load(argv[1])))return 1;
    scene.frame.clear({.025f,.025f,.025f,1});pbc::CudaProgram shader;
    if(!report(pbc::CudaRenderer::builtinFragment(pbc::BuiltinFragment::Phong,shader.device_fragment)))return 1;
    pbc::GpuInfo info;
    if(!report(pbc::CudaRenderer{}.render(pbc::geometry(scene.mesh),scene.pass,shader,{},&info)))return 1;
    std::cout<<info.name<<": "<<info.compute_units<<" SMs; "<<info.blocks<<" blocks x "<<info.threads_per_block<<" threads (occupancy-selected)\n";
    if(info.physical_cores)std::cout<<info.physical_cores<<" GPU cores reported by NVML\n";
    else std::cout<<"GPU core count unavailable from this driver\n";
    return scene.write(argc>2?argv[2]:"cuda.ppm")?0:1;
}
