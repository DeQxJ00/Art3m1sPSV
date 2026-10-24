// Opt-in startup probe. Uses the real CDRAM allocator and production BC3 upload
// descriptors. It is not a model/effect or sustained-performance acceptance test.
size_t largest_cdram_probe(size_t ceiling){
    constexpr size_t unit=256*1024;
    size_t low=0,high=std::min(ceiling/unit,size_t(512));
    while(low<high){
        const size_t mid=(low+high+1)/2;
        int uid=sceKernelAllocMemBlock("atlas-capacity-probe",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,mid*unit,nullptr);
        if(uid>=0){sceKernelFreeMemBlock(uid);low=mid;}else high=mid-1;
    }
    return low*unit; // Largest successful size within the requested ceiling.
}
bool texture_fragment_self_test(){
    if(active||groupDepth)return false;
    wait();const auto savedPressure=texturePressure;
    release(texturePressureReserve);texturePressureReserve={};
    const size_t initialFree=effect_pan_free();bool passed=true;unsigned fragmentedRounds=0;
    std::vector<uint8_t> pixels(960*544*4);
    for(unsigned round=0;round<13;++round){
        wait();texturePressure={};
        const size_t before=effect_pan_free();
        std::vector<Memory> blocks;
        uint32_t seed=0x9e3779b9u^(round+1);
        // Pack small interleaved blocks, then leave holes of varying sizes.
        // Raw blocks model cache residency, not OS/system allocations.
        while(effect_pan_free()>1024*1024&&blocks.size()<128){
            seed=seed*1664525u+1013904223u;
            const size_t bytes=round==12?1024*1024:(1+(seed%12))*256*1024;
            auto m=allocate(bytes,0,4,true);if(!m.p)break;blocks.push_back(m);
        }
        for(size_t i=0;i<blocks.size();++i)if(round==12?i%2!=0:(i+round)%3!=0){release(blocks[i]);blocks[i]={};}
        const auto fragmentedFree=effect_pan_free();const auto largest=largest_cdram_probe(8*1024*1024);
        if(fragmentedFree>=8*1024*1024&&largest<8*1024*1024)++fragmentedRounds;
        std::vector<size_t> order;
        // Adversarial final round: retire separated cold blocks first. Total
        // freed capacity rises while the remaining live blocks still split it.
        if(round==12){for(size_t mod:{size_t(0),size_t(2)})for(size_t i=mod;i<blocks.size();i+=4)order.push_back(i);}
        else for(size_t i=0;i<blocks.size();++i)order.push_back(i);
        unsigned reclaimedBlocks=0;size_t cursor=0;
        auto reclaim=[&](size_t wanted){
            size_t freed=0;
            while(cursor<order.size()&&freed<wanted){auto& m=blocks[order[cursor++]];if(!m.p)continue;
                freed+=m.charge.bytes;release(m);m={};++reclaimedBlocks;}
            return freed;
        };
        Texture* atlas[4]{};bool ok=true;unsigned retries=0,ready=0,maxRetries=0;
        const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
        for(unsigned n=0;n<4;++n){
            auto result=recover_texture_block([&](){
                auto m=allocate(8*1024*1024,0,4,true);if(!m.p)return false;
                texturePressureReserve=m;return true;
            },reclaim);
            retries+=result.retries;
            maxRetries=std::max(maxRetries,result.retries);
            if(!result.ready){ok=false;break;}
            atlas[n]=texture_bc3_prepare(4096,2048,false);
            if(!atlas[n]){ok=false;break;}
            ++ready;
            // Solid opaque BC3 blocks are layout-independent. Different colors
            // detect stale descriptors, aliasing and damage to earlier atlases.
            uint8_t block[16]={255,255,0,0,0,0,0,0};
            block[8]=block[10]=uint8_t(colors[n]);block[9]=block[11]=uint8_t(colors[n]>>8);
            for(size_t offset=0;offset<8*1024*1024;offset+=16)std::memcpy(atlas[n]->pixels+offset,block,16);
        }
        if(ok){
            begin();rect(0,0,960,544,0x000000ff);
            for(unsigned n=0;n<4;++n){float x=340.f+(n%2)*190,y=200.f+(n/2)*150;
                Vertex q[]={{x,y,0,0,1,1,1,1},{x+170,y,1,0,1,1,1,1},{x,y+130,0,1,1,1,1,1},{x+170,y+130,1,1,1,1,1}};
                draw_quad(atlas[n],q);}
            end();wait();ok=readback(960,544,pixels.data());
            const uint8_t expected[][3]={{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
            for(unsigned n=0;n<4;++n){size_t p=((250+(n/2)*150)*960+400+(n%2)*190)*4;
                for(unsigned c=0;c<3;++c)ok=ok&&std::abs(int(pixels[p+c])-expected[n][c])<=1;}
        }
        wait();for(auto* t:atlas)if(t)destroy(t);
        for(auto m:blocks)release(m);
        release(texturePressureReserve);texturePressureReserve={};wait();
        const size_t after=effect_pan_free();const bool leakOk=after>=before;
        log("[atlas-fragment-test] round=%u seed=%u blocks=%u free_fragmented=%u largest_up_to_8m=%u atlases=%u retries=%u max_atlas_retries=%u reclaimed_blocks=%u free_before=%u free_after=%u pixel_ok=%d leak_ok=%d",
            round+1,0x9e3779b9u^(round+1),unsigned(blocks.size()),unsigned(fragmentedFree),unsigned(largest),ready,retries,maxRetries,reclaimedBlocks,unsigned(before),unsigned(after),int(ok),int(leakOk));
        passed=passed&&ok&&leakOk;
    }
    // Live barriers cannot be evicted by a cold-cache callback. Keep many
    // uploaded images visible while requesting four full-sized atlases.
    // The pre-compaction allocation MUST fail, and every pinned image must
    // still sample correctly after relocation of its storage.
    for(unsigned round=0;round<3;++round){
        wait();texturePressure={};const auto before=effect_pan_free();
        const auto registryBefore=movableTextures.size();
        std::vector<Memory> holes;std::vector<Texture*> pins;
        const unsigned h=round==0?256:round==1?1024:512;
        const size_t pinBytes=size_t(256)*h*4;
        std::vector<uint8_t> rgba(pinBytes);
        bool ok=true;
        while(effect_pan_free()>=pinBytes+2*1024*1024&&pins.size()<64){
            auto hole=allocate(2*1024*1024,0,4,true);if(!hole.p)break;holes.push_back(hole);
            const unsigned n=pins.size();
            for(size_t p=0;p<rgba.size();p+=4){rgba[p]=32+n*3;rgba[p+1]=220-n*2;rgba[p+2]=64+n;rgba[p+3]=255;}
            auto* t=texture_unfitted(256,h,rgba.data());if(!t){ok=false;break;}pins.push_back(t);
        }
        // Consume the tail too, so the unmodified allocator cannot just take
        // an untouched 8 MiB block after the interleaved allocation sequence.
        while(effect_pan_free()>=256*1024){auto m=allocate(256*1024,0,4,true);if(!m.p)break;holes.push_back(m);}
        for(auto m:holes)release(m);holes.clear();
        const auto freeFragmented=effect_pan_free();const auto largest=largest_cdram_probe(8*1024*1024);
        const bool reproduced=freeFragmented>=32*1024*1024&&largest<8*1024*1024;
        Texture* atlas[4]{};unsigned ready=0;
        const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
        for(unsigned n=0;n<4&&ok;++n){
            auto m=allocate(8*1024*1024,0,4,true);
            if(m.p){texturePressureReserve=m;texturePressureReserveAt=sceKernelGetProcessTimeWide();}
            else if(!compact_texture_storage(8*1024*1024)){ok=false;break;}
            atlas[n]=texture_bc3_prepare(4096,2048,false);if(!atlas[n]){ok=false;break;}++ready;
            uint8_t block[16]={255,255,0,0,0,0,0,0};
            block[8]=block[10]=uint8_t(colors[n]);block[9]=block[11]=uint8_t(colors[n]>>8);
            for(size_t off=0;off<8*1024*1024;off+=16)std::memcpy(atlas[n]->pixels+off,block,16);
        }
        if(ok){
            begin();rect(0,0,960,544,0x000000ff);
            // Stay clear of the device performance overlay when sampling.
            for(unsigned n=0;n<pins.size();++n){const float x=350.f+(n%9)*60,y=140.f+(n/9)*40;
                Vertex q[]={{x,y,0,0,1,1,1,1},{x+48,y,1,0,1,1,1,1},{x,y+32,0,1,1,1,1,1},{x+48,y+32,1,1,1,1,1,1}};draw_quad(pins[n],q);}
            for(unsigned n=0;n<4;++n){const float x=20.f+n*235;
                Vertex q[]={{x,300,0,0,1,1,1,1},{x+220,300,1,0,1,1,1,1},{x,500,0,1,1,1,1,1},{x+220,500,1,1,1,1,1,1}};draw_quad(atlas[n],q);}
            end();wait();ok=readback(960,544,pixels.data());
            bool reported=false;
            for(unsigned n=0;n<pins.size();++n){const size_t p=((150+(n/9)*40)*960+370+(n%9)*60)*4;
                const int expected[]={32+int(n)*3,220-int(n)*2,64+int(n)};
                for(unsigned c=0;c<3;++c){const bool match=std::abs(int(pixels[p+c])-expected[c])<=1;ok=ok&&match;
                    if(!match&&!reported){reported=true;log("[atlas-pinned-pixel] pin=%u channel=%u actual=%u expected=%d cpu=%u",n,c,unsigned(pixels[p+c]),expected[c],unsigned(pins[n]->pixels[c]));}}}
            const int expected[][3]={{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
            for(unsigned n=0;n<4;++n){const size_t p=(400*960+100+n*235)*4;
                for(unsigned c=0;c<3;++c)ok=ok&&std::abs(int(pixels[p+c])-expected[n][c])<=1;}
        }
        wait();for(auto* t:atlas)destroy(t);for(auto* t:pins)destroy(t);
        release(texturePressureReserve);texturePressureReserve={};wait();
        const bool leakOk=effect_pan_free()>=before&&movableTextures.size()==registryBefore;
        log("[atlas-pinned-test] round=%u pins=%u pin_bytes=%u free_fragmented=%u largest_up_to_8m=%u reproduced=%d atlases=%u pixel_ok=%d leak_ok=%d",
            round+1,unsigned(pins.size()),unsigned(pinBytes),unsigned(freeFragmented),unsigned(largest),int(reproduced),ready,int(ok),int(leakOk));
        passed=passed&&reproduced&&ok&&leakOk;
    }
    texturePressure=savedPressure;
    passed=passed&&fragmentedRounds>0;
    log("[atlas-fragment-test] done=1 passed=%d fragmented_rounds=%u initial_free=%u final_free=%u",int(passed),fragmentedRounds,unsigned(initialFree),effect_pan_free());
    return passed;
}
