//! Translator for the small Direct3D9 HLSL effect subset used by Artemis.
//!
//! Artemis shader files contain a fixed pass/vertex wrapper and a compact
//! `ps()` pixel function. The renderer supplies its own vertex stage and turns
//! the pixel function plus global scalar declarations into GLSL.

const SAMPLERS: [(&str, &str); 4] = [
    ("samplerBack", "u_texture_back"),
    ("samplerFore", "u_texture_fore"),
    ("samplerMask", "u_texture_mask"),
    ("samplerUser", "u_texture_user"),
];

pub fn translate_effect(source: &[u8]) -> Result<String, String> {
    let decoded = String::from_utf8_lossy(source).replace('\r', "");
    let source = strip_comments(&decoded);
    let ps_start = source
        .find("void ps")
        .ok_or_else(|| "HLSL effect has no ps() function".to_string())?;
    let body_open = source[ps_start..]
        .find('{')
        .map(|offset| ps_start + offset)
        .ok_or_else(|| "HLSL ps() has no body".to_string())?;
    let body_close = matching_brace(&source, body_open)
        .ok_or_else(|| "HLSL ps() body is not balanced".to_string())?;

    let globals_end = source.find("void vs").unwrap_or(ps_start).min(ps_start);
    let globals = translate_globals(&source[..globals_end]);
    let body = translate_tokens(&source[body_open + 1..body_close]);

    Ok(format!(
        r#"
in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_texture_back;
uniform sampler2D u_texture_fore;
uniform sampler2D u_texture_mask;
uniform sampler2D u_texture_user;
{globals}

void main() {{
    vec2 texCoord0 = v_uv;
    vec2 texCoord1 = v_uv;
    vec4 result = vec4(0.0);
{body}
    frag_color = result;
}}
"#
    ))
}

fn translate_globals(source: &str) -> String {
    let mut out = String::new();
    for statement in source.split(';') {
        let line = statement.trim();
        if line.is_empty()
            || line.starts_with("texture ")
            || line.starts_with("sampler ")
            || line.contains("sampler_state")
        {
            continue;
        }

        if line.starts_with("const float") {
            out.push_str(&translate_tokens(line));
            out.push_str(";\n");
        } else if line.starts_with("float") {
            out.push_str("uniform ");
            out.push_str(&translate_tokens(line));
            out.push_str(";\n");
        }
    }
    out
}

fn translate_tokens(source: &str) -> String {
    let mut out = replace_identifier(source, "float2", "vec2");
    out = replace_identifier(&out, "float3", "vec3");
    out = replace_identifier(&out, "float4", "vec4");
    out = replace_identifier(&out, "tex2D", "texture");
    for (hlsl, glsl) in SAMPLERS {
        out = replace_identifier(&out, hlsl, glsl);
    }
    convert_loop_index_multiplication(&out)
}

fn convert_loop_index_multiplication(source: &str) -> String {
    let bytes = source.as_bytes();
    let mut out = String::with_capacity(source.len());
    let mut index = 0;
    while index < bytes.len() {
        if bytes[index] == b'*' {
            let mut cursor = index + 1;
            while bytes.get(cursor).is_some_and(u8::is_ascii_whitespace) {
                cursor += 1;
            }
            let negative = bytes.get(cursor) == Some(&b'-');
            if negative {
                cursor += 1;
            }
            if bytes.get(cursor) == Some(&b'i')
                && bytes.get(cursor + 1).is_none_or(|byte| !is_ident(*byte))
            {
                out.push('*');
                if negative {
                    out.push_str(" -float(i)");
                } else {
                    out.push_str(" float(i)");
                }
                index = cursor + 1;
                continue;
            }
        }
        let ch = source[index..].chars().next().expect("valid char boundary");
        out.push(ch);
        index += ch.len_utf8();
    }
    out
}

fn replace_identifier(source: &str, from: &str, to: &str) -> String {
    let mut out = String::with_capacity(source.len());
    let bytes = source.as_bytes();
    let mut index = 0;
    while index < bytes.len() {
        if source[index..].starts_with(from) {
            let before = index.checked_sub(1).and_then(|i| bytes.get(i)).copied();
            let after = bytes.get(index + from.len()).copied();
            if before.is_none_or(|byte| !is_ident(byte)) && after.is_none_or(|byte| !is_ident(byte))
            {
                out.push_str(to);
                index += from.len();
                continue;
            }
        }
        let ch = source[index..].chars().next().expect("valid char boundary");
        out.push(ch);
        index += ch.len_utf8();
    }
    out
}

