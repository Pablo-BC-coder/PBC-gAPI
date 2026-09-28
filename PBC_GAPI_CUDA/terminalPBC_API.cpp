#pragma once

// === PROTEÇÃO CONTRA O VISUAL STUDIO (WIN32) ===
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#include <iostream>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

// === DIRETIVAS CUDA ===
#ifdef __CUDACC__
#define __HD__ __host__ __device__
#define PBC_MAX fmaxf
#define PBC_MIN fminf
#define PBC_SQRT sqrtf
#define PBC_POW powf
#else
#define __HD__ 
#define PBC_MAX (std::max)
#define PBC_MIN (std::min)
#define PBC_SQRT std::sqrt
#define PBC_POW std::pow
#endif

// Estruturas Matemáticas Básicas
struct Color { float a, r, g, b; };
struct Vec3 { float x, y, z; };
struct Vec2 { float u, v; };
struct Quad { int v[4]; Vec3 normal; };

// Struct para substituir o std::array (Garante compatibilidade total em VRAM)
// Substitua a struct VaryData (remova o __HD__):
struct VaryData { float v[8]; };

// O Framebuffer com a proteção de macro corrigida:
class Framebuffer {
public:
    int width, height;
    Color* color_buffer;
    float* depth_buffer;

    __HD__ Framebuffer(int w, int h, Color* c_buf, float* d_buf)
        : width(w), height(h), color_buffer(c_buf), depth_buffer(d_buf) {}

    __HD__ void setPixel(int x, int y, const Color& c) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            color_buffer[y * width + x] = c;
        }
    }

    __HD__ Color getTexturePixel(float u, float v) const {
        return { 1.0f, 0.8f, 0.8f, 0.8f };
    }

    // A MÁGICA ESTÁ AQUI: Apenas #ifdef _WIN32, permitindo que a CPU a veja mesmo no CUDA!
#ifdef _WIN32
    void displayHDC(HDC hdc) const {
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        std::vector<uint32_t> pixels(width * height);
        for (int i = 0; i < width * height; ++i) {
            const Color& c = color_buffer[i];
            uint32_t r = static_cast<uint32_t>(PBC_MAX(0.0f, PBC_MIN(1.0f, c.r)) * 255.0f);
            uint32_t g = static_cast<uint32_t>(PBC_MAX(0.0f, PBC_MIN(1.0f, c.g)) * 255.0f);
            uint32_t b = static_cast<uint32_t>(PBC_MAX(0.0f, PBC_MIN(1.0f, c.b)) * 255.0f);
            pixels[i] = (r << 16) | (g << 8) | b;
        }
        SetDIBitsToDevice(hdc, 0, 0, width, height, 0, 0, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);
    }
#endif
};

// Shader Phong Adaptado (Funciona na CPU e na GPU)
inline __HD__ Color SimplePhongTexture2(const VaryData& vary, const Framebuffer& fb) {
    float n_norm[3] = { vary.v[0], vary.v[1], vary.v[2] };
    float n_mod = PBC_SQRT(n_norm[0] * n_norm[0] + n_norm[1] * n_norm[1] + n_norm[2] * n_norm[2]);
    n_norm[0] /= n_mod; n_norm[1] /= n_mod; n_norm[2] /= n_mod;

    const float light[3] = { 0.05555556f, -0.2222222f, 0.05555556f };

    float n_pos[3] = { vary.v[3], vary.v[4], vary.v[5] };
    float mod_pos = PBC_SQRT(n_pos[0] * n_pos[0] + n_pos[1] * n_pos[1] + n_pos[2] * n_pos[2]);
    n_pos[0] /= mod_pos; n_pos[1] /= mod_pos; n_pos[2] /= mod_pos;

    n_pos[0] += light[0]; n_pos[1] += light[1]; n_pos[2] += light[2];

    mod_pos = PBC_SQRT(n_pos[0] * n_pos[0] + n_pos[1] * n_pos[1] + n_pos[2] * n_pos[2]);
    n_pos[0] /= mod_pos; n_pos[1] /= mod_pos; n_pos[2] /= mod_pos;

    float dot_phong = n_pos[0] * n_norm[0] + n_pos[1] * n_norm[1] + n_pos[2] * n_norm[2];
    float phong = PBC_POW(PBC_MAX(dot_phong, 0.0f), 32.0f);

    float dot_lum = light[0] * n_norm[0] + light[1] * n_norm[1] + light[2] * n_norm[2];
    float lum = PBC_MAX(dot_lum, 0.0f) + 0.25f;

    Color tex_color = fb.getTexturePixel(vary.v[6] * 32.0f, vary.v[7] * 32.0f);

    return {
        tex_color.a + phong,
        phong + lum * tex_color.r,
        phong + lum * tex_color.g,
        phong + lum * tex_color.b
    };
}

