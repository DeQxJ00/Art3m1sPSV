// Uses the runtime converter so offline and on-device uniform layouts agree.
#[path="../core/src/render_pipeline/hlsl.rs"] mod hlsl;
#[path="../core/src/render_pipeline/glsl.rs"] mod glsl;
#[path="../core/src/render_pipeline/dx11.rs"] mod dx11;
fn main(){
    let args:Vec<_>=std::env::args().collect();
    assert_eq!(args.len(),3,"usage: translate-portable-shader SOURCE OUTPUT_PREFIX");
    let data=std::fs::read(&args[1]).unwrap();
    let (cg,uniforms)=hlsl::translate_cg_effect(&data).expect("unsupported shader semantics");
    std::fs::write(format!("{}.cg",args[2]),cg).unwrap();
    let layout=uniforms.iter().map(|u|format!("{{\"name\":\"{}\",\"offset\":{},\"count\":{}}}",u.name,u.offset,u.count)).collect::<Vec<_>>().join(",");
    std::fs::write(format!("{}.uniforms.json",args[2]),format!("[{layout}]" )).unwrap();
}
