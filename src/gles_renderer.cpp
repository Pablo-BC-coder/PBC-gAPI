// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/gpu.hpp"
#include "pbc/detail/pipeline.hpp"
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <cstring>

namespace pbc {
namespace {
const char* vertexSource=R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec4 aClip;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aPosition;
layout(location=3) in vec2 aUV;
out vec3 vNormal;out vec3 vPosition;out vec2 vUV;
void main(){gl_Position=aClip;vNormal=aNormal;vPosition=aPosition;vUV=aUV;}
)GLSL";
const char* depthVertex=R"GLSL(#version 300 es
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}
)GLSL";
const char* depthFragment=R"GLSL(#version 300 es
precision highp float;precision highp int;
uniform highp sampler2D uDepth;
layout(location=0) out vec4 result;
void main(){uint z=uint(round(texelFetch(uDepth,ivec2(gl_FragCoord.xy),0).r*16777215.0));
result=vec4(float(z&255u),float((z>>8)&255u),float((z>>16)&255u),255.0)/255.0;}
)GLSL";
GLuint compile(GLenum stage,const char* source,std::string& error) {
    GLuint shader=glCreateShader(stage);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){GLint n=0;glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&n);std::vector<char> log(std::max(1,n));
        glGetShaderInfoLog(shader,n,nullptr,log.data());error=log.data();glDeleteShader(shader);return 0;}
    return shader;
}
GLuint link(const char* vertex,const char* fragment,std::string& error) {
    GLuint vs=compile(GL_VERTEX_SHADER,vertex,error);if(!vs)return 0;
    GLuint fs=compile(GL_FRAGMENT_SHADER,fragment,error);if(!fs){glDeleteShader(vs);return 0;}
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){GLint n=0;glGetProgramiv(program,GL_INFO_LOG_LENGTH,&n);std::vector<char> log(std::max(1,n));
        glGetProgramInfoLog(program,n,nullptr,log.data());error=log.data();glDeleteProgram(program);return 0;}
    return program;
}
// The renderer owns temporary objects and restores caller context state. No hidden window.
struct State {
    GLint draw_fbo,read_fbo,viewport[4],program,vao,array_buffer,active_texture,pack,unpack,depth_func;
    GLint pack_buffer,unpack_buffer,pack_row,unpack_row,pack_skip_rows,pack_skip_pixels,unpack_skip_rows,unpack_skip_pixels;
    GLboolean depth_mask,color_mask[4];GLfloat depth_range[2];
    static constexpr unsigned CapCount=10;
    GLenum caps[CapCount]={GL_DEPTH_TEST,GL_BLEND,GL_CULL_FACE,GL_SCISSOR_TEST,GL_DITHER,GL_RASTERIZER_DISCARD,
        GL_STENCIL_TEST,GL_POLYGON_OFFSET_FILL,GL_SAMPLE_COVERAGE,GL_SAMPLE_ALPHA_TO_COVERAGE};
    GLboolean enabled[CapCount];GLint textures[MaxTextures],samplers[MaxTextures];
    State() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_fbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fbo);
        glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array_buffer);glGetIntegerv(GL_ACTIVE_TEXTURE,&active_texture);
        glGetIntegerv(GL_PACK_ALIGNMENT,&pack);glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);glGetIntegerv(GL_DEPTH_FUNC,&depth_func);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);glGetBooleanv(GL_COLOR_WRITEMASK,color_mask);
        glGetFloatv(GL_DEPTH_RANGE,depth_range);glDepthRangef(0,1);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack_buffer);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack_buffer);
        glGetIntegerv(GL_PACK_ROW_LENGTH,&pack_row);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&unpack_row);
        glGetIntegerv(GL_PACK_SKIP_ROWS,&pack_skip_rows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&pack_skip_pixels);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&unpack_skip_rows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&unpack_skip_pixels);
        for(unsigned i=0;i<CapCount;++i){enabled[i]=glIsEnabled(caps[i]);glDisable(caps[i]);}
        for(unsigned i=0;i<MaxTextures;++i){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);
            glGetIntegerv(GL_SAMPLER_BINDING,&samplers[i]);glBindSampler(i,0);}
        glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
    }
    ~State() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw_fbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glUseProgram(program);glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER,array_buffer);glDepthFunc(depth_func);glDepthMask(depth_mask);
        glColorMask(color_mask[0],color_mask[1],color_mask[2],color_mask[3]);
        glDepthRangef(depth_range[0],depth_range[1]);
        glPixelStorei(GL_PACK_ALIGNMENT,pack);glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);
        glPixelStorei(GL_PACK_ROW_LENGTH,pack_row);glPixelStorei(GL_UNPACK_ROW_LENGTH,unpack_row);
        glPixelStorei(GL_PACK_SKIP_ROWS,pack_skip_rows);glPixelStorei(GL_PACK_SKIP_PIXELS,pack_skip_pixels);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,unpack_skip_rows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,unpack_skip_pixels);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,pack_buffer);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack_buffer);
        for(unsigned i=0;i<CapCount;++i)if(enabled[i])glEnable(caps[i]);else glDisable(caps[i]);
        for(unsigned i=0;i<MaxTextures;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);glBindSampler(i,samplers[i]);}
        glActiveTexture(active_texture);
    }
};
struct Objects {
    std::vector<GLuint> textures,programs;GLuint fbo=0,depth_fbo=0,vao=0,vbo=0;
    ~Objects(){for(auto p:programs)glDeleteProgram(p);if(!textures.empty())glDeleteTextures(static_cast<GLsizei>(textures.size()),textures.data());
        glDeleteFramebuffers(1,&fbo);glDeleteFramebuffers(1,&depth_fbo);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);}
    GLuint texture(){GLuint t;glGenTextures(1,&t);textures.push_back(t);glBindTexture(GL_TEXTURE_2D,t);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);return t;}
};
std::vector<unsigned char> rgba(ImageView image,bool flip) {
    std::vector<unsigned char> bytes(static_cast<std::size_t>(image.width)*image.height*4);
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
        auto c=image.pixels[static_cast<std::size_t>(flip?image.height-1-y:y)*image.width+x];
        auto i=(static_cast<std::size_t>(y)*image.width+x)*4;
        bytes[i]=static_cast<unsigned char>(std::lround(detail::saturate(c.r)*255));
        bytes[i+1]=static_cast<unsigned char>(std::lround(detail::saturate(c.g)*255));
        bytes[i+2]=static_cast<unsigned char>(std::lround(detail::saturate(c.b)*255));
        bytes[i+3]=static_cast<unsigned char>(std::lround(detail::saturate(c.a)*255));
    }
    return bytes;
}
}
std::string GlesRenderer::fragmentPrelude() {
    return R"GLSL(#version 300 es
precision highp float;precision highp int;
in vec3 vNormal;in vec3 vPosition;in vec2 vUV;
uniform sampler2D uTextures[8];uniform int uTextureCount;uniform int uFilter;uniform int uAddress;
ivec2 pbcSize(int slot){
 switch(slot){case 0:return textureSize(uTextures[0],0);case 1:return textureSize(uTextures[1],0);
 case 2:return textureSize(uTextures[2],0);case 3:return textureSize(uTextures[3],0);
 case 4:return textureSize(uTextures[4],0);case 5:return textureSize(uTextures[5],0);
 case 6:return textureSize(uTextures[6],0);default:return textureSize(uTextures[7],0);}}
vec4 pbcTexel(int slot,ivec2 p){
 ivec2 s=pbcSize(slot);p=uAddress==1?((p%s)+s)%s:clamp(p,ivec2(0),s-1);
 switch(slot){case 0:return texelFetch(uTextures[0],p,0);case 1:return texelFetch(uTextures[1],p,0);
 case 2:return texelFetch(uTextures[2],p,0);case 3:return texelFetch(uTextures[3],p,0);
 case 4:return texelFetch(uTextures[4],p,0);case 5:return texelFetch(uTextures[5],p,0);
 case 6:return texelFetch(uTextures[6],p,0);default:return texelFetch(uTextures[7],p,0);}}
vec4 pbcCubic(vec4 a,vec4 b,vec4 c,vec4 d,float t){return b+.5*t*(c-a+t*(2.0*a-5.0*b+4.0*c-d+t*(3.0*(b-c)+d-a)));}
vec4 pbcSample(int slot,vec2 uv){
 if(slot<0||slot>=uTextureCount)return vec4(1);
 uv=uAddress==1?fract(uv):clamp(uv,0.0,1.0);
 vec2 size=vec2(pbcSize(slot));if(uFilter==0)return pbcTexel(slot,ivec2(floor(uv*size)));
 vec2 p=uv*size-.5;ivec2 i=ivec2(floor(p));vec2 f=fract(p);
 if(uFilter==1)return mix(mix(pbcTexel(slot,i),pbcTexel(slot,i+ivec2(1,0)),f.x),
 mix(pbcTexel(slot,i+ivec2(0,1)),pbcTexel(slot,i+ivec2(1,1)),f.x),f.y);
 vec4 r[4];for(int j=0;j<4;++j)r[j]=pbcCubic(pbcTexel(slot,i+ivec2(-1,j-1)),pbcTexel(slot,i+ivec2(0,j-1)),
 pbcTexel(slot,i+ivec2(1,j-1)),pbcTexel(slot,i+ivec2(2,j-1)),f.x);
 return pbcCubic(r[0],r[1],r[2],r[3],f.y);}
)GLSL";
}
Result GlesRenderer::deviceInfo(GpuInfo& info) {
    auto version=glGetString(GL_VERSION);if(!version)return {false,"An OpenGL ES 3.0 context must be current"};
    GLint major=0;glGetIntegerv(GL_MAJOR_VERSION,&major);if(major<3)return {false,"OpenGL ES 3.0 is required"};
    info={};info.backend="OpenGL ES";info.name=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    info.vendor=reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    return {}; // ES deliberately has no physical-core/SM query.
}
Result GlesRenderer::render(Geometry mesh,const RenderPass& pass,GlesProgram shader,RenderOptions options) const {
    auto result=detail::validatePass(pass,false);if(!result)return result;
    GpuInfo info;result=deviceInfo(info);if(!result)return result;
    GLint max_targets=0,max_texture=0;glGetIntegerv(GL_MAX_DRAW_BUFFERS,&max_targets);glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_texture);
    if(pass.target_count>static_cast<unsigned>(max_targets))return {false,"Too many color outputs for this GPU"};
    int w=pass.targets[0]->width(),h=pass.targets[0]->height();std::size_t n=static_cast<std::size_t>(w)*h;
    if(w>max_texture||h>max_texture)return {false,"Framebuffer exceeds GL_MAX_TEXTURE_SIZE"};
    for(unsigned i=0;i<pass.resources.texture_count;++i)if(pass.resources.textures[i].width>max_texture||pass.resources.textures[i].height>max_texture)
        return {false,"Texture exceeds GL_MAX_TEXTURE_SIZE"};
    std::vector<detail::PreparedTriangle> triangles;
    result=detail::buildTriangles(mesh,pass.shaders,w,h,options.cull,triangles,nullptr);if(!result)return result;
    if(triangles.empty())return {};
    if(!shader.fragment_source&&pass.target_count!=1)return {false,"Multiple outputs require an explicit GLSL fragment program"};
    State saved;Objects objects;glActiveTexture(GL_TEXTURE0);
    // Ignore no preexisting GL errors: a dirty context is reported rather than masking it.
    if(glGetError()!=GL_NO_ERROR)return {false,"OpenGL context contains an error before rendering"};
    std::string error,source;
    if(shader.fragment_source)source=shader.fragment_source;
    else {
        source=fragmentPrelude()+"\nlayout(location=0) out vec4 outColor;\nvoid main(){outColor=pbcSample(0,vUV);";
        if(shader.builtin==BuiltinFragment::Phong)source+=
            "float l=length(vNormal);float d=0.0;float s=0.0;"
            "if(l>1e-12){vec3 n=vNormal/l;vec3 light=vec3(.2,-.4,.8944272);d=max(0.0,dot(n,light));"
            "float eye=length(vPosition);if(d>0.0&&eye>1e-12)s=.15*pow(max(0.0,dot(2.0*d*n-light,-vPosition/eye)),32.0);}"
            "outColor.rgb=outColor.rgb*(.25+.75*d)+s;";
        source+="}";
    }
    GLuint program=link(vertexSource,source.c_str(),error);if(!program)return {false,error};objects.programs.push_back(program);
    GLuint depth_program=link(depthVertex,depthFragment,error);if(!depth_program)return {false,error};objects.programs.push_back(depth_program);
    glGenFramebuffers(1,&objects.fbo);glBindFramebuffer(GL_FRAMEBUFFER,objects.fbo);
    GLenum attachments[MaxTargets];
    for(unsigned i=0;i<pass.target_count;++i) {
        GLuint texture=objects.texture();auto pixels=rgba(pass.targets[i]->view().image(),true);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        attachments[i]=GL_COLOR_ATTACHMENT0+i;glFramebufferTexture2D(GL_FRAMEBUFFER,attachments[i],GL_TEXTURE_2D,texture,0);
    }
    std::vector<std::uint32_t> depth(n);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        double z=detail::saturate(pass.targets[0]->view().depth[static_cast<std::size_t>(h-1-y)*w+x]);
        depth[static_cast<std::size_t>(y)*w+x]=static_cast<std::uint32_t>(z*4294967295.0);
    }
    GLuint depth_texture=objects.texture();glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,w,h,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,depth.data());
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth_texture,0);
    glDrawBuffers(pass.target_count,attachments);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return {false,"Incomplete GPU framebuffer"};
    std::vector<VertexOutput> vertices;vertices.reserve(triangles.size()*3);
    for(const auto& t:triangles)for(const auto& v:t.v) {
        float clip_w=1/v.inv_w;VertexOutput p;
        p.clip={(v.x/(16.0f*w)*2-1)*clip_w,(1-v.y/(16.0f*h)*2)*clip_w,v.ndc_z*clip_w,clip_w};
        std::copy_n(v.varying,VaryingCount,p.varying);vertices.push_back(p);
    }
    if(vertices.size()>static_cast<std::size_t>(INT32_MAX))return {false,"Too many GPU vertices"};
    glGenVertexArrays(1,&objects.vao);glBindVertexArray(objects.vao);glGenBuffers(1,&objects.vbo);glBindBuffer(GL_ARRAY_BUFFER,objects.vbo);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(VertexOutput),vertices.data(),GL_STREAM_DRAW);
    const int sizes[4]={4,3,3,2};const std::size_t offsets[4]={offsetof(VertexOutput,clip),offsetof(VertexOutput,varying),
        offsetof(VertexOutput,varying)+3*sizeof(float),offsetof(VertexOutput,varying)+6*sizeof(float)};
    for(unsigned i=0;i<4;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,sizeof(VertexOutput),reinterpret_cast<const void*>(offsets[i]));}
    glUseProgram(program);GLint slots[MaxTextures];
    for(unsigned i=0;i<MaxTextures;++i) {
        slots[i]=i;glActiveTexture(GL_TEXTURE0+i);objects.texture();
        if(i<pass.resources.texture_count) {
            auto image=pass.resources.textures[i];auto pixels=rgba(image,false);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,image.width,image.height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        } else {const unsigned char white[4]={255,255,255,255};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);}
    }
    glUniform1iv(glGetUniformLocation(program,"uTextures"),MaxTextures,slots);
    glUniform1i(glGetUniformLocation(program,"uTextureCount"),pass.resources.texture_count);
    glUniform1i(glGetUniformLocation(program,"uFilter"),static_cast<int>(pass.resources.sampler.filter));
    glUniform1i(glGetUniformLocation(program,"uAddress"),static_cast<int>(pass.resources.sampler.address));
    if(shader.bind)shader.bind(program,shader.user);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glViewport(0,0,w,h);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));
    std::vector<unsigned char> colors[MaxTargets];
    for(unsigned i=0;i<pass.target_count;++i){colors[i].resize(n*4);glReadBuffer(attachments[i]);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,colors[i].data());}
    // ES3 cannot read depth directly. Copy it through a shader into packed RGBA8.
    glActiveTexture(GL_TEXTURE0);glGenFramebuffers(1,&objects.depth_fbo);glBindFramebuffer(GL_FRAMEBUFFER,objects.depth_fbo);
    GLuint packed=objects.texture();glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,packed,0);
    GLenum attachment=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&attachment);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return {false,"Incomplete depth-readback framebuffer"};
    glBindTexture(GL_TEXTURE_2D,depth_texture);glUseProgram(depth_program);glUniform1i(glGetUniformLocation(depth_program,"uDepth"),0);
    glDisable(GL_DEPTH_TEST);glDrawArrays(GL_TRIANGLES,0,3);
    std::vector<unsigned char> packed_depth(n*4);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,packed_depth.data());
    GLenum gl_error=glGetError();if(gl_error!=GL_NO_ERROR)return {false,"OpenGL ES rendering failed: "+std::to_string(gl_error)};
    // Commit only after successful draw/readback. Color targets use RGBA8, depth uses 24 bits.
    for(unsigned k=0;k<pass.target_count;++k)for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        std::size_t i=static_cast<std::size_t>(h-1-y)*w+x,p=static_cast<std::size_t>(y)*w+x;auto* c=&colors[k][i*4];auto* d=&packed_depth[i*4];
        pass.targets[k]->view().color[p]={c[0]/255.0f,c[1]/255.0f,c[2]/255.0f,c[3]/255.0f};
        pass.targets[k]->view().depth[p]=(static_cast<unsigned>(d[0])|(static_cast<unsigned>(d[1])<<8)|(static_cast<unsigned>(d[2])<<16))/16777215.0f;
    }
    return {};
}
} // namespace pbc
