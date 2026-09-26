// Render-thread only. The core admits source-hash-verified Kawase chains;
// this host additionally checks the plain opaque image and all encoded inputs.
namespace {
Offscreen blurPanTargets[2];
BlurPanKey blurPanKey;
bool blurPanValid=false;
bool blurPanAllowed=true;
unsigned blurPanOutput=0,blurPanBuilds=0,blurPanHits=0;
float blurPanX=0,blurPanY=0;
uint64_t blurPanUsed=0;
void blur_pan_clear(){
    // Called outside a scene, after a fence. Never release a queued reader.
    blurPanValid=false;
    for(auto& o:blurPanTargets)if(o.image){
        sceGxmDestroyRenderTarget(o.target);sceGxmSyncObjectDestroy(o.sync);
        auto* image=o.image;release({image->uid,image->pixels,0,image->allocation});delete image;o={};
    }
}
void blur_pan_begin(){
    if(blurPanTargets[0].image&&sceKernelGetProcessTimeWide()-blurPanUsed>2000000){
        blur_pan_clear();log("[blur-pan-cache] released inactive result");
    }
}
bool blur_pan_parameters(const CustomDraw& d,unsigned& sizeIndex,float& offset){
    if(!d.program||d.program>64||!externalPrograms[d.program-1])return false;
    bool sizeFound=false,offsetFound=false;
    for(const auto& u:externalPrograms[d.program-1]->uniforms){
        const auto* name=sceGxmProgramParameterGetName(u.parameter);
        if(!std::strcmp(name,"size")&&u.count==1){sizeIndex=u.offset;sizeFound=true;}
        if(!std::strcmp(name,"offset")&&u.count==1){offset=d.values[u.offset];offsetFound=true;}
        if(!std::strcmp(name,"alpha")&&(u.count!=1||d.values[u.offset]!=1))return false;
    }
    return sizeFound&&offsetFound&&std::isfinite(d.values[sizeIndex])&&std::isfinite(offset);
}
}
bool draw_cached_blur(Texture* texture,const EffectDraw& source,const EffectDraw* passes,
    unsigned count,uint64_t revision,float sx,float sy){
    if(!blurPanAllowed||!active||!texture||!passes||count<3||count>16||!init_builtins()
        ||source.mask||source.mesh||source.custom.program||source.custom.userTexture
        ||(source.blend!=0&&source.blend!=5)
        ||!(texture->opaque||texture->opaqueTiles.covers(0,0,1,1)))return false;
    for(float v:source.effects.flags)if(v!=0)return false;
    for(float v:source.tint)if(v!=1)return false;
    const auto* m=source.transform;
    if(m[1]!=0||m[2]!=0||m[0]<=0||m[3]<=0||source.quad[0]<=0||source.quad[1]<=0||sx<=0||sy<=0)return false;
    for(float v:source.transform)if(!std::isfinite(v))return false;
    auto fullClip=[&](const EffectDraw& d){return !d.hasClip||(d.clip[0]<=0&&d.clip[1]<=0
        &&(d.clip[0]+d.clip[2])*sx>=959.99f&&(d.clip[1]+d.clip[3])*sy>=543.99f);};
    if(!fullClip(source))return false;
    BlurPanKey key;key.texture=source.texture;key.content=texture->contentRevision;key.shaders=revision;
    key.shape={m[0],m[1],m[2],m[3],source.quad[0],source.quad[1],source.uv[0],source.uv[1],source.uv[2],source.uv[3],sx,sy};
    for(float v:key.shape)if(!std::isfinite(v))return false;
    key.count=count;key.blend=passes[count-1].blend;
    CustomDraw adjusted[16];float guard=2.f/544;
    for(unsigned i=0;i<count;++i){
        const auto& p=passes[i];
        if(p.mask||p.custom.userTexture||p.blend!=5||!fullClip(p))return false;
        for(float v:p.tint)if(v!=1)return false;
        key.passes[i].program=p.custom.program;
        for(unsigned j=0;j<128;++j){if(!std::isfinite(p.custom.values[j]))return false;key.passes[i].values[j]=p.custom.values[j];}
        unsigned sizeIndex=0;float offset=0;if(!blur_pan_parameters(p.custom,sizeIndex,offset))return false;
        adjusted[i]=p.custom;adjusted[i].values[sizeIndex]*=.5f;
        // Each pass has bilinear support as well as its explicit sample radius.
        guard+=std::abs(offset*adjusted[i].values[sizeIndex])+1.f/544;
    }
    const float x=m[4]*sx,y=m[5]*sy;float uv[4];
    if(!blur_pan_crop(0,0,guard,uv))return false;
    const bool hit=blurPanValid&&key==blurPanKey&&blur_pan_crop(x-blurPanX,y-blurPanY,guard,uv);
    if(!hit){
        blurPanValid=false;blurPanUsed=sceKernelGetProcessTimeWide();
        if(!create_offscreen(blurPanTargets[0])||!create_offscreen(blurPanTargets[1]))return false;
        auto* parent=current_offscreen();finish_scene_for_target_change();
        if(!resume_target(&blurPanTargets[0])){resume_target(parent);return false;}
        clear_offscreen();
        const float x0=x*.5f+240,y0=y*.5f+136;
        const float x1=x0+m[0]*source.quad[0]*sx*.5f,y1=y0+m[3]*source.quad[1]*sy*.5f;
        const auto* u=source.uv;
        Vertex q[]={{x0,y0,u[0],u[1],1,1,1,1},{x1,y0,u[0]+u[2],u[1],1,1,1,1},
            {x0,y1,u[0],u[1]+u[3],1,1,1,1},{x1,y1,u[0]+u[2],u[1]+u[3],1,1,1,1}};
        draw_quad(texture,q);finish_scene_for_target_change();unsigned input=0;
        for(unsigned i=0;i<count;++i){
            const unsigned output=1-input;
            if(!resume_target(&blurPanTargets[output])){resume_target(parent);return false;}
            Vertex v[]={{0,0,0,0,1,1,1,1},{960,0,1,0,1,1,1,1},{0,544,0,1,1,1,1,1},{960,544,1,1,1,1,1,1}};
            const auto before=frameStats.draws;
            draw_external(blurPanTargets[input].image,v,4,false,10,nullptr,nullptr,nullptr,adjusted[i]);
            finish_scene_for_target_change();
            if(frameStats.draws==before){resume_target(parent);return false;}input=output;
        }
        if(!resume_target(parent))return true;
        blurPanKey=key;blurPanX=x;blurPanY=y;blurPanOutput=input;blurPanValid=true;
        blur_pan_crop(0,0,guard,uv);++blurPanBuilds;
        if(blurPanBuilds<=8)log("[blur-pan-cache] build=%u passes=%u tex=%llu content=%llu at=%.2f,%.2f guard=%.5f storage_mib=4.0",
            blurPanBuilds,count,(unsigned long long)source.texture,(unsigned long long)key.content,x,y,guard);
    }else{
        ++blurPanHits;
        if(blurPanHits<=3||blurPanHits%300==0)log("[blur-pan-cache] hit=%u builds=%u delta=%.2f,%.2f",blurPanHits,blurPanBuilds,x-blurPanX,y-blurPanY);
    }
    blurPanUsed=sceKernelGetProcessTimeWide();
    Vertex q[]={{0,0,uv[0],uv[1],1,1,1,1},{960,0,uv[2],uv[1],1,1,1,1},
        {0,544,uv[0],uv[3],1,1,1,1},{960,544,uv[2],uv[3],1,1,1,1}};
    // Already filtered premultiplied pixels: exact copy, no composite uniforms.
    BuiltinEffects neutral{};neutral.flags[0]=5;
    draw_builtin(blurPanTargets[blurPanOutput].image,q,4,false,5,nullptr,nullptr,neutral);
    return true;
}
