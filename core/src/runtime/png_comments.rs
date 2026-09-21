//! Script PNG metadata queries do not need to read/decode the image payload.
use std::collections::{HashMap,VecDeque};
use std::sync::{Arc,Mutex};

type Comments=HashMap<String,String>;
pub(super) type SharedComments=Arc<Mutex<CommentCache>>;
const LIMIT:usize=1024*1024;
const MAX_ENTRIES:usize=1024;
#[derive(Default)]
pub(super) struct CommentCache {
    entries: VecDeque<(String,Comments,usize,bool)>,
    bytes: usize,
    pub hits: u64,
    pub prepared_hits:u64,
    prepared:u64,
    generation:u64,
}
impl CommentCache {
    pub fn get(&mut self,path:&str)->Option<Comments>{
        let index=self.entries.iter().position(|(p,_,_,_)|p==path)?;
        let entry=self.entries.remove(index)?;let value=entry.1.clone();
        if entry.3{self.prepared_hits+=1;}
        self.entries.push_back(entry);self.hits+=1;Some(value)
    }
    pub fn insert(&mut self,path:String,value:Comments){
        self.insert_inner(path,value,false);
    }
    fn insert_inner(&mut self,path:String,value:Comments,prepared:bool)->bool{
        if let Some(i)=self.entries.iter().position(|(p,_,_,_)|p==&path){
            self.bytes-=self.entries.remove(i).unwrap().2;
        }
        // Bound retained string content and number of containers separately.
        // HashMap control bytes/allocator overhead are not a process heap limit.
        let bytes=path.capacity()+value.iter().map(|(k,v)|k.capacity()+v.capacity()).sum::<usize>();
        if bytes>LIMIT{return false;}
        while self.entries.len()>=MAX_ENTRIES || self.bytes+bytes>LIMIT{
            self.bytes-=self.entries.pop_front().unwrap().2;
        }
        self.bytes+=bytes;self.entries.push_back((path,value,bytes,prepared));true
    }
    fn prepare_epoch(&self,path:&str)->Option<u64>{
        (!self.entries.iter().any(|(p,_,_,_)|p==path)).then_some(self.generation)
    }
    fn insert_prepared(&mut self,path:String,value:Comments,generation:u64)->bool{
        if self.generation!=generation||self.prepare_epoch(&path).is_none(){return false;}
        if !self.insert_inner(path,value,true){return false;}
        self.prepared+=1;true
    }
    pub fn clear(&mut self){let generation=self.generation.wrapping_add(1);*self=Self{generation,..Default::default()};}
}

// Snapshot before the worker reads the source. A write invalidates this epoch;
// parse/publication never hold a loader, I/O or GPU lock together with this lock.
pub(super) fn prepare_epoch(cache:&SharedComments,path:&str)->Option<u64>{cache.lock().unwrap().prepare_epoch(path)}
pub(super) fn prepare_loaded(cache:&SharedComments,path:&str,bytes:&[u8],epoch:u64,cancelled:&dyn Fn()->bool){
    if cancelled(){return;}
    let started=std::time::Instant::now();let mut scanned=0usize;
    // Preserve the existing tEXt parser (Latin-1, duplicates, post-IDAT text).
    // Bound speculative allocation; oversized metadata falls back to the normal
    // callback without publishing an empty or truncated result.
    let value=read_comments(bytes.len() as u64,|at,n|{
        if cancelled(){return None;}
        scanned=scanned.checked_add(n)?;if scanned>LIMIT{return None;}
        let at=usize::try_from(at).ok()?;
        Some(bytes.get(at..at.checked_add(n)?)?.to_vec())
    });
    let Some(value)=value else{return;};
    if cancelled(){return;}
    let mut c=cache.lock().unwrap();
    let inserted=c.insert_prepared(path.to_owned(),value,epoch);
    let (count,retained,entries)=(c.prepared,c.bytes,c.entries.len());drop(c);
    if inserted&&(count<=16||count%64==0){crate::core_info!("[png-comments-prefetch] path={} prepared={} elapsed_us={} source_bytes={} read_bytes=0 retained_string_bytes={} entries={}",path,count,started.elapsed().as_micros(),bytes.len(),retained,entries);}
}

pub(super) fn read_buffered_comments(size:u64,mut read:impl FnMut(u64,usize)->Option<Vec<u8>>)->Option<Comments>{
    let mut start=0u64;let mut buffer=Vec::new();
    read_comments(size,|at,n|{
        let end=at.checked_add(n as u64)?;
        if at<start || end>start+buffer.len() as u64 {
            start=at;
            // Small expression/portrait PNGs cost less as one read than many
            // file lookups. Large images still skip their compressed payload.
            let ahead=if size<=32768 {size as usize}else{512};
            let count=(size.checked_sub(at)?).min(n.max(ahead) as u64) as usize;
            buffer=read(at,count)?;
        }
        let from=usize::try_from(at-start).ok()?;
        Some(buffer.get(from..from.checked_add(n)?)?.to_vec())
    })
}

