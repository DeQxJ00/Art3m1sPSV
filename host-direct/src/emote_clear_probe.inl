// Exercise moving/shrinking/disappearing content after reusing the same physical
// targets. Compare both color and mask stale-pixel cleanup with full clears.
bool emote_clear_self_test(){
    std::vector<uint8_t> pixels(17*9*4),reference(960*544*4),candidate(reference.size());
    for(unsigned i=0;i<17*9;++i){auto* p=pixels.data()+i*4;
        p[0]=uint8_t(i*7);p[1]=uint8_t(i*13);p[2]=uint8_t(i*19);p[3]=uint8_t(i*37);}
    auto* t=texture(17,9,pixels.data());if(!t)return false;
    const bool oldDisabled=emoteClearDisabled;
    bool passed=true;
    BuiltinEffects native;native.flags[3]=1;for(float& c:native.corners)c=1;
    native.modelClip[0]=native.modelClip[1]=-1.e30f;
    native.modelClip[2]=native.modelClip[3]=1.e30f;
    EffectDraw composite{};composite.effects.flags[0]=2;composite.blend=5;
    for(float& c:composite.tint)c=1;
    EffectDraw outer=composite;outer.effects.flags[0]=3;
    for(unsigned pass=0;pass<8;++pass){
        auto render=[&](bool fast,std::vector<uint8_t>& out){
            bool ok=true,usedPartial=false;
            // Establish old contents with full clears, then reuse in a new
            // frame. Opposing positions expose both color and mask trails.
            for(unsigned step=0;step<3;++step){
                if(pass==7&&step){
                    // The preceding nested case populated both physical depths.
                    // Dirty bounds must travel with storage, not logical slots.
                    std::swap(groups[0].color,groups[1].color);
                    std::swap(groups[0].mask,groups[1].mask);
                }
                begin();emoteClearAllowed=fast&&step!=0;emoteClearDisabled=false;
                const auto hits=emoteClearPartial;
                rect(0,0,960,544,0x204060ff);
                if(pass==3)ok=group_begin()&&ok;
                ok=group_begin()&&ok;
                const float x=step==0?405.25f:(step==1?670.75f:520.5f);
                const float y=step==0?160.25f:(step==1?340.75f:255.5f);
                const float size=step==0?175.f:(step==1?75.f:45.f);
                Vertex q[]={{x,y,0,0,1,.7f,.9f,.65f},{x+size,y+3,1,0,1,.7f,.9f,.65f},
                    {x-5,y+size,0,1,1,.7f,.9f,.65f},{x+size+4,y+size+7,1,1,1,.7f,.9f,.65f}};
                if(pass!=2||step!=2){
                    if(pass==4)draw_quad(t,q);
                    else if(pass==1){Vertex m[]={q[0],q[1],q[2],q[2],q[1],q[3]};draw_builtin(t,m,6,true,0,nullptr,nullptr,native);}
                    else draw_builtin(t,q,4,false,0,nullptr,nullptr,native);
                }
                ok=group_mask_begin()&&ok;
                if(pass!=5||step!=2){
                    Vertex m[]={{x-20,y-20,0,0,1,1,1,.75f},{x+size+20,y-20,1,0,1,1,1,.75f},
                        {x-20,y+size+20,0,1,1,1,1,.75f},{x+size+20,y+size+20,1,1,1,1,1,.75f}};
                    draw_builtin(t,m,4,false,0,nullptr,nullptr,pass==6?BuiltinEffects{}:native);
                }
                group_end(composite,nullptr,1,1);
                if(pass==3)group_end(outer,nullptr,1,1);
                // end() can reset the interval counters on slower devices.
                usedPartial=usedPartial||emoteClearPartial>hits;
                end();wait();
            }
            return readback(960,544,out.data())&&ok&&(!fast||usedPartial);
        };
        bool ok=render(false,reference);ok=render(true,candidate)&&ok;
        unsigned delta=0,total=0,visible=0;
        const size_t background=(140*960+380)*4;
        // Exclude independently changing system HUDs; cover old and new areas.
        for(unsigned y=140;y<490;++y)for(unsigned x=380;x<790;++x){
            const auto at=(size_t(y)*960+x)*4;
            for(unsigned c=0;c<4;++c){const auto d=unsigned(std::abs(int(reference[at+c])-int(candidate[at+c])));delta=std::max(delta,d);total+=d;}
            visible+=reference[at]!=reference[background]||reference[at+1]!=reference[background+1]||reference[at+2]!=reference[background+2];
        }
        const double mean=double(total)/(350*410*4);
        ok=ok&&delta<=1&&mean<=.01&&((pass==2||pass==5)?visible==0:visible>100);
        log("[emote-clear-test] pass=%u max_delta=%u mean_delta=%.6f visible=%u ok=%d",pass,delta,mean,visible,int(ok));
        passed=passed&&ok;
    }
    destroy(t);emoteClearDisabled=oldDisabled;emoteClearAllowed=passed;
    log("[emote-clear-test] enabled=%d",int(passed));return passed;
}
