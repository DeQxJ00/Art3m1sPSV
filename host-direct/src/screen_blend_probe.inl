// Opt-in device regression: straight sprite alpha must affect Screen RGB.
bool screen_blend_self_test(){
    const bool savedGeneric=genericBuiltinForced;
    std::vector<uint8_t> pixels(960*544*4);
    const unsigned background[]={32,64,96};
    const float tint[]={.75f,1.f,.5f};
    unsigned cases=0,maxError=0;bool passed=true;
    for(unsigned generic=0;generic<2;++generic)
    for(unsigned alpha:{0u,128u,255u})for(float opacity:{0.f,.5f,1.f}){
        const uint8_t rgba[]={240,160,80,uint8_t(alpha)};
        auto* source=texture(1,1,rgba);
        if(!source){passed=false;continue;}
        begin();genericBuiltinForced=generic!=0;
        rect(0,0,960,544,0x204060ff);
        Vertex q[]={{440,280,0,0,tint[0],tint[1],tint[2],opacity},
                    {500,280,1,0,tint[0],tint[1],tint[2],opacity},
                    {440,340,0,1,tint[0],tint[1],tint[2],opacity},
                    {500,340,1,1,tint[0],tint[1],tint[2],opacity}};
        BuiltinEffects effects{};
        const float clip[]={450,290,490,330};
        draw_builtin(source,q,4,false,3,clip,nullptr,effects);
        end();wait();bool ok=readback(960,544,pixels.data());
        const auto* inside=pixels.data()+(310*960+470)*4;
        const auto* outside=pixels.data()+(310*960+445)*4;
        for(unsigned c=0;c<3;++c){
            const float s=(rgba[c]/255.f)*tint[c]*(alpha/255.f)*opacity;
            const int expected=int(std::lround(255.f*(s+(background[c]/255.f)*(1.f-s))));
            const unsigned error=unsigned(std::abs(int(inside[c])-expected));
            maxError=std::max(maxError,error);
            ok=ok&&error<=2&&outside[c]==background[c];
        }
        ok=ok&&inside[3]==255&&outside[3]==255;
        log("[screen-blend-case] generic=%u tex_alpha=%u opacity=%.1f rgb=%u,%u,%u pass=%d",
            generic,alpha,opacity,inside[0],inside[1],inside[2],int(ok));
        passed=passed&&ok;++cases;destroy(source);
    }
    genericBuiltinForced=savedGeneric;
    log("[screen-blend-self-test] cases=%u max_error=%u pass=%d",cases,maxError,int(passed));
    return passed;
}
