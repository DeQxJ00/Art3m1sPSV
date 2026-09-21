//! Optional, bounded hints from data already executed by the game's Lua VM.
//! Never evaluates game functions, traverses metatables, or reads asset files.
use mlua::{Lua,Table,Value};
use std::collections::HashSet;

fn string(v:Value)->Option<String>{match v{Value::String(s)=>{let s=s.to_str().ok()?.to_string();(s.len()<=1024&&!s.is_empty()).then_some(s)},_=>None}}
fn table(t:&Table,k:&str)->Option<Table>{t.raw_get(k).ok()}
fn text(t:&Table,k:&str)->Option<String>{string(t.raw_get(k).ok()?)}
pub(crate) fn data_table(lua:&Lua,path:&str)->Option<Table>{
    let key=if path.to_ascii_lowercase().ends_with(".ast"){"ast"}else if path.to_ascii_lowercase().ends_with(".ipt"){"ipt"}else{return None};
    lua.globals().raw_get(key).ok()
}

pub(crate) fn masks(lua:&Lua,ast:&Table)->Vec<String>{
    let globals=lua.globals();
    let Some(game)=table(&globals,"game") else{return vec![]};
    let Some(paths)=table(&game,"path") else{return vec![]};
    let Some(prefix)=text(&paths,"rule") else{return vec![]};
    let Some(ext)=text(&game,"ruleext") else{return vec![]};
    let init=table(&globals,"init");
    let mut result=Vec::new();let mut seen=HashSet::new();let mut visited=HashSet::new();let mut budget=16384usize;
    fn walk(t:&Table,depth:usize,budget:&mut usize,visited:&mut HashSet<usize>,f:&mut impl FnMut(&Table)){
        if depth>6||*budget==0||!visited.insert(t.to_pointer() as usize){return;}
        *budget-=1;f(t);
        for i in 1..=(*budget).min(16384){
            if *budget==0{break;}*budget-=1;
            match t.raw_get::<Value>(i){Ok(Value::Table(child))=>walk(&child,depth+1,budget,visited,f),Ok(Value::Nil)|Err(_)=>break,_=>{}}
        }
    }
    walk(ast,0,&mut budget,&mut visited,&mut |tag|{
        if result.len()>=128{return;}
        let command=tag.raw_get::<Value>(1).ok().and_then(string);
        // Image commands can carry their transition inline; the game's image
        // helper forwards p.rule to the same getRule/trans path.
        if !matches!(command.as_deref(),Some("extrans"|"trans"|"bg"|"fg"|"cg"|"cgdel")){return;}
        let mut rule=match tag.raw_get::<Value>("rule").ok(){Some(Value::Integer(n)) if (0..=999).contains(&n)=>format!("{n:03}"),Some(Value::Number(n)) if n>=0.&&n<=999.&&n.fract()==0.=>format!("{:03}",n as u32),Some(v)=>match string(v){Some(s)=>s,None=>return},None=>return};
        let mut language=false;
        if let Some(init)=&init{
            if let Ok(v)=init.raw_get::<Value>(rule.as_str()){
                match v{
                    Value::Table(t)=>{
                        if let Some(s)=t.raw_get::<Value>(1).ok().and_then(string){rule=s;}else{return;}
                        language=t.raw_get::<Value>(3).is_ok_and(|v|!matches!(v,Value::Nil|Value::Boolean(false)));
                    },Value::Nil|Value::Boolean(false)=>{},v=>{if let Some(s)=string(v){rule=s;}else{return;}}
                }
            }
        }
        // Language-dependent selection is left to the game; no guessed file.
        if language{return;}
        let path=format!("{}/{rule}{ext}",prefix.trim_end_matches(['/', '\\']));
        if seen.insert(path.clone()){result.push(path);}
    });result
}

