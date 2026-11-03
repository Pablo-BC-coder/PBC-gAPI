// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "scene.hpp"
#include "pbc/gpu.hpp"
#include "pbc/surface.hpp"
int main(int argc,char** argv) {
    pbc::EglHeadless context;if(!report(context.makeCurrent()))return 1;
    Scene scene;if(argc>1&&!report(scene.load(argv[1])))return 1;
    pbc::GpuInfo info;if(!report(pbc::GlesRenderer::deviceInfo(info)))return 1;
    std::cout<<info.backend<<": "<<info.vendor<<" / "<<info.name<<"; physical core count not exposed by ES\n";
    scene.frame.clear({.025f,.025f,.025f,1});pbc::GlesProgram shader;shader.builtin=pbc::BuiltinFragment::Phong;
    if(!report(pbc::GlesRenderer{}.render(pbc::geometry(scene.mesh),scene.pass,shader)))return 1;
    return scene.write(argc>2?argv[2]:"gles.ppm")?0:1;
}
