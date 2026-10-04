#include "../../host-direct/src/indexed_mesh.hpp"
#include <vector>
#include <cassert>
int main(){
    unsigned offset=0;
    for(unsigned side=2;side<=32;++side){
        assert(direct::indexed_mesh_offset(side)==offset);
        std::vector<uint16_t> indices(direct::indexed_mesh_count(side));
        direct::indexed_mesh_indices(indices.data(),side);
        unsigned n=0;
        for(unsigned y=0;y<side-1;++y)for(unsigned x=0;x<side-1;++x){
            const unsigned i=y*side+x;
            for(unsigned k:{i,i+1,i+side+1,i,i+side+1,i+side})assert(indices[n++]==k);
        }
        assert(n==indices.size());assert(direct::indexed_mesh_valid(side,side*side));
        assert(!direct::indexed_mesh_valid(side,side*side-1));offset+=n;
    }
    assert(offset==direct::indexedMeshTableCount);
    assert((65536+offset)*sizeof(uint16_t)<=262144);
    for(auto side:{0u,1u,33u,65536u})assert(!direct::indexed_mesh_valid(side,size_t(side)*side));
}
