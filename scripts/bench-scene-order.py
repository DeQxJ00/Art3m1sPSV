"""Compile the production traversal cache/comparator into an optimized host microbenchmark."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/effects-regression/order-bench'
out.mkdir(parents=True, exist_ok=True)
scene = (root / 'core/src/compositor/scene.rs').read_text(encoding='utf-8')
compare = scene[scene.index('fn compare_layer_id('):scene.index('/// 整棵场景树。')]
cache = (root / 'core/src/compositor/scene_order.rs').as_posix()
source = f'''use std::cmp::Ordering;
use std::hint::black_box;
use std::time::Instant;
const MESSAGE_LAYER_OVERLAY_PREFIX: &str = "@art3m1s-message-";
#[path = "{cache}"] mod cache;
{compare}
fn main() {{
 for count in [4,16,64,128] {{
  let ids:Vec<String>=(0..count).map(|i|format!("1.2.{{}}",i*37%count)).collect();
  let cache=cache::TraversalOrder::default();
  cache.sorted(Some("1.2"), &ids, compare_layer_id);
  let begin=Instant::now();
  for _ in 0..100000 {{
    let mut v:Vec<_>=black_box(&ids).iter().map(String::as_str).collect();
    v.sort_by(|a,b|compare_layer_id(a,b)); black_box(v);
  }}
  let baseline=begin.elapsed(); let begin=Instant::now();
  for _ in 0..100000 {{ black_box(cache.sorted(Some("1.2"),black_box(&ids),compare_layer_id)); }}
  let cached=begin.elapsed();
  println!("siblings={{count}} rounds=100000 baseline_us={{}} cached_us={{}} ratio={{:.2}}",baseline.as_micros(),cached.as_micros(),baseline.as_secs_f64()/cached.as_secs_f64());
 }}
}}
'''
(out/'bench.rs').write_text(source, encoding='utf-8')
compiler = Path('F:/WorkSpaceAI2/art3m1s-psv/.tools/rustup/toolchains/nightly-2026-08-28-x86_64-pc-windows-msvc/bin/rustc.exe')
subprocess.run([str(compiler), '--edition=2024', '-O', str(out/'bench.rs'), '-o', str(out/'bench.exe')], check=True)
result = subprocess.check_output([str(out/'bench.exe')], text=True)
(out/'result.txt').write_text(result, encoding='utf-8')
print(result)
