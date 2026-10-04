// Compare identical triangles through the legacy and indexed upload paths.
bool indexed_mesh_self_test(){
    std::vector<uint8_t> rgba(31*19*4),reference(960*544*4),candidate(reference.size());
    for(unsigned i=0;i<31*19;++i){rgba[i*4]=i*7;rgba[i*4+1]=i*13;rgba[i*4+2]=i*19;rgba[i*4+3]=i*37;}
    auto* source=texture(31,19,rgba.data());if(!source)return false;
    bool passed=true;const bool oldDisabled=indexedMeshDisabled;
    indexedMeshTesting=true;indexedMeshAllowed=true;indexedMeshDisabled=false;
    for(unsigned pass=0;pass<20;++pass){
        const unsigned side=pass%4==0?2:pass%4==1?3:pass%4==2?8:32;
        std::vector<std::array<float,4>> grid,expanded;
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){
            const float u=float(x)/(side-1),v=float(y)/(side-1);
            const float px=pass>=16?(u<.5f?u*2:(1-u)*2):u;
            grid.push_back({px*276+(v*v-v)*37,v*241+(u*u-u)*29,u,v});
        }
        std::vector<uint16_t> order(indexed_mesh_count(side));indexed_mesh_indices(order.data(),side);
        for(auto i:order)expanded.push_back(grid[i]);
        auto render=[&](bool indexed,std::vector<uint8_t>& out){
            begin();rect(0,0,960,544,0x204060ff);bool ok=true;
            const bool grouped=pass>=8;
            if(grouped)ok=group_begin();
            const auto before=frameStats.draws;
            for(unsigned item=0;item<(pass>=12?3u:1u);++item){
                EffectDraw d{};d.gridSide=indexed?side:0;d.meshCount=indexed?grid.size():expanded.size();
                d.mesh=reinterpret_cast<const float(*)[4]>((indexed?grid:expanded).data());
                d.quad[0]=276;d.quad[1]=241;
                d.transform[0]=pass%3==0?-.91f:1.07f;d.transform[1]=pass%2?.113f:0;
                d.transform[2]=pass%2?-.083f:0;d.transform[3]=.89f;
                d.transform[4]=pass%3==0?897.375f:557.375f;d.transform[5]=257.625f+item*12;
                d.uv[0]=.125f;d.uv[1]=.0625f;d.uv[2]=.6875f;d.uv[3]=.8125f;
                d.tint[0]=.87f;d.tint[1]=.93f;d.tint[2]=.74f;d.tint[3]=.71f;
                d.blend=pass>=12&&item==1?7:0;
                BuiltinEffects e{};e.flags[3]=1;
                e.modelX[0]=1.f/960;e.modelY[1]=1.f/544;
                for(unsigned c=0;c<16;++c)e.corners[c]=pass%2?(.5f+(c%5)*.1f):1;
                e.modelClip[0]=e.modelClip[1]=-1.e30f;e.modelClip[2]=e.modelClip[3]=1.e30f;
                const float clip[]={570.25f,286.5f,846.75f,506.25f};
                draw_effect_mesh(source,d,pass%2?clip:nullptr,nullptr,nullptr,e,.97f,1.03f);
            }
            ok=frameStats.draws>before&&ok;
            if(grouped){
                ok=group_mask_begin()&&ok;
                Vertex q[]={{530,250,0,0,1,1,1,.63f},{925,250,1,0,1,1,1,.63f},
                    {530,540,0,1,1,1,1,.63f},{925,540,1,1,1,1,1,.63f}};
                draw_builtin(source,q,4,false,0,nullptr,nullptr,{});
                EffectDraw composite{};composite.effects.flags[0]=2;composite.blend=5;
                for(auto& c:composite.tint)c=1;
                group_end(composite,nullptr,1,1);
            }
            end();wait();return readback(960,544,out.data())&&ok;
        };
        bool ok=render(false,reference)&&render(true,candidate);unsigned delta=0,visible=0;uint64_t total=0,outside=0;
        unsigned minX=960,minY=544,maxX=0,maxY=0;
        // All tested geometry and masks lie in this rectangle. The user's
        // external performance overlay mutates displayed pixels elsewhere.
        for(unsigned y=0;y<544;++y)for(unsigned x=0;x<960;++x){
            const size_t i=(size_t(y)*960+x)*4;const bool inside=x>=500&&x<950&&y>=220;
            for(unsigned c=0;c<4;++c){
                const unsigned d=std::abs(int(reference[i+c])-int(candidate[i+c]));
                if(inside){delta=std::max(delta,d);total+=d;}else outside+=d;
                if(d){minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);}
            }
            if(inside)visible+=reference[i]!=32;
        }
        // Folding a two-column grid maps both columns onto the same line.
        // Both routes must correctly produce no coverage for that case.
        ok=ok&&delta==0&&(pass==16?visible==0:visible>100);
        log("[emote-indexed-test] pass=%u side=%u vertices=%u indices=%u max_delta=%u total_delta=%llu visible=%u ok=%d",pass,side,
            unsigned(grid.size()),unsigned(order.size()),delta,(unsigned long long)total,visible,int(ok));
        log("[emote-indexed-diff] outside_delta=%llu bbox=%u,%u,%u,%u",(unsigned long long)outside,minX,minY,maxX,maxY);
        passed=passed&&ok;
    }
    destroy(source);indexedMeshTesting=false;indexedMeshDisabled=oldDisabled;indexedMeshAllowed=passed;
    log("[emote-indexed-test] enabled=%d",int(passed));return passed;
}
