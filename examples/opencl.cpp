// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "scene.hpp"
#include "pbc/gpu.hpp"
#include <string>
int main(int argc,char** argv) {
    // Production always selects an Intel GPU. CPU/other devices require explicit test opt-in.
    bool testing=argc>1&&std::string(argv[1])=="--test-device";int argument=testing?2:1;
    pbc::OpenClRenderer renderer(testing?pbc::ComputeDeviceSelection::AnyDeviceForTesting:pbc::ComputeDeviceSelection::IntelGpu);
    pbc::OpenClDeviceInfo device;if(!report(renderer.deviceInfo(device)))return 1;
    std::cout<<device.gpu.backend<<": "<<device.gpu.vendor<<" / "<<device.gpu.name<<"; "<<(device.is_gpu?"GPU":"CPU test device")<<'\n';
    if(testing)std::cout<<"Explicit testing mode: this run does not establish Intel GPU acceleration\n";
    Scene scene;if(argc>argument&&!report(scene.load(argv[argument])))return 1;
    scene.frame.clear({.025f,.025f,.025f,1});pbc::OpenClProgram program;program.builtin=pbc::BuiltinFragment::Phong;
    pbc::GpuInfo launch;
    if(!report(renderer.render(pbc::geometry(scene.mesh),scene.pass,program,{},&launch)))return 1;
    std::cout<<launch.compute_units<<" OpenCL compute units; "<<launch.blocks<<" tile groups x "<<launch.threads_per_block<<" work-items\n";
    return scene.write(argc>argument+1?argv[argument+1]:"opencl.ppm")?0:1;
}
