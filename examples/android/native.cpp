// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/pbc.hpp"
#include "pbc/surface.hpp"
#ifdef PBC_HAS_GLES
#include "pbc/gpu.hpp"
#endif
#include <android_native_app_glue.h>
#include <android/log.h>
namespace {
void command(android_app* app,int32_t event) {
    if(event!=APP_CMD_INIT_WINDOW||!app->window)return;
    pbc::AndroidSurface surface(app->window);pbc::FrameBuffer fb(320,240);fb.clear();
    fb.generateTexture(32,32,[](float u,float v,int,int,void*){return pbc::Color{u,v,.5f,1};});
    pbc::Vertex vertices[]={{{-.9f,-.9f,0},{0,0,1},{0,1}},{{.9f,-.9f,0},{0,0,1},{1,1}},{{0,.9f,0},{0,0,1},{.5f,0}}};
    pbc::RenderPass pass;pass.targets[0]=&fb;pass.target_count=1;pass.shaders={pbc::transformVertex,pbc::texturedFragment,nullptr};
    pass.resources.textures[0]=fb.texture();pass.resources.texture_count=1;pass.resources.sampler.filter=pbc::Filter::Bilinear;
    pbc::RenderOptions options;options.threads=2;
    pbc::Result result{false,"No rendering backend selected"};
#ifdef PBC_HAS_GLES
    pbc::EglHeadless context;result=context.makeCurrent();
    if(result)result=pbc::GlesRenderer{}.render({vertices,3,nullptr,0},pass);
    if(!result)__android_log_print(ANDROID_LOG_WARN,"PBC-gAPI","GLES unavailable (%s); using CPU",result.message.c_str());
#endif
    if(!result)result=pbc::CpuRenderer{}.render({vertices,3,nullptr,0},pass,options);
    if(result)result=surface.present(fb);
    if(!result)__android_log_print(ANDROID_LOG_ERROR,"PBC-gAPI","%s",result.message.c_str());
}
}
extern "C" void android_main(android_app* app) {
    app->onAppCmd=command;
    while(!app->destroyRequested){int events;android_poll_source* source=nullptr;
        if(ALooper_pollOnce(-1,nullptr,&events,reinterpret_cast<void**>(&source))>=0&&source)source->process(app,source);}
}