// Rasterizador de Triângulos Adaptado
template <typename FragmentShader>
__HD__ void ShaderTri(float x, float y, float z, float x1, float y1, float z1, float x2, float y2, float z2,
    const VaryData& vary1, const VaryData& vary2, const VaryData& vary3,
    Framebuffer& fb, float iz, float iz1, float iz2, FragmentShader shader)
{
    int imin = static_cast<int>(PBC_MAX(0.0f, PBC_MIN(y, PBC_MIN(y1, y2))));
    int imax = static_cast<int>(PBC_MIN((float)fb.height - 1, PBC_MAX(y, PBC_MAX(y1, y2))));
    int jmin = static_cast<int>(PBC_MAX(0.0f, PBC_MIN(x, PBC_MIN(x1, x2))));
    int jmax = static_cast<int>(PBC_MIN((float)fb.width - 1, PBC_MAX(x, PBC_MAX(x1, x2))));

    float area_tri = 0.5f * std::abs(x * (y1 - y2) + x1 * (y2 - y) + x2 * (y - y1));
    if (area_tri == 0.0f) return;

    int fixed_x = static_cast<int>(x * 16.0f); int fixed_y = static_cast<int>(y * 16.0f);
    int fixed_x1 = static_cast<int>(x1 * 16.0f); int fixed_y1 = static_cast<int>(y1 * 16.0f);
    int fixed_x2 = static_cast<int>(x2 * 16.0f); int fixed_y2 = static_cast<int>(y2 * 16.0f);

    int delta_xa = fixed_x2 - fixed_x1; int delta_ya = fixed_y2 - fixed_y1;
    int delta_xb = fixed_x - fixed_x2; int delta_yb = fixed_y - fixed_y2;
    int delta_xc = fixed_x1 - fixed_x; int delta_yc = fixed_y1 - fixed_y;

    for (int i = imin; i <= imax; ++i) {
        int fixed_i = i << 4; int fixed_j_start = jmin << 4;
        int edge_a = ((fixed_j_start - fixed_x1) * delta_ya >> 4) - ((fixed_i - fixed_y1) * delta_xa >> 4) - delta_ya;
        int edge_b = ((fixed_j_start - fixed_x2) * delta_yb >> 4) - ((fixed_i - fixed_y2) * delta_xb >> 4) - delta_yb;
        int edge_c = ((fixed_j_start - fixed_x) * delta_yc >> 4) - ((fixed_i - fixed_y) * delta_xc >> 4) - delta_yc;

        for (int j = jmin; j <= jmax; ++j) {
            edge_a += delta_ya; edge_b += delta_yb; edge_c += delta_yc;

            bool top_left_edge_a = (edge_a == 0 && (delta_ya < 0 || (delta_ya == 0 && delta_xa > 0)));
            bool top_left_edge_b = (edge_b == 0 && (delta_yb < 0 || (delta_yb == 0 && delta_xb > 0)));
            bool top_left_edge_c = (edge_c == 0 && (delta_yc < 0 || (delta_yc == 0 && delta_xc > 0)));
            bool insideA = edge_a < 0 || (edge_a == 0 && top_left_edge_a);
            bool insideB = edge_b < 0 || (edge_b == 0 && top_left_edge_b);
            bool insideC = edge_c < 0 || (edge_c == 0 && top_left_edge_c);

            if (insideA && insideB && insideC) {

                float a = 0.5f * std::abs(j * (y1 - y2) + x1 * (y2 - i) + x2 * (i - y1)) / area_tri;
                float b = 0.5f * std::abs(x * (i - y2) + j * (y2 - y) + x2 * (y - i)) / area_tri;
                float c = 0.5f * std::abs(x * (y1 - i) + x1 * (i - y) + j * (y - y1)) / area_tri;

                float z_pixel = 1.0f / (a * iz + b * iz1 + c * iz2);
                int fb_index = i * fb.width + j;

                if (fb.depth_buffer[fb_index] != 0.0f && z_pixel > fb.depth_buffer[fb_index]) continue;

                fb.depth_buffer[fb_index] = z_pixel;

                float dp_a = z_pixel * a * iz; float dp_b = z_pixel * b * iz1; float dp_c = z_pixel * c * iz2;

                VaryData vary_in;
                for (size_t v = 0; v < 8; ++v) {
                    vary_in.v[v] = (vary1.v[v] * dp_a) + (vary2.v[v] * dp_b) + (vary3.v[v] * dp_c);
                }

                Color col = shader(vary_in, fb);
                fb.setPixel(j, i, col);
            }
        }
    }
}