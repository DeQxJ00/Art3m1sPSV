// Opt-in hardware test. Exercises the exact embedded GXP and reflected layout.
struct BundledUniform {const char* name;unsigned offset,count;};
struct BundledEntry {const char* name;const uint8_t* data;size_t size;const BundledUniform* uniforms;size_t count;};
#include "bundled_probe_data.hpp"
bool bundled_shader_self_test(){
    if(active)return false;
    const char* directory="ux0:data/art3m1s-gxm/shader-validation";sceIoMkdir(directory,0777);
    uint8_t a[16],m[16],u[16];for(unsigned i=0;i<4;++i){
        const uint8_t av[]={64,128,192,255},uv[]={192,64,32,128};
        std::memcpy(a+i*4,av,4);std::memcpy(u+i*4,uv,4);std::memset(m+i*4,255,4);
    }
    auto* fore=texture(2,2,a);auto* mask=texture(2,2,m);auto* user=texture(2,2,u);
    std::vector<uint8_t> pattern(64*64*4),pixels(960*544*4);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){auto* p=pattern.data()+(y*64+x)*4;
        p[0]=x*4;p[1]=y*4;p[2]=((x/4+y/4)%2)?224:32;p[3]=255;}
    auto* grid=texture(64,64,pattern.data());bool all=fore&&mask&&user&&grid;unsigned passed=0;
    for(const auto& e:bundledEntries){
        if(!fore||!mask||!user||!grid)break;
        auto id=external_register(e.data,e.size);bool ok=id!=0;CustomDraw d{};d.program=id;
        auto set=[&](const char* name,float v){for(size_t j=0;j<e.count;++j)if(!std::strcmp(e.uniforms[j].name,name))d.values[e.uniforms[j].offset]=v;};
        for(size_t j=0;j<e.count;++j){const auto& u=e.uniforms[j];ok=external_uniform(id,u.name,u.offset,u.count)&&ok;
            if(!std::strcmp(u.name,"colorMultiply"))for(unsigned k=0;k<u.count;++k)d.values[u.offset+k]=1;
            if(!std::strcmp(u.name,"weights"))for(unsigned k=0;k<u.count;++k)d.values[u.offset+k]=k?0.05f:0.3f;}
        set("alpha",1);set("param",0.7f);set("size",0.16f);set("ratio",1);set("offset",1);
        set("width",0.02f);set("height",0.02f);set("centerx",0.5f);set("centery",0.5f);
        set("radius",0.2f);set("thick",0.15f);set("dark",0.6f);set("ax",0.5f);set("ay",0.5f);
        set("fade",0.8f);set("angle",30);set("inter",1440);set("steps",0.6f);set("dost",0.01f);
        set("red",0.2f);set("green",-0.1f);set("blue",0.1f);
        const std::string name=e.name;
        if(name=="cmul"){set("red",0.7f);set("green",0.5f);set("blue",1.1f);}
        if(name=="rgb"){set("red",-0.2f);set("green",0.1f);set("blue",0.2f);}
        if(name=="sepia"||name=="sepia2"){set("red",name=="sepia"?1.f:0.1f);set("green",0.8f);set("blue",0.6f);}
        if(name=="mosaic")set("size",8);
        const Vertex v[]={{160,110,0,0,1,1,1,1},{800,110,1,0,1,1,1,1},{160,430,0,1,1,1,1,1},{800,430,1,1,1,1,1,1}};
        const float clip[]={200,130,780,410};
        float expect[]={64/255.f,128/255.f,192/255.f},usr[]={192/255.f,64/255.f,32/255.f};
        const float ua=128/255.f,gray=expect[0]*0.298912f+expect[1]*0.586611f+expect[2]*0.114478f;
        for(int c=0;c<3;++c){float f=expect[c],u=usr[c];
            if(name=="add")expect[c]=f+ua*u*0.7f;
            else if(name=="compbr"||name=="compbrc")expect[c]=(f*(1-ua)+std::max(f,u)*ua)*0.7f;
            else if(name=="compdk"||name=="compdkc")expect[c]=(f*(1-ua)+std::min(f,u)*ua)*0.7f;
            else if(name=="mul")expect[c]=f*(f*(1-ua)+u*ua+0.3f);
            else if(name=="screen")expect[c]=1-(1-f)*(1-u*ua);
            else if(name=="gray")expect[c]=gray;
            else if(name=="nega")expect[c]=1-f;
            else if(name=="cadd")expect[c]=f+(c==0?0.2f:c==1?-0.1f:0.1f);
            else if(name=="cmul")expect[c]=f*(c==0?0.7f:c==1?0.5f:1.1f);
            else if(name=="rgb")expect[c]=c==0?0:f+(c==1?0.1f:0.2f);
            else if(name=="sepia")expect[c]=gray*(c==0?1.f:c==1?0.8f:0.6f);
            else if(name=="sepia2")expect[c]=c==0?gray+0.1f:gray*(c==1?0.8f:0.6f);
            else if(name=="dimover")expect[c]=f*0.6f;
        }
        for(unsigned pass=0;id&&pass<3;++pass){
            // Pass 1 verifies masked alpha; pass 2 records spatial effects.
            if(pass==1){set("alpha",0.75f);for(unsigned i=0;i<4;++i)m[i*4+3]=128;update(mask,m,0,0,2,2);}
            if(pass==2){set("alpha",1);for(unsigned i=0;i<4;++i)m[i*4+3]=255;update(mask,m,0,0,2,2);}
            begin();rect(0,0,960,544,0x182334ff);draw_external(pass==2?grid:fore,v,4,false,10,clip,mask,user,d);end();wait();
            bool read=readback(960,544,pixels.data());ok=ok&&read;const auto* p=pixels.data()+(270*960+480)*4;
            const auto* outside=pixels.data()+(270*960+180)*4;
            bool sample=read&&outside[3]==0;
            if(pass<2){float factor=pass?(128/255.f)*0.75f:1.f;float alpha=name=="reset"?(pass?0.75f:1.f):name=="dimhole"?0.f:factor;
                sample=sample&&std::abs(int(p[3])-int(alpha*255+0.5f))<=4;
                for(int c=0;c<3;++c){float expected=expect[c];
                    if(name=="blur_k"||name=="blur_kx"||name=="blur_ky")expected*=factor;
                    if(pass&&name=="add")expected=(c==0?64:c==1?128:192)/255.f+(128/255.f)*ua*usr[c]*0.7f;
                    sample=sample&&std::abs(int(p[c])-int(std::clamp(expected,0.f,1.f)*255+0.5f))<=4;
                }
            }else{
                const auto path=std::string(directory)+"/"+e.name+".ppm";FILE* f=std::fopen(path.c_str(),"wb");
                if(f){std::fprintf(f,"P6\n160 80\n255\n");for(unsigned y=0;y<80;++y)for(unsigned x=0;x<160;++x){
                    const auto* q=pixels.data()+((110+y*4)*960+160+x*4)*4;std::fwrite(q,1,3,f);}std::fclose(f);}
            }
            ok=ok&&sample;log("[bundled-pixel] name=%s pass=%u rgba=%u,%u,%u,%u outside=%u ok=%d",e.name,pass,p[0],p[1],p[2],p[3],outside[3],int(sample));
        }
        all=all&&ok;if(ok)++passed;external_release(id);
    }
    wait();destroy(fore);destroy(mask);destroy(user);destroy(grid);
    log("[bundled-validation] passed=%u total=31 ok=%d",passed,int(all));return all;
}
