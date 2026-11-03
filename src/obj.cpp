// PBC-gAPI — property and copyright authority: Pablo B. C.
#include "pbc/pbc.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>

namespace pbc {
namespace {
struct Corner { int p=-1,t=-1,n=-1; };
bool index(const std::string& token,std::size_t count,int& out) {
    if(token.empty()||count>static_cast<std::size_t>(std::numeric_limits<int>::max()))return false;
    int value=0;
    const char* begin=token.data();if(*begin=='+')++begin;
    auto parsed=std::from_chars(begin,token.data()+token.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size()||value==0)return false;
    auto resolved=value>0 ? static_cast<long long>(value)-1 : static_cast<long long>(count)+value;
    if(resolved<0||resolved>=static_cast<long long>(count))return false;
    out=static_cast<int>(resolved);return true;
}
bool corner(const std::string& s,std::size_t p,std::size_t t,std::size_t n,Corner& out) {
    auto a=s.find('/');if(a==std::string::npos)return index(s,p,out.p);
    if(!index(s.substr(0,a),p,out.p))return false;
    auto b=s.find('/',a+1);
    if(b==std::string::npos)return index(s.substr(a+1),t,out.t);
    if(s.find('/',b+1)!=std::string::npos)return false;
    if(b>a+1&&!index(s.substr(a+1,b-a-1),t,out.t))return false;
    return index(s.substr(b+1),n,out.n);
}
Vec3 normalized(Vec3 n) {
    float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    return length>1e-12f ? Vec3{n.x/length,n.y/length,n.z/length}:Vec3{0,0,1};
}
float orient(Vec2 a,Vec2 b,Vec2 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool triangulate(const std::vector<Corner>& face,const std::vector<Vec3>& positions,
                 std::vector<std::uint32_t>& triangles,Vec3& normal) {
    Vec3 n{};
    for(std::size_t i=0;i<face.size();++i) {
        auto a=positions[face[i].p],b=positions[face[(i+1)%face.size()].p];
        n.x+=(a.y-b.y)*(a.z+b.z);n.y+=(a.z-b.z)*(a.x+b.x);n.z+=(a.x-b.x)*(a.y+b.y);
    }
    if(std::abs(n.x)+std::abs(n.y)+std::abs(n.z)<1e-12f)return false;
    normal=normalized(n);int axis=std::abs(n.x)>std::abs(n.y)?0:1;
    if(std::abs(n.z)>(axis==0?std::abs(n.x):std::abs(n.y)))axis=2;
    std::vector<Vec2> points;
    for(auto c:face) {auto v=positions[c.p];points.push_back(axis==0?Vec2{v.y,v.z}:axis==1?Vec2{v.x,v.z}:Vec2{v.x,v.y});}
    float area=0;for(std::size_t i=0;i<points.size();++i){auto a=points[i],b=points[(i+1)%points.size()];area+=a.x*b.y-a.y*b.x;}
    if(std::abs(area)<1e-12f)return false;
    float sign=area>0?1.0f:-1.0f;
    std::vector<std::uint32_t> remaining;
    for(std::size_t i=0;i<points.size();++i)remaining.push_back(static_cast<std::uint32_t>(i));
    while(remaining.size()>3) {
        bool found=false;
        for(std::size_t i=0;i<remaining.size();++i) {
            auto a=remaining[(i+remaining.size()-1)%remaining.size()],b=remaining[i],c=remaining[(i+1)%remaining.size()];
            if(orient(points[a],points[b],points[c])*sign<=1e-12f)continue;
            bool contains=false;
            for(auto p:remaining) if(p!=a&&p!=b&&p!=c &&
                orient(points[a],points[b],points[p])*sign>=0 && orient(points[b],points[c],points[p])*sign>=0 &&
                orient(points[c],points[a],points[p])*sign>=0){contains=true;break;}
            if(contains)continue;
            triangles.insert(triangles.end(),{a,b,c});remaining.erase(remaining.begin()+i);found=true;break;
        }
        if(!found)return false;
    }
    triangles.insert(triangles.end(),remaining.begin(),remaining.end());return true;
}
}
Result loadObj(const std::string& text,Mesh& destination,ObjOptions options) {
    Mesh mesh;std::vector<Vec3> positions,normals;std::vector<Vec2> uvs;
    std::istringstream input(text);std::string line;unsigned line_number=0;
    auto error=[&](const char* why)->Result{return {false,"OBJ line "+std::to_string(line_number)+": "+why};};
    while(std::getline(input,line)) {
        ++line_number;line=line.substr(0,line.find('#'));
        std::istringstream row(line);std::string kind;row>>kind;
        if(kind=="v"||kind=="vn") {
            Vec3 v;
            if(!(row>>v.x>>v.y>>v.z)||!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return error("invalid position/normal");
            if(kind=="v")positions.push_back(v);else normals.push_back(v);
        } else if(kind=="vt") {
            Vec2 uv;if(!(row>>uv.x))return error("invalid UV");
            row>>std::ws;if(!row.eof()&&!(row>>uv.y))return error("invalid UV");
            if(!std::isfinite(uv.x)||!std::isfinite(uv.y))return error("non-finite UV");
            if(options.flip_v)uv.y=1-uv.y;
            uvs.push_back(uv);
        } else if(kind=="f") {
            std::vector<Corner> face;std::string token;
            while(row>>token) {
                Corner c;if(!corner(token,positions.size(),uvs.size(),normals.size(),c))return error("invalid or out-of-range face index");
                face.push_back(c);
            }
            if(face.size()<3)return error("a face needs at least three corners");
            if(face.size()>4096)return error("face exceeds the 4096-corner safety limit");
            std::vector<std::uint32_t> triangles;Vec3 normal;
            if(!triangulate(face,positions,triangles,normal))return error("degenerate or unsupported polygon");
            if(mesh.vertices.size()+face.size()>std::numeric_limits<std::uint32_t>::max())return error("too many vertices");
            auto base=static_cast<std::uint32_t>(mesh.vertices.size());
            for(auto c:face)mesh.vertices.push_back({positions[c.p],c.n>=0?normalized(normals[c.n]):normal,c.t>=0?uvs[c.t]:Vec2{}});
            for(auto i:triangles)mesh.indices.push_back(base+i);
        }
    }
    if(input.bad())return {false,"Could not read OBJ text"};
    destination=std::move(mesh);return {};
}
Result loadObjFile(const char* path,Mesh& destination,ObjOptions options) {
    if(!path)return {false,"Missing OBJ path"};
    std::ifstream file(path);if(!file)return {false,std::string("Could not open OBJ: ")+path};
    std::ostringstream contents;contents<<file.rdbuf();
    if(file.bad())return {false,"Could not read OBJ file"};
    return loadObj(contents.str(),destination,options);
}
} // namespace pbc
