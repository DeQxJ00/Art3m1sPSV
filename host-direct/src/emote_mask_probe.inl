// Compare small masked quads and deformed triangles against full-target
// compositing. Includes nested groups, clipping and a non-native fallback.
bool emote_mask_bounds_self_test(){
    std::vector<uint8_t> pixels(17*9*4),reference(960*544*4),candidate(reference.size());
    for(unsigned y=0;y<9;++y)for(unsigned x=0;x<17;++x){
        auto* p=pixels.data()+(y*17+x)*4;p[0]=uint8_t(x*15);p[1]=uint8_t(y*29);
        p[2]=uint8_t((x+y)*9);p[3]=uint8_t((x*3+y*5)%8*36);
    }
    auto* t=texture(17,9,pixels.data());if(!t)return false;
    const bool oldDisabled=emoteMaskBoundsDisabled;
    bool passed=true;
    for(unsigned pass=0;pass<6;++pass){
        Vertex q[]={{602.25f,338.75f,0,0,1,1,1,.8f},{836.75f,341.25f,1,0,1,1,1,.8f},
                    {583.75f,469.75f,0,1,1,1,1,.8f},{857.25f,487.5f,1,1,1,1,1,.8f}};
        Vertex mesh[]={q[0],q[1],q[2],q[2],q[1],q[3]};
        if(pass==1){mesh[2].x-=15;mesh[3].x-=15;mesh[1].y+=17;mesh[4].y+=17;}
        Vertex m[]={{615.5f,321.75f,0,0,1,1,1,1},{831.25f,321.75f,1,0,1,1,1,1},
                    {615.5f,501.25f,0,1,1,1,1,1},{831.25f,501.25f,1,1,1,1,1,1}};
        BuiltinEffects native;native.flags[3]=1;for(float& c:native.corners)c=1;
        native.modelClip[0]=native.modelClip[1]=-1.e30f;
        native.modelClip[2]=native.modelClip[3]=1.e30f;
        if(pass==4)native.flags[3]=0;
        EffectDraw composite{};composite.effects.flags[0]=2;composite.blend=5;
        for(float& c:composite.tint)c=1;
        if(pass==2){composite.hasClip=1;composite.clip[0]=640.25f;composite.clip[1]=350.75f;composite.clip[2]=150;composite.clip[3]=100;}
        EffectDraw outer=composite;outer.effects.flags[0]=3;outer.hasClip=0;
        outer.tint[0]=.8f;outer.tint[3]=.7f;
        auto render=[&](bool fast,std::vector<uint8_t>& out){
            begin();emoteMaskBoundsAllowed=fast;emoteMaskBoundsDisabled=false;
            rect(0,0,960,544,0x204060ff);
            bool ok=true;
            if(pass==3)ok=group_begin();
            ok=group_begin()&&ok;
            if(ok){
                draw_builtin(t,pass==1?mesh:q,pass==1?6:4,pass==1,0,nullptr,nullptr,native);
                if(pass==5){Vertex extra[4];std::memcpy(extra,q,sizeof(q));for(auto& v:extra){v.x+=26;v.y-=19;}
                    draw_builtin(t,extra,4,false,0,nullptr,nullptr,native);}
                ok=group_mask_begin();
                draw_builtin(t,m,4,false,0,nullptr,nullptr,{});
                const auto before=emoteMaskBoundsDraws;group_end(composite,nullptr,1,1);
                ok=ok&&((emoteMaskBoundsDraws!=before)==(fast&&pass!=4));
            }
            if(pass==3)group_end(outer,nullptr,1,1);
            end();wait();return readback(960,544,out.data())&&ok;
        };
        bool ok=render(false,reference);ok=render(true,candidate)&&ok;
        unsigned delta=0,total=0,visible=0;
        for(unsigned y=300;y<525;++y)for(unsigned x=546;x<930;++x){
            const auto at=(size_t(y)*960+x)*4;
            for(unsigned c=0;c<4;++c){auto d=unsigned(std::abs(int(reference[at+c])-int(candidate[at+c])));delta=std::max(delta,d);total+=d;}
            visible+=reference[at]!=32||reference[at+1]!=64||reference[at+2]!=96;
        }
        const auto outside=(300*960+546)*4;const double mean=double(total)/(225*384*4);
        ok=ok&&delta<=1&&mean<=.03&&visible>100&&reference[outside]==32&&reference[outside+1]==64&&reference[outside+2]==96&&reference[outside+3]==255;
        log("[emote-mask-self-test] pass=%u max_delta=%u mean_delta=%.6f visible=%u ok=%d",pass,delta,mean,visible,int(ok));
        passed=passed&&ok;
    }
    destroy(t);emoteMaskBoundsDisabled=oldDisabled;emoteMaskBoundsAllowed=passed;
    log("[emote-mask-self-test] enabled=%d",int(passed));return passed;
}
