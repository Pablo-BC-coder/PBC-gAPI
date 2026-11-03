// PBC-gAPI — property and copyright authority: Pablo B. C.
#pragma once
#include "pbc/pbc.hpp"
#include <memory>
struct ANativeWindow;

namespace pbc {
class Surface {
public:
    virtual ~Surface()=default;
    virtual Result present(const FrameBuffer&)=0;
};
class HdcWindow final:public Surface {
public:
    HdcWindow(int width,int height,const char* title);
    ~HdcWindow();
    HdcWindow(const HdcWindow&)=delete;HdcWindow& operator=(const HdcWindow&)=delete;
    bool poll();bool valid() const;
    Result present(const FrameBuffer&) override;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// SDL2 selects Raspbian's X11, Wayland or KMSDRM driver; it owns presentation only.
// Rendering can independently use CPU, CUDA, or an offscreen EGL/GLES context.
class SdlWindow final:public Surface {
public:
    SdlWindow(int width,int height,const char* title);
    ~SdlWindow();
    SdlWindow(const SdlWindow&)=delete;SdlWindow& operator=(const SdlWindow&)=delete;
    bool poll();bool valid() const;
    Result present(const FrameBuffer&) override;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// LineageOS/Android NDK: NativeActivity supplies the window and handles lifecycle events.
class AndroidSurface final:public Surface {
public:
    explicit AndroidSurface(ANativeWindow*);
    ~AndroidSurface();
    AndroidSurface(const AndroidSurface&)=delete;AndroidSurface& operator=(const AndroidSurface&)=delete;
    Result present(const FrameBuffer&) override;
private:ANativeWindow* window_=nullptr;
};
class EglHeadless {
public:
    EglHeadless();~EglHeadless();
    EglHeadless(const EglHeadless&)=delete;EglHeadless& operator=(const EglHeadless&)=delete;
    Result makeCurrent();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace pbc
