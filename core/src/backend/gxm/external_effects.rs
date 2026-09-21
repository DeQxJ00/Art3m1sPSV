//! External AGX1 packages generated from the game's fixed DX9 pixel wrapper.
//! This backend owns one host GXM context/render thread. Registrations live only
//! for the active renderer and are released before returning to the launcher.
use std::{cell::RefCell, collections::BTreeMap, ffi::CString};
use serde::{Deserialize,Serialize};
use crate::render_pipeline::draw::ShaderEffect;
const MAX_VALUES:usize=128;
#[derive(Deserialize,Serialize,Clone)]
struct Uniform {name:String,offset:usize,count:usize}
#[derive(Deserialize)]
struct Metadata {abi:u32,source_hash:String,uniforms:Vec<Uniform>}
#[derive(Deserialize,Serialize)]
struct Converted {abi:u32,source_hash:String,cg_hash:String,cg:String,uniforms:Vec<Uniform>}
struct Program {handle:u32,uniforms:Vec<Uniform>,builtin_gray:bool,builtin_mosaic:bool,builtin_blur:bool}
thread_local!{static PROGRAMS:RefCell<BTreeMap<String,Program>>=RefCell::new(BTreeMap::new());}
thread_local!{static REVISION:std::cell::Cell<u64>=const{std::cell::Cell::new(0)};}
fn changed(){REVISION.with(|r|r.set(r.get().saturating_add(1)));}
pub(super) fn revision()->u64{REVISION.with(|r|r.get())}
#[repr(C)]
#[derive(Clone,Copy)]
pub(super) struct CustomDraw {pub program:u32,pub values:[f32;MAX_VALUES],pub user_texture:u64}
impl Default for CustomDraw {fn default()->Self{Self{program:0,values:[0.;MAX_VALUES],user_texture:0}}}
unsafe extern "C" {
 fn art3m1s_gxm_shader_stage(stage:i32);
 fn art3m1s_gxm_external_conversion_enabled()->i32;
 fn art3m1s_gxm_external_cache_path(file:*const std::ffi::c_char,out:*mut std::ffi::c_char,size:usize)->i32;
 fn art3m1s_gxm_external_shared_cache_path(file:*const std::ffi::c_char,out:*mut std::ffi::c_char,size:usize)->i32;
 fn art3m1s_gxm_external_compile(id:*const std::ffi::c_char,key:*const std::ffi::c_char,cg:*const std::ffi::c_char)->u32;
 fn art3m1s_gxm_external_compiler_end();
 fn art3m1s_gxm_external_register(data:*const u8,len:usize)->u32;
 fn art3m1s_gxm_external_uniform(handle:u32,name:*const std::ffi::c_char,offset:u32,count:u32)->i32;
 fn art3m1s_gxm_external_release(handle:u32);
}
fn hash(data:&[u8])->String {format!("{:016x}",data.iter().fold(0xcbf29ce484222325u64,|n,b|(n^u64::from(*b)).wrapping_mul(0x100000001b3)))}
fn parse<'a>(source:&[u8],package:&'a[u8])->Result<(Metadata,&'a[u8]),String>{
 parse_hashed(&hash(source),package)
}
fn parse_hashed<'a>(source_hash:&str,package:&'a[u8])->Result<(Metadata,&'a[u8]),String>{
 if package.len()<12||&package[..4]!=b"AGX1"{return Err("not an AGX1 external shader package".into());}
 let m=u32::from_le_bytes(package[4..8].try_into().unwrap()) as usize;
 let b=u32::from_le_bytes(package[8..12].try_into().unwrap()) as usize;
 if m>32768||!(156..=1024*1024).contains(&b)||12+m+b!=package.len(){return Err("external shader length/limit mismatch".into());}
 let meta:Metadata=serde_json::from_slice(&package[12..12+m]).map_err(|e|e.to_string())?;
 if meta.abi!=1||meta.source_hash!=source_hash{return Err("external shader ABI/source hash mismatch; recompile companion".into());}
 let mut occupied=[false;MAX_VALUES];let mut names=std::collections::HashSet::new();
 for u in &meta.uniforms{
  if u.name.is_empty()||u.name.len()>63||u.name.starts_with("art_")||!u.name.bytes().all(|b|b.is_ascii_alphanumeric()||b==b'_')
    || !names.insert(&u.name)||u.count==0||u.count>MAX_VALUES||u.offset>MAX_VALUES-u.count {return Err("external uniform bounds/name".into());}
  for i in u.offset..u.offset+u.count{if occupied[i]{return Err("external uniform overlap".into());}occupied[i]=true;}
 }
 let gxp=&package[12+m..];
 let declared=u32::from_le_bytes(gxp[8..12].try_into().unwrap()) as usize;
 if &gxp[..4]!=b"GXP\0"||declared<156||declared>b||(b!=declared&&b!=((declared+3)&!3)){return Err("GXP size/magic mismatch".into());}
 Ok((meta,gxp))
}
pub(super) fn register(id:&str,source:&[u8],package:&[u8])->Result<(),String>{
 if id.is_empty()||id.len()>128||matches!(id,"group-composite"|"alpha-mask"|"rule-trans"|"sprite"){return Err("reserved/invalid shader id".into());}
 let(meta,gxp)=parse(source,package)?;
 let handle=unsafe{art3m1s_gxm_external_register(gxp.as_ptr(),gxp.len())};
 if handle==0{return Err("host rejected GXP program".into());}
 for u in &meta.uniforms{
  let name=CString::new(u.name.as_str()).unwrap();
  if unsafe{art3m1s_gxm_external_uniform(handle,name.as_ptr(),u.offset as u32,u.count as u32)}==0{
   unsafe{art3m1s_gxm_external_release(handle)};return Err(format!("GXP uniform mismatch: {}",u.name));
  }
 }
 changed();PROGRAMS.with(|p|{if let Some(old)=p.borrow_mut().insert(id.into(),Program{handle,uniforms:meta.uniforms,builtin_gray:false,builtin_mosaic:false,builtin_blur:false}){unsafe{art3m1s_gxm_external_release(old.handle)}}});
 Ok(())
}
pub(super) fn register_source(id:&str,source:&[u8])->Result<(),String>{
 register_source_at(id,id,source)
}
pub(super) fn register_source_at(id:&str,file:&str,source:&[u8])->Result<(),String>{
 if id.is_empty()||id.len()>128||matches!(id,"group-composite"|"alpha-mask"|"rule-trans"|"sprite"){return Err("reserved/invalid shader id".into());}
 if let Some((name,package))=super::bundled_effects::lookup(&hash(source)){
  unsafe{art3m1s_gxm_shader_stage(3)}; // builtin
  register(id,source,package)?;
  PROGRAMS.with(|p|{if let Some(program)=p.borrow_mut().get_mut(id){program.builtin_gray=name=="gray";program.builtin_mosaic=name=="mosaic";program.builtin_blur=matches!(name,"blur_h"|"blur_v");}});
  crate::core_info!("[shader-builtin] id={} implementation={} bytes={} no runtime conversion/compile",id,name,package.len());
  return Ok(());
 }
 let id_c=CString::new(file).map_err(|_|"invalid shader path")?;
 let mut base=[0u8;1024];
 let cache=if unsafe{art3m1s_gxm_external_cache_path(id_c.as_ptr(),base.as_mut_ptr().cast(),base.len())}!=0{
  Some(String::from_utf8_lossy(&base[..base.iter().position(|b|*b==0).unwrap_or(base.len())]).to_string())
 }else{None};
 let mut shared_base=[0u8;1024];
 let shared=if unsafe{art3m1s_gxm_external_shared_cache_path(id_c.as_ptr(),shared_base.as_mut_ptr().cast(),shared_base.len())}!=0{
  Some(String::from_utf8_lossy(&shared_base[..shared_base.iter().position(|b|*b==0).unwrap_or(shared_base.len())]).to_string())
 }else{None};
 unsafe{art3m1s_gxm_shader_stage(4)}; // conversion cache or supported source preparation
 let converted=prepare_conversion_shared(source,cache.as_deref(),shared.as_deref(),unsafe{art3m1s_gxm_external_conversion_enabled()!=0})?;
 let key=CString::new(format!("{}-{}",hash(source),hash(converted.cg.as_bytes()))).unwrap();
 let source_c=CString::new(converted.cg).map_err(|_|"Cg contains NUL")?;
 let handle=unsafe{art3m1s_gxm_external_compile(id_c.as_ptr(),key.as_ptr(),source_c.as_ptr())};
 if handle==0{return Err("libshacccg compile/cache/program validation failed; see host.log".into());}
 let uniforms=converted.uniforms;
 for u in &uniforms{let name=CString::new(u.name.as_str()).unwrap();
  if unsafe{art3m1s_gxm_external_uniform(handle,name.as_ptr(),u.offset as u32,u.count as u32)}==0{
   unsafe{art3m1s_gxm_external_release(handle)};return Err(format!("GXP uniform mismatch: {}",u.name));
  }
 }
 changed();PROGRAMS.with(|p|{if let Some(old)=p.borrow_mut().insert(id.into(),Program{handle,uniforms,builtin_gray:false,builtin_mosaic:false,builtin_blur:false}){unsafe{art3m1s_gxm_external_release(old.handle)}}});Ok(())
}
fn conversion_valid(c:&Converted,source:&[u8])->bool{
 if c.abi!=2||c.source_hash!=hash(source)||c.cg.len()>256*1024||c.cg_hash!=hash(c.cg.as_bytes()){return false;}
 let mut occupied=[false;MAX_VALUES];let mut names=std::collections::HashSet::new();
 for u in &c.uniforms{
  if u.name.is_empty()||u.name.len()>63||u.name.starts_with("art_")||!u.name.bytes().all(|b|b.is_ascii_alphanumeric()||b==b'_')||!names.insert(&u.name)||u.count==0||u.count>MAX_VALUES||u.offset>MAX_VALUES-u.count{return false;}
  for i in u.offset..u.offset+u.count{if occupied[i]{return false;}occupied[i]=true;}
 }true
}
fn write_conversion_file(path:&str,data:&[u8])->std::io::Result<()>{
 let tmp=format!("{path}.tmp");std::fs::write(&tmp,data)?;std::fs::rename(&tmp,path)
}
#[cfg(test)]
fn prepare_conversion(source:&[u8],base:Option<&str>,allow:bool)->Result<Converted,String>{
 prepare_conversion_shared(source,base,None,allow)
}
fn prepare_conversion_shared(source:&[u8],base:Option<&str>,shared:Option<&str>,allow:bool)->Result<Converted,String>{
 for (scope,base) in [("game",base),("shared",shared)]{
 if let Some(base)=base{
  let path=format!("{base}.conversion.json");
  if std::fs::metadata(&path).is_ok_and(|m|m.len()<=512*1024){
   if let Ok(data)=std::fs::read(path){if let Ok(c)=serde_json::from_slice::<Converted>(&data){
    if conversion_valid(&c,source)&&std::fs::read(format!("{base}.cg")).is_ok_and(|s|s==c.cg.as_bytes()){
     crate::core_info!("[shader-cache] Cg hit scope={} path={}",scope,base);return Ok(c);
    }
   }}
  }
 }
 }
 if !allow{return Err("automatic shader conversion disabled; no matching Cg cache".into());}
 let(cg,layout)=crate::render_pipeline::hlsl::translate_cg_effect(source)?;
 let c=Converted{abi:2,source_hash:hash(source),cg_hash:hash(cg.as_bytes()),cg,uniforms:layout.into_iter().map(|u|Uniform{name:u.name,offset:u.offset,count:u.count}).collect()};
 if let Some(base)=base{
  let result=write_conversion_file(&format!("{base}.cg"),c.cg.as_bytes()).and_then(|_|write_conversion_file(&format!("{base}.conversion.json"),&serde_json::to_vec(&c).unwrap()));
  if let Err(e)=result{crate::core_warn!("[shader-cache] Cg save failed: {}",e);}
 }Ok(c)
}
pub(super) fn clear(){changed();unsafe{art3m1s_gxm_external_compiler_end()};PROGRAMS.with(|p|{for(_,v)in std::mem::take(&mut *p.borrow_mut()){unsafe{art3m1s_gxm_external_release(v.handle)}}});}
pub(super) fn registered(id:&str)->bool{PROGRAMS.with(|p|p.borrow().contains_key(id))}
// Source identity, not the user-selected ID/path. Arbitrary filters can emit
// RGB greater than alpha and depend on clamping at an isolation boundary.
pub(super) fn premultiplied_gray(effect:&ShaderEffect)->bool{
 effect.mask_texture.is_none() && effect.user_texture.is_none()
 && effect.uniforms.get("alpha").is_none_or(|v|v.as_slice()==[1.])
 && PROGRAMS.with(|p|p.borrow().get(&effect.name).is_some_and(|p|p.builtin_gray))
}
#[cfg(test)]
pub(super) fn mark_test_builtin_gray(id:&str){PROGRAMS.with(|p|p.borrow_mut().get_mut(id).unwrap().builtin_gray=true);}
pub(super) fn encode(effect:Option<&ShaderEffect>,alpha:f32,color:[f32;3])->CustomDraw{
 let mut d=CustomDraw::default();let Some(e)=effect else{return d;};
 PROGRAMS.with(|p|{let programs=p.borrow();if let Some(p)=programs.get(&e.name){
  d.program=p.handle;d.user_texture=e.user_texture.map_or(0,|t|t.0);
  for u in &p.uniforms{
   let dst=&mut d.values[u.offset..u.offset+u.count];
   if u.name=="alpha"{dst[0]=alpha;}else if u.name=="colorMultiply"{for(a,b)in dst.iter_mut().zip(color){*a=b;}}
   if let Some(values)=e.uniforms.get(&u.name){for(a,b)in dst.iter_mut().zip(values){if b.is_finite(){*a=*b;}}}
  }
 }});d
}
#[cfg(test)]
mod tests{
 use super::*;
 #[test]fn shared_cg_cache_fallback_priority_and_mismatch(){
  let source=b"float alpha; void vs(){resultPosition=position;resultTexCoord0=texCoord0;resultTexCoord1=texCoord1;} void ps(){result=float4(alpha,0,0,1);}";
  let root=std::env::temp_dir().join(format!("art3-shared-cg-{}-{}",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
  std::fs::create_dir(&root).unwrap();
  let local=root.join("local").to_str().unwrap().to_string();let shared=root.join("shared").to_str().unwrap().to_string();
  let original=prepare_conversion(source,Some(&shared),true).unwrap();
  let shared_bytes=std::fs::read(format!("{shared}.conversion.json")).unwrap();
  assert_eq!(prepare_conversion_shared(source,Some(&local),Some(&shared),false).unwrap().cg,original.cg);
  assert!(!std::path::Path::new(&format!("{local}.cg")).exists());
  assert!(prepare_conversion_shared(b"other-source",Some(&local),Some(&shared),false).is_err());
  let mut own=prepare_conversion(source,Some(&local),true).unwrap();own.cg.push_str("\n// per-game variant");own.cg_hash=hash(own.cg.as_bytes());
  std::fs::write(format!("{local}.cg"),&own.cg).unwrap();std::fs::write(format!("{local}.conversion.json"),serde_json::to_vec(&own).unwrap()).unwrap();
  assert_eq!(prepare_conversion_shared(source,Some(&local),Some(&shared),false).unwrap().cg,own.cg);
  std::fs::write(format!("{local}.cg"),b"corrupt").unwrap();
  assert_eq!(prepare_conversion_shared(source,Some(&local),Some(&shared),false).unwrap().cg,original.cg);
  assert_eq!(std::fs::read(format!("{shared}.conversion.json")).unwrap(),shared_bytes);
  std::fs::write(format!("{shared}.cg"),b"corrupt").unwrap();
  assert!(prepare_conversion_shared(source,Some(&local),Some(&shared),false).is_err());
  assert!(prepare_conversion_shared(source,Some(&local),Some(&shared),true).is_ok());
  assert_eq!(std::fs::read(format!("{shared}.cg")).unwrap(),b"corrupt");
  std::fs::remove_dir_all(root).unwrap();
 }
 #[test]fn cg_cache_reuses_without_conversion_and_invalidates_changed_source(){
  let source=b"float alpha; void vs(){resultPosition=position;resultTexCoord0=texCoord0;resultTexCoord1=texCoord1;} void ps(){result=float4(alpha,0,0,1);}";
  let base=std::env::temp_dir().join(format!("art3-cg-{}-{}",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
  let base=base.to_str().unwrap();assert!(prepare_conversion(source,Some(base),false).is_err());
  let c=prepare_conversion(source,Some(base),true).unwrap();
  assert_eq!(prepare_conversion(source,Some(base),false).unwrap().cg,c.cg);
  assert!(prepare_conversion(b"changed",Some(base),false).is_err());
  std::fs::write(format!("{base}.cg"),b"corrupt").unwrap();assert!(prepare_conversion(source,Some(base),false).is_err());
  prepare_conversion(source,Some(base),true).unwrap();
  assert!(prepare_conversion(source,Some(base),false).is_ok());
  std::fs::remove_file(format!("{base}.cg")).unwrap();std::fs::remove_file(format!("{base}.conversion.json")).unwrap();
 }
 #[test]fn bundled_programs_cover_34_unique_sources_and_reject_changed_source(){
  let hash="not a known source";assert!(super::super::bundled_effects::lookup(hash).is_none());
  let manifest:serde_json::Value=serde_json::from_str(include_str!("bundled/manifest.json")).unwrap();
  assert_eq!(manifest.as_array().unwrap().len(),34);
  for e in manifest.as_array().unwrap(){let(_,package)=super::super::bundled_effects::lookup(e["source_hash"].as_str().unwrap()).unwrap();
   assert!(parse_hashed(e["source_hash"].as_str().unwrap(),package).is_ok(),"{}",e["name"]);
   assert_eq!(&package[..4],b"AGX1");let m=u32::from_le_bytes(package[4..8].try_into().unwrap())as usize;
   let meta:Metadata=serde_json::from_slice(&package[12..12+m]).unwrap();assert_eq!(meta.source_hash,e["source_hash"].as_str().unwrap());
   let b=u32::from_le_bytes(package[8..12].try_into().unwrap())as usize;assert_eq!(12+m+b,package.len());
   assert!(unsafe{art3m1s_gxm_external_register(package[12+m..].as_ptr(),b)}!=0);
  }
 }
 #[unsafe(no_mangle)]extern "C" fn art3m1s_gxm_external_conversion_enabled()->i32{1}
 #[unsafe(no_mangle)]extern "C" fn art3m1s_gxm_external_cache_path(_:*const std::ffi::c_char,_:*mut std::ffi::c_char,_:usize)->i32{0}
 #[unsafe(no_mangle)]extern "C" fn art3m1s_gxm_external_shared_cache_path(_:*const std::ffi::c_char,_:*mut std::ffi::c_char,_:usize)->i32{0}
 #[unsafe(no_mangle)]extern "C" fn art3m1s_gxm_external_compile(_:*const std::ffi::c_char,_:*const std::ffi::c_char,_:*const std::ffi::c_char)->u32{7}
 #[unsafe(no_mangle)]extern "C" fn art3m1s_gxm_shader_stage(_:i32){}
 #[unsafe(no_mangle)]extern "C" fn art3m1s_gxm_external_compiler_end(){}
 #[unsafe(no_mangle)]extern "C" fn art3m1s_gxm_external_register(_:*const u8,_:usize)->u32{7}
 #[unsafe(no_mangle)]extern "C" fn art3m1s_gxm_external_uniform(_:u32,_:*const std::ffi::c_char,_:u32,_:u32)->i32{1}
 #[unsafe(no_mangle)]extern "C" fn art3m1s_gxm_external_release(_:u32){}
 pub(super) fn fixture(source:&[u8],uniforms:serde_json::Value)->Vec<u8>{
  let m=serde_json::to_vec(&serde_json::json!({"abi":1,"source_hash":hash(source),"uniforms":uniforms})).unwrap();
  let mut b=vec![0;156];b[..4].copy_from_slice(b"GXP\0");b[8..12].copy_from_slice(&156u32.to_le_bytes());
  let mut p=b"AGX1".to_vec();p.extend((m.len() as u32).to_le_bytes());p.extend((b.len() as u32).to_le_bytes());p.extend(m);p.extend(b);p
 }
 #[test]fn package_checks_source_length_uniform_bounds_and_atomic_replacement(){
  let src=b"test";let good=fixture(src,serde_json::json!([{"name":"alpha","offset":0,"count":1},{"name":"weights","offset":1,"count":8}]));
  assert!(parse(src,&good).is_ok());assert!(parse(b"changed",&good).is_err());assert!(parse(src,&good[..good.len()-1]).is_err());
  let bad=fixture(src,serde_json::json!([{"name":"bad","offset":127,"count":8}]));assert!(parse(src,&bad).is_err());
  register("test_external",src,&good).unwrap();assert!(registered("test_external"));
  assert!(register("test_external",b"changed",&good).is_err());assert!(registered("test_external"));
  let e=ShaderEffect{name:"test_external".into(),uniforms:[("weights".into(),vec![0.5,0.25])].into(),mask_texture:None,user_texture:None};
  let d=encode(Some(&e),0.7,[1.;3]);assert_eq!(&d.values[..4],&[0.7,0.5,0.25,0.]);clear();assert!(!registered("test_external"));
 }
}

