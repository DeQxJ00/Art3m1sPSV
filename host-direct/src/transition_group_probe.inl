// Absolute pixel proof for rule fades applied to premultiplied group targets.
// Exercise both the specialized shader and its generic fallback.
bool transition_group_self_test(){
    if(active)return false;
    const uint8_t maskPixel[]={128,128,128,255};
    auto* mask=texture(1,1,maskPixel);if(!mask)return false;
    const bool previousGeneric=genericBuiltinForced;
    bool passed=true;
    std::vector<uint8_t> pixels(960*544*4);
    for(unsigned alpha:{0u,128u,255u}){
        const uint8_t sourcePixel[]={200,100,50,uint8_t(alpha)};
        auto* source=texture(1,1,sourcePixel);
        if(!source){passed=false;break;}
        for(unsigned generic=0;generic<2;++generic)for(float progress:{0.f,.25f,.5f,.75f,1.f}){
            genericBuiltinForced=generic!=0;
            EffectDraw d{};for(float& v:d.tint)v=1;
            d.blend=5;d.effects.flags[0]=6;
            d.effects.transition[0]=progress;d.effects.transition[1]=.25f;
            Vertex q[]={{400,240,0,0,1,1,1,1},{600,240,1,0,1,1,1,1},
                {400,400,0,1,1,1,1,1},{600,400,1,1,1,1,1,1}};
            begin();rect(0,0,960,544,0x204060ff);
            bool ok=group_begin();
            if(ok){draw_quad(source,q);group_end(d,mask,1,1);}
            end();wait();ok=readback(960,544,pixels.data())&&ok;
            float f=std::max(0.f,std::min(1.f,(128.f/255-(progress*1.25f-.25f))/.25f));
            f=f*f*(3-2*f);const float a=alpha/255.f*f;
            const unsigned bg[]={32,64,96};unsigned delta=0;
            for(unsigned c=0;c<4;++c){
                const float expected=c==3?255:sourcePixel[c]*a+bg[c]*(1-a);
                delta=std::max(delta,unsigned(std::abs(int(pixels[(320*960+500)*4+c])-int(expected+.5f))));
            }
            ok=ok&&delta<=3;passed=passed&&ok;
            log("[transition-group-proof] alpha=%u generic=%u progress=%.2f max_delta=%u ok=%d",
                alpha,generic,progress,delta,int(ok));
        }
        wait();destroy(source);
    }
    // Compare the direct screen-space rule with the proven offscreen route.
    // UV cropping/translation must not move the rule's screen-space boundary.
    std::vector<uint8_t> ramp(256*4),reference(pixels.size());
    for(unsigned x=0;x<256;++x){ramp[x*4]=ramp[x*4+1]=ramp[x*4+2]=uint8_t(x);ramp[x*4+3]=255;}
    auto* spatialMask=texture(256,1,ramp.data());
    if(!spatialMask)passed=false;
    if(spatialMask)for(unsigned alpha:{128u,255u}){
        const uint8_t rgba[]={200,100,50,uint8_t(alpha)};auto* source=texture(1,1,rgba);
        if(!source){passed=false;break;}
        for(unsigned generic=0;generic<2;++generic)for(float progress:{0.f,.25f,.5f,.75f,1.f}){
            genericBuiltinForced=generic!=0;
            Vertex q[]={{215.25f,200.5f,.2f,.1f,1,1,1,.7f},{895.25f,200.5f,.8f,.1f,1,1,1,.7f},
                {215.25f,510.5f,.2f,.9f,1,1,1,.7f},{895.25f,510.5f,.8f,.9f,1,1,1,.7f}};
            EffectDraw d{};for(float& v:d.tint)v=1;d.blend=5;d.effects.flags[0]=6;
            d.effects.transition[0]=progress;d.effects.transition[1]=.25f;
            bool ok=true;
            for(unsigned direct=0;direct<2;++direct){
                begin();rect(0,0,960,544,0x204060ff);
                if(direct){auto e=d.effects;e.flags[0]=7;draw_builtin(source,q,4,false,0,nullptr,spatialMask,e);}
                else if(group_begin()){draw_quad(source,q);group_end(d,spatialMask,1,1);}
                else ok=false;
                end();wait();ok=readback(960,544,(direct?pixels:reference).data())&&ok;
            }
            unsigned delta=0;
            for(unsigned y=210;y<500;++y)for(unsigned x=200;x<920;++x)for(unsigned c=0;c<4;++c){
                const size_t i=(y*960+x)*4+c;
                delta=std::max(delta,unsigned(std::abs(int(pixels[i])-int(reference[i]))));
            }
            ok=ok&&delta<=3;passed=passed&&ok;
            log("[transition-direct-proof] alpha=%u generic=%u progress=%.2f max_delta=%u ok=%d",alpha,generic,progress,delta,int(ok));
        }
        wait();destroy(source);
    }
    wait();destroy(spatialMask);
    genericBuiltinForced=previousGeneric;wait();destroy(mask);
    log("[transition-group-proof] result=%d",int(passed));
    return passed;
}
