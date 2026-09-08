use std::{env, error::Error, fs, path::{Path, Component}};
use pf8::{Pf8Reader, Pf8Builder};

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = env::args().collect();
    let usage = "usage: art3m1s-assets list ARCHIVE | extract ARCHIVE EMPTY_DIR | pack DIR NEW_ARCHIVE";
    let command = args.get(1).ok_or(usage)?;
    let source = Path::new(args.get(2).ok_or(usage)?);
    match command.as_str() {
        "extract-file" => {
            let entry = args.get(3).ok_or("missing archive entry")?;
            let target = Path::new(args.get(4).ok_or("missing output path")?);
            if target.exists() { return Err("output file already exists".into()); }
            let mut reader = Pf8Reader::open(source)?;
            let data = reader.read_file(entry)?;
            if let Some(parent) = target.parent() { fs::create_dir_all(parent)?; }
            fs::write(target, data)?;
        }
        "list" => {
            let reader = Pf8Reader::open(source)?;
            let entries: Vec<_> = reader.entries().map(|entry| serde_json::json!({
                "path": entry.pf8_path(), "size": entry.size(), "offset": entry.offset()
            })).collect();
            println!("{}", serde_json::to_string_pretty(&entries)?);
        }
        "extract" => {
            let target = Path::new(args.get(3).ok_or(usage)?);
            if target.exists() && fs::read_dir(target)?.next().is_some() {
                return Err("extraction target must be empty".into());
            }
            let mut reader = Pf8Reader::open(source)?;
            // Validate all paths before allowing the upstream extractor to write.
            for entry in reader.entries() {
                let name = entry.pf8_path().replace('\\', "/");
                if name.contains(':') || Path::new(&name).components().any(|c| !matches!(c, Component::Normal(_) | Component::CurDir)) {
                    return Err(format!("unsafe archive path: {name}").into());
                }
            }
            fs::create_dir_all(target)?;
            reader.extract_all(target)?;
            eprintln!("extracted {} files", reader.len());
        }
        "pack" => {
            let target = Path::new(args.get(3).ok_or(usage)?);
            if target.exists() { return Err("output archive already exists".into()); }
            let mut builder = Pf8Builder::new();
            builder.base_path(source).add_dir(source)?;
            builder.write_to_file(target)?;
            eprintln!("packed {} files", builder.file_count());
        }
        "verify" => {
            let directory = Path::new(args.get(3).ok_or(usage)?);
            let mut reader = Pf8Reader::open(source)?;
            let paths: Vec<_> = reader.entries().map(|e| e.path().to_owned()).collect();
            for path in &paths {
                if reader.read_file(path)? != fs::read(directory.join(path))? {
                    return Err(format!("archive differs: {}", path.display()).into());
                }
            }
            eprintln!("verified {} file contents", paths.len());
        }
        "verify-conversion" => {
            let directory = Path::new(args.get(3).ok_or(usage)?);
            let scale: f32 = args.get(4).ok_or("missing scale")?.parse()?;
            let mut reader = Pf8Reader::open(source)?;
            let paths: Vec<_> = reader.entries().map(|e| e.path().to_owned()).collect();
            let mut images = 0;
            for path in &paths {
                let original = reader.read_file(path)?;
                let converted = fs::read(directory.join(path))?;
                if path.extension().is_some_and(|ext| ext.eq_ignore_ascii_case("png")) {
                    let dimensions = |bytes: &[u8]| -> Result<(u32, u32), Box<dyn Error>> {
                        if bytes.len() < 24 || &bytes[..8] != b"\x89PNG\r\n\x1a\n" { return Err("invalid PNG header".into()); }
                        Ok((u32::from_be_bytes(bytes[16..20].try_into()?), u32::from_be_bytes(bytes[20..24].try_into()?)))
                    };
                    let (w,h) = dimensions(&original)?;
                    let actual = dimensions(&converted)?;
                    let expected = (((w as f32 * scale) as u32).max(1), ((h as f32 * scale) as u32).max(1));
                    if actual != expected { return Err(format!("wrong dimensions: {}: {actual:?} != {expected:?}", path.display()).into()); }
                    images += 1;
                } else if path != Path::new("system.ini") && original != converted {
                    return Err(format!("unexpected non-image change: {}", path.display()).into());
                }
            }
            eprintln!("verified {} PNG dimensions and preserved non-image contents across {} entries", images, paths.len());
        }
        _ => return Err(usage.into()),
    }
    Ok(())
}
