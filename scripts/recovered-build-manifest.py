#!/usr/bin/env python3
"""Record build inputs from both worktrees, including uncommitted/untracked source."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--recovery',type=Path,required=True);p.add_argument('--core',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--compare',type=Path);p.add_argument('--elf',type=Path);p.add_argument('--mode',choices=['ON','OFF'],required=True);a=p.parse_args()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def files(repo,dirs):
 if (repo/'source-manifest.json').is_file():
  return {p.relative_to(repo).as_posix():sha(p) for folder in dirs for p in sorted((repo/folder).rglob('*')) if p.is_file()}
 raw=subprocess.check_output(['git','-c','core.safecrlf=false','-c','core.autocrlf=false','-C',str(repo),'ls-files','-z','--cached','--others','--exclude-standard','--',*dirs]);result={}
 for name in sorted(set(raw.decode().split('\0'))-set([''])):
  path=repo/name
  if path.is_file():result[name]=sha(path)
 return result
def rev(path):
 if (path/'source-manifest.json').is_file():return json.loads((path/'source-manifest.json').read_text())['source_commit']
 return subprocess.check_output(['git','-c','core.safecrlf=false','-c','core.autocrlf=false','-C',str(path),'rev-parse','HEAD'],text=True).strip()
r={'scope':'Source hashes are observed worktree inputs; core archive is independently pinned. Matching core source/archive provenance requires the producing cargo build record.','integration_limits':['Recovered candidate fixes mesh=100/full 8x8 without changing saved preferences','No device acceptance implied by ELF linkage'], 'mode':a.mode,'host_head':rev(a.root),'recovery_head':rev(a.recovery),'core_head':rev(a.root/'core'),'core_archive_sha256':sha(a.core),
 'host_sources':files(a.root,['host-direct/src','host-direct/CMakeLists.txt','host','scripts/build-recovered-host.sh','scripts/recovered-build-manifest.py']),
 'core_sources':files(a.root/'core',['Cargo.toml','Cargo.lock','build.rs','src','crates']),
 'recovery_sources':files(a.recovery,['native','recovered'])}
if a.compare:
 old=json.loads(a.compare.read_text());changes=[k for k in r if old.get(k)!=r[k]]
 if changes:raise SystemExit('Build inputs changed while compiling: '+', '.join(changes)+'. Rebuild stable inputs before acceptance.')
if a.elf:r['elf_sha256']=sha(a.elf)
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2)+'\n');print('Recorded source manifest:',a.output)
