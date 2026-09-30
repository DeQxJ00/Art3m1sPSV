// Render-thread-only, no presentation or frame-arena reset. Ordinary draws
// never use this path: it serves explicit mask composition and hit testing.
static bool read_texture_region_impl(Texture* t,unsigned x,unsigned y,unsigned w,unsigned h,uint8_t* out,size_t length){
    if(!ctx||!t||!out||!w||!h||x>=t->w||y>=t->h||w>t->w-x||h>t->h-y||length!=size_t(w)*h*4)return false;
    constexpr unsigned tile=256;
    const unsigned tiles=((w+tile-1)/tile)*((h+tile-1)/tile);
    if(vertexUsed+size_t(tiles)*4>vertexCapacity)return false;
    const bool wasActive=active;auto* parent=activeOffscreen;
    if(active)finish_scene_for_target_change(true);
    else wait();
    const auto stats=frameStats;const auto timing=effectFrameTiming;
    Offscreen scratch;bool ok=create_offscreen(scratch,tile,tile);
    // Shallow descriptor only, owns no memory. Use the exact-copy shader:
    // image_f premultiplies even when blending is disabled.
    Texture source;source.descriptor=t->descriptor;source.w=t->w;source.h=t->h;
    source.nativeRG=t->nativeRG;source.opaque=true;source.alphaBounds={0,0,t->w,t->h,true};
    sceGxmTextureSetMinFilter(&source.descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&source.descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    for(unsigned ty=0;ok&&ty<h;ty+=tile)for(unsigned tx=0;ok&&tx<w;tx+=tile){
        const unsigned rw=std::min(tile,w-tx),rh=std::min(tile,h-ty);
        if(!resume_target(&scratch)){ok=false;break;}
        const float right=960.f*rw/tile,bottom=544.f*rh/tile;
        const float u0=float(x+tx)/t->w,v0=float(y+ty)/t->h,u1=float(x+tx+rw)/t->w,v1=float(y+ty+rh)/t->h;
        Vertex q[]={{0,0,u0,v0,1,1,1,1},{right,0,u1,v0,1,1,1,1},{0,bottom,u0,v1,1,1,1,1},{right,bottom,u1,v1,1,1,1,1}};
        BuiltinEffects copy;
        const auto draws=frameStats.draws;draw_builtin(&source,q,4,false,10,nullptr,nullptr,copy);finish_scene_for_target_change(true);
        if(frameStats.draws==draws){ok=false;break;}
        const uint8_t* pixels=scratch.image->pixels;
        for(unsigned row=0;row<rh;++row)
            sceClibMemcpy(out+(size_t(ty+row)*w+tx)*4,pixels+size_t(row)*tile*4,rw*4);
    }
    activeOffscreen=nullptr;
    if(scratch.target)sceGxmDestroyRenderTarget(scratch.target);
    if(scratch.sync)sceGxmSyncObjectDestroy(scratch.sync);
    destroy(scratch.image);
    if(wasActive&&!resume_target(parent))ok=false;
    frameStats=stats;effectFrameTiming=timing;
    return ok;
}

bool read_texture_region(Texture* t,unsigned x,unsigned y,unsigned w,unsigned h,uint8_t* out,size_t length){
    static int supported=-1;
    if(supported<0){
        uint8_t expected[8*8*4],observed[sizeof(expected)]{};
        for(unsigned i=0;i<64;++i){expected[4*i]=uint8_t(i*3);expected[4*i+1]=83;expected[4*i+2]=177;expected[4*i+3]=uint8_t(i*4);}
        auto* probe=texture(8,8,expected);
        if(!probe)return false; // Allocation pressure is not a permanent capability failure.
        bool ok=probe&&read_texture_region_impl(probe,0,0,8,8,observed,sizeof(observed));
        unsigned error=0;for(unsigned i=0;i<sizeof(expected);++i)error=std::max(error,unsigned(std::abs(int(expected[i])-int(observed[i]))));
        destroy(probe);
        if(!ok)return false;
        supported=error<=1;
        log("[gxm-texture-readback] self_test=%d max_delta=%u; emulator requires surface synchronization",supported,error);
    }
    return supported>0&&read_texture_region_impl(t,x,y,w,h,out,length);
}
