// Native compression is never scanned as RGBA. Only explicit CPU pixel
// consumers request a bounded GPU conversion through read_texture_region.
bool compressed_texture_allowed(unsigned format){return format>=1&&format<=14&&(format!=14||extendedTextureFormats);}
Texture* texture_compressed(unsigned w,unsigned h,unsigned format,bool opaque,const uint8_t* blocks,size_t length){
    const auto bytes=compressed_bytes(format,w,h,true);
    if(!compressed_texture_allowed(format)||!blocks||!bytes||length!=compressed_bytes(format,w,h,false))return nullptr;
    SceGxmTextureFormat fmt;
    switch(format){
    case 1:fmt=opaque?SCE_GXM_TEXTURE_FORMAT_UBC1_1BGR:SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR;break;
    case 2:fmt=opaque?SCE_GXM_TEXTURE_FORMAT_UBC2_1BGR:SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR;break;
    case 3:fmt=opaque?SCE_GXM_TEXTURE_FORMAT_UBC3_1BGR:SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR;break;
    case 4:fmt=SCE_GXM_TEXTURE_FORMAT_UBC4_1RRR;opaque=true;break;
    case 5:fmt=SCE_GXM_TEXTURE_FORMAT_SBC4_1RRR;opaque=true;break;
    case 6:fmt=SCE_GXM_TEXTURE_FORMAT_UBC5_GR;opaque=true;break;
    case 7:fmt=SCE_GXM_TEXTURE_FORMAT_SBC5_GR;opaque=true;break;
    case 8:case 9:opaque=opaque||format==8;fmt=opaque?SCE_GXM_TEXTURE_FORMAT_PVRT2BPP_1BGR:SCE_GXM_TEXTURE_FORMAT_PVRT2BPP_ABGR;break;
    case 10:case 11:opaque=opaque||format==10;fmt=opaque?SCE_GXM_TEXTURE_FORMAT_PVRT4BPP_1BGR:SCE_GXM_TEXTURE_FORMAT_PVRT4BPP_ABGR;break;
    case 12:fmt=opaque?SCE_GXM_TEXTURE_FORMAT_PVRTII2BPP_1BGR:SCE_GXM_TEXTURE_FORMAT_PVRTII2BPP_ABGR;break;
    case 13:fmt=opaque?SCE_GXM_TEXTURE_FORMAT_PVRTII4BPP_1BGR:SCE_GXM_TEXTURE_FORMAT_PVRTII4BPP_ABGR;break;
    case 14:fmt=SCE_GXM_TEXTURE_FORMAT_ETC1_1BGR;opaque=true;break;
    default:return nullptr;
    }
    auto* t=new Texture;t->w=w;t->h=h;t->compressed=true;t->opaque=opaque;
    auto m=allocate(bytes);if(!m.p){delete t;return nullptr;}
    t->uid=m.uid;t->pixels=static_cast<uint8_t*>(m.p);t->allocation=m.charge;
    if(!compressed_swizzle(t->pixels,bytes,blocks,length,format,w,h)||
       !check(sceGxmTextureInitSwizzledArbitrary(&t->descriptor,t->pixels,fmt,w,h,0),"CompressedTexture")){
        release(m);delete t;return nullptr;
    }
    sceGxmTextureSetMinFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);
    t->alphaBounds={0,0,w,h,true};
    t->nativeRG=format==6||format==7;
    log("[gxm-native-texture] format=%u size=%ux%u source_bytes=%u gpu_pixel_bytes=%u opaque=%d",format,w,h,unsigned(length),unsigned(bytes),int(opaque));
    return t;
}
