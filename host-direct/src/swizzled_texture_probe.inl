// Compare direct pre-swizzled upload with the existing upload-time conversion.
// Both paths use identical blocks, sizes, filtering and blending.
bool swizzled_texture_self_test(){
    bool passed=true;
    const unsigned sizes[][2]={{1,1},{12,8},{8,20},{17,9},{65,33},{960,544}};
    for(unsigned format:{1u,3u})for(bool opaque:{false,true})
    for(const auto& size:sizes){
        const auto w=size[0],h=size[1],stride=format==1?8u:16u;
        std::vector<uint8_t> linear(compressed_bytes(format,w,h,false));
        for(size_t i=0;i<linear.size()/stride;++i){
            auto* b=linear.data()+i*stride;const unsigned offset=format==1?0:8;
            if(format==3){b[0]=uint8_t(i*19);b[1]=255;for(unsigned j=2;j<8;++j)b[j]=uint8_t(i*31+j*17);}
            b[offset]=0;b[offset+1]=0xf8;b[offset+2]=0xe0;b[offset+3]=7;
            for(unsigned j=4;j<8;++j)b[offset+j]=uint8_t(i*13+j*29);
        }
        std::vector<uint8_t> sw(compressed_bytes(format,w,h,true));
        bool ok=compressed_swizzle(sw.data(),sw.size(),linear.data(),linear.size(),format,w,h);
        auto* reference=texture_compressed(w,h,format,opaque,linear.data(),linear.size());
        auto* candidate=texture_compressed(w,h,format,opaque,sw.data(),sw.size(),true);
        ok=ok&&reference&&candidate;
        std::vector<uint8_t> a(960*544*4),b(a.size());
        if(ok){
            Vertex q[]={{552.25f,306.25f,0,0,1,1,1,.8f},{914.25f,306.25f,1,0,1,1,1,.8f},
                        {552.25f,518.25f,0,1,1,1,1,.8f},{914.25f,518.25f,1,1,1,1,1,.8f}};
            BuiltinEffects e;e.flags[3]=1;for(float& c:e.corners)c=1;
            e.modelClip[0]=e.modelClip[1]=-1.e30f;e.modelClip[2]=e.modelClip[3]=1.e30f;
            auto render=[&](Texture* t,std::vector<uint8_t>& out){
                begin();rect(0,0,960,544,0x204060ff);draw_builtin(t,q,4,false,0,nullptr,nullptr,e);
                end();wait();return readback(960,544,out.data());
            };
            ok=render(reference,a)&&render(candidate,b);
        }
        unsigned delta=0,visible=0;
        for(unsigned y=300;y<525;++y)for(unsigned x=546;x<920;++x){
            const auto i=(size_t(y)*960+x)*4;
            for(unsigned c=0;c<4;++c)delta=std::max(delta,unsigned(std::abs(int(a[i+c])-int(b[i+c]))));
            visible+=a[i]!=32||a[i+1]!=64||a[i+2]!=96;
        }
        ok=ok&&delta==0&&visible>100;
        log("[gxmsw-self-test] format=%u opaque=%d size=%ux%u max_delta=%u visible=%u ok=%d",
            format,int(opaque),w,h,delta,visible,int(ok));
        passed=passed&&ok;destroy(reference);destroy(candidate);
    }
    log("[gxmsw-self-test] passed=%d",int(passed));return passed;
}