// Restrict the source-cache optimization to the source-hash-verified built-in.
// Mask/user inputs and invalid coordinates keep the original isolated route.
pub(super) fn cacheable_mosaic(effect:&ShaderEffect)->bool {
 effect.mask_texture.is_none() && effect.user_texture.is_none()
 && ["size","ratio"].iter().all(|key|effect.uniforms.get(*key)
     .is_some_and(|v|v.len()==1 && v[0].is_finite() && v[0]>0.))
 && revision()!=u64::MAX
 && PROGRAMS.with(|p|p.borrow().get(&effect.name).is_some_and(|p|p.builtin_mosaic))
}
// Verified mosaic only selects a bilinear sample. Verified H/V blur is a
// nonnegative weighted sum. With alpha=1 and no mask, both preserve RGB<=A;
// a neutral source-over wrapper can be removed while the filter still runs.
pub(super) fn premultiplied_spatial_filter(effect:&ShaderEffect)->bool {
 if effect.mask_texture.is_some() || effect.user_texture.is_some()
    || effect.uniforms.get("alpha").is_some_and(|v|v.as_slice()!=[1.]) {return false;}
 if cacheable_mosaic(effect){return true;}
 PROGRAMS.with(|p|p.borrow().get(&effect.name).is_some_and(|p|p.builtin_blur))
    && effect.uniforms.get("weights").is_some_and(|v|v.len()==8 && v.iter().all(|w|w.is_finite()&&(0. ..=1.).contains(w)))
}
#[cfg(test)]
pub(super) fn mark_test_builtin_mosaic(id:&str){PROGRAMS.with(|p|p.borrow_mut().get_mut(id).unwrap().builtin_mosaic=true);changed();}
