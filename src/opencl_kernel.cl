// PBC-gAPI — property and copyright authority: Pablo B. C.
// OpenCL C 1.2 computation only: no graphics rasterizer, GL/Vulkan sharing,
// hardware samplers or image-format assumptions. Host code appends pbcFragment.
#pragma OPENCL FP_CONTRACT OFF

// Every member is 16 bytes/aligned to 16; host uses cl_int4/cl_float4.
// Size: 176 bytes. Do not transfer the host's PreparedTriangle directly.
typedef struct {
    int4 bounds;       // min_x, min_y, max_x, max_y (inclusive)
    int4 xy01;         // fixed-point x0,y0,x1,y1, in units of 1/16 pixel
    int4 xy2_pad;      // fixed-point x2,y2,0,0
    float4 perspective; // inv_w0, inv_w1, inv_w2,0
    float4 ndc_depth;  // ndc_z0, ndc_z1, ndc_z2,0
    float4 varying[6]; // two float4 vectors per vertex, eight channels total
} PbcTriangle;

typedef struct {
    float varying[8]; // normal xyz, view position xyz, normalized UV xy
    float depth;
    int x, y;
} PbcFragmentInput;

typedef struct {
    __global const float4* texture_pixels;
    __global const int4* texture_meta; // pixel offset,width,height,unused
    uint texture_count, target_count;
    int filter;  // 0 nearest,1 bilinear,2 Catmull-Rom bicubic
    int address; // 0 clamp,1 repeat
} PbcResources;

typedef struct {
    float4 colors[4]; // RGBA, targets in submission order
    uint mask;
    uint discard;
} PbcFragmentOutput;

inline PbcFragmentOutput pbcOutput(float4 color) {
    PbcFragmentOutput out;
    for (int i = 0; i < 4; ++i) out.colors[i] = (float4)(0,0,0,1);
    out.colors[0] = color;
    out.mask = 1;
    out.discard = 0;
    return out;
}

inline int pbcAddress(int coordinate, int size, int address) {
    if (address == 1) {
        int wrapped = coordinate % size;
        return wrapped < 0 ? wrapped + size : wrapped;
    }
    return clamp(coordinate, 0, size - 1);
}

inline float4 pbcTexel(PbcResources resources, int slot, int x, int y) {
    if (slot < 0 || (uint)slot >= resources.texture_count) return (float4)(0);
    int4 image = resources.texture_meta[slot];
    if (image.y <= 0 || image.z <= 0) return (float4)(0);
    int column = pbcAddress(x, image.y, resources.address);
    int row = pbcAddress(y, image.z, resources.address);
    // Metadata offsets are host-checked to fit INT_MAX. Pixel arithmetic is unsigned.
    size_t index = (size_t)image.x + (size_t)row * (size_t)image.y + (size_t)column;
    return resources.texture_pixels[index];
}

inline float4 pbcMix(float4 a, float4 b, float t) {
    return a + (b-a)*t;
}

inline float4 pbcCubic(float4 a, float4 b, float4 c, float4 d, float t) {
    return b + 0.5f*t*(c-a+t*(2.0f*a-5.0f*b+4.0f*c-d+t*(3.0f*(b-c)+d-a)));
}

inline float4 pbcSample(PbcResources resources, int slot, float u, float v) {
    if (slot < 0 || (uint)slot >= resources.texture_count || !isfinite(u) || !isfinite(v))
        return (float4)(0);
    int4 image = resources.texture_meta[slot];
    if (image.y <= 0 || image.z <= 0) return (float4)(0);
    if (resources.address == 1) {
        u -= floor(u);
        v -= floor(v);
    } else {
        u = clamp(u, 0.0f, 1.0f);
        v = clamp(v, 0.0f, 1.0f);
    }
    if (resources.filter == 0)
        return pbcTexel(resources,slot,convert_int_rtn(u*image.y),convert_int_rtn(v*image.z));
    float x = u*image.y-0.5f, y = v*image.z-0.5f;
    int ix = convert_int_rtn(x), iy = convert_int_rtn(y);
    float tx = x-ix, ty = y-iy;
    if (resources.filter == 1)
        return pbcMix(pbcMix(pbcTexel(resources,slot,ix,iy),pbcTexel(resources,slot,ix+1,iy),tx),
                      pbcMix(pbcTexel(resources,slot,ix,iy+1),pbcTexel(resources,slot,ix+1,iy+1),tx),ty);
    float4 rows[4];
    for (int j = 0; j < 4; ++j)
        rows[j] = pbcCubic(pbcTexel(resources,slot,ix-1,iy+j-1),pbcTexel(resources,slot,ix,iy+j-1),
                           pbcTexel(resources,slot,ix+1,iy+j-1),pbcTexel(resources,slot,ix+2,iy+j-1),tx);
    return pbcCubic(rows[0],rows[1],rows[2],rows[3],ty);
}

inline PbcFragmentOutput pbcTextured(PbcFragmentInput in, PbcResources resources) {
    return pbcOutput(resources.texture_count ? pbcSample(resources,0,in.varying[6],in.varying[7]) : (float4)(1));
}

