// Real GXM comparison, run once at startup before enabling queued target changes.
// Exercises nested groups, repeated sibling target reuse, mask consumption and
// retained target swaps. A full CPU fence closes every measured sample so the
// comparison cannot merely move its cost to the following frame.
namespace {
void offscreen_queue_self_test(){
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    offscreenQueueTesting=true;
    const bool oldDisabled=offscreenQueueDisabled;
    const bool hadMask=groups[1].mask.image!=nullptr;
    offscreenQueueDisabled=false;
    std::vector<uint8_t> reference(960*544*4),queued(reference.size());
    uint64_t times[2]{};bool ok=true;size_t differences=0;
    for(unsigned sample=0;sample<4&&ok;++sample){
        // Alternate the order to avoid consistently favoring a warm candidate.
        for(unsigned run=0;run<2;++run){
            const unsigned mode=run^(sample&1);
            offscreenQueueAllowed=mode!=0;
            begin();
            const auto started=sceKernelGetProcessTimeWide();
            rect(0,0,960,544,0x183048ff);
            EffectDraw normal{};normal.tint[0]=normal.tint[1]=normal.tint[2]=normal.tint[3]=1;
            normal.blend=5;normal.effects.transition[2]=1;
            ok=group_begin()&&ok;
            if(ok){
                rect(10,10,940,520,0x905020ff);
                for(unsigned part=0;part<4&&ok;++part){
                    ok=group_begin();
                    if(!ok)break;
                    const float x=40.f+part*210.f+sample*7.f;
                    rect(x,40,160,440,0xa04080a0+part*0x140a0800u);
                    rect(x+23,83+sample*9,95,70,0x20d080ff);
                    if(part==2){
                        ok=group_mask_begin();
                        if(ok)rect(x,40,160,440,0xffffff80);
                    }
                    group_end(normal,nullptr,1,1);
                }
                EffectDraw gray=normal;gray.effects.flags[0]=3;gray.effects.flags[1]=1;
                if(ok)ok=group_end_cached(gray,1,1,0);
            }
            if(ok&&(sample&1)){
                ok=group_begin();
                if(ok){
                    rect(105,105,90,90,0xf0c020c0);
                    float bounds[]={105,105,90,90};
                    ok=overlay_end_cached(1,bounds);
                }
            }
            if(active){
                // Deliberately use a CPU fence only at the end of the chain.
                finish_scene_for_target_change(true);
                const auto elapsed=sceKernelGetProcessTimeWide()-started;
                if(sample)times[mode]+=elapsed; // First sample includes allocation.
                if(ok){
                    auto& pixels=mode?queued:reference;
                    copy_completed_frame(buffers[back].pixels,960,544,pixels.data());
                }
                // Display queue / vblank time is outside the measured chain.
                if(resume_target(nullptr))end();else ok=false;
            }else ok=false;
            wait();groupDepth=0;
        }
        if(ok){
            for(size_t i=0;i<reference.size();++i)differences+=reference[i]!=queued[i];
            // Also reject two identically blank/black outputs.
            const auto* p=reference.data()+(100*960+100)*4;
            ok=differences==0&&p[0]>20&&p[1]>20&&p[2]>20;
        }
    }
    for(auto& valid:retainedValid)valid=false;
    for(auto& revision:retainedRevision)++revision;
    retainedHits=retainedBuilds=0;
    // The nested mask is probe scratch, not a reason to reserve another 2 MiB
    // throughout games that never use this depth. All queued readers are done.
    if(!hadMask&&groups[1].mask.image){
        auto& mask=groups[1].mask;
        sceGxmDestroyRenderTarget(mask.target);sceGxmSyncObjectDestroy(mask.sync);
        auto* image=mask.image;release({image->uid,image->pixels,0,image->allocation});delete image;
        mask={};
    }
    offscreenQueueAllowed=ok;offscreenQueueDisabled=oldDisabled;offscreenQueueTesting=false;
    log("[offscreen-queue-self-test] pass=%d differing_bytes=%u fenced_us=%llu queued_us=%llu samples=3; complete GPU chains, excluding display queue",
        int(ok),unsigned(differences),(unsigned long long)times[0],(unsigned long long)times[1]);
#endif
}
}
