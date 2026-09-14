bool external_shader_self_test(){
    if(active)return false;
    sceIoMkdir("ux0:data/art3m1s-gxm/games/__shader_probe",0777);
    external_cache_root("ux0:data/art3m1s-gxm/games/__shader_probe/shader-cache");
    const char* cg=R"CG(
uniform sampler2D samplerFore:TEXUNIT0;
uniform sampler2D samplerMask:TEXUNIT1;
uniform sampler2D samplerUser:TEXUNIT3;
uniform float alpha;uniform float weights[2];uniform float3 colorMultiply;uniform float4 art_clip;
float4 main(float2 uv:TEXCOORD0,float4 tint:COLOR0,float2 pixel:TEXCOORD1):COLOR {
 float4 a=tex2D(samplerFore,uv),b=tex2D(samplerUser,uv),m=tex2D(samplerMask,uv);
 float cover=step(art_clip.x,pixel.x)*step(art_clip.y,pixel.y)*(1-step(art_clip.z,pixel.x))*(1-step(art_clip.w,pixel.y));
 return float4(a.r*weights[0]+b.r*weights[1],m.a,colorMultiply.b,alpha)*cover;
})CG";
    auto first=external_compile("pixel_probe","pixel-probe-v1",cg);if(!first){external_compiler_end();return false;}
    auto uniform=[&](unsigned id){return external_uniform(id,"alpha",0,1)&&external_uniform(id,"weights",1,2)&&external_uniform(id,"colorMultiply",3,3);};
    bool ok=uniform(first);
    external_compiler_end();external_release(first);
    // Same source must take the persistent cache path, with compiler unloaded.
    auto id=external_compile("pixel_probe","pixel-probe-v1",cg);ok=ok&&id&&uniform(id);
    if(!id){external_compiler_end();return false;}
    uint8_t a[16],m[16],u[16];for(unsigned i=0;i<4;++i){
        const uint8_t av[]={64,0,0,255},mv[]={0,0,0,128},uv[]={192,0,0,255};
        std::memcpy(a+i*4,av,4);std::memcpy(m+i*4,mv,4);std::memcpy(u+i*4,uv,4);
    }
    auto* fore=texture(2,2,a);auto* mask=texture(2,2,m);auto* user=texture(2,2,u);
    if(!fore||!mask||!user)ok=false;
    std::vector<uint8_t> pixels(960*544*4);
    for(unsigned pass=0;ok&&pass<2;++pass){
        CustomDraw d{};d.program=id;d.values[0]=pass?1.f:0.5f;d.values[1]=0.5f;d.values[2]=0.5f;d.values[5]=pass?0.75f:0.25f;
        Vertex v[]={{200,160,0,0,1,1,1,1},{760,160,1,0,1,1,1,1},{200,400,0,1,1,1,1,1},{760,400,1,1,1,1,1,1}};
        float clip[]={300,200,650,350};begin();rect(0,0,960,544,0x203040ff);
        draw_external(fore,v,4,false,10,clip,mask,user,d);end();wait();
        ok=readback(960,544,pixels.data());const auto* p=pixels.data()+(280*960+480)*4;
        const auto* outside=pixels.data()+(280*960+250)*4;
        auto near=[](int a,int b){return std::abs(a-b)<=3;};
        ok=ok&&near(p[0],128)&&near(p[1],128)&&near(p[2],pass?191:64)&&near(p[3],pass?255:128)&&outside[3]==0;
        log("[external-pixel-test] pass=%u rgba=%u,%u,%u,%u outside_alpha=%u ok=%d",pass,p[0],p[1],p[2],p[3],outside[3],int(ok));
    }
    wait();destroy(fore);destroy(mask);destroy(user);external_release(id);external_compiler_end();
    log("[external-pixel-test] complete ok=%d",int(ok));return ok;
}