pub(super) fn read_comments(
    size: u64,
    mut read: impl FnMut(u64, usize) -> Option<Vec<u8>>,
) -> Option<HashMap<String, String>> {
    let mut out = HashMap::new();
    if size < 8 || read(0, 8)?.as_slice() != b"\x89PNG\r\n\x1a\n" {
        return Some(out);
    }
    let mut offset = 8u64;
    while size.saturating_sub(offset) >= 8 {
        let header = read(offset, 8)?;
        if header.len() != 8 { return None; }
        let len = u32::from_be_bytes(header[..4].try_into().unwrap()) as u64;
        let start = offset + 8;
        if len > size - start { break; }
        let end = start + len;
        if &header[4..] == b"tEXt" && len != 0 {
            let data = read(start, usize::try_from(len).ok()?)?;
            if data.len() as u64 != len { return None; }
            if let Some(nul) = data.iter().position(|&b| b == 0) {
                // Preserve PNG Latin-1 conversion and last-duplicate-wins.
                let key = data[..nul].iter().map(|&b| b as char).collect();
                let value = data[nul + 1..].iter().map(|&b| b as char).collect();
                out.insert(key, value);
            }
        }
        if &header[4..] == b"IEND" { break; }
        // Existing callback reads tEXt only and does not validate CRCs. Keep
        // that behavior, including text after IDAT; skip every other payload.
        let Some(next) = end.checked_add(4) else { break; };
        offset = next;
    }
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn prepared_metadata_matches_foreground_parser_without_reading_idat(){
        let mut bytes=b"\x89PNG\r\n\x1a\n".to_vec();
        chunk(&mut bytes,b"tEXt",b"position\0old");
        chunk(&mut bytes,b"IDAT",&vec![77;1024*1024]);
        chunk(&mut bytes,b"tEXt",b"position\0new");chunk(&mut bytes,b"tEXt",b"latin\0\xe9");
        chunk(&mut bytes,b"IEND",b"");
        let cache=SharedComments::default();let epoch=prepare_epoch(&cache,"actual.png").unwrap();
        prepare_loaded(&cache,"actual.png",&bytes,epoch,&||false);
        let expected=read_buffered_comments(bytes.len() as u64,|at,n|Some(bytes[at as usize..at as usize+n].to_vec())).unwrap();
        let mut c=cache.lock().unwrap();assert_eq!(c.get("actual.png"),Some(expected));assert_eq!(c.prepared_hits,1);
        assert!(c.get("actual").is_none()); // Do not alias fallback names to different files.
    }
    #[test] fn prepared_empty_metadata_is_a_hit_but_oversize_is_not_false_empty(){
        let cache=SharedComments::default();let mut bytes=b"\x89PNG\r\n\x1a\n".to_vec();chunk(&mut bytes,b"IEND",b"");
        let epoch=prepare_epoch(&cache,"empty.png").unwrap();
        prepare_loaded(&cache,"empty.png",&bytes,epoch,&||false);
        assert_eq!(cache.lock().unwrap().get("empty.png"),Some(Comments::new()));
        let mut bytes=b"\x89PNG\r\n\x1a\n".to_vec();let mut text=vec![b'a';LIMIT+1];text[1]=0;
        chunk(&mut bytes,b"tEXt",&text);
        prepare_loaded(&cache,"large.png",&bytes,epoch,&||false);
        assert!(cache.lock().unwrap().get("large.png").is_none());
    }
    #[test] fn invalidation_cancellation_and_foreground_result_win_over_late_preparation(){
        let cache=SharedComments::default();let epoch=prepare_epoch(&cache,"a.png").unwrap();
        cache.lock().unwrap().clear();
        prepare_loaded(&cache,"a.png",b"not a png",epoch,&||false);
        assert!(cache.lock().unwrap().get("a.png").is_none());
        let epoch=prepare_epoch(&cache,"a.png").unwrap();
        prepare_loaded(&cache,"a.png",b"not a png",epoch,&||true);
        assert!(cache.lock().unwrap().get("a.png").is_none());
        cache.lock().unwrap().insert("a.png".into(),HashMap::from([("new".into(),"value".into())]));
        prepare_loaded(&cache,"a.png",b"not a png",epoch,&||false);
        assert_eq!(cache.lock().unwrap().get("a.png").unwrap()["new"],"value");
        assert_eq!(cache.lock().unwrap().prepared_hits,0);
    }
    #[test] fn preparation_never_holds_metadata_lock_while_checking_loader_cancellation(){
        let cache=SharedComments::default();let epoch=prepare_epoch(&cache,"a.png").unwrap();
        let mut bytes=b"\x89PNG\r\n\x1a\n".to_vec();chunk(&mut bytes,b"IEND",b"");
        let checks=std::cell::Cell::new(0);
        prepare_loaded(&cache,"a.png",&bytes,epoch,&||{
            let mut c=cache.try_lock().expect("metadata lock held across loader callback");
            checks.set(checks.get()+1);if checks.get()==3{c.clear();}false
        });
        assert!(checks.get()>=3);assert!(cache.lock().unwrap().get("a.png").is_none());
    }
    #[test]
    fn cached_metadata_is_bounded_promoted_and_cleared(){
        let mut c=CommentCache::default();
        for i in 0..MAX_ENTRIES{c.insert(format!("{i}"),Comments::new());}
        assert_eq!(c.get("0"),Some(Comments::new()));
        c.insert("next".into(),Comments::from([("x".into(),"12".into())]));
        assert!(c.get("1").is_none());assert!(c.get("0").is_some());
        c.insert("large".into(),Comments::from([("x".into(),"a".repeat(LIMIT))]));
        assert!(c.get("large").is_none());assert!(c.bytes<=LIMIT&&c.entries.len()<=MAX_ENTRIES);
        c.insert("next".into(),Comments::from([("x".into(),"changed".into())]));
        assert_eq!(c.get("next").unwrap()["x"],"changed");
        c.clear();assert_eq!(c.bytes,0);assert!(c.get("next").is_none());
    }
    #[test]
    fn read_ahead_combines_headers_and_text_while_skipping_large_payload(){
        let mut bytes=b"\x89PNG\r\n\x1a\n".to_vec();
        chunk(&mut bytes,b"IHDR",&[0;13]);
        chunk(&mut bytes,b"tEXt",b"before\0value");
        chunk(&mut bytes,b"IDAT",&vec![77;1024*1024]);
        chunk(&mut bytes,b"tEXt",b"after\0other");chunk(&mut bytes,b"IEND",b"");
        let reference=read_comments(bytes.len() as u64,|at,n|Some(bytes[at as usize..at as usize+n].to_vec())).unwrap();
        let(mut total,mut calls)=(0,0);
        let result=read_buffered_comments(bytes.len() as u64,|at,n|{
            total+=n;calls+=1;Some(bytes[at as usize..at as usize+n].to_vec())
        }).unwrap();
        assert_eq!(result,reference);assert_eq!(calls,2);assert!(total<1024);
        assert!(read_buffered_comments(20,|_,_|Some(vec![0;3])).is_none());
    }
    fn chunk(bytes: &mut Vec<u8>, kind: &[u8;4], data: &[u8]) {
        bytes.extend_from_slice(&(data.len() as u32).to_be_bytes());
        bytes.extend_from_slice(kind); bytes.extend_from_slice(data);
        bytes.extend_from_slice(&[0;4]);
    }
    #[test]
    fn reads_text_before_and_after_image_without_reading_image_payload() {
        let mut bytes=b"\x89PNG\r\n\x1a\n".to_vec();
        chunk(&mut bytes,b"tEXt",b"position\0old");
        let image_start=bytes.len()+8;
        chunk(&mut bytes,b"IDAT",&vec![77;1024*1024]);
        let image_end=image_start+1024*1024;
        chunk(&mut bytes,b"tEXt",b"position\0new");
        chunk(&mut bytes,b"tEXt",b"latin\0\xe9");
        chunk(&mut bytes,b"tEXt",b"invalid-no-null");
        chunk(&mut bytes,b"tEXt",b"");
        chunk(&mut bytes,b"IEND",b"");
        chunk(&mut bytes,b"tEXt",b"ignored\0after-end");
        let mut total=0;
        let result=read_comments(bytes.len() as u64,|at,n| {
            assert!(n>0);
            let at=at as usize;
            assert!(at+n<=image_start || at>=image_end);
            total+=n; Some(bytes[at..at+n].to_vec())
        }).unwrap();
        assert_eq!(result,HashMap::from([("position".into(),"new".into()),("latin".into(),"é".into())]));
        assert!(total<256);
    }
    #[test]
    fn bounded_malformed_and_short_reads() {
        assert!(read_comments(0,|_,_|panic!("empty file must not read")).unwrap().is_empty());
        assert!(read_comments(8,|_,_|Some(vec![0;8])).unwrap().is_empty());
        let mut bytes=b"\x89PNG\r\n\x1a\n".to_vec();
        bytes.extend_from_slice(&u32::MAX.to_be_bytes());bytes.extend_from_slice(b"tEXt");
        assert!(read_comments(16,|at,n|Some(bytes[at as usize..at as usize+n].to_vec())).unwrap().is_empty());
        assert!(read_comments(16,|at,_| if at==0{Some(bytes[..8].to_vec())}else{Some(vec![0;7])}).is_none());
        assert!(read_comments(16,|_,_|None).is_none());
    }
}
