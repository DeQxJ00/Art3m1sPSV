// Included in direct namespace after the normal draw path; all GXM work stays
// on the rendering thread. GXP bytes remain alive until patcher unregistration.
namespace {
struct ExternalUniform {const SceGxmProgramParameter* parameter;unsigned offset,count;};
struct ExternalProgram {
    std::vector<uint32_t> bytes;SceGxmShaderPatcherId id{};bool registered=false;
    SceGxmFragmentProgram* blends[11]{};
    const SceGxmProgramParameter* clip=nullptr;
    std::vector<ExternalUniform> uniforms;
};
ExternalProgram* externalPrograms[64]{};
Texture* externalTransparent=nullptr;
}
unsigned external_register(const uint8_t* data,size_t size){
    if(active||!data||size<156||size>1024*1024||std::memcmp(data,"GXP",4)){log("[external-reject] header active=%d bytes=%u",int(active),unsigned(size));return 0;}
    uint32_t declared=0;std::memcpy(&declared,data+8,4);// libshacccg returns a 4-byte padded allocation; its GXP header reports
    // the unpadded program length (e.g. 629 versus 632). Accept only that padding.
    if(declared<156||declared>size||(size!=declared&&size!=((declared+3u)&~3u))){log("[external-reject] size header=%u supplied=%u",declared,unsigned(size));return 0;}
    unsigned slot=0;while(slot<64&&externalPrograms[slot])++slot;if(slot==64)return 0;
    auto* e=new ExternalProgram;e->bytes.resize((size+3)/4);std::memcpy(e->bytes.data(),data,size);
    auto* program=reinterpret_cast<const SceGxmProgram*>(e->bytes.data());
    const int checked=sceGxmProgramCheck(program);
    if(checked!=0||sceGxmProgramGetSize(program)!=declared||sceGxmProgramGetType(program)!=SCE_GXM_FRAGMENT_PROGRAM){log("[external-reject] program check=%08x size=%u type=%u",unsigned(checked),sceGxmProgramGetSize(program),unsigned(sceGxmProgramGetType(program)));delete e;return 0;}
    e->clip=sceGxmProgramFindParameterByName(program,"art_clip");
    if(!e->clip||sceGxmProgramParameterGetCategory(e->clip)!=SCE_GXM_PARAMETER_CATEGORY_UNIFORM){
        log("[external-reject] art_clip missing or wrong category");
        for(unsigned i=0;i<sceGxmProgramGetParameterCount(program);++i){auto* p=sceGxmProgramGetParameter(program,i);log("[external-param] %s category=%u resource=%u",sceGxmProgramParameterGetName(p),unsigned(sceGxmProgramParameterGetCategory(p)),sceGxmProgramParameterGetResourceIndex(p));}
        delete e;return 0;}
    for(unsigned i=0;i<sceGxmProgramGetParameterCount(program);++i){
        auto* p=sceGxmProgramGetParameter(program,i);
        if(sceGxmProgramParameterGetCategory(p)==SCE_GXM_PARAMETER_CATEGORY_SAMPLER){
            const auto unit=sceGxmProgramParameterGetResourceIndex(p);
            if(unit!=0&&unit!=1&&unit!=3){log("[external-reject] sampler=%s unit=%u",sceGxmProgramParameterGetName(p),unit);delete e;return 0;}
        }
    }
    if(!check(sceGxmShaderPatcherRegisterProgram(patcher,program,&e->id),"RegisterExternal")){delete e;return 0;}
    e->registered=true;externalPrograms[slot]=e;
    log("[external-shader] registered handle=%u bytes=%u",slot+1,unsigned(size));return slot+1;
}
bool external_uniform(unsigned id,const char* name,unsigned offset,unsigned count){
    if(!id||id>64||!externalPrograms[id-1]||!name||!count||count>128||offset>128-count)return false;
    auto& e=*externalPrograms[id-1];const auto* program=reinterpret_cast<const SceGxmProgram*>(e.bytes.data());
    const auto* p=sceGxmProgramFindParameterByName(program,name);if(!p)return true; // optimized-out declaration
    if(sceGxmProgramParameterGetCategory(p)!=SCE_GXM_PARAMETER_CATEGORY_UNIFORM||
       sceGxmProgramParameterGetType(p)!=SCE_GXM_PARAMETER_TYPE_F32||
       sceGxmProgramParameterGetComponentCount(p)*sceGxmProgramParameterGetArraySize(p)!=count)return false;
    e.uniforms.push_back({p,offset,count});return true;
}
void external_release(unsigned id){
    if(!id||id>64||!externalPrograms[id-1])return;
    // Registration/replacement/destruction occurs outside a frame. Reject an
    // accidental mid-scene release rather than invalidating queued GPU work.
    if(active){log("[external-shader] refused in-scene release %u",id);return;}
    wait();auto* e=externalPrograms[id-1];
    for(auto* p:e->blends)if(p)sceGxmShaderPatcherReleaseFragmentProgram(patcher,p);
    if(e->registered)sceGxmShaderPatcherUnregisterProgram(patcher,e->id);
    delete e;externalPrograms[id-1]=nullptr;
    bool any=false;for(auto* p:externalPrograms)any=any||p;
    if(!any){destroy(externalTransparent);externalTransparent=nullptr;}
    log("[external-shader] released handle=%u",id);
}
void draw_external(Texture* t,const Vertex* src,size_t count,bool triangles,unsigned blend,
                   const float* clip,Texture* mask,Texture* user,const CustomDraw& d){
    if(!active||!t||!src||!count||blend>10||!d.program||d.program>64||!externalPrograms[d.program-1]||
       (triangles?count%3!=0:count!=4)||!init_builtins())return;
    auto& e=*externalPrograms[d.program-1];
    if(!e.blends[blend]){
        SceGxmBlendInfo b{};b.colorMask=SCE_GXM_COLOR_MASK_ALL;b.colorFunc=b.alphaFunc=SCE_GXM_BLEND_FUNC_ADD;
        b.colorSrc=SCE_GXM_BLEND_FACTOR_SRC_ALPHA;b.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        switch(blend){
        case 1:b.alphaSrc=SCE_GXM_BLEND_FACTOR_SRC_ALPHA;b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;break;
        case 2:b.colorSrc=b.alphaSrc=SCE_GXM_BLEND_FACTOR_DST_COLOR;break;
        case 3:b.colorSrc=b.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;break;
        case 4:b.colorFunc=SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT;[[fallthrough]];
        case 7:b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;b.alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO;break;
        case 5:b.colorSrc=SCE_GXM_BLEND_FACTOR_ONE;break;
        case 6:b.colorSrc=b.alphaSrc=b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;break;
        case 8:b.colorSrc=SCE_GXM_BLEND_FACTOR_DST_COLOR;b.alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO;b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;break;
        case 9:b.colorSrc=SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR;b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;b.alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO;break;
        case 10:b.colorFunc=b.alphaFunc=SCE_GXM_BLEND_FUNC_NONE;b.colorSrc=b.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ZERO;break;
        }
        if(!check(sceGxmShaderPatcherCreateFragmentProgram(patcher,e.id,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,
             &b,reinterpret_cast<const SceGxmProgram*>(sprite_v),&e.blends[blend]),"LinkExternal"))return;
    }
    if(!externalTransparent){uint8_t rgba[16]{};externalTransparent=texture(2,2,rgba);if(!externalTransparent)return;}
    flush_batch();boundProgram=nullptr;boundImage=boundRule=nullptr;
    sceGxmSetFragmentProgram(ctx,e.blends[blend]);
    Texture* inputs[]={t,mask?mask:solid,externalTransparent,user?user:externalTransparent};
    for(unsigned unit: {0u,1u,3u}){
        // External filters need bilinear samples even when their input is an
        // intermediate surface otherwise sampled at exact pixels.
        auto descriptor=inputs[unit]->descriptor;sceGxmTextureSetMinFilter(&descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetMagFilter(&descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmSetFragmentTexture(ctx,unit,&descriptor);
    }
    const float fullClip[]={0,0,960,544};if(!clip)clip=fullClip;
    void* buffer=nullptr;if(!check(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&buffer),"ExternalUniform"))return;
    sceGxmSetUniformDataF(buffer,e.clip,0,4,clip);
    for(auto& u:e.uniforms)sceGxmSetUniformDataF(buffer,u.parameter,0,u.count,d.values+u.offset);
    ++frameStats.uniforms;
    size_t offset=0;
    while(offset<count){unsigned n=unsigned(std::min<size_t>(count-offset,65535));if(vertexUsed+n>vertexCapacity)return;
        for(unsigned i=0;i<n;++i){auto v=src[offset+i];v.x=v.x/480-1;v.y=1-v.y/272;std::memcpy(vertices+vertexUsed+i,&v,sizeof(v));}
        sceGxmSetVertexStream(ctx,0,vertices+vertexUsed);
        check(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,triangles?triangleIndices:indices,triangles?n:6),"ExternalDraw");
        vertexUsed+=n;offset+=n;++frameStats.draws;
    }
    frameStats.quads+=triangles?unsigned(count/3):1;
    boundProgram=nullptr;boundImage=boundRule=nullptr;
}
