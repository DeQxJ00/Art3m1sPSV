// Group mask targets are consumed through alpha by stencil/intermediate
// composites. Compare every blend's stored alpha with the original RGBA target.
bool alpha_mask_self_test(){
    Offscreen rgba,alpha;
    auto releaseTarget=[](Offscreen& o){
        if(!o.image)return;
        sceGxmDestroyRenderTarget(o.target);sceGxmSyncObjectDestroy(o.sync);
        auto* t=o.image;release({t->uid,t->pixels,0,t->allocation});delete t;o={};
    };
    if(!create_offscreen(rgba)||!create_offscreen(alpha,960,544,true)){
        releaseTarget(rgba);releaseTarget(alpha);return false;
    }
    std::vector<uint8_t> pixels(17*9*4),reference(960*544*4),candidate(reference.size());
    for(unsigned i=0;i<17*9;++i){auto* p=pixels.data()+i*4;
        p[0]=uint8_t(i*17);p[1]=uint8_t(i*31);p[2]=uint8_t(i*53);p[3]=uint8_t(i*37);}
    auto* t=texture(17,9,pixels.data());
    if(!t){releaseTarget(rgba);releaseTarget(alpha);return false;}
    bool passed=true;
    for(unsigned pass=0;pass<14;++pass){
        BuiltinEffects native;native.flags[3]=1;
        for(float& c:native.corners)c=1;
        native.modelClip[0]=native.modelClip[1]=-1.e30f;
        native.modelClip[2]=native.modelClip[3]=1.e30f;
        if(pass&1){native.corners[3]=.3f;native.corners[7]=.7f;native.corners[11]=.9f;}
        if(pass==12){native.wipe[0]=1.3f;native.wipe[1]=-.2f;native.wipe[2]=1;}
        Vertex q[]={{560.25f,310.75f,0,0,.2f,.8f,.4f,.6f},{885.5f,310.75f,1,0,.2f,.8f,.4f,.6f},
            {560.25f,500.25f,0,1,.2f,.8f,.4f,.6f},{885.5f,500.25f,1,1,.2f,.8f,.4f,.6f}};
        Vertex mesh[]={q[0],q[1],q[2],q[2],q[1],q[3]};mesh[0].x+=23;mesh[5].y-=19;
        const float clip[]={590.25f,332.75f,867.25f,484.5f};
        auto render=[&](Offscreen& o,std::vector<uint8_t>& out){
            begin();rect(0,0,960,544,0x204060ff);finish_scene_for_target_change();
            bool ok=resume_target(&o);
            if(ok){
                clear_offscreen();
                // A nonzero destination exercises destination-alpha factors.
                draw_builtin(t,q,4,false,0,nullptr,nullptr,{});
                if(pass==11)draw_quad(t,q,0,clip);
                else draw_builtin(t,pass==13?mesh:q,pass==13?6:4,pass==13,
                    pass<11?pass:0,pass==12?clip:nullptr,nullptr,native);
                finish_scene_for_target_change();
            }
            ok=resume_target(nullptr)&&ok;
            BuiltinEffects mask;mask.flags[0]=2;
            Vertex v[]={{540,296,0,0,.9f,.7f,.5f,1},{926,296,1,0,.9f,.7f,.5f,1},
                {540,524,0,1,.9f,.7f,.5f,1},{926,524,1,1,.9f,.7f,.5f,1}};
            // Both samplers use screen UVs; the source is a solid white texel.
            for(auto& p:v){p.u=p.x/960;p.v=p.y/544;}
            if(ok)draw_builtin(solid,v,4,false,5,nullptr,o.image,mask);
            end();wait();return readback(960,544,out.data())&&ok;
        };
        bool ok=render(rgba,reference);ok=render(alpha,candidate)&&ok;
        unsigned delta=0,total=0,visible=0;
        const size_t background=(298*960+542)*4;
        for(unsigned y=298;y<525;++y)for(unsigned x=542;x<926;++x){
            const auto at=(size_t(y)*960+x)*4;
            for(unsigned c=0;c<4;++c){const auto d=unsigned(std::abs(int(reference[at+c])-int(candidate[at+c])));delta=std::max(delta,d);total+=d;}
            visible+=reference[at]!=reference[background]||reference[at+1]!=reference[background+1]||reference[at+2]!=reference[background+2];
        }
        const double mean=double(total)/(227*384*4);
        ok=ok&&delta<=1&&mean<=.02&&visible>100;
        log("[alpha-mask-test] pass=%u max_delta=%u mean_delta=%.6f visible=%u ok=%d",pass,delta,mean,visible,int(ok));
        passed=passed&&ok;
    }
    destroy(t);wait();releaseTarget(rgba);releaseTarget(alpha);activeOffscreen=nullptr;
    SceIoStat st{};const bool disabled=sceIoGetstat("ux0:data/art3m1s-gxm/mask-target-rgba.on",&st)>=0;
    log("[alpha-mask-test] passed=%d enabled=%d",int(passed),int(passed&&!disabled));
    return passed&&!disabled;
}
