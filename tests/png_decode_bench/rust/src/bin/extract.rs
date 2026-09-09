use std::{ffi::CString,path::PathBuf};
fn main(){
    let args:Vec<_>=std::env::args().collect();let root=PathBuf::from(&args[1]);let out=PathBuf::from(&args[2]);std::fs::create_dir_all(&out).unwrap();
    let wanted=[("background.png","image/bg/zbg27k.png"),("portrait.png","image/fg/tor/z2/tor_z2a0100.png")];
    for file in std::fs::read_dir(&root).unwrap().flatten(){
        let name=file.file_name().to_string_lossy().into_owned();if !name.starts_with("root.pfs")||name.ends_with('~'){continue;}
        let path=CString::new(file.path().to_string_lossy().as_bytes()).unwrap();
        unsafe {let archive=pfs_upk::pfs_open_single(path.as_ptr(),c"utf-8".as_ptr());if archive.is_null(){continue;}
            for (output,needle) in wanted {
                for i in 0..pfs_upk::pfs_entry_count(archive) {
                    let mut buf=[0i8;1024];pfs_upk::pfs_entry_path(archive,i,buf.as_mut_ptr(),1024);
                    let source=std::ffi::CStr::from_ptr(buf.as_ptr());let s=source.to_string_lossy().replace('\\',"/");
                    if s.ends_with(needle)||s.ends_with(needle.trim_start_matches("image/")) {
                        let n=pfs_upk::pfs_file_size(archive,source.as_ptr());assert!(n>0);let mut data=vec![0u8;n as usize];
                        assert_eq!(pfs_upk::pfs_read(archive,source.as_ptr(),0,data.as_mut_ptr(),n as u32),n);
                        std::fs::write(out.join(output),data).unwrap();println!("{output}: {name} {s}");
                    }
                }
            }
            if !out.join("rgba32-game.png").exists(){
                for i in 0..pfs_upk::pfs_entry_count(archive){
                    let mut buf=[0i8;1024];pfs_upk::pfs_entry_path(archive,i,buf.as_mut_ptr(),1024);
                    let source=std::ffi::CStr::from_ptr(buf.as_ptr());let path=source.to_string_lossy();
                    if !path.ends_with(".png"){continue;}
                    let mut header=[0u8;33];if pfs_upk::pfs_read(archive,source.as_ptr(),0,header.as_mut_ptr(),33)!=33 {continue;}
                    if !header.starts_with(b"\x89PNG\r\n\x1a\n")||header[24]!=8||header[25]!=6 {continue;}
                    let w=u32::from_be_bytes(header[16..20].try_into().unwrap());let h=u32::from_be_bytes(header[20..24].try_into().unwrap());
                    if w<512||h<512||(w as u64)*(h as u64)>4*1024*1024 {continue;}
                    let n=pfs_upk::pfs_file_size(archive,source.as_ptr());if n<=0||n>8*1024*1024 {continue;}
                    let mut data=vec![0u8;n as usize];assert_eq!(pfs_upk::pfs_read(archive,source.as_ptr(),0,data.as_mut_ptr(),n as u32),n);
                    std::fs::write(out.join("rgba32-game.png"),data).unwrap();println!("rgba32-game.png: {name} {path} {w}x{h}");break;
                }
            }
            pfs_upk::pfs_close(archive);
        }
    }
}