pub(crate) fn frames(ipt:&Table)->Vec<String>{
    if !matches!(text(ipt,"mode").as_deref(),Some("anime"|"anime_full")){return vec![];}
    let Some(base)=table(ipt,"base") else{return vec![]};
    let Ok(max)=base.raw_get::<usize>("max") else{return vec![]};
    if max==0||max>128{return vec![];}
    let mut result=Vec::new();let mut seen=HashSet::new();
    for i in 1..=max{
        let Ok(t)=ipt.raw_get::<Table>(i) else{return vec![]};
        let Some(file)=t.raw_get::<Value>(1).ok().and_then(string) else{return vec![]};
        if seen.insert(file.clone()){result.push(file);}
    }result
}

pub(crate) fn animation_patterns(ast:&Table)->Vec<String>{
    let mut result=Vec::new();let mut files=HashSet::new();let mut tables=HashSet::new();let mut budget=16384usize;
    fn visit(t:&Table,depth:usize,left:&mut usize,tables:&mut HashSet<usize>,files:&mut HashSet<String>,out:&mut Vec<String>){
        if depth>6||*left==0||out.len()>=128||!tables.insert(t.to_pointer() as usize){return;}
        *left-=1;
        if let (Some(path),Some(file))=(text(t,"path"),text(t,"file")){
            let path=path.replace('\\',"/");
            if path.starts_with(":ani/")||path.starts_with(":anime/"){
                let pattern=format!("{path}{file}.ipt");
                if files.insert(pattern.clone()){out.push(pattern);}
            }
        }
        for i in 1..=(*left).min(16384){if *left==0||out.len()>=128{break;}*left-=1;
            match t.raw_get::<Value>(i){Ok(Value::Table(t))=>visit(&t,depth+1,left,tables,files,out),Ok(Value::Nil)|Err(_)=>break,_=>{}}
        }
    }
    visit(ast,0,&mut budget,&mut tables,&mut files,&mut result);result
}

// Interpret only this small data definition in a separate, bounded VM. It has
// no host callbacks or game globals, so probing cannot replace the live `ipt`.
// Complex definitions that need game functions keep their normal include path.
#[cfg(feature="backend-lua51")]
pub(crate) fn probe_frames(bytes:&[u8])->Vec<String>{
    if bytes.len()>128*1024{return vec![];}
    let Ok(lua)=Lua::new_with(mlua::StdLib::NONE,mlua::LuaOptions::default()) else{return vec![]};
    if lua.set_memory_limit(1024*1024).is_err(){return vec![];}
    let counter=std::sync::atomic::AtomicUsize::new(0);
    lua.set_hook(mlua::HookTriggers::new().every_nth_instruction(1000),move|_,_|{
        if counter.fetch_add(1,std::sync::atomic::Ordering::Relaxed)>=32{Err(mlua::Error::RuntimeError("IPT prefetch instruction limit".into()))}else{Ok(mlua::VmState::Continue)}
    });
    if lua.load(bytes).exec().is_err(){return vec![];}
    lua.globals().raw_get::<Table>("ipt").ok().map_or_else(Vec::new,|t|frames(&t))
}
#[cfg(not(feature="backend-lua51"))]
pub(crate) fn probe_frames(_bytes:&[u8])->Vec<String>{vec![]}

