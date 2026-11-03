// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "scene.hpp"
#include "pbc/surface.hpp"
#include <chrono>
#include <thread>
int main(int argc,char** argv) {
    Scene scene;if(argc>1&&!report(scene.load(argv[1])))return 1;
#ifdef _WIN32
    pbc::HdcWindow window(640,480,"PBC-gAPI / HDC");
#else
    pbc::SdlWindow window(640,480,"PBC-gAPI / Raspbian SDL2");
#endif
    if(!window.valid()){std::cerr<<"Could not create presentation window\n";return 1;}
    scene.frame.clear({.025f,.025f,.025f,1});
    pbc::RenderOptions options;options.threads=pbc::CpuRenderer::availableThreads();
    if(!report(pbc::CpuRenderer{}.render(pbc::geometry(scene.mesh),scene.pass,options)))return 1;
    while(window.poll()){if(!report(window.present(scene.frame)))return 1;std::this_thread::sleep_for(std::chrono::milliseconds(16));}
}
