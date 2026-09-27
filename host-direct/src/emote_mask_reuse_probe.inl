// Pixel proof for shared mask targets. The reference redraws each mask.
bool emote_mask_reuse_self_test(){
    std::vector<uint8_t> pixels(17*9*4),reference(960*544*4),candidate(reference.size());
    for(unsigned i=0;i<17*9;++i){auto* p=pixels.data()+i*4;
        p[0]=uint8_t(i*7);p[1]=uint8_t(i*13);p[2]=uint8_t(i*19);p[3]=uint8_t(i*37);}
    auto* t=texture(17,9,pixels.data());if(!t)return false;
    const bool oldDisabled=emoteMaskReuseDisabled;bool passed=true;
    uint64_t previous=0;
    for(unsigned pass=0;pass<5;++pass){
        auto render=[&](bool fast,std::vector<uint8_t>& out){
            begin();emoteMaskReuseAllowed=fast;emoteMaskReuseDisabled=false;
            rect(0,0,960,544,0x204060ff);bool ok=true;unsigned hits=0;
            EffectDraw composite{};composite.effects.flags[0]=2;composite.blend=5;for(float& c:composite.tint)c=1;
            EffectDraw outer=composite;outer.effects.flags[0]=3;outer.tint[3]=.65f;
            if(pass==1)ok=group_begin();
            uint64_t token=0;
            for(unsigned item=0;item<4;++item){
                ok=group_begin()&&ok;
                // A prior frame's token or a different depth must never hit.
                if(previous)ok=!group_mask_reuse(previous)&&ok;
                float x=410.25f+item*19,y=232.75f-item*11;
                Vertex q[]={{x,y,0,0,1,.7f,.9f,.65f},{x+230,y+5,1,0,1,.7f,.9f,.65f},
                    {x-6,y+170,0,1,1,.7f,.9f,.65f},{x+248,y+192,1,1,1,.7f,.9f,.65f}};
                BuiltinEffects native;native.flags[3]=1;for(float& c:native.corners)c=1;
                native.modelClip[0]=native.modelClip[1]=-1.e30f;
                native.modelClip[2]=native.modelClip[3]=1.e30f;
                draw_builtin(t,q,4,false,0,nullptr,nullptr,native);
                if(pass==4){composite.hasClip=1;composite.clip[0]=450;composite.clip[1]=245;composite.clip[2]=190;composite.clip[3]=133;}
                bool reused=token&&fast&&group_mask_reuse(token);
                if(reused)++hits;
                else {
                    ok=group_mask_begin()&&ok;
                    Vertex m[]={{430,214,0,0,1,1,1,.7f},{654,214,1,0,1,1,1,.7f},
                        {430,448,0,1,1,1,1,.7f},{654,448,1,1,1,1,1,.7f}};
                    if(pass==2&&item>=2)for(auto& v:m)v.x+=17;
                    draw_builtin(t,m,4,false,0,nullptr,nullptr,{});
                    token=group_mask_revision();ok=token!=0&&ok;
                }
                group_end(composite,nullptr,1,1);
                // Changed masks and same-depth overwrites invalidate the key.
                if(pass==2&&item==1)token=0;
                if(pass==3&&item==1){
                    ok=group_begin()&&ok;ok=group_mask_begin()&&ok;
                    group_end(composite,nullptr,1,1);
                    ok=group_begin()&&ok;ok=!group_mask_reuse(token)&&ok;
                    group_end(outer,nullptr,1,1);token=0;
                }
            }
            previous=token;
            if(pass==1){ok=!group_mask_reuse(token)&&ok;group_end(outer,nullptr,1,1);}
            end();wait();
            return readback(960,544,out.data())&&ok&&(!fast||hits==(pass==2||pass==3?2u:3u));
        };
        bool ok=render(false,reference);ok=render(true,candidate)&&ok;
        unsigned delta=0,total=0,visible=0;
        // Cover every submitted shape plus a background margin. System
        // performance overlays can modify the upper-left framebuffer between
        // captures; those pixels are unrelated to either rendering path.
        for(unsigned y=150;y<500;++y)for(unsigned x=380;x<750;++x){
            const auto at=(size_t(y)*960+x)*4;
            for(unsigned c=0;c<4;++c){const auto d=unsigned(std::abs(int(reference[at+c])-int(candidate[at+c])));delta=std::max(delta,d);total+=d;}
            visible+=reference[at]!=32;
        }
        const double mean=double(total)/(350*370*4);ok=ok&&delta<=1&&mean<=.01&&visible>100;
        log("[emote-mask-reuse-test] pass=%u max_delta=%u mean_delta=%.6f visible=%u ok=%d",pass,delta,mean,visible,int(ok));passed=passed&&ok;
    }
    destroy(t);emoteMaskReuseDisabled=oldDisabled;emoteMaskReuseAllowed=passed;
    log("[emote-mask-reuse-test] enabled=%d",int(passed));return passed;
}
