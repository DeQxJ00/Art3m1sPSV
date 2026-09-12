// Included in namespace direct; shares the renderer's context and patcher.
namespace {
struct VideoYuvaResources {
    Memory planes, geometry;
    SceGxmTexture input[4]{};
    SceGxmRenderTarget* target=nullptr;
    SceGxmSyncObject* sync=nullptr;
    unsigned w=0,h=0;
} videoYuva;
SceGxmFragmentProgram* videoYuvaProgram=nullptr;
bool videoYuvaProgramFailed=false;
uint64_t videoYuvaSince=0,videoYuvaCopy=0,videoYuvaRender=0;
unsigned videoYuvaFrames=0;
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
void video_yuva_release(){
    if(active)return; // caller closes video outside a render scene
    wait();
    if(videoYuva.target)sceGxmDestroyRenderTarget(videoYuva.target);
    if(videoYuva.sync)sceGxmSyncObjectDestroy(videoYuva.sync);
    release(videoYuva.planes);release(videoYuva.geometry);videoYuva={};
}
Texture* video_yuva_convert(Texture* existing,unsigned w,unsigned h,const uint8_t* planes){
    // The first candidate uses packed rows. Other dimensions stay on CPU.
    if(!ctx||active||!planes||!w||!h||w%8||w>1920||h>1088||!video_yuva_program())return nullptr;
    const uint64_t start=sceKernelGetProcessTimeWide();
    wait(); // protects prior display readers and the staging/vertex buffers
    if(videoYuva.w!=w||videoYuva.h!=h){
        video_yuva_release();
        videoYuva.planes=allocate(size_t(w)*h*4,0,8);
        videoYuva.geometry=allocate(sizeof(Vertex)*4,0,8);
        if(!videoYuva.planes.p||!videoYuva.geometry.p){video_yuva_release();return nullptr;}
        for(unsigned i=0;i<4;i++){
            if(!check(sceGxmTextureInitLinear(&videoYuva.input[i],static_cast<uint8_t*>(videoYuva.planes.p)+size_t(w)*h*i,
                SCE_GXM_TEXTURE_FORMAT_U8_R,w,h,0),"VideoYuvaPlane")){video_yuva_release();return nullptr;}
            sceGxmTextureSetMinFilter(&videoYuva.input[i],SCE_GXM_TEXTURE_FILTER_POINT);
            sceGxmTextureSetMagFilter(&videoYuva.input[i],SCE_GXM_TEXTURE_FILTER_POINT);
            sceGxmTextureSetUAddrMode(&videoYuva.input[i],SCE_GXM_TEXTURE_ADDR_CLAMP);
            sceGxmTextureSetVAddrMode(&videoYuva.input[i],SCE_GXM_TEXTURE_ADDR_CLAMP);
        }
        SceGxmRenderTargetParams p{};p.width=w;p.height=h;p.scenesPerFrame=1;p.driverMemBlock=-1;
        p.multisampleMode=SCE_GXM_MULTISAMPLE_NONE;
        if(!check(sceGxmCreateRenderTarget(&p,&videoYuva.target),"VideoYuvaTarget")||
           !check(sceGxmSyncObjectCreate(&videoYuva.sync),"VideoYuvaSync")){video_yuva_release();return nullptr;}
        Vertex q[]={{-1,1,0,0,1,1,1,1},{1,1,1,0,1,1,1,1},{-1,-1,0,1,1,1,1,1},{1,-1,1,1,1,1,1,1}};
        sceClibMemcpy(videoYuva.geometry.p,q,sizeof(q));videoYuva.w=w;videoYuva.h=h;
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
    // Input is cached CPU memory; copy only planar bytes, never read GPU output.
    sceClibMemcpy(videoYuva.planes.p,planes,size_t(w)*h*4);
    const uint64_t copied=sceKernelGetProcessTimeWide();
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
        sceGxmFinish(ctx);ok=ok&&ended; // no reuse or publication before completion
    }
    boundProgram=nullptr;boundImage=boundRule=nullptr;
    if(!ok){if(!reuse)destroy(out);return nullptr;}
    out->alphaBounds={};out->opaque=false;out->opaqueTiles.clear();
    const auto done=sceKernelGetProcessTimeWide();
    if(!videoYuvaSince)videoYuvaSince=start;
    videoYuvaCopy+=copied-start;videoYuvaRender+=done-copied;++videoYuvaFrames;
    if(done-videoYuvaSince>=5000000){
        log("[video-yuva-gxm] frames=%u stage_avg_us=%llu gpu_wait_avg_us=%llu; new video frames only, staging includes prior fence/allocation",
            videoYuvaFrames,(unsigned long long)(videoYuvaCopy/videoYuvaFrames),(unsigned long long)(videoYuvaRender/videoYuvaFrames));
        videoYuvaSince=done;videoYuvaCopy=videoYuvaRender=0;videoYuvaFrames=0;
    }
    return out;
}
bool video_yuva_self_test(){
    static int checked=-1;
    if(checked>=0)return checked;
    checked=0;constexpr unsigned w=64,h=32,n=w*h;
    std::vector<uint8_t> planes(n*4),reference(n*4),alpha(n);
    Texture* out=nullptr;unsigned rgbMax=0,alphaMax=0;
    for(unsigned pass=0;pass<4;pass++){
        for(unsigned i=0;i<n;i++){
            planes[i]=uint8_t(i*37+pass*13);planes[n+i]=uint8_t(i*53+pass*47);
            planes[2*n+i]=uint8_t(i*71+pass*31);planes[3*n+i]=uint8_t(i+pass*67);
        }
        for(unsigned y=0;y<h;y++){
            host_video_gray_row(planes.data()+3*n+y*w,alpha.data()+y*w,w);
            host_video_yuv444_row(planes.data()+y*w,planes.data()+n+y*w,planes.data()+2*n+y*w,
                alpha.data()+y*w,reference.data()+4*y*w,w);
        }
        auto* next=video_yuva_convert(out,w,h,planes.data());
        if(!next){destroy(out);video_yuva_release();log("[video-yuva-self-test] allocation/draw failed enabled=0");return false;}
        out=next;
        for(unsigned i=0;i<n*4;i++){
            unsigned d=unsigned(std::abs(int(out->pixels[i])-int(reference[i])));
            if(i%4==3)alphaMax=std::max(alphaMax,d);else rgbMax=std::max(rgbMax,d);
        }
    }
    destroy(out);video_yuva_release();checked=rgbMax<=1&&alphaMax==0;
    log("[video-yuva-self-test] pixels=%u rgb_max=%u alpha_max=%u enabled=%d; offscreen memory, unreadable emulator memory falls back to CPU",
        n*4,rgbMax,alphaMax,checked);return checked;
}
