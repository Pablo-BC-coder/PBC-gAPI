// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/detail/pipeline.hpp"
#include <algorithm>
#include <atomic>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "soc/soc_caps.h"
#else
#include <thread>
#include <system_error>
#if defined(__linux__)
#include <sched.h>
#endif
#endif

namespace pbc {
unsigned CpuRenderer::availableThreads() {
#ifdef ESP_PLATFORM
    return portNUM_PROCESSORS;
#else
#if defined(__linux__)
    cpu_set_t affinity;
    if(sched_getaffinity(0,sizeof(affinity),&affinity)==0&&CPU_COUNT(&affinity)>0)
        return static_cast<unsigned>(CPU_COUNT(&affinity));
#endif
    return std::max(1u,std::thread::hardware_concurrency());
#endif
}
Result CpuRenderer::render(Geometry mesh,const RenderPass& pass,RenderOptions options,RenderStats* stats) const {
    if(stats)*stats={};
    auto result=detail::validatePass(pass);if(!result)return result;
    if(!options.threads||!options.tile_size||options.tile_size>256)return {false,"threads must be >=1; tile_size must be 1..256"};
    std::vector<detail::PreparedTriangle> triangles;
    int width=pass.targets[0]->width(),height=pass.targets[0]->height();
    result=detail::buildTriangles(mesh,pass.shaders,width,height,options.cull,triangles,stats);
    if(!result)return result;
    int columns=(width+options.tile_size-1)/options.tile_size,rows=(height+options.tile_size-1)/options.tile_size;
    std::size_t tile_count=static_cast<std::size_t>(columns)*rows;
    // Compact CSR bins retain submission order without a vector allocation per tile.
    std::vector<std::size_t> offsets,items;
    auto forBins=[&](const detail::PreparedTriangle& t,const auto& visitor) {
        for(int y=t.min_y/static_cast<int>(options.tile_size);y<=t.max_y/static_cast<int>(options.tile_size);++y)
            for(int x=t.min_x/static_cast<int>(options.tile_size);x<=t.max_x/static_cast<int>(options.tile_size);++x)
                visitor(static_cast<std::size_t>(y)*columns+x);
    };
    if(options.bin_triangles) {
        offsets.resize(tile_count+1,0);
        for(const auto& t:triangles)forBins(t,[&](std::size_t tile){++offsets[tile+1];});
        for(std::size_t i=1;i<offsets.size();++i)offsets[i]+=offsets[i-1];
        items.resize(offsets.back());auto cursor=offsets;
        for(std::size_t i=0;i<triangles.size();++i)forBins(triangles[i],[&](std::size_t tile){items[cursor[tile]++]=i;});
    }
    ShaderResources resources=pass.resources;resources.target_count=pass.target_count;
    FrameBufferView targets[MaxTargets];for(unsigned i=0;i<pass.target_count;++i)targets[i]=pass.targets[i]->view();
    std::atomic<std::size_t> next{0};
    auto worker=[&] {
        std::size_t tile;
        while((tile=next.fetch_add(1,std::memory_order_relaxed))<tile_count) {
            int left=(tile%columns)*options.tile_size,top=(tile/columns)*options.tile_size;
            int right=std::min(width,left+static_cast<int>(options.tile_size)),bottom=std::min(height,top+static_cast<int>(options.tile_size));
            auto draw=[&](std::size_t index) {
                const auto& t=triangles[index];
                for(int y=std::max(top,t.min_y);y<std::min(bottom,t.max_y+1);++y)
                    for(int x=std::max(left,t.min_x);x<std::min(right,t.max_x+1);++x) {
                        FragmentInput in;
                        if(!detail::fragment(t,x,y,in))continue;
                        auto p=static_cast<std::size_t>(y)*width+x;
                        if(!(in.depth<targets[0].depth[p]))continue; // strict LESS: deterministic first primitive wins ties
                        auto output=pass.shaders.fragment(in,resources,pass.shaders.uniforms);
                        if(output.discard)continue;
                        for(unsigned k=0;k<pass.target_count;++k) {
                            targets[k].depth[p]=in.depth;
                            if(output.mask&(1u<<k))targets[k].color[p]=output.colors[k];
                        }
                    }
            };
            if(options.bin_triangles)for(auto i=offsets[tile];i<offsets[tile+1];++i)draw(items[i]);
            else for(std::size_t i=0;i<triangles.size();++i)draw(i);
        }
    };
    // A tile belongs to exactly one worker: no locks/atomics for color or depth.
    unsigned workers=triangles.empty()?1:static_cast<unsigned>(std::min<std::size_t>({options.threads,availableThreads(),tile_count}));
#ifdef ESP_PLATFORM
    // FreeRTOS owns ESP threads. Keep the caller's stack live until every task finishes.
    SemaphoreHandle_t done=xSemaphoreCreateCounting(workers,0);
    if(!done)return {false,"Could not allocate FreeRTOS completion semaphore"};
    struct Job { decltype(worker)* fn;SemaphoreHandle_t done; } job{&worker,done};
    unsigned started=0;
    for(unsigned i=1;i<workers;++i) {
        if(xTaskCreate([](void* arg){auto* j=static_cast<Job*>(arg);(*j->fn)();xSemaphoreGive(j->done);vTaskDelete(nullptr);},
                       "pbc-tile",4096,&job,tskIDLE_PRIORITY+1,nullptr)!=pdPASS)break;
        ++started;
    }
    worker();for(unsigned i=0;i<started;++i)xSemaphoreTake(done,portMAX_DELAY);
    vSemaphoreDelete(done);workers=started+1;
#else
    std::vector<std::thread> threads;
    // Resource exhaustion falls back to the workers already started, all of which are joined.
    try { for(unsigned i=1;i<workers;++i)threads.emplace_back(worker); }
    catch(const std::system_error&) { workers=static_cast<unsigned>(threads.size()+1); }
    worker();for(auto& t:threads)t.join();
#endif
    if(stats){stats->tiles=tile_count;stats->worker_threads=workers;}
    return {};
}
} // namespace pbc
