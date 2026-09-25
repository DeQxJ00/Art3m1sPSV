bool retained_screen_self_test(){
    std::vector<uint8_t> source(17*9*4),reference(960*544*4),candidate(reference.size());
    for(unsigned i=0;i<17*9;++i){
        source[i*4]=uint8_t(i*31);source[i*4+1]=uint8_t(i*71);
        source[i*4+2]=uint8_t(i*113);source[i*4+3]=uint8_t(i*17);
    }
    auto* t=texture(17,9,source.data());if(!t)return false;
    const bool oldAllowed=retainedAllowed;retainedAllowed=true;
    bool passed=true;
    const uint32_t backgrounds[]={0x204060ff,0x906020ff,0xc0d0e0ff};
    for(unsigned mode=0;mode<4;++mode){
        EffectDraw d{};d.blend=3;d.tint[0]=.9f;d.tint[1]=.7f;d.tint[2]=1;d.tint[3]=mode==2?1.f:.65f;
        d.effects.flags[0]=3;d.effects.transition[2]=mode==2?1:0;
        if(mode==3)d.effects.flags[1]=1;
        if(mode==1){d.hasClip=1;d.clip[0]=560.25f;d.clip[1]=320.25f;d.clip[2]=339.75f;d.clip[3]=184;}
        Vertex q[]={{552.25f,306.25f,0,0,1,1,1,200.f/255},{914.25f,306.25f,1,0,1,1,1,200.f/255},
                    {552.25f,518.25f,0,1,1,1,1,200.f/255},{914.25f,518.25f,1,1,1,1,1,200.f/255}};
        for(unsigned repeat=0;repeat<3;++repeat){
            begin();rect(0,0,960,544,backgrounds[repeat]);bool ok=group_begin();
            if(ok){draw_quad(t,q);group_end(d,nullptr,1,1);}end();wait();
            ok=readback(960,544,reference.data())&&ok;
            begin();rect(0,0,960,544,backgrounds[repeat]);
            if(!repeat){bool opened=group_begin();ok=opened&&ok;if(opened){draw_quad(t,q);ok=group_end_cached(d,1,1,0,nullptr)&&ok;}}
            else ok=draw_cached_group(0)&&ok;
            end();wait();ok=readback(960,544,candidate.data())&&ok;
            unsigned delta=0,visible=0;
            for(unsigned y=300;y<525;++y)for(unsigned x=546;x<920;++x){
                size_t i=(size_t(y)*960+x)*4;
                for(unsigned c=0;c<4;++c)delta=std::max(delta,unsigned(std::abs(int(reference[i+c])-int(candidate[i+c]))));
                visible+=reference[i]!=uint8_t(backgrounds[repeat]>>24);
            }
            ok=ok&&delta<=1&&visible>100;passed=passed&&ok;
            log("[retained-screen-self-test] mode=%u repeat=%u max_delta=%u visible=%u ok=%d",mode,repeat,delta,visible,int(ok));
        }
    }
    destroy(t);retainedAllowed=oldAllowed;retainedValid[0]=false;++retainedRevision[0];
    return passed;
}
