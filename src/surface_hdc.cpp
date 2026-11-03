// PBC-gAPI — property and copyright authority: Pablo B. C.
#define NOMINMAX
#include <windows.h>
#include "pbc/surface.hpp"

namespace pbc {
struct HdcWindow::Impl {
    HWND window=nullptr;bool open=true;
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtr(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);SetWindowLongPtr(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(message==WM_CLOSE){if(self)self->open=false;return 0;}
        return DefWindowProc(hwnd,message,w,l);
    }
    ~Impl(){if(window)DestroyWindow(window);}
};
HdcWindow::HdcWindow(int w,int h,const char* title):impl_(new Impl) {
    if(w<=0||h<=0)return;
    HINSTANCE instance=GetModuleHandle(nullptr);WNDCLASSA type{};
    type.lpfnWndProc=Impl::procedure;type.hInstance=instance;type.lpszClassName="PBC_GAPI_Window";
    if(!RegisterClassA(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    RECT r{0,0,w,h};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
    impl_->window=CreateWindowA(type.lpszClassName,title,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,
        r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,impl_.get());
    if(impl_->window)ShowWindow(impl_->window,SW_SHOW);
}
HdcWindow::~HdcWindow()=default;
bool HdcWindow::valid()const{return impl_->window!=nullptr;}
bool HdcWindow::poll(){MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)impl_->open=false;TranslateMessage(&msg);DispatchMessage(&msg);}return valid()&&impl_->open;}
Result HdcWindow::present(const FrameBuffer& fb) {
    if(!valid()||!fb.valid())return {false,"Invalid HDC window or framebuffer"};
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(fb.width())*fb.height());
    for(std::size_t i=0;i<pixels.size();++i){auto c=fb.view().color[i];pixels[i]=(static_cast<std::uint32_t>(detail::saturate(c.r)*255)<<16)|
        (static_cast<std::uint32_t>(detail::saturate(c.g)*255)<<8)|static_cast<std::uint32_t>(detail::saturate(c.b)*255);}
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=fb.width();info.bmiHeader.biHeight=-fb.height();
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    RECT rect;GetClientRect(impl_->window,&rect);HDC dc=GetDC(impl_->window);if(!dc)return {false,"GetDC failed"};
    int written=StretchDIBits(dc,0,0,rect.right,rect.bottom,0,0,fb.width(),fb.height(),pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    ReleaseDC(impl_->window,dc);return written==static_cast<int>(GDI_ERROR)?Result{false,"StretchDIBits failed"}:Result{};
}
} // namespace pbc
