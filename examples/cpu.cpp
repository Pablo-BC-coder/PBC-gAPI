// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "scene.hpp"
#include <charconv>
#include <cstring>
#include <chrono>
int main(int argc,char** argv) {
    Scene scene;pbc::RenderOptions options;
    if(argc>1) {
        auto end=argv[1]+std::strlen(argv[1]);auto parsed=std::from_chars(argv[1],end,options.threads);
        if(parsed.ec!=std::errc{}||parsed.ptr!=end||!options.threads){std::cerr<<"Usage: pbc_cpu [threads>=1] [model.obj] [output.ppm]\n";return 1;}
    }
    if(argc>2&&!report(scene.load(argv[2])))return 1;
    scene.frame.clear({.025f,.025f,.025f,1});pbc::RenderStats stats;
    auto start=std::chrono::steady_clock::now();
    if(!report(pbc::CpuRenderer{}.render(pbc::geometry(scene.mesh),scene.pass,options,&stats)))return 1;
    if(!scene.write(argc>3?argv[3]:"cpu.ppm"))return 1;
    std::cout<<stats.clipped_triangles<<" triangles; "<<stats.worker_threads<<" threads; "<<stats.tiles<<" tiles; "
        <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms including image write\n";
}
