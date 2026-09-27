// Synthetic blocks have independently constructed RGBA references. These cases
// check block order, rectangular/NPOT padding, alpha modes and filtering on GXM.
bool bc3_texture_self_test(){
    const unsigned sizes[][2]={{4,4},{12,8},{17,9},{8,20},{65,33}};
    bool passed=true;
    for(const auto& size:sizes){
        const unsigned w=size[0],h=size[1],bw=(w+3)/4,bh=(h+3)/4;
        std::vector<uint8_t> blocks(bc3_source_bytes(w,h)),rgba(size_t(w)*h*4);
        for(unsigned by=0;by<bh;++by)for(unsigned bx=0;bx<bw;++bx){
            auto* b=blocks.data()+(size_t(by)*bw+bx)*16;
            const bool alternate=(bx+by)&1;
            b[0]=alternate?0:255;b[1]=alternate?255:0;
            b[8]=0;b[9]=0xf8; // Red, RGB565.
            b[10]=alternate?0xe0:0x1f;b[11]=alternate?7:0; // Green or blue.
            unsigned alphas[8]={b[0],b[1]};
            if(b[0]>b[1])for(unsigned i=2;i<8;++i)alphas[i]=((8-i)*b[0]+(i-1)*b[1])/7;
            else {for(unsigned i=2;i<6;++i)alphas[i]=((6-i)*b[0]+(i-1)*b[1])/5;alphas[6]=0;alphas[7]=255;}
            uint64_t ai=0;uint32_t ci=0;
            for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
                const unsigned p=y*4+x,a=(p+bx+by*3)%8,c=(x+y+bx)%4;
                ai|=uint64_t(a)<<(p*3);ci|=c<<(p*2);
                const unsigned xx=bx*4+x,yy=by*4+y;if(xx>=w||yy>=h)continue;
                auto* r=rgba.data()+(size_t(yy)*w+xx)*4;
                r[0]=c==0?255:(c==1?0:(c==2?170:85));
                r[1]=alternate?255-r[0]:0;r[2]=alternate?0:255-r[0];r[3]=alphas[a];
            }
            for(unsigned i=0;i<6;++i)b[2+i]=uint8_t(ai>>(i*8));
            for(unsigned i=0;i<4;++i)b[12+i]=uint8_t(ci>>(i*8));
        }
        auto* reference=texture(w,h,rgba.data());auto* compressed=texture_bc3(w,h,blocks.data(),blocks.size());
        bool ok=reference&&compressed;
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
            ok=render(reference,a)&&render(compressed,b);
        }
        unsigned delta=0,total=0,lo=255,hi=0;
        for(unsigned y=300;y<525;++y)for(unsigned x=546;x<920;++x){
            const auto at=(size_t(y)*960+x)*4;
            for(unsigned c=0;c<4;++c){const auto d=unsigned(std::abs(int(a[at+c])-int(b[at+c])));delta=std::max(delta,d);total+=d;}
            lo=std::min(lo,unsigned(a[at]));hi=std::max(hi,unsigned(a[at]));
        }
        const double mean=double(total)/(225*374*4);const auto outside=(300*960+546)*4;
        ok=ok&&delta<=2&&mean<=.3&&hi-lo>8&&a[outside]==32&&a[outside+1]==64&&a[outside+2]==96&&a[outside+3]==255;
        log("[bc3-self-test] size=%ux%u max_delta=%u mean_delta=%.6f range=%u ok=%d",w,h,delta,mean,hi-lo,int(ok));
        passed=passed&&ok;destroy(reference);destroy(compressed);
    }
    log("[bc3-self-test] enabled=%d; failed paths decode to RGBA",int(passed));return passed;
}
