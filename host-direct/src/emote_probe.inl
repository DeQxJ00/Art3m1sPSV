// Compare the optimized route with the complete native material shader.
// The lower-right region stays outside common device debug overlays.
bool emote_simple_self_test(){
    std::vector<uint8_t> source(17*9*4),reference(960*544*4),candidate(reference.size());
    const uint8_t alpha[]={0,1,2,63,127,128,200,254,255};
    for(unsigned i=0;i<17*9;++i){source[i*4]=uint8_t(i*31);source[i*4+1]=uint8_t(i*71);
        source[i*4+2]=uint8_t(i*113);source[i*4+3]=alpha[i%9];}
    auto* t=texture(17,9,source.data());if(!t)return false;
    const bool oldDisabled=emoteSimpleDisabled,oldGeneric=genericBuiltinForced;
    bool passed=true;
    for(unsigned pass=0;pass<16;++pass){
        BuiltinEffects e;e.flags[3]=1;
        e.modelClip[0]=e.modelClip[1]=-1.e30f;e.modelClip[2]=e.modelClip[3]=1.e30f;
        e.modelX[0]=1.f/960;e.modelY[1]=1.f/544;e.modelX[3]=float((pass/6)%2);
        for(unsigned i=0;i<4;++i){e.corners[i*4]=.8f;e.corners[i*4+1]=1.1f;e.corners[i*4+2]=.7f;e.corners[i*4+3]=.7f;}
        e.wipe[3]=float(pass%6);
        const unsigned blends[]={0,1,4,8,9,2};
        const unsigned blend=blends[pass%6];
        // The final cases must fall back, including bilinear corner gradients.
        if(pass==12)e.corners[4]=.2f;
        if(pass==13){e.wipe[0]=1.4f;e.wipe[1]=-.2f;e.wipe[2]=1;}
        if(pass==14)e.modelClip[2]=.8f;
        if(pass==15)e.flags[1]=1;
        Vertex q[]={{552.25f,306.25f,0,0,.9f,.7f,1.1f,.65f},{914.25f,306.25f,1,0,.9f,.7f,1.1f,.65f},
                    {552.25f,518.25f,0,1,.9f,.7f,1.1f,.65f},{914.25f,518.25f,1,1,.9f,.7f,1.1f,.65f}};
        const float clip[]={560.25f,320.25f,900.25f,504.25f};
        auto render=[&](bool fast,std::vector<uint8_t>& out){
            begin();emoteSimpleAllowed=fast;emoteSimpleDisabled=false;genericBuiltinForced=false;
            rect(0,0,960,544,0x204060ff);
            const auto used=builtinFamilyCounts[7];
            draw_builtin(t,q,4,false,blend,pass%2?clip:nullptr,nullptr,e);
            const bool routeOK=(builtinFamilyCounts[7]!=used)==(fast&&pass<12);
            end();wait();return readback(960,544,out.data())&&routeOK;
        };
        bool ok=render(false,reference);ok=render(true,candidate)&&ok;
        unsigned delta=0,visible=0,minRed=255,maxRed=0,difference=0;
        for(unsigned y=300;y<525;++y)for(unsigned x=546;x<920;++x){
            const size_t i=(size_t(y)*960+x)*4;
            for(unsigned c=0;c<4;++c){
                const unsigned d=unsigned(std::abs(int(reference[i+c])-int(candidate[i+c])));
                delta=std::max(delta,d);difference+=d;
            }
            minRed=std::min(minRed,unsigned(reference[i]));maxRed=std::max(maxRed,unsigned(reference[i]));
            visible+=reference[i]!=0x20||reference[i+1]!=0x40||reference[i+2]!=0x60;
        }
        const size_t outside=(300*960+546)*4;
        const bool background=reference[outside]==0x20&&reference[outside+1]==0x40
            &&reference[outside+2]==0x60&&reference[outside+3]==255;
        // Removing constant corner interpolation can change final UNORM rounding
        // by two levels after MOD2X plus additive blending. Bound both the largest
        // error and the mean error; fallback cases must remain byte-identical.
        const double mean=double(difference)/(225*374*4);
        ok=ok&&delta<=(pass<12?2u:0u)&&mean<=.25&&visible>100&&maxRed-minRed>4&&background;
        log("[emote-simple-self-test] pass=%u blend=%u max_delta=%u mean_delta=%.6f visible=%u range=%u background=%d ok=%d",pass,blend,delta,mean,visible,maxRed-minRed,int(background),int(ok));
        passed=passed&&ok;
    }
    destroy(t);emoteSimpleDisabled=oldDisabled;genericBuiltinForced=oldGeneric;
    emoteSimpleAllowed=passed;log("[emote-simple-self-test] enabled=%d",int(passed));return passed;
}