inline PbcFragmentOutput pbcPhong(PbcFragmentInput in, PbcResources resources) {
    PbcFragmentOutput out = pbcTextured(in,resources);
    float nx=in.varying[0], ny=in.varying[1], nz=in.varying[2];
    float normal_length=sqrt(nx*nx+ny*ny+nz*nz), diffuse=0, specular=0;
    if (normal_length > 1e-12f) {
        nx/=normal_length; ny/=normal_length; nz/=normal_length;
        diffuse=fmax(0.0f,0.2f*nx-0.4f*ny+0.8944272f*nz);
        float vx=-in.varying[3], vy=-in.varying[4], vz=-in.varying[5];
        float eye_length=sqrt(vx*vx+vy*vy+vz*vz);
        if (diffuse > 0 && eye_length > 1e-12f) {
            float reflected=((2.0f*diffuse*nx-0.2f)*vx+(2.0f*diffuse*ny+0.4f)*vy+
                             (2.0f*diffuse*nz-0.8944272f)*vz)/eye_length;
            specular=0.15f*pow(fmax(0.0f,reflected),32.0f);
        }
    }
    out.colors[0].xyz=out.colors[0].xyz*(0.25f+0.75f*diffuse)+(float3)(specular);
    return out;
}

// User/builtin definition is appended to this translation unit before clBuildProgram.
PbcFragmentOutput pbcFragment(PbcFragmentInput in, PbcResources resources, __global const uchar* uniforms);

inline long pbcEdge(int2 a, int2 b, int2 pixel) {
    return (long)(b.x-a.x)*(long)(pixel.y-a.y)-(long)(b.y-a.y)*(long)(pixel.x-a.x);
}

inline int pbcTopLeft(int2 a, int2 b) {
    return b.y<a.y || (b.y==a.y && b.x>a.x);
}

inline int pbcInterpolate(PbcTriangle triangle, int x, int y, PbcFragmentInput* in) {
    int2 v0=triangle.xy01.xy, v1=triangle.xy01.zw, v2=triangle.xy2_pad.xy;
    int2 pixel=(int2)(x*16+8,y*16+8);
    long e0=pbcEdge(v1,v2,pixel), e1=pbcEdge(v2,v0,pixel), e2=pbcEdge(v0,v1,pixel);
    if (e0<0 || (e0==0 && !pbcTopLeft(v1,v2)) ||
        e1<0 || (e1==0 && !pbcTopLeft(v2,v0)) ||
        e2<0 || (e2==0 && !pbcTopLeft(v0,v1))) return 0;
    long area=pbcEdge(v0,v1,v2);
    if (area<=0) return 0;
    float b[3]={convert_float(e0)/convert_float(area),convert_float(e1)/convert_float(area),convert_float(e2)/convert_float(area)};
    in->depth=(b[0]*triangle.ndc_depth.x+b[1]*triangle.ndc_depth.y+b[2]*triangle.ndc_depth.z)*0.5f+0.5f;
    if (!(in->depth>=0 && in->depth<=1)) return 0;
    float reciprocal[3]={triangle.perspective.x,triangle.perspective.y,triangle.perspective.z};
    float denominator=b[0]*reciprocal[0]+b[1]*reciprocal[1]+b[2]*reciprocal[2];
    if (!(denominator>0)) return 0;
    for (int k=0;k<8;++k) {
        in->varying[k]=0;
        for (int vertex=0;vertex<3;++vertex)
            in->varying[k]+=b[vertex]*reciprocal[vertex]*triangle.varying[vertex*2+k/4][k%4]/denominator;
    }
    in->x=x; in->y=y;
    return 1;
}

// One group owns one tile, and each work-item owns disjoint pixels of that tile.
// Each pixel processes candidates in submission order: strict LESS keeps the
// first primitive on depth ties, and color/depth updates need no atomics.
__kernel void pbcRaster(
    __global const PbcTriangle* triangles,
    uint triangle_count,
    __global const ulong* offsets,
    __global const uint* indices,
    uint use_bins,
    __global float4* colors,
    __global float* depths,
    uint width,
    uint height,
    __global const float4* texture_pixels,
    __global const int4* texture_meta,
    uint texture_count,
    uint target_count,
    int filter,
    int address,
    uint tile_size,
    uint columns,
    __global const uchar* uniforms) {
    size_t tile=get_group_id(0), local_index=get_local_id(0), workers=get_local_size(0);
    size_t left=(tile%(size_t)columns)*(size_t)tile_size;
    size_t top=(tile/(size_t)columns)*(size_t)tile_size;
    size_t begin=use_bins ? (size_t)offsets[tile] : 0;
    size_t end=use_bins ? (size_t)offsets[tile+1] : (size_t)triangle_count;
    PbcResources resources={texture_pixels,texture_meta,texture_count,target_count,filter,address};
    size_t pixel_count=(size_t)width*(size_t)height;
    for (size_t item=local_index;item<(size_t)tile_size*(size_t)tile_size;item+=workers) {
        size_t x=left+item%(size_t)tile_size, y=top+item/(size_t)tile_size;
        if (x>=width || y>=height) continue;
        size_t pixel=y*(size_t)width+x;
        float closest=depths[pixel];
        for (size_t candidate=begin;candidate<end;++candidate) {
            uint index=use_bins ? indices[candidate] : (uint)candidate;
            PbcTriangle triangle=triangles[index];
            if ((int)x<triangle.bounds.x || (int)y<triangle.bounds.y ||
                (int)x>triangle.bounds.z || (int)y>triangle.bounds.w) continue;
            PbcFragmentInput in;
            if (!pbcInterpolate(triangle,(int)x,(int)y,&in) || !(in.depth<closest)) continue;
            PbcFragmentOutput out=pbcFragment(in,resources,uniforms);
            if (out.discard) continue;
            closest=in.depth;
            for (uint target=0;target<target_count;++target)
                if (out.mask&(1u<<target)) colors[(size_t)target*pixel_count+pixel]=out.colors[target];
        }
        depths[pixel]=closest;
    }
}
