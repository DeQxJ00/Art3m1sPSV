#![allow(dead_code)]
#[path="../../core/src/render_pipeline/recovered.rs"] mod recovered;
use std::sync::atomic::{AtomicUsize,Ordering};
static PREPARE:AtomicUsize=AtomicUsize::new(0);
static RELEASE:AtomicUsize=AtomicUsize::new(0);
static CLASSIFY:AtomicUsize=AtomicUsize::new(0);
static SERIAL:std::sync::Mutex<()>=std::sync::Mutex::new(());
#[unsafe(no_mangle)] extern "C" fn art3m1s_recovery_packet_prepare(id:u64)->i32 {assert_eq!(id,7);PREPARE.fetch_add(1,Ordering::SeqCst);1}
#[unsafe(no_mangle)] extern "C" fn art3m1s_recovery_packet_release(id:u64){assert_eq!(id,7);RELEASE.fetch_add(1,Ordering::SeqCst);}
#[unsafe(no_mangle)] extern "C" fn art3m1s_recovery_packet_source_over(id:u64)->i32 {assert_eq!(id,7);CLASSIFY.fetch_add(1,Ordering::SeqCst);1}
#[test] fn owned_snapshot_clones_and_foreign_thread_drop_are_queue_only(){
 let _lock=SERIAL.lock().unwrap();PREPARE.store(0,Ordering::SeqCst);RELEASE.store(0,Ordering::SeqCst);
 CLASSIFY.store(0,Ordering::SeqCst);
 let packet=recovered::RecoveredPacket::from_handle(7).unwrap();assert!(packet.prepare());assert!(packet.source_over());
 let other=packet.clone();std::thread::spawn(move||{assert!(!other.on_owner_thread());assert!(!other.prepare());assert!(!other.source_over());drop(other);}).join().unwrap();
 assert_eq!(CLASSIFY.load(Ordering::SeqCst),1);
 assert_eq!(PREPARE.load(Ordering::SeqCst),1);assert_eq!(RELEASE.load(Ordering::SeqCst),0);
 std::thread::spawn(move||drop(packet)).join().unwrap();assert_eq!(RELEASE.load(Ordering::SeqCst),1);
}
#[test] fn failure_snapshot_keeps_position_without_foreign_resource_access(){
 let _lock=SERIAL.lock().unwrap();PREPARE.store(0,Ordering::SeqCst);RELEASE.store(0,Ordering::SeqCst);
 let failed=recovered::RecoveredPacket::failed();assert_eq!(failed.handle(),0);assert!(!failed.prepare());assert!(!failed.source_over());
 let frame_copy=failed.clone();drop(failed);assert_eq!(frame_copy.handle(),0);drop(frame_copy);
 assert_eq!(PREPARE.load(Ordering::SeqCst),0);assert_eq!(RELEASE.load(Ordering::SeqCst),0);
}
