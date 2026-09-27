// Compare native masked groups with both their baked and replayed RGBA source.
bool emote_composite_self_test(){
    std::vector<uint8_t> pixels(17*9*4),reference(960*544*4),candidate(reference.size());
    for(unsigned i=0;i<17*9;++i){auto* p=pixels.data()+i*4;
        p[0]=uint8_t(i*7);p[1]=uint8_t(i*13);p[2]=uint8_t(i*19);p[3]=uint8_t(i*37);}
    auto* t=texture(17,9,pixels.data());if(!t)return false;
    bool passed=true;const bool previousAllowed=retainedAllowed;retainedAllowed=true;
    for(unsigned pass=0;pass<6;++pass){
        auto render=[&](unsigned mode,std::vector<uint8_t>& out){
            begin();rect(0,0,960,544,0x204060ff);bool ok=true;
            EffectDraw composite{};composite.effects.flags[0]=3;composite.blend=5;
            for(float& c:composite.tint)c=1;
            if(pass==3){composite.tint[0]=.6f;composite.tint[3]=.75f;}
            if(pass==4){composite.hasClip=1;composite.clip[0]=450;composite.clip[1]=230;composite.clip[2]=210;composite.clip[3]=180;}
            if(pass==5){composite.blend=3;}
            if(mode==2)ok=draw_cached_group(0);
            else {
                ok=group_begin();
                for(unsigned item=0;item<3;++item){
                    float x=410.25f+item*19,y=232.75f-item*11;
                    Vertex q[]={{x,y,0,0,1,.7f,.9f,.65f},{x+230,y+5,1,0,1,.7f,.9f,.65f},
                        {x-6,y+170,0,1,1,.7f,.9f,.65f},{x+248,y+192,1,1,1,.7f,.9f,.65f}};
                    Vertex mesh[]={q[0],q[1],q[2],q[2],q[1],q[3]};
                    BuiltinEffects native;native.flags[3]=1;for(float& c:native.corners)c=1;
                    native.modelClip[0]=native.modelClip[1]=-1.e30f;
                    native.modelClip[2]=native.modelClip[3]=1.e30f;
                    ok=group_begin()&&ok;
                    draw_builtin(t,pass==1?mesh:q,pass==1?6:4,pass==1,pass==2&&item==1?7:0,nullptr,nullptr,native);
                    ok=group_mask_begin()&&ok;
                    Vertex m[]={{430,214,0,0,1,1,1,.7f},{654,214,1,0,1,1,1,.7f},
                        {430,448,0,1,1,1,1,.7f},{654,448,1,1,1,1,1,.7f}};
                    draw_builtin(t,m,4,false,0,nullptr,nullptr,{});
                    EffectDraw maskEnd{};maskEnd.effects.flags[0]=2;maskEnd.blend=5;for(float& c:maskEnd.tint)c=1;
                    group_end(maskEnd,nullptr,1,1);
                }
                if(mode==3){frameGroupsComplete=false;ok=!group_end_cached(composite,1,1,0,nullptr)&&ok;}
                else if(mode==1)ok=group_end_cached(composite,1,1,0,nullptr)&&ok;
                else group_end(composite,nullptr,1,1);
            }
            end();wait();return readback(960,544,out.data())&&ok;
        };
        bool ok=render(0,reference);
        for(unsigned mode=1;mode<=3;++mode){
            bool match=render(mode,candidate);unsigned delta=0,total=0,visible=0;
            for(unsigned y=150;y<500;++y)for(unsigned x=380;x<750;++x){const auto at=(size_t(y)*960+x)*4;
                for(unsigned c=0;c<4;++c){const auto d=unsigned(std::abs(int(reference[at+c])-int(candidate[at+c])));delta=std::max(delta,d);total+=d;}
                visible+=reference[at]!=32;
            }
            const double mean=double(total)/(350*370*4);match=match&&delta<=1&&mean<=.01&&visible>100;
            log("[emote-composite-test] pass=%u mode=%u max_delta=%u mean_delta=%.6f visible=%u ok=%d",pass,mode,delta,mean,visible,int(match));ok=ok&&match;
        }
        passed=passed&&ok;
    }
    destroy(t);retainedAllowed=previousAllowed;
    for(auto& valid:retainedValid)valid=false;for(auto& r:retainedRevision)++r;
    log("[emote-composite-test] enabled=%d",int(passed));return passed;
}
