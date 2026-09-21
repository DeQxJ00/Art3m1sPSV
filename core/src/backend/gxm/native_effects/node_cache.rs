//! Input surfaces share the five physical slots with final composites.
//! A slot submitted or reserved this frame cannot be overwritten recursively.
use super::*;

#[derive(Default)]
struct Entry {
    input: InputSnapshot,
    identity: Option<ShaderGroup>,
    serial: u64,
    touched: u64,
}
#[derive(Default)]
pub(super) struct NodeCache {
    entries: [Entry;5],
    busy: [bool;5],
    final_owned: [bool;5],
    protected: [bool;5],
    clock: u64,
}

pub(super) fn eligible(frame:&DrawList,gi:usize)->bool {
    let g=&frame.shader_groups[gi];
    if g.start>=g.end || g.end>frame.commands.len() || g.end-g.start>4096
        || frame.mask_commands.len()>4096 || g.mask_range.is_some()
        || frame.commands[g.start..g.end].iter().any(|c|
            c.mesh.is_some()||c.native_emote.is_some()||c.stencil.is_some()) {return false;}
    // Avoid adding isolation to ordinary sprites. Reuse existing effect inputs,
    // including composites containing multiple registered child passes. A
    // single-pass neutral wrapper can already flatten without an extra target.
    super::super::external_effects::registered(&g.effect.name)
        || (g.effect.name==GROUP_COMPOSITE_SHADER && frame.shader_groups[..gi].iter().filter(|n|
            n.start>=g.start && n.end<=g.end && n.start<n.end
            && super::super::external_effects::registered(&n.effect.name)).take(2).count()>=2)
}
fn same_node(a:&ShaderGroup,b:&ShaderGroup)->bool {
    match (&a.key,&b.key) {
        (Some(a),Some(b))=>a==b,
        (None,None)=>a.start==b.start&&a.end==b.end&&a.effect.name==b.effect.name,
        _=>false,
    }
}
impl NodeCache {
    pub fn begin(&mut self){self.busy.fill(false);self.final_owned.fill(false);self.protected.fill(false);self.clock=self.clock.saturating_add(1);}
    pub fn claim_final(&mut self,slot:usize){self.final_owned[slot]=true;}
    pub fn release_final(&mut self,slot:usize){self.final_owned[slot]=false;}
    pub fn busy(&self,slot:usize)->bool {self.busy[slot]}
    pub fn protected(&self,slot:usize)->bool {self.protected[slot]}
    pub fn protect_inputs(&mut self,frame:&DrawList,size:(u32,u32)){
        // Keep reusable inputs safe from other input admissions. A settled
        // final composite may reclaim its slot after consuming these inputs.
        for (slot,e) in self.entries.iter().enumerate(){
            self.protected[slot]=e.input.valid
                && e.serial==unsafe{art3m1s_gxm_cache_slot_revision(slot as u32)}
                && (0..frame.shader_groups.len()).any(|gi|eligible(frame,gi)&&e.input.matches(frame,gi,size));
        }
    }
    pub fn use_slot(&mut self,slot:usize){self.busy[slot]=true;self.entries[slot].touched=self.clock;}
    pub fn find(&self,frame:&DrawList,gi:usize,size:(u32,u32))->Option<usize>{
        self.entries.iter().enumerate().find_map(|(i,e)|
            (e.serial==unsafe{art3m1s_gxm_cache_slot_revision(i as u32)}
                && e.input.matches(frame,gi,size)).then_some(i))
    }
    pub fn seen_changed(&self,frame:&DrawList,gi:usize)->bool {
        self.entries.iter().any(|e|e.identity.as_ref().is_some_and(|g|same_node(g,&frame.shader_groups[gi])))
    }
    pub fn ready_to_build(&self,frame:&DrawList,gi:usize,size:(u32,u32))->bool {
        let mut known=false;
        for e in &self.entries {
            if e.identity.as_ref().is_some_and(|g|same_node(g,&frame.shader_groups[gi])) {
                known=true;if e.input.dependencies_match(frame,gi,size){return true;}
            }
        }
        !known
    }
    pub fn observe_change(&mut self,frame:&DrawList,gi:usize,size:(u32,u32)){
        if let Some(slot)=(0..5).find(|&i|!self.busy[i]&&!self.protected[i]
            && self.entries[i].identity.as_ref().is_some_and(|g|same_node(g,&frame.shader_groups[gi]))) {
            // No GPU write: discard stale metadata and wait for an identical
            // observation. Changing parents can still reuse/build their children.
            let e=&mut self.entries[slot];e.input.store(frame,gi,size);e.input.valid=false;
            e.identity=Some(frame.shader_groups[gi].clone());e.touched=self.clock;
        }
    }
    pub fn reserve(&mut self,frame:&DrawList,gi:usize)->Option<usize>{
        let slot=(0..5).filter(|&i|!self.busy[i]&&!self.protected[i]&&!self.final_owned[i]).min_by_key(|&i|{
            let e=&self.entries[i];
            // Replace an earlier version of this node before an independent
            // child's reusable input; otherwise prefer unused / oldest slots.
            (e.identity.as_ref().is_none_or(|g|!same_node(g,&frame.shader_groups[gi])),
                e.input.valid,e.touched,4-i)
        })?;
        self.use_slot(slot);Some(slot)
    }
    pub fn store(&mut self,slot:usize,frame:&DrawList,gi:usize,size:(u32,u32),valid:bool){
        let e=&mut self.entries[slot];e.input.valid=valid;
        if valid {e.input.store(frame,gi,size);}
        e.identity=Some(frame.shader_groups[gi].clone());
        e.serial=unsafe{art3m1s_gxm_cache_slot_revision(slot as u32)};
    }
    pub fn invalidate(&mut self,slot:usize){self.entries[slot].input.valid=false;}
}
