// Included in namespace direct; shares the renderer's context and patcher.
namespace {
struct VideoYuvaResources {
    Memory planes, geometry;
    SceGxmTexture input[4]{};
    SceGxmRenderTarget* target=nullptr;
    SceGxmSyncObject* sync=nullptr;
    unsigned w=0,h=0;
} videoYuva;
struct VideoYuvaQueue {
    Memory slots[3];
    unsigned w=0,h=0;
} videoYuvaQueue;
SceGxmFragmentProgram* videoYuvaProgram=nullptr;
bool videoYuvaProgramFailed=false;
uint64_t videoYuvaSince=0,videoYuvaCopy=0,videoYuvaRender=0;
uint64_t videoYuvaPriorWait=0,videoYuvaSetup=0,videoYuvaMemcpy=0;
unsigned videoYuvaFrames=0;
unsigned videoYuvaMappedFrames=0;
unsigned videoYuvaOverlapFrames=0;
bool videoYuvaPending=false;
bool video_yuva_program(){
    if(videoYuvaProgram)return true;
    if(videoYuvaProgramFailed)return false;
    videoYuvaProgramFailed=true;
    SceGxmShaderPatcherId id{};
    if(!check(sceGxmShaderPatcherRegisterProgram(patcher,reinterpret_cast<const SceGxmProgram*>(video_yuva_f),&id),"VideoYuvaRegister"))return false;
    SceGxmBlendInfo b{};b.colorMask=SCE_GXM_COLOR_MASK_ALL;
    b.colorFunc=b.alphaFunc=SCE_GXM_BLEND_FUNC_NONE;
    b.colorSrc=b.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;
    b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ZERO;
    if(!check(sceGxmShaderPatcherCreateFragmentProgram(patcher,id,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
        SCE_GXM_MULTISAMPLE_NONE,&b,reinterpret_cast<const SceGxmProgram*>(sprite_v),&videoYuvaProgram),"VideoYuvaProgram"))return false;
    log("[video-yuva-gxm] dedicated conversion program ready; original sprite programs unchanged");return true;
}
}
void video_yuva_queue_close(){
    if(active)return;
    wait();
    for(auto& m:videoYuvaQueue.slots)release(m);
    videoYuvaQueue={};
}
bool video_yuva_queue_open(unsigned w,unsigned h,uint8_t** slots,unsigned count){
    if(!ctx||active||!slots||count!=3||!w||!h||w%8||w>1920||h>1088||size_t(w)*h*4>4*1024*1024)return false;
    for(unsigned i=0;i<count;i++)slots[i]=nullptr;
    video_yuva_queue_close();
    for(unsigned i=0;i<count;i++){
        videoYuvaQueue.slots[i]=allocate(size_t(w)*h*4,0,8);
        if(!videoYuvaQueue.slots[i].p){video_yuva_queue_close();return false;}
    }
    videoYuvaQueue.w=w;videoYuvaQueue.h=h;
    for(unsigned i=0;i<count;i++)slots[i]=static_cast<uint8_t*>(videoYuvaQueue.slots[i].p);
    log("[video-yuva-queue] mapped=1 slots=%u payload_bytes=%u regions=%u/%u/%u; owned by renderer, released after producer join",
        count,unsigned(size_t(w)*h*4*count),videoYuvaQueue.slots[0].charge.region,
        videoYuvaQueue.slots[1].charge.region,videoYuvaQueue.slots[2].charge.region);
    return true;
}
void video_yuva_release(){
    if(active)return; // caller closes video outside a render scene
    wait();
    videoYuvaPending=false;
    if(videoYuva.target)sceGxmDestroyRenderTarget(videoYuva.target);
    if(videoYuva.sync)sceGxmSyncObjectDestroy(videoYuva.sync);
    release(videoYuva.planes);release(videoYuva.geometry);videoYuva={};
}
Texture* video_yuva_convert(Texture* existing,unsigned w,unsigned h,const uint8_t* planes,bool overlap){
    // The first candidate uses packed rows. Other dimensions stay on CPU.
    if(!ctx||active||!planes||!w||!h||w%8||w>1920||h>1088||!video_yuva_program())return nullptr;
    const uint64_t start=sceKernelGetProcessTimeWide();
    // A caller may pump twice without drawing. Drain the previous conversion
    // before touching its private staging or descriptors, even in that case.
    if(videoYuvaPending){wait();videoYuvaPending=false;}
    bool mapped=false;
    if(videoYuvaQueue.w==w&&videoYuvaQueue.h==h)
        for(const auto& m:videoYuvaQueue.slots)if(m.p==planes)mapped=true;
    if(videoYuva.w!=w||videoYuva.h!=h){
        video_yuva_release();
        videoYuva.geometry=allocate(sizeof(Vertex)*4,0,8);
        if(!videoYuva.geometry.p){video_yuva_release();return nullptr;}
        SceGxmRenderTargetParams p{};p.width=w;p.height=h;p.scenesPerFrame=1;p.driverMemBlock=-1;
        p.multisampleMode=SCE_GXM_MULTISAMPLE_NONE;
        if(!check(sceGxmCreateRenderTarget(&p,&videoYuva.target),"VideoYuvaTarget")||
           !check(sceGxmSyncObjectCreate(&videoYuva.sync),"VideoYuvaSync")){video_yuva_release();return nullptr;}
        Vertex q[]={{-1,1,0,0,1,1,1,1},{1,1,1,0,1,1,1,1},{-1,-1,0,1,1,1,1,1},{1,-1,1,1,1,1,1,1}};
        sceClibMemcpy(videoYuva.geometry.p,q,sizeof(q));videoYuva.w=w;videoYuva.h=h;
    }
    if(!mapped&&!videoYuva.planes.p){
        videoYuva.planes=allocate(size_t(w)*h*4,0,8);
        if(!videoYuva.planes.p)return nullptr;
    }
    // Only exact live pool slot pointers are eligible. Mapped input remains
    // synchronous; only private copied input may outlive the queue loan.
    const uint8_t* input=mapped?planes:static_cast<const uint8_t*>(videoYuva.planes.p);
    for(unsigned i=0;i<4;i++){
        if(!check(sceGxmTextureInitLinear(&videoYuva.input[i],input+size_t(w)*h*i,
            SCE_GXM_TEXTURE_FORMAT_U8_R,w,h,0),"VideoYuvaPlane"))return nullptr;
        sceGxmTextureSetMinFilter(&videoYuva.input[i],SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetMagFilter(&videoYuva.input[i],SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetUAddrMode(&videoYuva.input[i],SCE_GXM_TEXTURE_ADDR_CLAMP);
        sceGxmTextureSetVAddrMode(&videoYuva.input[i],SCE_GXM_TEXTURE_ADDR_CLAMP);
    }
    const bool reuse=existing&&existing->pixels&&existing->w==w&&existing->h==h&&existing->stride==w;
    Texture* out=reuse?existing:surface_prepare(w,h);
    if(!out)return nullptr;
    if(!reuse){
        if(!check(sceGxmTextureInitLinear(&out->descriptor,out->pixels,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,w,h,0),"VideoYuvaOutput")){surface_abort(out);return nullptr;}
        sceGxmTextureSetMinFilter(&out->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetMagFilter(&out->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetUAddrMode(&out->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);
        sceGxmTextureSetVAddrMode(&out->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);
    }
    // The previous conversion finished before publication. Ordinary display
    // scenes only read its RGBA output, never these private YUV planes. Copy
    // next-frame input while the old display runs, then fence before touching
    // the RGBA output. Resize/release above still fence before freeing buffers.
    const uint64_t staging=sceKernelGetProcessTimeWide();
    if(!mapped)sceClibMemcpy(videoYuva.planes.p,planes,size_t(w)*h*4);
    const uint64_t copied=sceKernelGetProcessTimeWide();
    wait(); // retains protection for every prior RGBA display reader
    const uint64_t waited=sceKernelGetProcessTimeWide();
    SceGxmColorSurface surface{};
    bool ok=check(sceGxmColorSurfaceInit(&surface,SCE_GXM_COLOR_FORMAT_A8B8G8R8,SCE_GXM_COLOR_SURFACE_LINEAR,
        SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,w,h,w,out->pixels),"VideoYuvaSurface");
    if(ok)ok=check(sceGxmBeginScene(ctx,0,videoYuva.target,nullptr,nullptr,videoYuva.sync,&surface,nullptr),"VideoYuvaBegin");
    if(ok){
        sceGxmSetViewport(ctx,w*.5f,w*.5f,h*.5f,-float(h)*.5f,.5f,.5f);
        sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
        sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
        sceGxmSetVertexProgram(ctx,vp);sceGxmSetFragmentProgram(ctx,videoYuvaProgram);
        for(unsigned i=0;i<4;i++)sceGxmSetFragmentTexture(ctx,i,&videoYuva.input[i]);
        sceGxmSetVertexStream(ctx,0,videoYuva.geometry.p);
        ok=check(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6),"VideoYuvaDraw");
        const bool ended=check(sceGxmEndScene(ctx,nullptr,nullptr),"VideoYuvaEnd");
        ok=ok&&ended;
#ifdef DIRECT_DEFERRED_FINISH_PROBE
        // Only copied inputs may outlive the queue loan. First publication and
        // mapped inputs stay synchronous. begin/update/destroy/readback drain
        // gpuPending; CPU surface views also explicitly wait in the bridge.
        if(ok&&overlap&&!mapped&&reuse){gpuPending=true;videoYuvaPending=true;}
        else
#endif
        sceGxmFinish(ctx);
    }
    boundProgram=nullptr;boundImage=boundRule=nullptr;
    if(!ok){if(!reuse)destroy(out);return nullptr;}
    out->alphaBounds={};out->opaque=false;out->opaqueTiles.clear();
    const auto done=sceKernelGetProcessTimeWide();
    if(!videoYuvaSince)videoYuvaSince=start;
    videoYuvaCopy+=waited-start;videoYuvaRender+=done-waited;++videoYuvaFrames;
    videoYuvaPriorWait+=waited-copied;videoYuvaSetup+=staging-start;videoYuvaMemcpy+=copied-staging;
    videoYuvaMappedFrames+=mapped;
    videoYuvaOverlapFrames+=videoYuvaPending;
    if(done-videoYuvaSince>=5000000){
        log("[video-yuva-gxm] frames=%u stage_avg_us=%llu gpu_wait_avg_us=%llu; new video frames only, staging includes prior fence/allocation",
            videoYuvaFrames,(unsigned long long)(videoYuvaCopy/videoYuvaFrames),(unsigned long long)(videoYuvaRender/videoYuvaFrames));
        log("[video-yuva-stage] frames=%u prior_wait_avg_us=%llu setup_avg_us=%llu memcpy_avg_us=%llu mapped_frames=%u",
            videoYuvaFrames,(unsigned long long)(videoYuvaPriorWait/videoYuvaFrames),
            (unsigned long long)(videoYuvaSetup/videoYuvaFrames),(unsigned long long)(videoYuvaMemcpy/videoYuvaFrames),videoYuvaMappedFrames);
        log("[video-yuva-completion] deferred_frames=%u; deferred GPU time is submission only; frame begin or CPU readback drains completion",videoYuvaOverlapFrames);
        videoYuvaSince=done;videoYuvaCopy=videoYuvaRender=0;videoYuvaFrames=0;
        videoYuvaPriorWait=videoYuvaSetup=videoYuvaMemcpy=0;
        videoYuvaMappedFrames=0;
        videoYuvaOverlapFrames=0;
    }
    return out;
}
bool video_yuva_self_test(){
    static int checked=-1;
    if(checked>=0)return checked;
    checked=0;constexpr unsigned w=64,h=32,n=w*h;
    std::vector<uint8_t> planes(n*4),reference(n*4),alpha(n);
    Texture* out=nullptr;unsigned rgbMax=0,alphaMax=0;uint8_t* slots[3]{};
    for(unsigned pass=0;pass<7;pass++){
        if(pass==4&&!video_yuva_queue_open(w,h,slots,3)){
            destroy(out);video_yuva_release();log("[video-yuva-self-test] mapped pool allocation failed enabled=0");return false;
        }
        for(unsigned i=0;i<n;i++){
            planes[i]=uint8_t(i*37+pass*13);planes[n+i]=uint8_t(i*53+pass*47);
            planes[2*n+i]=uint8_t(i*71+pass*31);planes[3*n+i]=uint8_t(i+pass*67);
        }
        for(unsigned y=0;y<h;y++){
            host_video_gray_row(planes.data()+3*n+y*w,alpha.data()+y*w,w);
            host_video_yuv444_row(planes.data()+y*w,planes.data()+n+y*w,planes.data()+2*n+y*w,
                alpha.data()+y*w,reference.data()+4*y*w,w);
        }
        const uint8_t* input=planes.data();
        if(pass>=4){input=slots[pass-4];sceClibMemcpy(slots[pass-4],planes.data(),n*4);}
        auto* next=video_yuva_convert(out,w,h,input);
        if(!next){destroy(out);video_yuva_release();video_yuva_queue_close();log("[video-yuva-self-test] allocation/draw failed enabled=0");return false;}
        out=next;
        for(unsigned i=0;i<n*4;i++){
            unsigned d=unsigned(std::abs(int(out->pixels[i])-int(reference[i])));
            if(i%4==3)alphaMax=std::max(alphaMax,d);else rgbMax=std::max(rgbMax,d);
        }
    }
    destroy(out);video_yuva_release();video_yuva_queue_close();checked=rgbMax<=1&&alphaMax==0;
    log("[video-yuva-self-test] pixels=%u rgb_max=%u alpha_max=%u enabled=%d mapped_slots=3; offscreen memory, unreadable emulator memory falls back to CPU",
        n*7,rgbMax,alphaMax,checked);return checked;
}