fn is_ident(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || byte == b'_'
}

fn matching_brace(source: &str, open: usize) -> Option<usize> {
    let mut depth = 0usize;
    for (offset, ch) in source[open..].char_indices() {
        match ch {
            '{' => depth += 1,
            '}' => {
                depth = depth.checked_sub(1)?;
                if depth == 0 {
                    return Some(open + offset);
                }
            }
            _ => {}
        }
    }
    None
}

fn strip_comments(source: &str) -> String {
    let mut out = String::with_capacity(source.len());
    let mut chars = source.chars().peekable();
    while let Some(ch) = chars.next() {
        if ch == '/' && chars.peek() == Some(&'/') {
            chars.next();
            for next in chars.by_ref() {
                if next == '\n' {
                    out.push('\n');
                    break;
                }
            }
        } else if ch == '/' && chars.peek() == Some(&'*') {
            chars.next();
            let mut previous = '\0';
            for next in chars.by_ref() {
                if previous == '*' && next == '/' {
                    break;
                }
                previous = next;
            }
        } else {
            out.push(ch);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const SEPIA: &str = r#"
texture textureFore;
sampler samplerFore = sampler_state { texture = <textureFore>; };
float alpha;
float red;
const float3 graydata = float3(0.3, 0.6, 0.1);
void vs(float4 position : POSITION) { }
void ps(float2 texCoord0 : TEXCOORD0, float2 texCoord1 : TEXCOORD1,
        out float4 result : COLOR0)
{
    float4 fore = tex2D(samplerFore, texCoord1);
    float gray = dot(fore.rgb, graydata);
    fore.rgb = float3(gray * red, gray, gray);
    fore.a *= alpha;
    result = fore;
}
technique technique0 { }
"#;

    #[test]
    fn translates_artemis_effect_pixel_function() {
        let glsl = translate_effect(SEPIA.as_bytes()).unwrap();
        assert!(glsl.contains("uniform float alpha;"));
        assert!(glsl.contains("uniform float red;"));
        assert!(glsl.contains("const vec3 graydata = vec3"));
        assert!(glsl.contains("vec4 fore = texture(u_texture_fore, texCoord1);"));
        assert!(glsl.contains("frag_color = result;"));
        assert!(!glsl.contains("sampler_state"));
        assert!(!glsl.contains("float4"));
        assert!(!glsl.contains("tex2D"));
    }

    #[test]
    fn rejects_effect_without_pixel_function() {
        let error = translate_effect(b"void vs() {}").unwrap_err();
        assert!(error.contains("no ps()"));
    }
}

/// Cg front end for the same fixed Artemis DX9 effect wrapper. Original shader
/// bodies stay intact; unsupported global/vertex/backbuffer semantics fail closed.
#[derive(Clone,Debug)]
pub struct CgUniform {pub name:String,pub offset:usize,pub count:usize}
pub fn translate_cg_effect(data:&[u8])->Result<(String,Vec<CgUniform>),String>{
    let s=strip_comments(&String::from_utf8_lossy(data)).replace('\r',"");
    let extract=|name:&str|->Result<(usize,String),String>{
        let at=s.find(&format!("void {name}")).ok_or_else(||format!("missing DX9 {name} wrapper"))?;
        let open=at+s[at..].find('{').ok_or("missing body")?;
        let end=matching_brace(&s,open).ok_or("unbalanced body")?;
        Ok((at,s[open+1..end].into()))
    };
    let(vs_at,vs)=extract("vs")?;let(ps_at,ps)=extract("ps")?;
    let vs:String=vs.chars().filter(|c|!c.is_whitespace()).collect();
    if vs!="resultPosition=position;resultTexCoord0=texCoord0;resultTexCoord1=texCoord1;"{return Err("custom vertex shader requires a separate port".into());}
    if ps.split(|c:char|!c.is_ascii_alphanumeric()&&c!='_').any(|t|t=="samplerBack") {return Err("backbuffer shader requires explicit snapshot support".into());}
    let mut globals=&s[..vs_at.min(ps_at)];let mut declarations=String::new();let mut uniforms=Vec::new();let mut offset=0;
    while !globals.trim().is_empty(){
        globals=globals.trim_start();
        if globals.starts_with("sampler "){
            let open=globals.find('{').ok_or("sampler declaration")?;
            let close=matching_brace(globals,open).ok_or("sampler state")?;
            globals=globals[close+1..].trim_start().strip_prefix(';').ok_or("sampler terminator")?;continue;
        }
        let end=globals.find(';').ok_or("global terminator")?;let item=globals[..end].trim();globals=&globals[end+1..];
        if item.starts_with("texture "){continue;}
        if item.starts_with("const float"){declarations.push_str("static ");declarations.push_str(item);declarations.push_str(";\n");continue;}
        let split=item.find(char::is_whitespace).ok_or("global declaration")?;
        let ty=&item[..split];let components=match ty{"float"|"float1"=>1,"float2"=>2,"float3"=>3,"float4"=>4,_=>return Err(format!("unsupported global type {ty}"))};
        let var=item[split..].trim();let(name,array)=if let Some(i)=var.find('['){
            let array=var[i+1..].strip_suffix(']').ok_or("array declaration")?.trim().parse::<usize>().map_err(|_|"array length")?;
            (var[..i].trim(),array)
        }else{(var,1)};
        let count=components*array.min(129);
        if name.is_empty()||name.starts_with("art_")||name.len()>63||!name.bytes().all(|b|b.is_ascii_alphanumeric()||b==b'_')||count==0||offset+count>128
            ||uniforms.iter().any(|u:&CgUniform|u.name==name){return Err("uniform bounds/name".into());}
        uniforms.push(CgUniform{name:name.into(),offset,count});offset+=count;
        declarations.push_str("uniform ");declarations.push_str(item);declarations.push_str(";\n");
    }
    let cg=format!("uniform sampler2D samplerFore : TEXUNIT0;\nuniform sampler2D samplerMask : TEXUNIT1;\nuniform sampler2D samplerUser : TEXUNIT3;\nuniform float4 art_clip;\n{declarations}float4 main(float2 texCoord1:TEXCOORD0,float4 tint:COLOR0,float2 pixel:TEXCOORD1):COLOR {{\nfloat2 texCoord0=texCoord1; float4 result=float4(0,0,0,0);\n{ps}\nfloat cover=step(art_clip.x,pixel.x)*step(art_clip.y,pixel.y)*(1-step(art_clip.z,pixel.x))*(1-step(art_clip.w,pixel.y));\nreturn result*cover;\n}}\n");
    Ok((cg,uniforms))
}

#[cfg(test)]
mod cg_tests {
 use super::*;
 #[test]fn cg_constants_are_static_not_unset_uniforms(){
  let s=SRC.replace("float alpha;","const float3 graydata=float3(0.3,0.6,0.1); float alpha;");
  let(cg,u)=translate_cg_effect(s.as_bytes()).unwrap();assert!(cg.contains("static const float3 graydata="));assert!(!u.iter().any(|u|u.name=="graydata"));
 }
 const SRC:&str="texture textureFore; sampler samplerFore = sampler_state { texture = <textureFore>; }; float alpha; float3 colorMultiply; float weights[8]; void vs(float4 position:POSITION) { resultPosition = position; resultTexCoord0 = texCoord0; resultTexCoord1 = texCoord1; } void ps(float2 texCoord0:TEXCOORD0,float2 texCoord1:TEXCOORD1,out float4 result:COLOR0) { result=tex2D(samplerFore,texCoord1)*weights[0]*alpha; }";
 #[test]fn cg_preserves_body_array_layout_and_sampler_abi(){let(cg,u)=translate_cg_effect(SRC.as_bytes()).unwrap();assert!(cg.contains("TEXUNIT0"));assert!(cg.contains("uniform float weights[8]"));assert!(cg.contains("result=tex2D(samplerFore,texCoord1)*weights[0]*alpha;"));assert_eq!(u.iter().map(|u|(u.offset,u.count)).collect::<Vec<_>>(),[(0,1),(1,3),(4,8)]);}
 #[test]fn cg_rejects_wrong_vertex_backbuffer_and_bad_uniforms(){for s in [SRC.replace("resultPosition = position","resultPosition = position*2"),SRC.replace("result=tex2D(samplerFore","result=tex2D(samplerBack"),SRC.replace("weights[8]","weights[129]"),SRC.replace("float alpha","int alpha")]{assert!(translate_cg_effect(s.as_bytes()).is_err());}}
}
