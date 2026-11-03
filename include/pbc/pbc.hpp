// PBC-gAPI — property and copyright authority: Pablo B. C.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <cfloat>

#ifdef __CUDACC__
#define PBC_HD __host__ __device__
#else
#define PBC_HD
#endif

namespace pbc {
struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Vec4 { float x = 0, y = 0, z = 0, w = 1; };
struct Color { float r = 0, g = 0, b = 0, a = 1; };
enum class Filter { Nearest, Bilinear, Bicubic };
enum class AddressMode { Clamp, Repeat };
struct Sampler { Filter filter = Filter::Nearest; AddressMode address = AddressMode::Clamp; };

// Top-left image origin. UV (0,0)..(1,1) covers the whole image; texel centers
// are ((x+.5)/width, (y+.5)/height). texel() uses integer image coordinates.
struct ImageView {
    const Color* pixels = nullptr;
    int width = 0, height = 0;
    PBC_HD Color texel(int x, int y, AddressMode mode = AddressMode::Clamp) const;
    PBC_HD Color sample(float u, float v, Sampler sampler = {}) const;
};
struct FrameBufferView {
    Color* color = nullptr;
    float* depth = nullptr;
    int width = 0, height = 0;
    PBC_HD ImageView image() const { return {color, width, height}; }
};

using TextureGenerator = Color (*)(float u, float v, int x, int y, void* user);
class FrameBuffer {
public:
    FrameBuffer(int width, int height);
    // External storage permits ESP internal RAM/PSRAM and caller-controlled lifetime.
    FrameBuffer(int width, int height, Color* colors, float* depths, std::size_t count);
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;
    FrameBuffer(FrameBuffer&&) = delete;
    FrameBuffer& operator=(FrameBuffer&&) = delete;
    int width() const { return view_.width; }
    int height() const { return view_.height; }
    FrameBufferView view() const { return view_; }
    bool valid() const;
    void clear(Color color = {}, float depth = 1.0f);
    // Generated texture is distinct from render storage, so it can be sampled safely.
    bool generateTexture(int width, int height, TextureGenerator generator, void* user = nullptr);
    ImageView texture() const;
    Color getTexturePixel(float u, float v, Sampler sampler = {}) const;
    Color getTexel(int x, int y, AddressMode mode = AddressMode::Clamp) const;
private:
    std::vector<Color> owned_color_, texture_;
    std::vector<float> owned_depth_;
    FrameBufferView view_;
    int texture_width_ = 0, texture_height_ = 0;
};
using Framebuffer = FrameBuffer;

constexpr unsigned MaxTargets = 4, MaxTextures = 8, VaryingCount = 8;
struct Vertex { Vec3 position; Vec3 normal{0,0,1}; Vec2 uv; };
struct VertexOutput { Vec4 clip; float varying[VaryingCount]{}; };
struct FragmentInput {
    float varying[VaryingCount]{}; // normal xyz, view position xyz, UV xy
    float depth = 1;
    int x = 0, y = 0;
};
struct ShaderResources {
    ImageView textures[MaxTextures]{};
    unsigned texture_count = 0, target_count = 0;
    Sampler sampler;
};
struct FragmentOutput {
    Color colors[MaxTargets]{};
    unsigned mask = 1;
    bool discard = false;
};
using VertexShader = VertexOutput (*)(const Vertex&, const void* uniforms);
using FragmentShader = FragmentOutput (*)(const FragmentInput&, const ShaderResources&, const void* uniforms);
struct ShaderProgram {
    VertexShader vertex = nullptr;
    FragmentShader fragment = nullptr;
    const void* uniforms = nullptr;
};
struct Mesh { std::vector<Vertex> vertices; std::vector<std::uint32_t> indices; };
struct Geometry {
    const Vertex* vertices = nullptr;
    std::size_t vertex_count = 0;
    const std::uint32_t* indices = nullptr;
    std::size_t index_count = 0;
};
inline Geometry geometry(const Mesh& mesh) {
    return {mesh.vertices.data(), mesh.vertices.size(), mesh.indices.data(), mesh.indices.size()};
}
struct RenderPass {
    FrameBuffer* targets[MaxTargets]{};
    unsigned target_count = 0;
    ShaderResources resources;
    ShaderProgram shaders;
};
enum class CullMode { None, Back, Front };
struct RenderOptions {
    unsigned threads = 1; // strict >=1; never silently enables all available cores
    unsigned tile_size = 16;
    bool bin_triangles = true; // disable to save bin memory on ESP32
    CullMode cull = CullMode::None; // CCW in clip/NDC is front-facing
};
struct Result {
    bool ok = true;
    std::string message;
    explicit operator bool() const { return ok; }
};
struct RenderStats {
    std::size_t input_triangles = 0, clipped_triangles = 0, tiles = 0;
    unsigned worker_threads = 0;
};
// Synchronous. Targets have equal dimensions and use target[0]'s depth test.
// All output depths are updated together. Sampling an output is rejected: ping-pong instead.
class CpuRenderer {
public:
    Result render(Geometry mesh, const RenderPass&, RenderOptions = {}, RenderStats* = nullptr) const;
    static unsigned availableThreads();
};

// Column-major MVP. Normal matrix is a separate inverse-transpose 3x3.
struct TransformUniforms {
    float mvp[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float model_view[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float normal[9]{1,0,0, 0,1,0, 0,0,1};
};
VertexOutput transformVertex(const Vertex&, const void* uniforms);
FragmentOutput texturedFragment(const FragmentInput&, const ShaderResources&, const void*);
FragmentOutput phongFragment(const FragmentInput&, const ShaderResources&, const void*);

// Transactional OBJ loader: v/vt/vn, v//vn, negative indices and convex polygon fans.
// Missing UV stays (0,0); malformed/out-of-range indices return a line-numbered error.
struct ObjOptions { bool flip_v = true; }; // OBJ lower-left -> image top-left
Result loadObj(const std::string& text, Mesh& destination, ObjOptions = {});
Result loadObjFile(const char* path, Mesh& destination, ObjOptions = {});
} // namespace pbc

#include "detail/sampling.hpp"
