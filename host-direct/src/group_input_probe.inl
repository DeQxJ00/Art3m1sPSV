// Compare first display, the following retained bake, and cache replay with
// full-frame pixels. Timings drain GPU work before stopping the clock.
namespace {
bool group_input_self_test(Texture* mask){
    const bool previousTesting=retainedTesting;retainedTesting=false;
    std::vector<uint8_t> reference[3],candidate(960*544*4);
    for(auto& pixels:reference)pixels.resize(candidate.size());
    uint64_t firstUs[2]{},bakeUs[2]{};size_t differences=0;bool ok=true;
    for(unsigned variant=0;variant<4&&ok;++variant){
        uint64_t variantFirst[2]{},variantBake[2]{};size_t variantDifferences=0;unsigned maxDelta=0;
        for(unsigned sample=0;sample<4&&ok;++sample){
            // Warm both ownership paths once, then measure three iterations.
            for(unsigned mode=0;mode<2&&ok;++mode){
                EffectDraw root{};root.tint[0]=root.tint[1]=root.tint[2]=1;
                root.tint[3]=variant==1?0.7f:1;root.blend=5;
                root.effects.flags[0]=3;root.effects.flags[1]=variant<2;
                root.effects.transition[2]=variant==0;
                if(variant==1){root.hasClip=1;root.clip[0]=101.25f;root.clip[1]=61.25f;root.clip[2]=731.5f;root.clip[3]=437.5f;}
                const unsigned slot=sample; // Include all four final slots.
                const unsigned inputSlot=variant==3?4:slot; // Shared node-input slot too.
                for(unsigned phase=0;phase<3&&ok;++phase){
                    begin();const auto started=sceKernelGetProcessTimeWide();
                    rect(0,0,960,544,0x183048ff);
                    if(phase==2){ok=draw_cached_group(slot);}
                    else if(mode&&phase==1){
                        ok=group_begin_cached_input(inputSlot);
                        if(ok)ok=group_end_cached(root,1,1,slot,variant==1?mask:nullptr);
                    }else{
                        const bool flatReference=variant>=2&&!mode&&phase==0;
                        ok=flatReference||group_begin();
                        if(ok){
                            rect(0,0,960,544,variant%2?0x90502050:0x905020ff);
                            for(unsigned part=0;part<4&&ok;++part){
                                ok=group_begin();if(!ok)break;
                                const float x=40.f+part*190.f+sample*7.f;
                                rect(x,40,240,440,0xa04080a0+part*0x140a0800u);
                                rect(x+23,83+sample*9,95,70,0x20d080ff);
                                EffectDraw child{};child.tint[0]=child.tint[1]=child.tint[2]=child.tint[3]=1;
                                child.blend=5;child.effects.flags[0]=3;
                                child.effects.transition[2]=0;group_end(child,nullptr,1,1);
                            }
                            if(ok){
                                if(phase==1)ok=group_end_cached(root,1,1,slot,variant==1?mask:nullptr);
                                else if(mode)ok=node_source_end(root,inputSlot,variant==1?mask:nullptr,nullptr,1,1);
                                else if(!flatReference)group_end(root,variant==1?mask:nullptr,1,1);
                            }
                        }
                    }
                    if(active){
                        finish_scene_for_target_change(true);
                        const auto elapsed=sceKernelGetProcessTimeWide()-started;
                        if(sample&&phase==0){firstUs[mode]+=elapsed;variantFirst[mode]+=elapsed;}
                        if(sample&&phase==1){bakeUs[mode]+=elapsed;variantBake[mode]+=elapsed;}
                        if(ok){
                            auto& pixels=mode?candidate:reference[phase];
                            copy_completed_frame(buffers[back].pixels,960,544,pixels.data());
                            if(mode){
                                for(size_t i=0;i<pixels.size();++i){
                                    const unsigned delta=unsigned(std::abs(int(pixels[i])-int(reference[phase][i])));
                                    differences+=delta!=0;variantDifferences+=delta!=0;maxDelta=std::max(maxDelta,delta);
                                }
                                const auto* p=reference[phase].data()+(100*960+200)*4;
                                // A formerly flattened transparent source can
                                // add one UNORM rounding step, like the existing
                                // retained output. Reject anything beyond 1/255.
                                ok=maxDelta<=(variant>=2?1u:0u)&&p[0]>20&&p[1]>20&&p[2]>20;
                            }
                        }
                        if(resume_target(nullptr))end();else ok=false;
                    }else ok=false;
                    wait();groupDepth=0;
                }
            }
        }
        log("[group-input-variant] variant=%u pass=%d differing_bytes=%u max_delta=%u first_redraw_us=%llu first_capture_us=%llu bake_redraw_us=%llu bake_reuse_us=%llu samples=3",
            variant,int(ok),unsigned(variantDifferences),maxDelta,(unsigned long long)variantFirst[0],(unsigned long long)variantFirst[1],
            (unsigned long long)variantBake[0],(unsigned long long)variantBake[1]);
    }
    retainedTesting=previousTesting;
    log("[group-input-self-test] pass=%d differing_bytes=%u first_redraw_us=%llu first_capture_us=%llu bake_redraw_us=%llu bake_reuse_us=%llu samples=12; complete GPU work excluding display queue",
        int(ok),unsigned(differences),(unsigned long long)firstUs[0],(unsigned long long)firstUs[1],
        (unsigned long long)bakeUs[0],(unsigned long long)bakeUs[1]);
    return ok;
}
}
