// Opt-in external assets only; no game resources are packaged in the VPK.
extern "C" void* art3m1s_texture_study_decode(const uint8_t*,size_t,uint32_t*,uint32_t*,const uint8_t**);
extern "C" void art3m1s_texture_study_free(void*);
namespace texture_study {
struct Layer{char name[32]{};unsigned w=0,h=0,role=0,opaque=0;float x=0,y=0,dw=0,dh=0;};
const char* modes[]={"png","rgba","bc3","bc3psv","pvr1","pvr2","pvr2tw"};
std::vector<uint8_t> read(const std::string& path){
    FILE* f=std::fopen(path.c_str(),"rb");if(!f)return {};
    std::fseek(f,0,SEEK_END);const long n=std::ftell(f);std::rewind(f);
    if(n<=0||n>16*1024*1024){std::fclose(f);return {};}
    std::vector<uint8_t> b(n);const auto got=std::fread(b.data(),1,b.size(),f);std::fclose(f);
    if(got!=b.size())b.clear();return b;
}
void save(const std::string& path,const std::vector<uint8_t>& b){FILE* f=std::fopen(path.c_str(),"wb");if(f){std::fwrite(b.data(),1,b.size(),f);std::fclose(f);}}
Texture* packed(const Layer& l,unsigned mode,const std::vector<uint8_t>& b){
    const unsigned pw=bc3_extent(l.w),ph=bc3_extent(l.h);
    const size_t expected=size_t(pw)*ph/(mode>=4?2:1);
    if(b.size()!=expected)return nullptr;
    auto* t=new Texture;auto m=allocate(expected);if(!m.p){delete t;return nullptr;}
    t->uid=m.uid;t->pixels=(uint8_t*)m.p;t->allocation=m.charge;t->compressed=true;
    t->w=mode>=4?pw:l.w;t->h=mode>=4?ph:l.h;
    sceClibMemcpy(t->pixels,b.data(),expected);
    const auto format=mode==3?SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR:mode==4?SCE_GXM_TEXTURE_FORMAT_PVRT4BPP_ABGR:SCE_GXM_TEXTURE_FORMAT_PVRTII4BPP_ABGR;
    const int result=mode==5?sceGxmTextureInitLinear(&t->descriptor,t->pixels,format,pw,ph,0):
        sceGxmTextureInitSwizzledArbitrary(&t->descriptor,t->pixels,format,t->w,t->h,0);
    if(result<0){log("[texture-study] init format=%s error=%08x",modes[mode],unsigned(result));release(m);delete t;return nullptr;}
    sceGxmTextureSetMinFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);sceGxmTextureSetMagFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);sceGxmTextureSetVAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);
    t->opaque=l.opaque!=0;t->alphaBounds={0,0,t->w,t->h,true};return t;
}
Texture* load(const std::string& root,const Layer& l,unsigned mode,unsigned round,FILE* csv,const char* rawSuffix=nullptr){
    const auto start=sceKernelGetProcessTimeWide();auto bytes=read(root+"/"+l.name+"."+(rawSuffix?rawSuffix:modes[mode]));
    const auto readEnd=sceKernelGetProcessTimeWide();Texture* t=nullptr;uint64_t decodeUs=0,alphaUs=0,uploadUs=0;size_t cpuPeak=bytes.size();
    if(mode<=1){
        const uint8_t* pixels=bytes.data();void* handle=nullptr;uint32_t w=l.w,h=l.h;
        if(mode==0){handle=art3m1s_texture_study_decode(bytes.data(),bytes.size(),&w,&h,&pixels);if(!handle)pixels=nullptr;}
        if(mode==1&&bytes.size()!=size_t(w)*h*4)pixels=nullptr;
        const auto decoded=sceKernelGetProcessTimeWide();decodeUs=decoded-readEnd;
        std::vector<uint8_t> proof(32+size_t((w+63)/64)*((h+63)/64));
        const bool valid=pixels&&w==l.w&&h==l.h&&prepare_opacity(w,h,pixels,proof.data(),proof.size());
        const auto alpha=sceKernelGetProcessTimeWide();alphaUs=alpha-decoded;
        if(valid)t=texture(w,h,pixels,proof.data(),proof.size());
        uploadUs=sceKernelGetProcessTimeWide()-alpha;
        cpuPeak+=mode==0?size_t(w)*h*4:0;cpuPeak+=proof.size();
        if(handle)art3m1s_texture_study_free(handle);
    }else{
        const auto before=sceKernelGetProcessTimeWide();
        t=mode==2?texture_bc3(l.w,l.h,bytes.data(),bytes.size()):packed(l,mode,bytes);
        uploadUs=sceKernelGetProcessTimeWide()-before;
        if(t)t->opaque=l.opaque!=0;
    }
    const auto elapsed=sceKernelGetProcessTimeWide()-start;
    if(csv){std::fprintf(csv,"%s,%s,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%d\n",l.name,modes[mode],round,l.w,l.h,
        (unsigned long long)(readEnd-start),(unsigned long long)decodeUs,(unsigned long long)alphaUs,(unsigned long long)uploadUs,
        (unsigned long long)elapsed,unsigned(bytes.size()),unsigned(cpuPeak),t?unsigned(t->allocation.bytes):0,int(t!=nullptr));std::fflush(csv);}
    return t;
}
void release_all(std::vector<Texture*>& textures){wait();for(auto* t:textures)destroy(t);textures.clear();wait();collect();}
void scene(const std::vector<Layer>& layers,const std::vector<Texture*>& textures,unsigned frame,float offset=0,float scale=1){
    const float p=float(frame%120)/120;const float fade=frame>=120?.55f:1.f;
    for(size_t i=0;i<layers.size();++i){const auto& l=layers[i];auto* t=textures[i];if(!t)continue;
        float a=l.role==2?1-p:l.role==3?p:1;if(l.role)a*=fade;
        const float pan=l.role?float(frame%120)*.08f:0;
        const float x=offset+(l.x+pan)*scale,y=l.y*scale,w=l.dw*scale,h=l.dh*scale;
        const float u=float(l.w)/t->w,v=float(l.h)/t->h;
        Vertex q[]={{x,y,0,0,1,1,1,a},{x+w,y,u,0,1,1,1,a},{x,y+h,0,v,1,1,1,a},{x+w,y+h,u,v,1,1,1,a}};
        draw_quad(t,q);
    }
}
}
bool texture_study_demo(const std::string& root,bool automatic){
    using namespace texture_study;
    FILE* input=std::fopen((root+"/texture-study.scene").c_str(),"r");if(!input)return false;
    unsigned count=0;bool valid=std::fscanf(input,"%u",&count)==1&&count>0&&count<=8;std::vector<Layer> layers(valid?count:0);
    for(auto& l:layers){valid=valid&&std::fscanf(input,"%31s %u %u %f %f %f %f %u %u",l.name,&l.w,&l.h,&l.x,&l.y,&l.dw,&l.dh,&l.role,&l.opaque)==9;
        valid=valid&&l.w>0&&l.h>0&&l.w<=2048&&l.h<=2048&&l.dw>0&&l.dh>0&&l.role<=3&&std::strchr(l.name,'/')==nullptr&&std::strchr(l.name,'.')==nullptr;}
    std::fclose(input);if(!valid)return false;
    const int cpu=scePowerGetArmClockFrequency(),gpu=scePowerGetGpuClockFrequency();
    scePowerSetArmClockFrequency(333);scePowerSetGpuClockFrequency(111);startup_validation_display(true);
    log("[texture-study] begin root=%s cpu=%d gpu=%d automatic=%d",root.c_str(),scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency(),int(automatic));
    FILE* csv=automatic?std::fopen((root+"/timings.csv").c_str(),"w"):nullptr;
    if(csv)std::fprintf(csv,"layer,format,round,width,height,read_us,decode_us,alpha_us,upload_us,total_us,file_bytes,cpu_buffer_peak,gpu_alloc_bytes,ok\n");
    std::vector<Texture*> textures,reference;
    if(automatic){
        for(unsigned mode=0;mode<7;++mode){
            for(unsigned round=0;round<3;++round){
                release_all(textures);
                for(const auto& l:layers){sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);textures.push_back(load(root,l,mode,round,csv));}
            }
            bool ok=std::all_of(textures.begin(),textures.end(),[](Texture* t){return t!=nullptr;});
            uint64_t frameUs=0,maxUs=0;
            for(unsigned frame=0;frame<150&&ok;++frame){
                sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);const auto start=sceKernelGetProcessTimeWide();begin();scene(layers,textures,frame);end();wait();
                const auto us=sceKernelGetProcessTimeWide()-start;frameUs+=us;maxUs=std::max(maxUs,uint64_t(us));
                if(frame==0||frame==45||frame==90||frame==135){std::vector<uint8_t> rgba(960*544*4);if(readback(960,544,rgba.data()))save(root+"/capture-"+modes[mode]+"-"+std::to_string(frame)+".rgba",rgba);}
            }
            log("[texture-study] rendered root=%s mode=%s ok=%d frames=150 mean_us=%llu max_us=%llu",root.c_str(),modes[mode],int(ok),(unsigned long long)(frameUs/150),(unsigned long long)maxUs);
            if(mode>=2&&ok){
                release_all(textures);
                const char* suffix=mode<4?"bc3.png":mode==4?"pvr1.png":"pvr2.png";
                for(const auto& l:layers)textures.push_back(load(root,l,0,0,nullptr,suffix));
                if(std::all_of(textures.begin(),textures.end(),[](Texture* t){return t!=nullptr;}))
                    for(unsigned frame:{0u,45u,90u,135u}){
                        begin();scene(layers,textures,frame);end();wait();std::vector<uint8_t> rgba(960*544*4);
                        if(readback(960,544,rgba.data()))save(root+"/reference-"+modes[mode]+"-"+std::to_string(frame)+".rgba",rgba);
                    }
            }
        }
    }else{
        for(const auto& l:layers)reference.push_back(load(root,l,0,0,nullptr));
        unsigned mode=3,frame=0,oldMode=99;uint32_t previous=SCE_CTRL_CIRCLE;
        for(;;){
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);const auto pressed=pad.buttons&~previous;previous=pad.buttons;
            if(pressed&SCE_CTRL_CROSS)break;if(pressed&SCE_CTRL_RIGHT)mode=(mode+1)%7;if(pressed&SCE_CTRL_LEFT)mode=(mode+6)%7;if(pressed&SCE_CTRL_CIRCLE)frame=(frame+60)%180;
            if(mode!=oldMode){release_all(textures);for(const auto& l:layers)textures.push_back(load(root,l,mode,0,nullptr));oldMode=mode;}
            begin();scene(layers,reference,frame,0,.5f);scene(layers,textures,frame,480,.5f);
            menu_text(12,330,22,"PNG reference");menu_text(492,330,22,modes[mode]);menu_text(12,380,20,"Left/Right: format   Circle: face/fade   Cross: exit");end();wait();
        }
    }
    if(csv)std::fclose(csv);release_all(textures);release_all(reference);
    scePowerSetArmClockFrequency(cpu);scePowerSetGpuClockFrequency(gpu);
    log("[texture-study] complete root=%s restored_cpu=%d restored_gpu=%d",root.c_str(),scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency());return true;
}
