// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "scene.hpp"
#include <chrono>
#include <cstring>
#include <vector>
int main(int argc,char** argv) {
    Scene scene;if(argc>1&&!report(scene.load(argv[1])))return 1;
    // Compare identical output while varying only scheduling/binning. No timing assertions.
    std::vector<pbc::Color> reference;std::vector<float> reference_depth;
    std::cout<<"threads,tile_size,bins,mean_ms\n";
    for(unsigned threads:{1u,2u,4u})for(unsigned tile:{8u,16u,32u})for(bool bins:{false,true}) {
        double elapsed=0;pbc::RenderStats stats;
        for(int iteration=0;iteration<6;++iteration) {
            scene.frame.clear();auto start=std::chrono::steady_clock::now();
            if(!report(pbc::CpuRenderer{}.render(pbc::geometry(scene.mesh),scene.pass,{threads,tile,bins,pbc::CullMode::None},&stats)))return 1;
            if(iteration)elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        }
        auto view=scene.frame.view();std::size_t n=static_cast<std::size_t>(view.width)*view.height;
        if(reference.empty()){reference.assign(view.color,view.color+n);reference_depth.assign(view.depth,view.depth+n);}
        else if(std::memcmp(reference.data(),view.color,n*sizeof(pbc::Color))||std::memcmp(reference_depth.data(),view.depth,n*sizeof(float))){
            std::cerr<<"Scheduling changed the rendered output\n";return 1;}
        std::cout<<stats.worker_threads<<','<<tile<<','<<bins<<','<<elapsed/5<<'\n';
    }
}
