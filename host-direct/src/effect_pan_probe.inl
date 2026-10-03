// Deployment-only GPU comparisons. All filters use the original full-resolution
// path as the oracle, including viewport clamp edges and translucent layers.
bool effect_pan_self_test(){
    if(active)return false;
    const char* names[]={"blur_h","blur_v","gray"};const BundledEntry* entries[3]{};unsigned ids[3]{};
    bool all=true;
    for(unsigned i=0;i<3;++i){for(const auto& e:bundledEntries)if(!std::strcmp(e.name,names[i]))entries[i]=&e;
        if(!entries[i])return false;ids[i]=external_register(entries[i]->data,entries[i]->size);if(!ids[i])return false;
        for(size_t j=0;j<entries[i]->count;++j){const auto& u=entries[i]->uniforms[j];all=external_uniform(ids[i],u.name,u.offset,u.count)&&all;}
    }
    std::vector<uint8_t> pixels(96*64*4),reference(960*544*4),candidate(reference.size());
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<96;++x){auto* p=pixels.data()+(y*96+x)*4;
        p[0]=uint8_t(x*2);p[1]=uint8_t(y*3);p[2]=uint8_t(50+(x+y)%120);p[3]=255;}
    auto* background=texture(96,64,pixels.data());
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<96;++x){auto* p=pixels.data()+(y*96+x)*4;
        p[0]=240;p[1]=80;p[2]=100;p[3]=(x>12&&x<84&&y>8&&y<56)?uint8_t(100+x):0;}
    auto* foreground=texture(96,64,pixels.data());
    if(!background||!foreground){wait();destroy(background);destroy(foreground);for(auto id:ids)external_release(id);return false;}
    const float weights[]={.2f,.16f,.12f,.07f,.03f,.015f,.004f,.001f};EffectDraw filters[3]{};
    for(unsigned i=0;i<3;++i){auto& p=filters[i];p.blend=5;p.custom.program=ids[i];for(auto& t:p.tint)t=1;
        for(size_t j=0;j<entries[i]->count;++j){const auto& u=entries[i]->uniforms[j];auto* v=p.custom.values+u.offset;
            if(!std::strcmp(u.name,"alpha"))v[0]=1;
            if(!std::strcmp(u.name,"width"))v[0]=1.f/960;
            if(!std::strcmp(u.name,"height"))v[0]=1.f/544;
            if(!std::strcmp(u.name,"weights"))std::copy(weights,weights+8,v);
        }
    }
    for(unsigned mode=0;mode<3;++mode){
        wait();blur_pan_clear();blurPanAdmission={};blurPanRequestedWidth=0;
        EffectDraw sources[2]{};Texture* textures[]={background,foreground};
        for(unsigned i=0;i<2;++i){auto& s=sources[i];s.texture=0x234560+i;s.transform[0]=s.transform[3]=1;
            s.quad[0]=i?410:1344;s.quad[1]=i?300:762;s.uv[2]=s.uv[3]=1;for(auto& v:s.tint)v=1;}
        const unsigned n=mode?2:1,count=mode==2?1:2;const unsigned kinds[]={mode==2?6u:1u,2};
        const auto* passes=mode==2?filters+2:filters;
        const float offsets[]={0,0,1,9.5f,-37,142,143};
        for(unsigned step=0;step<7;++step){
            sources[0].transform[4]=-192+offsets[step];sources[0].transform[5]=-109+offsets[step]*.25f;
            sources[1].transform[4]=210+offsets[step];sources[1].transform[5]=100+offsets[step]*.25f;
            if(mode==1)sources[0].tint[3]=.7f;
            bool ok=true,accepted=false;const auto builds=blurPanBuilds,hits=blurPanHits;
            for(unsigned run=0;run<2;++run){
                prepare_effect_cache();
                // The oracle/readback can take seconds in an emulator. Keep
                // this continuous-frame comparison separate from idle expiry.
                if(blurPanTargets[0].image)blurPanUsed=sceKernelGetProcessTimeWide();
                begin();rect(0,0,960,544,0x285080ff);
                accepted=run&&draw_cached_effect(textures,sources,n,passes,kinds,count,999,1,1,false);
                if(!accepted){ok=group_begin()&&ok;effect_pan_sources(textures,sources,n,1,1);
                    if(count>1)ok=group_filter_chain(passes,nullptr,nullptr,count-1,1,1)&&ok;
                    group_end(passes[count-1],nullptr,1,1);}
                rect(700,350,1,120,0xff2050ff);end();wait();ok=readback(960,544,(run?candidate:reference).data())&&ok;
            }
            unsigned delta=0,edge=0,stripe=0;uint64_t sum=0,samples=0;
            for(unsigned y=0;y<544;++y)for(unsigned x=0;x<960;++x)for(unsigned c=0;c<4;++c){
                if(x<400&&y<220)continue;const auto at=(size_t(y)*960+x)*4+c;
                const auto d=unsigned(std::abs(int(reference[at])-int(candidate[at])));delta=std::max(delta,d);sum+=d;++samples;
                if(x<8||x>=952||y<8||y>=536)edge=std::max(edge,d);
                if(x==700&&y>=350&&y<470)stripe=std::max(stripe,d);
            }
            // A disabled emulator readback can return identical zero buffers.
            // Require the exact foreground marker before trusting a comparison.
            const size_t marker=(size_t(400)*960+700)*4;
            const bool readable=reference[marker]>240&&reference[marker+1]<50&&reference[marker+2]>70&&reference[marker+3]>240;
            ok=ok&&readable&&(step<2?!accepted:accepted)&&delta<=8&&double(sum)/samples<=.35&&stripe==0&&(mode==2||edge<=1);
            all=all&&ok;
            log("[effect-pan-self-test] mode=%u step=%u accepted=%d delta=%u mean=%.4f edge=%u foreground=%u builds=%u hits=%u ok=%d",
                mode,step,int(accepted),delta,double(sum)/samples,edge,stripe,blurPanBuilds-builds,blurPanHits-hits,int(ok));
        }
        // Changing a source, local expression position or shader constants must
        // be refused for one observation, then rebuilt rather than reuse pixels.
        for(unsigned change=0;change<4;++change){
            if(change==0)++background->contentRevision;
            if(change==1)sources[0].transform[0]+=.1f;
            if(change==2)sources[n-1].tint[3]=.55f;
            const auto before=blurPanBuilds;bool first=false,second=false;
            for(unsigned run=0;run<2;++run){
                if(blurPanTargets[0].image)blurPanUsed=sceKernelGetProcessTimeWide();
                begin();const bool result=draw_cached_effect(textures,sources,n,passes,kinds,count,change==3?1000:999,1,1,false);end();wait();if(run)second=result;else first=result;}
            const bool ok=!first&&second&&blurPanBuilds==before+1;all=all&&ok;
            log("[effect-pan-self-test] mode=%u invalidation=%u builds=%u ok=%d",mode,change,blurPanBuilds-before,int(ok));
        }
    }
    wait();blur_pan_clear();destroy(background);destroy(foreground);for(auto id:ids)external_release(id);
    return all;
}
