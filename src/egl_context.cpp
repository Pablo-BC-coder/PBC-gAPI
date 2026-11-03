// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/surface.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
namespace pbc {
struct EglHeadless::Impl {
    EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;
    bool initialized=false;
    ~Impl(){if(initialized){if(eglGetCurrentContext()==context)eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
        if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);
        eglTerminate(display);}}
};
EglHeadless::EglHeadless():impl_(new Impl) {
    auto& p=*impl_;p.display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(p.display==EGL_NO_DISPLAY||!eglInitialize(p.display,nullptr,nullptr))return;
    p.initialized=true;if(!eglBindAPI(EGL_OPENGL_ES_API))return;
    EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count=0;if(!eglChooseConfig(p.display,attributes,&config,1,&count)||count==0)return;
    EGLint size[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};p.surface=eglCreatePbufferSurface(p.display,config,size);
    EGLint version[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};p.context=eglCreateContext(p.display,config,EGL_NO_CONTEXT,version);
}
EglHeadless::~EglHeadless()=default;
Result EglHeadless::makeCurrent() {
    auto& p=*impl_;
    if(p.context==EGL_NO_CONTEXT||p.surface==EGL_NO_SURFACE||!eglMakeCurrent(p.display,p.surface,p.surface,p.context))
        return {false,"Could not create/make current an EGL ES3 context; check the platform display and driver"};
    return {};
}
} // namespace pbc