#[cfg(test)] mod tests{
    use super::*;
    #[test] fn chapter_uses_actual_rule_path_alias_order_and_deduplicates(){
        let lua=Lua::new();lua.load(r#"game={path={rule='custom/masks/'},ruleext='.png'};init={fade={'wipe_13',5}};ast={{{'extrans',rule='fade'},{'trans',rule=2},{'extrans',rule='fade'}}}"#).exec().unwrap();
        assert_eq!(masks(&lua,&data_table(&lua,"chapter.ast").unwrap()),["custom/masks/wipe_13.png","custom/masks/002.png"]);
    }
    #[test] fn hints_ignore_metatables_cycles_and_language_rules(){
        let lua=Lua::new();lua.load(r#"game={path={rule='r'},ruleext='.png'};init={localized={'a',1,true}};ast={};ast[1]=ast;ast[2]={'extrans',rule='localized'};setmetatable(ast,{__index=function() error('not raw') end})"#).exec().unwrap();
        assert!(masks(&lua,&data_table(&lua,"c.ast").unwrap()).is_empty());assert!(data_table(&lua,"ui.asb").is_none());
    }
    #[test] fn inline_background_rules_are_prefetched_and_deduplicated(){
        let lua=Lua::new();lua.load(r#"game={path={rule='image/rule/'},ruleext='.png'};init={};ast={{{'bg',file='zbg04a',rule='008b'}},{{'bg',rule='wipe_14'},{'bg',rule='wipe_14'}},{{'bg',rule='wipe_17'},{'fg',rule='wipe_17'}}}"#).exec().unwrap();
        assert_eq!(masks(&lua,&data_table(&lua,"chapter.ast").unwrap()),["image/rule/008b.png","image/rule/wipe_14.png","image/rule/wipe_17.png"]);
    }
    #[test] fn removal_only_transition_is_included_without_a_matching_show_command(){
        let lua=Lua::new();lua.load(r#"game={path={rule='image/rule/'},ruleext='.png'};init={cinema={'cinema_mask',40}};ast={{{'cgdel',id=5,rule='cinema',time=400},{'cgdel',id=1,rule='cinema'}}}"#).exec().unwrap();
        assert_eq!(masks(&lua,&data_table(&lua,"chapter.ast").unwrap()),["image/rule/cinema_mask.png"]);
    }
    #[test]
    #[ignore="requires user-installed chapter data via ARTEMIS_PREFETCH_AST"]
    fn installed_chapter_scan_covers_inline_explicit_and_removal_rules(){
        let path=std::env::var("ARTEMIS_PREFETCH_AST").expect("set installed s2_0611_d1_ro.ast path");
        let lua=Lua::new();lua.load("game={path={rule='image/rule/'},ruleext='.png'};init={}").exec().unwrap();
        lua.load(std::fs::read(path).unwrap()).exec().unwrap();
        let ast=data_table(&lua,"chapter.ast").unwrap();let rules=masks(&lua,&ast);
        assert_eq!(rules,["image/rule/wipe_17.png","image/rule/cinema.png","image/rule/wipe_02.png","image/rule/wipe_13.png","image/rule/wipe_14.png"]);
        let patterns=animation_patterns(&ast);assert!(patterns.iter().any(|p|p==":ani/line2.ipt"));
        println!("installed chapter rules={rules:?} animation_patterns={patterns:?}");
    }
    #[test] fn animation_validates_mode_max_and_frame_table(){
        let lua=Lua::new();lua.load("ipt={mode='anime_full',base={max=3},{'one'},{'two'},{'one'}}").exec().unwrap();
        let t=data_table(&lua,"a.ipt").unwrap();assert_eq!(frames(&t),["one","two"]);
        t.raw_set("mode","cut").unwrap();assert!(frames(&t).is_empty());
        t.raw_set("mode","anime").unwrap();t.raw_get::<Table>("base").unwrap().raw_set("max",129).unwrap();assert!(frames(&t).is_empty());
    }
    #[test] fn chapter_detects_explicit_animation_paths_in_first_use_order(){
        let lua=Lua::new();lua.load("ast={{{'bg',path=':ani/',file='rays'},{'fg',path=':fg/',file='face'},{'bg',path=':ani/',file='rays'},{'bg',path=':ani/intro/',file='in'}}}").exec().unwrap();
        assert_eq!(animation_patterns(&data_table(&lua,"a.ast").unwrap()),[":ani/rays.ipt",":ani/intro/in.ipt"]);
    }
    #[cfg(feature="backend-lua51")]
    #[test] fn definition_probe_is_isolated_bounded_and_recognizes_actual_frame_names(){
        assert_eq!(probe_frames(b"ipt={mode='anime_full',base={max=3},{'line21'},{'line22'},{'line23'}}"),["line21","line22","line23"]);
        assert!(probe_frames(b"while true do end").is_empty());
        assert!(probe_frames(b"e:include('game.lua')").is_empty());
        assert!(probe_frames(&vec![b' ';128*1024+1]).is_empty());
    }
}
