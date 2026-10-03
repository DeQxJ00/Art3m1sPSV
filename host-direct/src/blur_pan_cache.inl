// Render-thread only. Verified coordinate contracts share one bounded pair of
// surfaces; shader-specific metadata only describes the sampling footprint.
namespace {
Offscreen blurPanTargets[2];
EffectPanKey blurPanKey,blurPanPending;
bool blurPanValid=false,blurPanPendingValid=false,blurPanClaimed=false;
bool blurPanAllowed=true,blurPanDisabled=false;
unsigned blurPanOutput=0,blurPanBuilds=0,blurPanHits=0;
float blurPanX=0,blurPanY=0;
uint64_t blurPanUsed=0,blurPanPoll=0;
EffectPanAdmission blurPanAdmission;
unsigned blurPanRequestedWidth=0,blurPanRequestedHeight=0;
uint32_t blurPanFree=0;
uint32_t effect_pan_free(){
    SceKernelFreeMemorySizeInfo info{};info.size=sizeof(info);
    return sceKernelGetFreeMemorySize(&info)>=0?info.size_cdram:0;
}
void blur_pan_clear(){
    // Outside a scene, after a fence. Never release a queued reader.
    blurPanValid=false;blurPanPendingValid=false;
    for(auto& o:blurPanTargets)if(o.image){
        sceGxmDestroyRenderTarget(o.target);sceGxmSyncObjectDestroy(o.sync);
        auto* image=o.image;release({image->uid,image->pixels,0,image->allocation});delete image;o={};
    }
}
void blur_pan_begin(){
    blurPanClaimed=false;
    const auto now=sceKernelGetProcessTimeWide();
    if(now-blurPanPoll>=1000000){
        blurPanPoll=now;SceIoStat st{};
        const bool disabled=diagnostic_stat("ux0:data/art3m1s-gxm/effect-pan-cache.off",&st)>=0;
        if(disabled!=blurPanDisabled){blurPanDisabled=disabled;log("[effect-pan-cache] enabled=%d",int(!disabled));}
    }
    blurPanFree=effect_pan_free();
    const bool pressure=blurPanFree<2*1024*1024;
    if(blurPanTargets[0].image&&(now-blurPanUsed>2000000||blurPanDisabled||pressure)){
        blur_pan_clear();blurPanFree=effect_pan_free();
        if(pressure)blurPanAdmission.deny(blurPanFree);
        log("[effect-pan-cache] released reason=%s",pressure?"memory-pressure":"inactive");
    }
}
struct PanPass {CustomDraw adjusted;float rx=0,ry=0;};
bool effect_pan_parameters(const CustomDraw& d,unsigned kind,const EffectPanGeometry& geometry,PanPass& out){
    if(!d.program||d.program>64||!externalPrograms[d.program-1]||kind<1||kind>6)return false;
    unsigned stepIndex=128,offsetIndex=128,weightsIndex=128;
    const char* stepName=kind==1?"width":kind==2?"height":"size";
    for(const auto& u:externalPrograms[d.program-1]->uniforms){
        const auto* name=sceGxmProgramParameterGetName(u.parameter);
        if(!std::strcmp(name,stepName)&&u.count==1)stepIndex=u.offset;
        if(!std::strcmp(name,"offset")&&u.count==1)offsetIndex=u.offset;
        if(!std::strcmp(name,"weights")&&u.count==8)weightsIndex=u.offset;
        if(!std::strcmp(name,"alpha")&&(u.count!=1||d.values[u.offset]!=1))return false;
    }
    out.adjusted=d;if(kind==6)return true;
    if(stepIndex>=128)return false;
    float radius=0;
    if(kind<=2){
        if(weightsIndex>120||d.values[stepIndex]<0)return false;
        for(unsigned i=0;i<8;++i){const float w=d.values[weightsIndex+i];if(w<0||w>1)return false;if(w>0)radius=float(i);}
    }else {if(offsetIndex>=128)return false;radius=std::abs(d.values[offsetIndex]);}
    const float uv=std::abs(d.values[stepIndex])*radius;
    if(kind!=2&&kind!=5)out.rx=std::ceil(uv*960)+1;
    if(kind!=1&&kind!=4)out.ry=std::ceil(uv*544)+1;
    // Both domain dimensions use the same scale, including Kawase's scalar size.
    out.adjusted.values[stepIndex]*=960/geometry.extentX;
    return true;
}
void effect_pan_sources(Texture* const* textures,const EffectDraw* sources,unsigned n,float sx,float sy,
                        float fx=1,float fy=1,float ox=0,float oy=0){
    for(unsigned i=0;i<n;++i){const auto& s=sources[i];const auto* m=s.transform;const auto* u=s.uv;const auto* c=s.tint;
        const float x=(m[4]*sx)*fx+ox,y=(m[5]*sy)*fy+oy;
        const float w=m[0]*s.quad[0]*sx*fx,h=m[3]*s.quad[1]*sy*fy;
        Vertex q[]={{x,y,u[0],u[1],c[0],c[1],c[2],c[3]},{x+w,y,u[0]+u[2],u[1],c[0],c[1],c[2],c[3]},
            {x,y+h,u[0],u[1]+u[3],c[0],c[1],c[2],c[3]},{x+w,y+h,u[0]+u[2],u[1]+u[3],c[0],c[1],c[2],c[3]}};
        if(s.blend==0)draw_quad(textures[i],q);
        else {BuiltinEffects e{};e.flags[0]=5;draw_builtin(textures[i],q,4,false,5,nullptr,nullptr,e);}
    }
}
void effect_pan_bands(Texture* t,const CustomDraw& p,float rx,float ry,unsigned blend){
    rx=std::min(std::ceil(rx),480.f);ry=std::min(std::ceil(ry),272.f);
    const float bands[][4]={{0,0,960,ry},{0,544-ry,960,544},{0,ry,rx,544-ry},{960-rx,ry,960,544-ry}};
    for(const auto& b:bands){if(b[2]<=b[0]||b[3]<=b[1])continue;
        Vertex q[]={{b[0],b[1],b[0]/960,b[1]/544,1,1,1,1},{b[2],b[1],b[2]/960,b[1]/544,1,1,1,1},
            {b[0],b[3],b[0]/960,b[3]/544,1,1,1,1},{b[2],b[3],b[2]/960,b[3]/544,1,1,1,1}};
        draw_external(t,q,4,false,blend,nullptr,nullptr,nullptr,p);
    }
}
// Preserve the original screen-clamped convolution on the narrow border.
// Earlier passes need a larger band, because later filters sample beyond it.
// Reuse normal group scratch; no additional resident border-cache surfaces.
bool effect_pan_border(Texture* const* textures,const EffectDraw* sources,unsigned n,const EffectDraw* passes,
                       const PanPass* plans,unsigned count,float sx,float sy,float rx,float ry){
    if(rx==0&&ry==0)return true;
    if(!create_offscreen(externalFilterScratch)||!group_begin())return false;
    effect_pan_sources(textures,sources,n,sx,sy);
    auto& g=groups[groupDepth-1];finish_scene_for_target_change();
    float remainingX=rx,remainingY=ry;
    for(unsigned i=0;i<count;++i){
        remainingX-=plans[i].rx;remainingY-=plans[i].ry;
        if(i+1==count){
            --groupDepth;if(!resume_target(current_offscreen())){group_failed();return false;}
            effect_pan_bands(g.color.image,passes[i].custom,rx,ry,5);
        }else{
            if(!resume_target(&externalFilterScratch)){--groupDepth;resume_target(current_offscreen());group_failed();return false;}
            effect_pan_bands(g.color.image,passes[i].custom,rx+remainingX,ry+remainingY,10);
            finish_scene_for_target_change();std::swap(g.color,externalFilterScratch);
        }
    }
    return true;
}
}
void prepare_effect_cache(void* context,size_t (*reclaim)(void*,size_t)){
    if(active||!blurPanRequestedWidth)return;
    const unsigned width=blurPanRequestedWidth,height=blurPanRequestedHeight;
    blurPanRequestedWidth=blurPanRequestedHeight=0;
    const auto now=sceKernelGetProcessTimeWide();
    if(!textureCachesAllowed||blurPanDisabled||!blurPanAllowed||now-blurPanAdmission.lastUse>2000000)return;
    // The callback must never re-enter Rust while a render call borrows runtime.
    wait();
    if(blurPanTargets[0].image&&(blurPanTargets[0].image->w!=width||blurPanTargets[0].image->h!=height))blur_pan_clear();
    const uint32_t storage=EffectPanAdmission::storage(width,height);
    auto free=effect_pan_free();const auto before=free;
    const unsigned present=(blurPanTargets[0].image?1:0)|(blurPanTargets[1].image?2:0);
    const auto allocation=allocate_effect_pan_pair(storage,present,effect_pan_free,
        [&](unsigned slot){
            int32_t error=0;
            const bool ready=create_offscreen(blurPanTargets[slot],width,height,false,true,&error);
            return EffectPanCreateResult{ready,error};
        },[&](uint32_t bytes){return reclaim?reclaim(context,bytes):size_t(0);});
    if(!allocation.ready){
        blur_pan_clear();free=effect_pan_free();blurPanAdmission.deny(free);
    }else {blurPanAdmission.ready();blurPanUsed=now;}
    blurPanFree=effect_pan_free();
    log("[effect-pan-memory] ready=%d target=%ux%u storage=%u free_before=%u requested=%u reclaimed=%u free_after=%u retries=%u failed_slot=%d alloc_error=%08x headroom_reclaims=%u remaining_deficit=%u elapsed_us=%llu cdram_only=1",
        int(allocation.ready),width,height,storage,before,allocation.requested,unsigned(allocation.reclaimed),blurPanFree,
        allocation.retries,allocation.failedSlot,unsigned(allocation.cdramError),allocation.headroomReclaims,allocation.remainingDeficit,
        (unsigned long long)(sceKernelGetProcessTimeWide()-now));
}
bool draw_cached_effect(Texture* const* textures,const EffectDraw* sources,unsigned n,const EffectDraw* passes,
    const unsigned* kinds,unsigned count,uint64_t revision,float sx,float sy,bool half){
    if(!textureCachesAllowed||!blurPanAllowed||blurPanDisabled||!active||!textures||!sources||!n||n>32||!passes||!kinds||!count||count>16
        ||!std::isfinite(sx)||!std::isfinite(sy)||sx<=0||sy<=0||!init_builtins())return false;
    EffectPanKey key;key.shaders=revision;key.sourceCount=n;key.count=count;key.half=half;key.sx=sx;key.sy=sy;
    auto fullClip=[&](const EffectDraw& d){return !d.hasClip||(std::isfinite(d.clip[0])&&std::isfinite(d.clip[1])
        &&std::isfinite(d.clip[2])&&std::isfinite(d.clip[3])&&d.clip[0]<=0&&d.clip[1]<=0
        &&(d.clip[0]+d.clip[2])*sx>=960&&(d.clip[1]+d.clip[3])*sy>=544);};
    const float x=sources[0].transform[4]*sx,y=sources[0].transform[5]*sy;
    for(unsigned i=0;i<n;++i){const auto& s=sources[i];auto* t=textures[i];const auto* m=s.transform;
        if(!t||s.mask||s.mesh||s.custom.program||s.custom.userTexture||(s.blend!=0&&s.blend!=5)||!fullClip(s)
           ||m[1]!=0||m[2]!=0||m[0]<=0||m[3]<=0||s.quad[0]<=0||s.quad[1]<=0)return false;
        for(float v:s.effects.flags)if(v!=0)return false;
        if(s.blend==5)for(float v:s.tint)if(v!=1)return false;
        auto& k=key.sources[i];k.texture=s.texture;k.content=t->contentRevision;k.blend=s.blend;
        k.values={m[0],m[1],m[2],m[3],m[4]-sources[0].transform[4],m[5]-sources[0].transform[5],
            s.quad[0],s.quad[1],s.uv[0],s.uv[1],s.uv[2],s.uv[3],s.tint[0],s.tint[1],s.tint[2],s.tint[3]};
        for(float v:k.values)if(!std::isfinite(v))return false;
    }
    if(half){
        if(n!=1||count<3||!(textures[0]->opaque||textures[0]->opaqueTiles.covers(0,0,1,1)))return false;
        for(float v:sources[0].tint)if(v!=1)return false;
        for(unsigned i=0;i<count;++i)if(kinds[i]<3||kinds[i]>5)return false;
    }
    const auto geometry=half?EffectPanGeometry::legacy():EffectPanGeometry{};
    PanPass plans[16];float rx=0,ry=0;float uv[4];
    for(unsigned i=0;i<count;++i){const auto& p=passes[i];
        if(p.mask||p.custom.userTexture||p.blend!=5||!fullClip(p))return false;
        for(float v:p.tint)if(v!=1)return false;
        key.kinds[i]=kinds[i];key.passes[i].program=p.custom.program;
        for(unsigned j=0;j<128;++j){if(!std::isfinite(p.custom.values[j]))return false;key.passes[i].values[j]=p.custom.values[j];}
        if(!effect_pan_parameters(p.custom,kinds[i],geometry,plans[i]))return false;
        rx+=plans[i].rx;ry+=plans[i].ry;
    }
    // Half-resolution legacy path keeps its established extra bilinear guard.
    const float guardX=half?rx+2*count+8:rx+2,guardY=half?ry+2*count+8:ry+2;
    if(!geometry.crop(0,0,guardX,guardY,uv))return false;
    if(!half&&(rx>0||ry>0)&&(groupDepth>=8||!create_offscreen(externalFilterScratch)||!create_offscreen(groups[groupDepth].color)))return false;
    if(blurPanClaimed)return false; // One resident result; no per-frame slot thrash.
    blurPanClaimed=true;
    const bool hit=blurPanValid&&key==blurPanKey&&geometry.crop(x-blurPanX,y-blurPanY,guardX,guardY,uv);
    if(!hit){
        const auto now=sceKernelGetProcessTimeWide();
        // Moving position is excluded, but changing animation/parameters must
        // settle before paying for a bake. Legacy admission is unchanged.
        if(!half&&(!blurPanPendingValid||!(key==blurPanPending))&&(!blurPanValid||!(key==blurPanKey))){
            blurPanPending=key;blurPanPendingValid=true;return false;
        }
        if(!blurPanTargets[0].image||!blurPanTargets[1].image||blurPanTargets[0].image->w!=geometry.width||blurPanTargets[0].image->h!=geometry.height){
            if(blurPanAdmission.allow(now,blurPanFree)){
                blurPanRequestedWidth=geometry.width;blurPanRequestedHeight=geometry.height;
            }
            return false;
        }
        auto* parent=current_offscreen();finish_scene_for_target_change(true);
        blurPanValid=false;
        if(!resume_target(&blurPanTargets[0])){resume_target(parent);return false;}
        clear_offscreen();
        effect_pan_sources(textures,sources,n,sx,sy,960/geometry.extentX,544/geometry.extentY,
            (geometry.extentX-960)*480/geometry.extentX,(geometry.extentY-544)*272/geometry.extentY);
        finish_scene_for_target_change();unsigned input=0;
        for(unsigned i=0;i<count;++i){const unsigned output=1-input;
            if(!resume_target(&blurPanTargets[output])){resume_target(parent);return false;}
            Vertex q[]={{0,0,0,0,1,1,1,1},{960,0,1,0,1,1,1,1},{0,544,0,1,1,1,1,1},{960,544,1,1,1,1,1,1}};
            const auto before=frameStats.draws;draw_external(blurPanTargets[input].image,q,4,false,10,nullptr,nullptr,nullptr,plans[i].adjusted);
            finish_scene_for_target_change();if(frameStats.draws==before){resume_target(parent);return false;}input=output;
        }
        if(!resume_target(parent)){group_failed();return true;}
        blurPanKey=key;blurPanX=x;blurPanY=y;blurPanOutput=input;blurPanValid=true;
        blurPanPendingValid=false;geometry.crop(0,0,guardX,guardY,uv);++blurPanBuilds;
        log("[effect-pan-cache] build=%u passes=%u layers=%u target=%ux%u radius=%.1f,%.1f tex=%llu content=%llu storage_kib=%u half=%d",
            blurPanBuilds,count,n,geometry.width,geometry.height,rx,ry,(unsigned long long)sources[0].texture,
            (unsigned long long)key.sources[0].content,unsigned((blurPanTargets[0].image->allocation.bytes+blurPanTargets[1].image->allocation.bytes)/1024),int(half));
    }else{
        ++blurPanHits;if(blurPanHits<=3||blurPanHits%300==0)log("[effect-pan-cache] hit=%u builds=%u delta=%.2f,%.2f layers=%u passes=%u",blurPanHits,blurPanBuilds,x-blurPanX,y-blurPanY,n,count);
    }
    blurPanUsed=sceKernelGetProcessTimeWide();
    const float bx=half?0:rx,by=half?0:ry;
    const float x0=bx,y0=by,x1=960-bx,y1=544-by;
    Vertex q[]={{x0,y0,uv[0]+bx/geometry.extentX,uv[1]+by/geometry.extentY,1,1,1,1},
        {x1,y0,uv[2]-bx/geometry.extentX,uv[1]+by/geometry.extentY,1,1,1,1},
        {x0,y1,uv[0]+bx/geometry.extentX,uv[3]-by/geometry.extentY,1,1,1,1},
        {x1,y1,uv[2]-bx/geometry.extentX,uv[3]-by/geometry.extentY,1,1,1,1}};
    BuiltinEffects neutral{};neutral.flags[0]=5;
    draw_builtin(blurPanTargets[blurPanOutput].image,q,4,false,5,nullptr,nullptr,neutral);
    if(!half&&!effect_pan_border(textures,sources,n,passes,plans,count,sx,sy,rx,ry))group_failed();
    return true; // The parent has been drawn; never replay over partial output.
}
bool draw_cached_blur(Texture* texture,const EffectDraw& source,const EffectDraw* passes,unsigned count,uint64_t revision,float sx,float sy){
    if(count>16)return false;unsigned kinds[16];std::fill(kinds,kinds+count,3u);
    return draw_cached_effect(&texture,&source,1,passes,kinds,count,revision,sx,sy,true);
}
