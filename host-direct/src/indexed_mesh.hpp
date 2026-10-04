#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
namespace direct {
constexpr unsigned indexedMeshMaxSide=32;
constexpr unsigned indexed_mesh_count(unsigned side){return side>=2&&side<=indexedMeshMaxSide?6*(side-1)*(side-1):0;}
constexpr unsigned indexed_mesh_offset(unsigned side){unsigned n=0;for(unsigned s=2;s<side;++s)n+=indexed_mesh_count(s);return n;}
constexpr unsigned indexedMeshTableCount=indexed_mesh_offset(indexedMeshMaxSide+1);
static_assert((65536+indexedMeshTableCount)*sizeof(uint16_t)<=262144,
    "Sequential and grid indices must fit the existing 256 KiB allocation");
inline void indexed_mesh_indices(uint16_t* out,unsigned side){
    unsigned j=0;
    for(unsigned y=0;y+1<side;++y)for(unsigned x=0;x+1<side;++x){
        const auto i=uint16_t(y*side+x);
        for(auto k:{unsigned(i),unsigned(i+1),unsigned(i+side+1),unsigned(i),unsigned(i+side+1),unsigned(i+side)})out[j++]=uint16_t(k);
    }
}
inline bool indexed_mesh_valid(unsigned side,size_t count){return side>=2&&side<=indexedMeshMaxSide&&count==size_t(side)*side;}
}
