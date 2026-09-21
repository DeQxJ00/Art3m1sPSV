//! Immutable input dependencies for a generic effect node.
//! Host stores the actual pre-filter render target; this describes its inputs.
use super::*;

#[derive(Default)]
pub(super) struct InputSnapshot {
    pub valid: bool,
    size:(u32,u32), programs:u64,
    commands:Vec<DrawCommand>, groups:Vec<ShaderGroup>, masks:Vec<DrawCommand>,
    textures:Vec<(u64,u64)>,
}
impl InputSnapshot {
    fn nested(frame:&DrawList,gi:usize)->impl Iterator<Item=&ShaderGroup> + '_ {
        let g=&frame.shader_groups[gi];
        frame.shader_groups.iter().enumerate().take(gi)
            .filter(move |(_,n)|n.start>=g.start && n.end<=g.end && n.start<n.end)
            .map(|(_,n)|n)
    }
    pub fn matches(&self,frame:&DrawList,gi:usize,size:(u32,u32))->bool {
        self.valid && self.dependencies_match(frame,gi,size)
    }
    pub fn dependencies_match(&self,frame:&DrawList,gi:usize,size:(u32,u32))->bool {
        let g=&frame.shader_groups[gi];
        self.size==size
            && self.programs==super::super::external_effects::revision()
            && self.commands==frame.commands[g.start..g.end]
            && self.masks==frame.mask_commands
            && self.groups.len()==Self::nested(frame,gi).count()
            && self.groups.iter().zip(Self::nested(frame,gi)).all(|(a,b)|
                a.start==b.start-g.start && a.end==b.end-g.start && a.key==b.key
                && a.effect==b.effect && a.clip_bounds==b.clip_bounds && a.mask_range==b.mask_range)
            && self.textures.iter().all(|&(id,r)|unsafe{art3m1s_gxm_texture_content_revision(id)==r})
    }
    pub fn store(&mut self,frame:&DrawList,gi:usize,size:(u32,u32)) {
        let g=&frame.shader_groups[gi];self.size=size;
        self.programs=super::super::external_effects::revision();
        self.commands.clear();self.commands.extend_from_slice(&frame.commands[g.start..g.end]);
        self.groups.clear();self.groups.extend(Self::nested(frame,gi).map(|n|{
            let mut n=n.clone();n.start-=g.start;n.end-=g.start;n
        }));
        self.masks.clone_from(&frame.mask_commands);
        let mut ids=Vec::new();
        for c in self.commands.iter().chain(self.masks.iter()) {
            ids.push(c.texture.0);
            if let Some(e)=&c.shader{ids.extend(e.mask_texture.into_iter().chain(e.user_texture).map(|t|t.0));}
        }
        for g in &self.groups {ids.extend(g.effect.mask_texture.into_iter().chain(g.effect.user_texture).map(|t|t.0));}
        ids.sort_unstable();ids.dedup();self.textures.clear();
        self.textures.extend(ids.into_iter().map(|id|(id,unsafe{art3m1s_gxm_texture_content_revision(id)})));
    }
}
