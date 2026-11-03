// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/surface.hpp"
#include <android/native_window.h>
namespace pbc {
AndroidSurface::AndroidSurface(ANativeWindow* window):window_(window){if(window_)ANativeWindow_acquire(window_);}
AndroidSurface::~AndroidSurface(){if(window_)ANativeWindow_release(window_);}
Result AndroidSurface::present(const FrameBuffer& fb) {
    if(!window_||!fb.valid())return {false,"Invalid Android window or framebuffer"};
    if(ANativeWindow_setBuffersGeometry(window_,fb.width(),fb.height(),WINDOW_FORMAT_RGBA_8888)!=0)return {false,"ANativeWindow_setBuffersGeometry failed"};
    ANativeWindow_Buffer buffer{};if(ANativeWindow_lock(window_,&buffer,nullptr)!=0)return {false,"ANativeWindow_lock failed"};
    auto* pixels=static_cast<unsigned char*>(buffer.bits);
    if(buffer.width!=fb.width()||buffer.height!=fb.height()||buffer.format!=WINDOW_FORMAT_RGBA_8888){ANativeWindow_unlockAndPost(window_);return {false,"Unexpected Android window format"};}
    for(int y=0;y<fb.height();++y)for(int x=0;x<fb.width();++x) {
        auto c=fb.view().color[static_cast<std::size_t>(y)*fb.width()+x];auto* p=pixels+(static_cast<std::size_t>(y)*buffer.stride+x)*4;
        p[0]=static_cast<unsigned char>(detail::saturate(c.r)*255);p[1]=static_cast<unsigned char>(detail::saturate(c.g)*255);
        p[2]=static_cast<unsigned char>(detail::saturate(c.b)*255);p[3]=255;
    }
    return ANativeWindow_unlockAndPost(window_)==0?Result{}:Result{false,"ANativeWindow_unlockAndPost failed"};
}
} // namespace pbc
