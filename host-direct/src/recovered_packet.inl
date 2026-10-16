/* Owned packet GPU adapter, using the host's sole context and shader patcher.
 * Shader byte arrays are exact copies of emote-psv-test's extracted GXP; no
 * Rust evaluator or replacement shader is used. Included once by gpu.cpp. */
#ifndef DIRECT_RECOVERED_PACKET
namespace {void recovered_frame_begin(){} void recovered_shutdown(){}
const SceGxmDepthStencilSurface* recovered_group_depth(Offscreen*){return nullptr;}
void recovered_neutral_state(){}}
bool recovered_packet_enabled(){return false;}
bool draw_recovered_packet(const EmRenderPacket*,Texture* const*,size_t,const float*,float,const float*){return false;}
#else
namespace {
bool recoveredQueueDisabled=false,recoveredClipEnabled=true,recoveredQueueTesting=false;
bool recovered_queue_enabled(){
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    return offscreenQueueAllowed&&!offscreenQueueDisabled&&!recoveredQueueDisabled;
#else
    return false;
#endif
}
struct RecoveredGpu {
    Memory arena,depthMemory;size_t used=0;
    std::vector<EmRenderVertex> mappedVertices; // CPU scratch; GPU only sees fenced arena copies.
    SceGxmDepthStencilSurface depth{};
    Offscreen masks[2];
    SceGxmShaderPatcherId vertexId{},fragmentId{},alphaId{},materialIds[6]{},softIds[10]{};
    SceGxmVertexProgram* vertex=nullptr;
    SceGxmFragmentProgram *plain=nullptr,*alpha=nullptr,*clear=nullptr;
    SceGxmFragmentProgram *materials[2][6]{},*softMaterials[2][6]{},*writers[4]{};
    const SceGxmProgramParameter *matrix=nullptr,*alphaRef=nullptr,*screen[10]{},*clearColor=nullptr;
    bool ready=false,depthUsed=false;
} recovered;
constexpr size_t recoveredArenaSize=8*1024*1024;
const unsigned char* const recoveredMaterials[]={emote_material_60,emote_material_61,emote_material_62,emote_material_90,emote_material_91,emote_material_92};
const unsigned char* const recoveredSoft[]={soft_shader_0,soft_shader_1,soft_shader_2,soft_shader_3,soft_shader_80,soft_shader_81,soft_shader_82,soft_shader_110,soft_shader_111,soft_shader_112};
// Called only after the host's frame-start completion barrier.
void recovered_frame_begin(){recovered.used=0;recovered.depthUsed=false;recoveredDepthAttached=false;}
const SceGxmDepthStencilSurface* recovered_group_depth(Offscreen* o){
    // Fresh scenes clear stencil. Ordered fragment submission permits another
    // use of the same DS without a CPU fence; conservative mode keeps the old
    // one-borrow restriction. Never reinitialize or free the backing storage.
    if(!recovered.ready||(recovered.depthUsed&&!recovered_queue_enabled())||(o&&(!o->image||o->image->w!=960||o->image->h!=544)))return nullptr;
    recovered.depthUsed=true;
    sceGxmDepthStencilSurfaceSetForceLoadMode(&recovered.depth,SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_DISABLED);
    return &recovered.depth;
}
void recovered_shutdown(){
    auto& r=recovered;
    // Only before first submission on failure, or after host wait at exit.
    for(auto& row:r.materials)for(auto* p:row)if(p)sceGxmShaderPatcherReleaseFragmentProgram(patcher,p);
    for(auto& row:r.softMaterials)for(auto* p:row)if(p)sceGxmShaderPatcherReleaseFragmentProgram(patcher,p);
    for(auto* p:r.writers)if(p)sceGxmShaderPatcherReleaseFragmentProgram(patcher,p);
    if(r.plain)sceGxmShaderPatcherReleaseFragmentProgram(patcher,r.plain);
    if(r.alpha)sceGxmShaderPatcherReleaseFragmentProgram(patcher,r.alpha);
    if(r.clear)sceGxmShaderPatcherReleaseFragmentProgram(patcher,r.clear);
    if(r.vertex)sceGxmShaderPatcherReleaseVertexProgram(patcher,r.vertex);
    for(auto id:r.materialIds)if(id)sceGxmShaderPatcherUnregisterProgram(patcher,id);
    for(auto id:r.softIds)if(id)sceGxmShaderPatcherUnregisterProgram(patcher,id);
    if(r.vertexId)sceGxmShaderPatcherUnregisterProgram(patcher,r.vertexId);
    if(r.fragmentId)sceGxmShaderPatcherUnregisterProgram(patcher,r.fragmentId);
    if(r.alphaId)sceGxmShaderPatcherUnregisterProgram(patcher,r.alphaId);
    for(auto& o:r.masks){
        if(o.sync)sceGxmSyncObjectDestroy(o.sync);
        if(o.target)sceGxmDestroyRenderTarget(o.target);
        if(o.image){release({o.image->uid,o.image->pixels,0,o.image->allocation});delete o.image;}
    }
    release(r.arena);release(r.depthMemory);r=RecoveredGpu{};
}
bool recovered_resources(){
    auto& r=recovered;if(r.ready)return true;
    bool ok=[&](){
        r.arena=allocate_upload(recoveredArenaSize,5);r.depthMemory=allocate(960*544*4,0,6);
        if(!r.arena.p||!r.depthMemory.p)return false;
        if(!check(sceGxmDepthStencilSurfaceInit(&r.depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,960,r.depthMemory.p,nullptr),"RecoveredDepth"))return false;
        sceGxmDepthStencilSurfaceSetForceStoreMode(&r.depth,SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);
        auto* v=reinterpret_cast<const SceGxmProgram*>(emote_vertex);
        auto* f=reinterpret_cast<const SceGxmProgram*>(emote_fragment);
        auto* a=reinterpret_cast<const SceGxmProgram*>(emote_alpha_test);
        if(!check(sceGxmShaderPatcherRegisterProgram(patcher,v,&r.vertexId),"RecoveredVertex")||
           !check(sceGxmShaderPatcherRegisterProgram(patcher,f,&r.fragmentId),"RecoveredFragment")||
           !check(sceGxmShaderPatcherRegisterProgram(patcher,a,&r.alphaId),"RecoveredAlpha"))return false;
        const char* names[]={"In.pos","In.diffuse","In.uv"};unsigned offsets[]={0,16,32},components[]={4,4,2};
        SceGxmVertexAttribute attrs[3]{};
        for(unsigned i=0;i<3;++i){auto* p=sceGxmProgramFindParameterByName(v,names[i]);if(!p)return false;
            attrs[i].offset=offsets[i];attrs[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attrs[i].componentCount=components[i];attrs[i].regIndex=sceGxmProgramParameterGetResourceIndex(p);}
        r.matrix=sceGxmProgramFindParameterByName(v,"modelViewProj");r.alphaRef=sceGxmProgramFindParameterByName(a,"alphaRef");
        if(!r.matrix||!r.alphaRef)return false;
        SceGxmVertexStream stream{sizeof(EmRenderVertex),SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
        if(!check(sceGxmShaderPatcherCreateVertexProgram(patcher,r.vertexId,attrs,3,&stream,1,&r.vertex),"RecoveredVertexProgram"))return false;
        SceGxmBlendInfo blend{};blend.colorMask=SCE_GXM_COLOR_MASK_ALL;blend.colorFunc=blend.alphaFunc=SCE_GXM_BLEND_FUNC_ADD;
        blend.colorSrc=SCE_GXM_BLEND_FACTOR_SRC_ALPHA;blend.colorDst=blend.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;blend.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;
        if(!check(sceGxmShaderPatcherCreateFragmentProgram(patcher,r.fragmentId,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,v,&r.plain),"RecoveredPlain"))return false;
        blend.colorMask=0;
        if(!check(sceGxmShaderPatcherCreateFragmentProgram(patcher,r.alphaId,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,v,&r.alpha),"RecoveredMask")||
           !check(sceGxmShaderPatcherCreateFragmentProgram(patcher,r.fragmentId,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,v,&r.clear),"RecoveredClear"))return false;
        for(unsigned i=0;i<6;++i)if(!check(sceGxmShaderPatcherRegisterProgram(patcher,reinterpret_cast<const SceGxmProgram*>(recoveredMaterials[i]),&r.materialIds[i]),"RecoveredMaterial"))return false;
        for(unsigned i=0;i<10;++i){auto* p=reinterpret_cast<const SceGxmProgram*>(recoveredSoft[i]);
            if(!check(sceGxmShaderPatcherRegisterProgram(patcher,p,&r.softIds[i]),"RecoveredSoft"))return false;
            r.screen[i]=sceGxmProgramFindParameterByName(p,"screenMul");
            if(i<4&&!check(sceGxmShaderPatcherCreateFragmentProgram(patcher,r.softIds[i],SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,nullptr,v,&r.writers[i]),"RecoveredSoftWriter"))return false;
            if(i>=4){auto* sampler=sceGxmProgramFindParameterByName(p,"alphaTexture");if(!r.screen[i]||!sampler||sceGxmProgramParameterGetResourceIndex(sampler)!=1)return false;}
        }
        r.clearColor=sceGxmProgramFindParameterByName(reinterpret_cast<const SceGxmProgram*>(soft_shader_3),"clearColor");
        if(!r.clearColor||!r.screen[1])return false;
        for(auto& o:r.masks){if(!create_offscreen(o))return false;
            if(!check(sceGxmTextureInitLinearStrided(&o.image->descriptor,o.image->pixels,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,960,544,960*4),"RecoveredMaskLayout"))return false;
            sceGxmTextureSetMagFilter(&o.image->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
            sceGxmTextureSetUAddrMode(&o.image->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);sceGxmTextureSetVAddrMode(&o.image->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);
        }
        return true;
    }();
    if(!ok){recovered_shutdown();return false;}r.ready=true;return true;
}
SceGxmFragmentProgram* recovered_material(unsigned mode,bool soft){
    auto& r=recovered;if(mode==UINT32_MAX)return r.plain;
    if((mode&~31u)||(mode&15)>5)return nullptr;
    const unsigned group=(mode>>4)&1,low=mode&15;
    auto*& out=soft?r.softMaterials[group][low]:r.materials[group][low];if(out)return out;
    EmMaterialState state{};state.blend_mode=mode;state.texture_present=state.blend_enabled=1;EmMaterialSelection select{};em_material_select(&state,&select);
    unsigned index=(group?3:0)+(select.family-(group?90:60));if(index>=6||select.program_slot<1||select.program_slot>6)return nullptr;
    SceGxmBlendInfo blend{};static_assert(sizeof(blend)==4,"original blend record ABI");std::memcpy(&blend,em_material_blend_record(select.program_slot),sizeof blend);
    if(!check(sceGxmShaderPatcherCreateFragmentProgram(patcher,soft?r.softIds[4+index]:r.materialIds[index],SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,reinterpret_cast<const SceGxmProgram*>(emote_vertex),&out),"RecoveredBlend"))return nullptr;
    return out;
}
// Fragment-stage accesses use the host's ordered scene queue. Storage and
// vertex arenas remain alive until the existing frame-start completion fence.
// CPU readback/retirement still drain that queue through the normal host path.
bool recovered_finish(){
    if(!active)return true;
    recoveredDepthAttached=false;
    flush_batch();const auto started=sceKernelGetProcessTimeWide();
    const bool ok=check(sceGxmEndScene(ctx,nullptr,nullptr),"RecoveredEndScene");active=false;
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    gpuPending=true;
    if(!ok||!recovered_queue_enabled()){
        sceGxmFinish(ctx);gpuPending=false;++gpuCompletionEpoch;recovered.depthUsed=false;
    }
#else
    sceGxmFinish(ctx);recovered.depthUsed=false;
#endif
    effectFrameTiming.switchUs+=sceKernelGetProcessTimeWide()-started;++effectFrameTiming.switches;return ok;
}
void recovered_neutral_state(){
    sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_OUTSIDE,0,0,959,543);
    sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);sceGxmSetViewport(ctx,480,480,272,-272,.5f,.5f);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetFrontStencilRef(ctx,0);sceGxmSetBackStencilRef(ctx,0);
    sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,255,0);
    sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,255,0);
}
struct RecoveredPlayback {
    Offscreen* parent;unsigned mode=UINT32_MAX,savedMode=UINT32_MAX,index=0,positive=0;
    bool building=false,bound=false,inMask=false;unsigned region[4]{0,0,959,543};
    const float* affine=nullptr;const float* clip=nullptr;
    SceGxmFragmentProgram* plainProgram=nullptr;bool plainValid=false;
    bool profiling=false;uint64_t parts[9]{};unsigned profileMeshes=0,profileVertices=0;
    uint64_t mark() const {return profiling?sceKernelGetProcessTimeWide():0;}
    void add(unsigned slot,uint64_t began){if(profiling)parts[slot]+=sceKernelGetProcessTimeWide()-began;}
    void invalidate_plain(){plainValid=false;}
    bool map(EmRenderVertex& v) const {if(!affine)return true;
        const float x=(v.x+v.w)*480,y=(v.w-v.y)*272;
        const float px=affine[0]*x+affine[2]*y+affine[4]*v.w;
        const float py=affine[1]*x+affine[3]*y+affine[5]*v.w;
        v.x=px/480-v.w;v.y=v.w-py/272;return std::isfinite(v.x)&&std::isfinite(v.y);
    }
    bool map_coefficients(float out[6]) const {
        if(!affine)return false;
        // Fold stage-to-clip conversions once per packet. Extreme transforms
        // retain the original arithmetic and overflow rejection behavior.
        for(unsigned i=0;i<4;++i)if(std::fabs(affine[i])>16)return false;
        if(std::fabs(affine[4])>16384||std::fabs(affine[5])>16384)return false;
        out[0]=affine[0];out[1]=-affine[2]*(272.f/480.f);
        out[2]=affine[0]+affine[2]*(272.f/480.f)+affine[4]/480.f-1;
        out[3]=-affine[1]*(480.f/272.f);out[4]=affine[3];
        out[5]=1-affine[1]*(480.f/272.f)-affine[3]-affine[5]/272.f;
        return true;
    }
    bool clip_setup(){invalidate_plain();if(!clip)return true;
        // Bit7 belongs only to host clipping. Recovered hard masks write bits0..6.
        sceGxmSetFragmentProgram(ctx,recovered.clear);
        sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_ZERO,SCE_GXM_STENCIL_OP_ZERO,SCE_GXM_STENCIL_OP_ZERO,128,128);
        sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_ZERO,SCE_GXM_STENCIL_OP_ZERO,SCE_GXM_STENCIL_OP_ZERO,128,128);
        if(!quad())return false;
        sceGxmSetFrontStencilRef(ctx,128);sceGxmSetBackStencilRef(ctx,128);
        sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_REPLACE,SCE_GXM_STENCIL_OP_REPLACE,SCE_GXM_STENCIL_OP_REPLACE,128,128);
        sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_REPLACE,SCE_GXM_STENCIL_OP_REPLACE,SCE_GXM_STENCIL_OP_REPLACE,128,128);
        const float x0=std::clamp(clip[0],0.f,960.f),y0=std::clamp(clip[1],0.f,544.f),x1=std::clamp(clip[2],0.f,960.f),y1=std::clamp(clip[3],0.f,544.f);
        if(x0>=x1||y0>=y1)return true;
        const EmRenderVertex v[]={{x0/480-1,1-y0/272,.5,1,1,1,1,1,0,0},{x1/480-1,1-y0/272,.5,1,1,1,1,1,1,0},{x0/480-1,1-y1/272,.5,1,1,1,1,1,0,1},{x1/480-1,1-y1/272,.5,1,1,1,1,1,1,1}};
        const uint16_t ix[]={0,1,2,1,3,2};return mesh(solid,v,4,ix,6,false);
    }
    bool scene(Offscreen* mask,bool load){
        invalidate_plain();
        // Keep the private DS attached between adjacent packet/ordinary draws.
        // Neutral depth/stencil state makes it inert for host sprites. A real
        // target change still takes the original finish/load path.
        const bool reuse=!mask&&!load&&active&&recoveredDepthAttached&&activeOffscreen==parent;
        flush_batch();
        if(!reuse&&!recovered_finish())return false;
        auto* destination=mask?mask:parent;
        if(!mask)sceGxmDepthStencilSurfaceSetForceLoadMode(&recovered.depth,load?SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED:SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_DISABLED);
        if(!reuse)active=check(sceGxmBeginScene(ctx,0,destination?destination->target:target,nullptr,nullptr,destination?destination->sync:buffers[back].sync,destination?&destination->surface:&buffers[back].surface,mask?nullptr:&recovered.depth),"RecoveredBeginScene");
        recoveredDepthAttached=active&&!mask;if(recoveredDepthAttached)recovered.depthUsed=true;activeOffscreen=active?destination:nullptr;boundProgram=nullptr;boundImage=boundRule=nullptr;if(!active)return false;
        recovered_neutral_state();sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_OUTSIDE,region[0],region[1],region[2],region[3]);
        sceGxmSetVertexProgram(ctx,recovered.vertex);void* ub=nullptr;const float matrix[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        return check(sceGxmReserveVertexDefaultUniformBuffer(ctx,&ub),"RecoveredMatrixBuffer")&&check(sceGxmSetUniformDataF(ub,recovered.matrix,0,16,matrix),"RecoveredMatrix");
    }
    const EmRenderVertex* packetStream=nullptr;
    const uint16_t* packetIndices=nullptr;
    bool stream_mesh(Texture* t,const EmRenderVertex* v,unsigned count,const uint16_t* ix,unsigned ni){
        auto began=mark();
        bool ok=check(sceGxmSetVertexStream(ctx,0,v),"RecoveredStream")&&check(sceGxmSetFragmentTexture(ctx,0,&t->descriptor),"RecoveredTexture");
        add(2,began);began=mark();
        if(ok)ok=check(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,ix,ni),"RecoveredDraw");
        add(3,began);if(profiling){++profileMeshes;profileVertices+=count;}
        if(ok)++frameStats.draws;return ok;
    }
    bool mesh(Texture* t,const EmRenderVertex* v,unsigned count,const uint16_t* ix,unsigned ni,bool transform=true){
        auto began=mark();
        const size_t vb=(count*sizeof(*v)+15)&~size_t(15),ib=(ni*sizeof(*ix)+15)&~size_t(15);
        if(vb+ib>recoveredArenaSize-recovered.used)return false;
        auto* dest=static_cast<uint8_t*>(recovered.arena.p)+recovered.used;
        if(transform&&affine){EmRenderVertex mapped[81];if(count>81)return false;std::memcpy(mapped,v,count*sizeof(*v));for(unsigned i=0;i<count;++i)if(!map(mapped[i]))return false;std::memcpy(dest,mapped,count*sizeof(*v));}
        else std::memcpy(dest,v,count*sizeof(*v));std::memcpy(dest+vb,ix,ni*sizeof(*ix));recovered.used+=vb+ib;
        add(1,began);
        return stream_mesh(t,reinterpret_cast<const EmRenderVertex*>(dest),count,reinterpret_cast<const uint16_t*>(dest+vb),ni);
    }
    bool quad(){const EmRenderVertex v[]={{-1,1,.5,1,1,1,1,1,0,0},{1,1,.5,1,1,1,1,1,1,0},{-1,-1,.5,1,1,1,1,1,0,1},{1,-1,.5,1,1,1,1,1,1,1}};const uint16_t ix[]={0,1,2,1,3,2};return mesh(solid,v,4,ix,6,false);}
    bool uniform(unsigned i){if(!recovered.screen[i])return true;void* ub=nullptr;const float mul[]={1.f/960,1.f/544};return check(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&ub),"RecoveredScreenBuffer")&&check(sceGxmSetUniformDataF(ub,recovered.screen[i],0,2,mul),"RecoveredScreen");}
    bool threshold(){void* ub=nullptr;uint32_t bits=1048608897;float value;std::memcpy(&value,&bits,4);return check(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&ub),"RecoveredAlphaBuffer")&&check(sceGxmSetUniformDataF(ub,recovered.alphaRef,0,1,&value),"RecoveredAlphaRef");}
    bool material(unsigned m){mode=m;return recovered_material(mode,bound)!=nullptr;}
    bool mask(unsigned read,unsigned write){
        // Packet-local ordinary-state reuse, matching recovered gpu.c. Any
        // stencil/soft/clip/target path invalidates; uniforms and texture binds
        // retain their original calls. No state survives packet ownership.
        const bool plain=!read&&!write&&!clip&&!bound&&!building;
        auto* program=write?recovered.alpha:recovered_material(mode,bound);
        if(plain&&plainValid&&plainProgram==program)return true;
        invalidate_plain();
        sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
        if(clip)read|=128;unsigned ref=read|write;auto test=read?SCE_GXM_STENCIL_FUNC_EQUAL:SCE_GXM_STENCIL_FUNC_ALWAYS;auto op=write?SCE_GXM_STENCIL_OP_REPLACE:SCE_GXM_STENCIL_OP_KEEP;
        sceGxmSetFrontStencilRef(ctx,ref);sceGxmSetBackStencilRef(ctx,ref);sceGxmSetFrontStencilFunc(ctx,test,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,op,read,write);sceGxmSetBackStencilFunc(ctx,test,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,op,read,write);
        sceGxmSetFragmentProgram(ctx,program);
        if(plain){plainProgram=program;plainValid=true;}
        if(write)return threshold();
        if(bound&&mode!=UINT32_MAX){unsigned low=mode&15,i=4+((mode&16)?3:0)+(low==5?1:(low==3||low==4)?2:0);return uniform(i)&&check(sceGxmSetFragmentTexture(ctx,1,&recovered.masks[index].image->descriptor),"RecoveredAlphaTexture");}return true;
    }
    bool step(unsigned ref,unsigned operation){
        invalidate_plain();
        sceGxmSetFrontDepthFunc(ctx,operation?SCE_GXM_DEPTH_FUNC_NEVER:SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,operation?SCE_GXM_DEPTH_FUNC_NEVER:SCE_GXM_DEPTH_FUNC_ALWAYS);
        auto op=operation==1?SCE_GXM_STENCIL_OP_INCR:operation==2?SCE_GXM_STENCIL_OP_DECR:SCE_GXM_STENCIL_OP_KEEP;
        if(clip)ref|=128;unsigned read=clip?255:127;
        sceGxmSetFrontStencilRef(ctx,ref);sceGxmSetBackStencilRef(ctx,ref);sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_EQUAL,SCE_GXM_STENCIL_OP_KEEP,op,op,read,127);sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_EQUAL,SCE_GXM_STENCIL_OP_KEEP,op,op,read,127);
        sceGxmSetFragmentProgram(ctx,operation?recovered.alpha:recovered_material(mode,bound));return !operation||threshold();
    }
    bool reset(unsigned initial){invalidate_plain();sceGxmSetFragmentProgram(ctx,recovered.clear);sceGxmSetFrontStencilRef(ctx,initial);sceGxmSetBackStencilRef(ctx,initial);
        sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_REPLACE,SCE_GXM_STENCIL_OP_REPLACE,SCE_GXM_STENCIL_OP_REPLACE,127,127);sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_REPLACE,SCE_GXM_STENCIL_OP_REPLACE,SCE_GXM_STENCIL_OP_REPLACE,127,127);return quad();}
    bool clear(float alpha){invalidate_plain();sceGxmSetFragmentProgram(ctx,recovered.writers[3]);void* ub=nullptr;const float color[]={0,0,0,alpha};return check(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&ub),"RecoveredClearBuffer")&&check(sceGxmSetUniformDataF(ub,recovered.clearColor,0,4,color),"RecoveredClearColor")&&quad();}
    bool disable(){invalidate_plain();if(building&&inMask&&!scene(nullptr,true))return false;building=inMask=bound=false;region[0]=region[1]=0;region[2]=959;region[3]=543;sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_OUTSIDE,0,0,959,543);return material(mode)&&check(sceGxmSetFragmentTexture(ctx,1,&solid->descriptor),"RecoveredDisableTexture")&&mask(0,0);}
    bool command(const EmRenderCommand& c,Texture* const* textures,const EmRenderVertex* vertices,const uint16_t* indices){
        if(c.op!=EM_RENDER_MATERIAL&&c.op!=EM_RENDER_MASK_STATE&&c.op!=EM_RENDER_MESH)invalidate_plain();
        switch(c.op){
        case EM_RENDER_MATERIAL:return material(uint32_t(c.args[0]));
        case EM_RENDER_MASK_STATE:return mask(c.args[0],c.args[1]);
        case EM_RENDER_HARD_RESET:return reset(c.args[0]);
        case EM_RENDER_HARD_STEP:return step(c.args[0],c.args[1]);
        case EM_RENDER_SOFT_BEGIN:if(building)return false;savedMode=mode;bound=false;building=true;inMask=false;positive=index=0;region[0]=region[1]=0;region[2]=959;region[3]=543;return true;
        case EM_RENDER_SOFT_REGION:{if(!building||inMask)return false;int x0=std::max(0,c.args[0]),y0=std::max(0,c.args[1]),x1=std::min(960,c.args[2]),y1=std::min(544,c.args[3]);
            // Original packet ROI is already clamped to its untransformed canvas.
            // Affine may bring previously offscreen geometry back into view; a
            // transformed clamped ROI is NOT conservative. Use the full target.
            if(affine){x0=y0=0;x1=960;y1=544;}
            if(x1<=x0||y1<=y0){region[0]=region[1]=0;region[2]=959;region[3]=543;}else {region[0]=unsigned(x0)&~31u;region[1]=unsigned(y0)&~31u;region[2]=std::min(959u,(unsigned(x1)-1)|31u);region[3]=std::min(543u,(unsigned(y1)-1)|31u);}return true;}
        case EM_RENDER_SOFT_POSITIVE:{if(!building)return false;unsigned previous=index;if(positive)index^=1;if(!scene(&recovered.masks[index],false))return false;inMask=true;if(!clear(0))return false;unsigned writer=positive?1:0;sceGxmSetFragmentProgram(ctx,recovered.writers[writer]);if(!uniform(writer))return false;if(writer&&!check(sceGxmSetFragmentTexture(ctx,1,&recovered.masks[previous].image->descriptor),"RecoveredPreviousMask"))return false;++positive;return true;}
        case EM_RENDER_SOFT_NEGATIVE:if(!building)return false;if(!inMask){if(!scene(&recovered.masks[index],false))return false;inMask=true;if(!clear(1))return false;}sceGxmSetFragmentProgram(ctx,recovered.writers[2]);return true;
        case EM_RENDER_SOFT_END:if(!building||!inMask||!scene(nullptr,true))return false;building=inMask=false;bound=true;return material(savedMode)&&mask(0,0);
        case EM_RENDER_SOFT_DISABLE:return disable();
        case EM_RENDER_MESH:
            if(packetStream)return stream_mesh(textures[c.texture],packetStream+c.vertex_first,c.vertex_count,packetIndices+c.index_first,c.index_count);
            return mesh(textures[c.texture],vertices+c.vertex_first,c.vertex_count,indices+c.index_first,c.index_count);
        default:return false;
        }
    }
};
}
bool recovered_packet_enabled(){return true;}
#ifdef DIRECT_RECOVERED_PROBE_DIAGNOSTICS
void recovered_queue_probe(bool enabled){
    recoveredQueueTesting=true;recoveredQueueDisabled=!enabled;recoveredClipEnabled=enabled;
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    offscreenQueueAllowed=enabled;
#endif
}
#endif
bool draw_recovered_packet(const EmRenderPacket* packet,Texture* const* textures,size_t count,const float* affine,float opacity,const float* clip){
    // Matrix order is [a,b,c,d,tx,ty], x=a*x+c*y+tx, y=b*x+d*y+ty.
    const float identity[]={1,0,0,1,0,0};
    if(opacity!=1)return group_failed();
    bool identityMap=true;if(affine)for(unsigned i=0;i<6;++i){if(!std::isfinite(affine[i]))return group_failed();identityMap=identityMap&&affine[i]==identity[i];}if(identityMap)affine=nullptr;
    if(clip){for(unsigned i=0;i<4;++i)if(!std::isfinite(clip[i]))return group_failed();if(clip[0]<=0&&clip[1]<=0&&clip[2]>=960&&clip[3]>=544)clip=nullptr;}
    if(!active||!packet||em_render_packet_version(packet)!=EM_RENDER_PACKET_VERSION)return false;
    auto* parent=activeOffscreen;const unsigned savedDepth=groupDepth;const bool prebound=recoveredDepthAttached;
    if(parent&&(!parent->image||parent->image->w!=960||parent->image->h!=544))return group_failed();
    uint32_t nc=0,nv=0,ni=0;auto* commands=em_render_packet_commands(packet,&nc);auto* vertices=em_render_packet_vertices(packet,&nv);auto* indices=em_render_packet_indices(packet,&ni);
    if(count!=em_render_packet_texture_count(packet)||(count&&!textures))return group_failed();
    for(size_t i=0;i<count;++i){EmRenderTextureInfo info{};if(!textures[i]||!em_render_packet_texture(packet,i,&info)||textures[i]->w!=info.width||textures[i]->h!=info.height)return group_failed();}
    // All allocations/material programs are ready before changing the parent.
    if(!recovered_resources())return group_failed();
    size_t needed=clip?384:0;
    RecoveredPlayback replay{parent};replay.affine=affine;replay.clip=clip;
    static unsigned sequence=0;replay.profiling=++sequence%121==0;const auto began=replay.mark();
    // Hard-mask-only packets never sample a neighboring soft-mask target.
    // Reject tiles outside the host clip before running fragment shaders, while
    // preserving the original stencil rectangle and every packet command.
    bool soft=false;for(uint32_t i=0;i<nc;++i)soft|=commands[i].op==EM_RENDER_SOFT_BEGIN;
    if(!soft&&recoveredClipEnabled)recovered_clip_region(clip,replay.region);
    replay.add(6,began);auto validateBegan=replay.mark();
    for(uint32_t i=0;i<nc;++i){auto& c=commands[i];
        if(clip&&c.op==EM_RENDER_MASK_STATE&&((uint32_t(c.args[0])|uint32_t(c.args[1]))&128))return group_failed();
        if(c.op==EM_RENDER_MATERIAL&&!recovered_material(uint32_t(c.args[0]),false))return group_failed();
        if(c.op==EM_RENDER_MATERIAL&&!recovered_material(uint32_t(c.args[0]),true))return group_failed();
        size_t bytes=192; // conservative private clear quad for every non-mesh command
        if(c.op==EM_RENDER_MESH){if(c.texture>=count||c.vertex_first>nv||c.vertex_count>nv-c.vertex_first||c.index_first>ni||c.index_count>ni-c.index_first)return group_failed();bytes=((size_t(c.vertex_count)*40+15)&~size_t(15))+((size_t(c.index_count)*2+15)&~size_t(15));}
        needed=needed>recoveredArenaSize||bytes>recoveredArenaSize-needed?recoveredArenaSize+1:needed+bytes;
    }
    // Upload one immutable packet stream before issuing draws. Append only;
    // neither later packets nor their CPU scratch can overwrite GPU-owned data.
    // Private clip/mask quads still allocate through mesh(). Preserve the old
    // per-mesh fallback when an unusually sparse packet cannot fit in bulk.
    const uint64_t vertexBytes64=(uint64_t(nv)*sizeof(*vertices)+15)&~uint64_t(15);
    const uint64_t indexBytes64=(uint64_t(ni)*sizeof(*indices)+15)&~uint64_t(15);
    uint64_t privateBytes=clip?384:0;
    for(uint32_t i=0;i<nc;++i)if(commands[i].op!=EM_RENDER_MESH)privateBytes+=192;
    const size_t available=recoveredArenaSize-recovered.used;
    const bool bulk=nv&&vertexBytes64+indexBytes64+privateBytes<=available;
    if(!bulk&&needed>available)return group_failed();
    const size_t vertexBytes=bulk?size_t(vertexBytes64):0,indexBytes=bulk?size_t(indexBytes64):0;
    replay.add(7,validateBegan);const auto mappingBegan=replay.mark();
    // Reject capacity failures before scratch allocation or coordinate work.
    // Validate and retain exactly the transformed values used by submission.
    // Never modify the immutable packet or reuse scratch across packets.
    if(affine){
        recovered.mappedVertices.resize(nv);
        // Bulk copy once, then update only XY in place. Avoid two 40-byte
        // stack round trips per vertex in the ARM scalar loop.
        if(nv)std::memcpy(recovered.mappedVertices.data(),vertices,size_t(nv)*sizeof(*vertices));
        float coefficients[6];
        if(replay.map_coefficients(coefficients)){
            for(auto& v:recovered.mappedVertices){
                const float x=v.x,y=v.y;
                v.x=coefficients[0]*x+coefficients[1]*y+coefficients[2]*v.w;
                v.y=coefficients[3]*x+coefficients[4]*y+coefficients[5]*v.w;
                if(!std::isfinite(v.x)||!std::isfinite(v.y))return group_failed();
            }
        }else for(auto& v:recovered.mappedVertices)if(!replay.map(v))return group_failed();
        vertices=recovered.mappedVertices.data();replay.affine=nullptr;
    }
    replay.add(6,mappingBegan);validateBegan=replay.mark();
    if(bulk){
        auto* dest=static_cast<uint8_t*>(recovered.arena.p)+recovered.used;
        std::memcpy(dest,vertices,size_t(nv)*sizeof(*vertices));
        if(ni)std::memcpy(dest+vertexBytes,indices,size_t(ni)*sizeof(*indices));
        replay.packetStream=reinterpret_cast<const EmRenderVertex*>(dest);
        replay.packetIndices=reinterpret_cast<const uint16_t*>(dest+vertexBytes);
        recovered.used+=vertexBytes+indexBytes;
    }
    replay.add(7,validateBegan);auto sceneBegan=replay.mark();
    bool ok=replay.scene(nullptr,false);
    if(ok)ok=replay.clip_setup()&&replay.material(UINT32_MAX)&&replay.mask(0,0);
    replay.add(8,sceneBegan);replay.add(0,began);auto commandBegan=replay.mark();
    if(ok)for(uint32_t i=0;i<nc;++i)if(!replay.command(commands[i],textures,vertices,indices)){ok=false;break;}
    replay.add(4,commandBegan);auto restoreBegan=replay.mark();
    if(replay.building)ok=false;
    // Even a failed Begin/uniform/draw follows the same detach/recover path.
    // Parent pixels survive every resume. No group ownership or depth changed.
    const bool keep=ok&&active&&recoveredDepthAttached&&activeOffscreen==parent;
    bool ended=true,restored=true;
    if(!keep){ended=recovered_finish();restored=resume_target(parent);}
    if(restored){recovered_neutral_state();sceGxmSetVertexProgram(ctx,vp);const bool t0=check(sceGxmSetFragmentTexture(ctx,0,&solid->descriptor),"RecoveredRestoreTexture0"),t1=check(sceGxmSetFragmentTexture(ctx,1,&solid->descriptor),"RecoveredRestoreTexture1");restored=t0&&t1;}
    boundProgram=nullptr;boundImage=boundRule=nullptr;
    if(parent)parent->dirty.invalidate();
    if(groupDepth&&!groups[groupDepth-1].masking)groups[groupDepth-1].bounds.invalidate();
    if(groupDepth!=savedDepth){log("recovered packet group depth invariant failed");ok=false;}
    replay.add(5,restoreBegan);
    if(replay.profiling){unsigned hard=0,soft=0;for(uint32_t i=0;i<nc;++i){hard+=commands[i].op==EM_RENDER_HARD_STEP;soft+=commands[i].op==EM_RENDER_SOFT_BEGIN;}
        log("[recovered-scene-requirements] clip=%d hard_steps=%u soft_groups=%u affine=%d prebound=%d parent_offscreen=%d region=%u,%u,%u,%u",clip!=nullptr,hard,soft,affine!=nullptr,prebound,parent!=nullptr,replay.region[0],replay.region[1],replay.region[2],replay.region[3]);}
    if(replay.profiling)log("[recovered-submit-parts] prepare_us=%llu copy_map_us=%llu bind_us=%llu draw_us=%llu commands_us=%llu restore_us=%llu meshes=%u vertices=%u ok=%d map_us=%llu validate_us=%llu scene_us=%llu; commands includes copy/bind/draw; one sampled packet",
        replay.parts[0],replay.parts[1],replay.parts[2],replay.parts[3],replay.parts[4],replay.parts[5],replay.profileMeshes,replay.profileVertices,int(ok&&ended&&restored),replay.parts[6],replay.parts[7],replay.parts[8]);
    if(!ok||!ended||!restored)return group_failed();return true;
}
#endif
