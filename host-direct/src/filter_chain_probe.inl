// Opt-in comparison of nested blur/isolation against sequential RGBA passes.
bool filter_chain_self_test(){
    if(active)return false;
    const BundledEntry* entry=nullptr;
    for(const auto& e:bundledEntries)if(!std::strcmp(e.name,"blur_k"))entry=&e;
    if(!entry)return false;
    auto id=external_register(entry->data,entry->size);if(!id)return false;
    bool hadColor[8]{};for(unsigned i=0;i<8;++i)hadColor[i]=groups[i].color.image!=nullptr;
    bool all=true;
    for(size_t j=0;j<entry->count;++j){const auto& u=entry->uniforms[j];all=external_uniform(id,u.name,u.offset,u.count)&&all;}
    std::vector<uint8_t> pixels(31*23*4),reference(960*544*4),candidate(reference.size());
    for(unsigned i=0;i<31*23;++i){pixels[i*4]=uint8_t(i*31);pixels[i*4+1]=uint8_t(i*71);pixels[i*4+2]=uint8_t(i*113);pixels[i*4+3]=uint8_t(i*17);}
    auto* t=texture(31,23,pixels.data());if(!t){external_release(id);return false;}
    EffectDraw neutral{};neutral.blend=5;for(float& v:neutral.tint)v=1;neutral.effects.flags[0]=3;
    EffectDraw passes[3];
    for(unsigned i=0;i<3;++i){passes[i]=neutral;passes[i].custom.program=id;
        for(size_t j=0;j<entry->count;++j){const auto& u=entry->uniforms[j];auto* v=passes[i].custom.values+u.offset;
            if(!std::strcmp(u.name,"alpha"))v[0]=1;
            if(!std::strcmp(u.name,"offset"))v[0]=float(i)+.5f;
            if(!std::strcmp(u.name,"size"))v[0]=1.f/960;
        }
    }
    for(unsigned mode=0;mode<4;++mode){
        bool ok=all;
        for(auto& p:passes){p.hasClip=mode&1;p.clip[0]=570;p.clip[1]=315;p.clip[2]=332;p.clip[3]=192;}
        Vertex q[]={{555,304,0,0,1,1,1,.75f},{915,304,1,0,1,1,1,.75f},{555,520,0,1,1,1,1,.75f},{915,520,1,1,1,1,1,.75f}};
        begin();rect(0,0,960,544,0x183050ff);
        for(unsigned i=0;i<3;++i){ok=group_begin()&&ok;ok=group_begin()&&ok;}
        if(mode&2)rect(0,0,960,544,0x345678ff);draw_quad(t,q);
        for(int i=2;i>=0;--i){group_end(neutral,nullptr,1,1);group_end(passes[i],nullptr,1,1);}
        end();wait();ok=readback(960,544,reference.data())&&ok;
        begin();rect(0,0,960,544,0x183050ff);ok=group_begin()&&ok;
        if(mode&2)rect(0,0,960,544,0x345678ff);draw_quad(t,q);
        const EffectDraw chain[]={passes[2],passes[1]};
        ok=group_filter_chain(chain,nullptr,nullptr,2,1,1)&&ok;
        group_end(passes[0],nullptr,1,1);end();wait();ok=readback(960,544,candidate.data())&&ok;
        unsigned delta=0,changed=0;
        for(unsigned y=292;y<530;++y)for(unsigned x=540;x<930;++x)for(unsigned c=0;c<4;++c){
            const size_t n=(size_t(y)*960+x)*4+c;const auto d=unsigned(std::abs(int(reference[n])-int(candidate[n])));
            delta=std::max(delta,d);changed+=d!=0;
        }
        ok=ok&&delta<=4;all=all&&ok;
        log("[filter-chain-self-test] mode=%u max_delta=%u changed=%u ok=%d",mode,delta,changed,int(ok));
    }
    // Approximate path: preserve coordinates, color, alpha and foreground detail.
    // A smooth ramp bounds downsample error; the exact foreground stripe must
    // remain full resolution after restoring the parent viewport.
    std::vector<uint8_t> ramp(64*32*4);
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){const auto n=(y*64+x)*4;
        ramp[n]=uint8_t(32+x*3);ramp[n+1]=uint8_t(48+y*5);ramp[n+2]=uint8_t(200-x*2);ramp[n+3]=255;}
    auto* smooth=texture(64,32,ramp.data());
    if(smooth){
        for(unsigned mode=0;mode<2;++mode){
            for(auto& p:passes)p.hasClip=0;
            EffectDraw blur[5]={passes[0],passes[1],passes[2],passes[1],passes[0]};
            Vertex q[]={{0,0,0,0,1,1,1,1},{960,0,1,0,1,1,1,1},{0,544,0,1,1,1,1,1},{960,544,1,1,1,1,1,1}};
            bool ok=true;
            for(unsigned run=0;run<2;++run){
                begin();rect(0,0,960,544,0x183050ff);
                if(mode)ok=group_begin()&&ok;
                ok=group_begin()&&ok;draw_quad(smooth,q);
                if(run)ok=group_end_half_blur(blur,5)&&ok;
                else{ok=group_filter_chain(blur,nullptr,nullptr,4,1,1)&&ok;group_end(blur[4],nullptr,1,1);}
                rect(700,350,1,120,0xff2050ff);rect(710,470,125,1,0x20ff50ff);
                if(mode)group_end(neutral,nullptr,1,1);
                end();wait();ok=readback(960,544,(run?candidate:reference).data())&&ok;
            }
            unsigned delta=0,alphaDelta=0,stripeDelta=0;uint64_t sum=0,n=0;
            for(unsigned y=292;y<530;++y)for(unsigned x=540;x<930;++x)for(unsigned c=0;c<4;++c){
                const auto at=(size_t(y)*960+x)*4+c;unsigned d=unsigned(std::abs(int(reference[at])-int(candidate[at])));
                delta=std::max(delta,d);sum+=d;++n;if(c==3)alphaDelta=std::max(alphaDelta,d);
                if((x==700&&y>=350&&y<470)||(y==470&&x>=710&&x<835))stripeDelta=std::max(stripeDelta,d);
            }
            const double mean=double(sum)/n;
            ok=ok&&delta<=12&&mean<=2&&alphaDelta==0&&stripeDelta==0;all=all&&ok;
            log("[half-blur-self-test] parent=%u max_delta=%u mean=%.3f alpha_delta=%u foreground_delta=%u ok=%d",mode,delta,mean,alphaDelta,stripeDelta,int(ok));
        }
        // Reuse the same filtered image while translating, including subpixels
        // and a pan beyond the guard. Compare against rebuilding every frame.
        smooth->opaque=true;smooth->contentRevision=1;
        wait();blur_pan_clear();
        EffectDraw source{};source.texture=0x123456;source.transform[0]=source.transform[3]=1;
        source.quad[0]=1440;source.quad[1]=816;source.uv[2]=source.uv[3]=1;
        for(float& v:source.tint)v=1;
        EffectDraw blur[5]={passes[0],passes[1],passes[2],passes[1],passes[0]};
        for(auto& p:blur)p.hasClip=0;
        const float translations[]={0,.5f,72,-80,280};
        for(unsigned step=0;step<5;++step){
            source.transform[4]=-240;source.transform[5]=-136+translations[step];
            const unsigned builds=blurPanBuilds,hits=blurPanHits;bool ok=true;
            for(unsigned run=0;run<2;++run){
                begin();rect(0,0,960,544,0x183050ff);ok=group_begin()&&ok;
                if(run)ok=draw_cached_blur(smooth,source,blur,5,1,1,1)&&ok;
                else{
                    const float x=source.transform[4],y=source.transform[5];
                    Vertex q[]={{x,y,0,0,1,1,1,1},{x+1440,y,1,0,1,1,1,1},
                        {x,y+816,0,1,1,1,1,1},{x+1440,y+816,1,1,1,1,1,1}};
                    ok=group_begin()&&ok;draw_quad(smooth,q);ok=group_end_half_blur(blur,5)&&ok;
                }
                rect(700,350,1,120,0xff2050ff);group_end(neutral,nullptr,1,1);
                end();wait();ok=readback(960,544,(run?candidate:reference).data())&&ok;
            }
            unsigned delta=0,stripe=0,overlayDelta=0;uint64_t sum=0,n=0;
            // Exclude the framebuffer boundary's original clamp footprint.
            for(unsigned y=32;y<512;++y)for(unsigned x=32;x<928;++x)for(unsigned c=0;c<4;++c){
                const size_t at=(size_t(y)*960+x)*4+c;
                const auto d=unsigned(std::abs(int(reference[at])-int(candidate[at])));
                // The performance plugin writes changing digits into the
                // top-left display surface. Keep reporting those differences,
                // but compare the game's pixels outside its known rectangle.
                if(x<400&&y<220){overlayDelta=std::max(overlayDelta,d);continue;}
                delta=std::max(delta,d);sum+=d;++n;
                if(x==700&&y>=350&&y<470)stripe=std::max(stripe,d);
            }
            const bool reuse=step>0&&step<4;
            ok=ok&&delta<=16&&double(sum)/n<=2&&stripe==0
                &&blurPanBuilds-builds==(reuse?0u:1u)&&blurPanHits-hits==(reuse?1u:0u);
            all=all&&ok;
            log("[blur-pan-self-test] step=%u max_delta=%u mean=%.3f foreground_delta=%u overlay_delta=%u builds=%u hits=%u ok=%d",
                step,delta,double(sum)/n,stripe,overlayDelta,blurPanBuilds-builds,blurPanHits-hits,int(ok));
        }
        // A live texture upload and changed uniforms must not reuse old pixels.
        for(unsigned change=0;change<3;++change){
            if(change==0)++smooth->contentRevision;
            if(change==1)for(const auto& u:externalPrograms[id-1]->uniforms)
                if(!std::strcmp(sceGxmProgramParameterGetName(u.parameter),"size"))blur[0].custom.values[u.offset]*=2;
            const unsigned before=blurPanBuilds;
            begin();bool ok=draw_cached_blur(smooth,source,blur,5,change==2?2:1,1,1);end();wait();
            ok=ok&&blurPanBuilds==before+1;all=all&&ok;
            log("[blur-pan-self-test] invalidation=%u ok=%d",change,int(ok));
        }
        blur_pan_clear();wait();destroy(smooth);
    }else all=false;
    wait();destroy(t);external_release(id);
    // Deep reference isolation is test scratch, not a permanent VRAM reserve.
    for(unsigned i=0;i<8;++i)if(!hadColor[i]&&groups[i].color.image){
        auto& o=groups[i].color;sceGxmDestroyRenderTarget(o.target);sceGxmSyncObjectDestroy(o.sync);
        auto* image=o.image;release({image->uid,image->pixels,0,image->allocation});delete image;o={};
    }
    blurPanAllowed=all;
    return all;
}
