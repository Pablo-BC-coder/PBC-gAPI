// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/surface.hpp"
#include <SDL.h>
#include <vector>

namespace pbc {
struct SdlWindow::Impl {
    SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;SDL_Texture* texture=nullptr;
    bool initialized=false,open=true;int width=0,height=0;
    ~Impl(){SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);if(initialized)SDL_QuitSubSystem(SDL_INIT_VIDEO);}
};
SdlWindow::SdlWindow(int w,int h,const char* title):impl_(new Impl) {
    if(w<=0||h<=0||SDL_InitSubSystem(SDL_INIT_VIDEO)!=0)return;
    impl_->initialized=true;
    impl_->window=SDL_CreateWindow(title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,w,h,SDL_WINDOW_RESIZABLE);
    if(impl_->window)impl_->renderer=SDL_CreateRenderer(impl_->window,-1,SDL_RENDERER_SOFTWARE);
}
SdlWindow::~SdlWindow()=default;
bool SdlWindow::valid()const{return impl_->window&&impl_->renderer;}
bool SdlWindow::poll(){SDL_Event event;while(SDL_PollEvent(&event))if(event.type==SDL_QUIT ||
    (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE && event.window.windowID==SDL_GetWindowID(impl_->window)))impl_->open=false;
    return valid()&&impl_->open;}
Result SdlWindow::present(const FrameBuffer& fb) {
    if(!valid()||!fb.valid())return {false,"Invalid SDL window or framebuffer"};
    if(!impl_->texture||impl_->width!=fb.width()||impl_->height!=fb.height()) {
        SDL_DestroyTexture(impl_->texture);impl_->texture=SDL_CreateTexture(impl_->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,fb.width(),fb.height());
        impl_->width=fb.width();impl_->height=fb.height();
        if(!impl_->texture)return {false,SDL_GetError()};
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(fb.width())*fb.height()*4);auto pixels=fb.view().color;
    for(std::size_t i=0;i<bytes.size()/4;++i){bytes[i*4]=static_cast<unsigned char>(detail::saturate(pixels[i].r)*255);
        bytes[i*4+1]=static_cast<unsigned char>(detail::saturate(pixels[i].g)*255);bytes[i*4+2]=static_cast<unsigned char>(detail::saturate(pixels[i].b)*255);bytes[i*4+3]=255;}
    if(SDL_UpdateTexture(impl_->texture,nullptr,bytes.data(),fb.width()*4)!=0||SDL_RenderCopy(impl_->renderer,impl_->texture,nullptr,nullptr)!=0)return {false,SDL_GetError()};
    SDL_RenderPresent(impl_->renderer);return {};
}
} // namespace pbc
