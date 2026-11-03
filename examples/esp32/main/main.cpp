// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/pbc.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
    // 64x64 color+depth = 80 KiB. Static buffers need no PSRAM on WROOM-32.
    static pbc::Color colors[64*64];static float depths[64*64];
    pbc::FrameBuffer frame(64,64,colors,depths,64*64);
    frame.generateTexture(8,8,[](float u,float v,int,int,void*){return pbc::Color{u,v,.4f,1};});
    pbc::Vertex vertices[]={{{-.9f,-.9f,0},{0,0,1},{0,1}},{{.9f,-.9f,0},{0,0,1},{1,1}},{{0,.9f,0},{0,0,1},{.5f,0}}};
    pbc::RenderPass pass;pass.targets[0]=&frame;pass.target_count=1;
    pass.resources.textures[0]=frame.texture();pass.resources.texture_count=1;
    pass.resources.sampler.filter=pbc::Filter::Bilinear;pass.shaders={pbc::transformVertex,pbc::texturedFragment,nullptr};
    pbc::RenderOptions options;options.threads=pbc::CpuRenderer::availableThreads();options.tile_size=8;options.bin_triangles=false;
    frame.clear();pbc::RenderStats stats;
    auto result=pbc::CpuRenderer{}.render({vertices,3,nullptr,0},pass,options,&stats);
    if(!result){ESP_LOGE("pbc","%s",result.message.c_str());return;}
    unsigned pixels=0;for(float d:depths)pixels+=d<1;
    ESP_LOGI("pbc","Rendered %u pixels with %u FreeRTOS workers",pixels,stats.worker_threads);
    // A display-specific driver can now consume frame.view(); no LCD model is assumed.
}
