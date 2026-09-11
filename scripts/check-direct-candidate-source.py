"""Reject stale host snapshots before building a new Direct performance candidate.
Pinned historical packages may still be installed deliberately; this is a new-build check.
"""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('--out',type=Path);p.add_argument('--elf',type=Path);p.add_argument('--self',dest='self_file',type=Path);p.add_argument('--previous-self',type=Path);a=p.parse_args()
canonical=Path(__file__).resolve().parents[1]/'host-direct/src'
files=['main.cpp','gpu.cpp','gpu.hpp','bridge.cpp','menu.cpp','fallback_menu.hpp','fallback_menu_atlas.hpp','diagnostic_io.hpp','status_cache.hpp','log_queue.hpp','resource_ledger.hpp','opaque_tiles.hpp','opaque_scan_probe.hpp','shared_surface_pixels.hpp','image_certificate.hpp']
errors=[];hashes={}
for name in files:
 candidate=a.source/'src'/name
 if not candidate.exists():errors.append('missing '+name);continue
 expected=(canonical/name).read_text(encoding='utf-8');actual=candidate.read_text(encoding='utf-8')
 if actual!=expected:errors.append('source drift '+name)
 hashes[name]=hashlib.sha256(actual.encode()).hexdigest()
if a.elf:
 binary=a.elf.read_bytes()
 for marker in (b'max_flush_refresh_us=',b'[frame-spike]',b'[render-capabilities] startup_validation=1'):
  if marker not in binary:errors.append('missing compiled marker '+marker.decode())
if a.previous_self:
 if not a.self_file:errors.append('--previous-self requires --self')
 elif a.self_file.read_bytes()==a.previous_self.read_bytes():errors.append('unchanged eboot: verify stale objects before deployment')
result={'source':str(a.source),'errors':errors,'normalized_source_sha256':hashes}
if a.out:a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2));raise SystemExit(bool(errors))
