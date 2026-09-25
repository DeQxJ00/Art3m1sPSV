// Hardware equivalence against the original fused single-image shader. The
// right-bottom probe avoids the device performance overlay in the top-left.
bool single_premul_self_test(){
    std::vector<uint8_t> source(17*9*4),reference(960*544*4),candidate(reference.size());
    const uint8_t alpha[]={0,1,2,63,127,128,200,254,255};
    for(unsigned i=0;i<17*9;++i){
        source[i*4]=uint8_t(i*31);source[i*4+1]=uint8_t(i*71);
        source[i*4+2]=uint8_t(i*113);source[i*4+3]=alpha[i%9];
    }
    auto* t=texture(17,9,source.data());if(!t)return false;
    const bool oldDisabled=premulSingleDisabled,oldGeneric=genericBuiltinForced;
    bool passed=true;
    for(unsigned pass=0;pass<12;++pass){
        BuiltinEffects e;e.flags[0]=4;
        e.corners[0]=pass%2?1.3f:1;e.corners[1]=.8f;e.corners[2]=1;
        e.corners[3]=pass%3==0?200.f/255:(pass%3==1?.5f:.001f);
        // Last three cases must retain the general shader.
        if(pass==9)e.flags[1]=1;
        if(pass==10)e.flags[2]=1;
        if(pass==11)e.transition[2]=1;
        const unsigned blend=pass%3==0?3:(pass%3==1?5:6);
        Vertex q[]={{552.25f,306.25f,0,0,.9f,.7f,1.1f,.65f},{914.25f,306.25f,1,0,.9f,.7f,1.1f,.65f},
                    {552.25f,518.25f,0,1,.9f,.7f,1.1f,.65f},{914.25f,518.25f,1,1,.9f,.7f,1.1f,.65f}};
        const float clip[]={560.25f,320.25f,900.25f,504.25f};
        auto render=[&](bool fast,std::vector<uint8_t>& out){
            begin();premulSingleAllowed=fast;premulSingleDisabled=false;genericBuiltinForced=false;
            rect(0,0,960,544,0x204060ff);
            const auto used=builtinFamilyCounts[6];
            draw_builtin(t,q,4,false,blend,pass%2?clip:nullptr,nullptr,e);
            const bool routeOK=(builtinFamilyCounts[6]!=used)==(fast&&pass<9);
            end();wait();return readback(960,544,out.data())&&routeOK;
        };
        bool ok=render(false,reference);ok=render(true,candidate)&&ok;
        unsigned delta=0,visible=0;
        for(unsigned y=300;y<525;++y)for(unsigned x=546;x<920;++x){
            const size_t i=(size_t(y)*960+x)*4;
            for(unsigned c=0;c<4;++c)delta=std::max(delta,unsigned(std::abs(int(reference[i+c])-int(candidate[i+c]))));
            visible+=reference[i]!=0x20||reference[i+1]!=0x40||reference[i+2]!=0x60;
        }
        ok=ok&&delta<=1&&(pass%3==2||visible>100);
        log("[single-premul-self-test] pass=%u blend=%u max_delta=%u visible=%u ok=%d",pass,blend,delta,visible,int(ok));
        passed=passed&&ok;
    }
    destroy(t);premulSingleDisabled=oldDisabled;genericBuiltinForced=oldGeneric;
    premulSingleAllowed=passed;
    log("[single-premul-self-test] enabled=%d",int(passed));return passed;
}
