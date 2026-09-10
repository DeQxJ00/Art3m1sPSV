"""Validate complete A1 snapshots. Partial snapshots are never used for totals."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
regions=('heap','cdram','uncached');owners=('source','decode','ready','provider','texture','fixed','offscreen','temporary')
snapshots=[];pending={};errors=[]
for number,line in enumerate(a.log.read_text(encoding='utf-8',errors='replace').splitlines(),1):
    if '[resource-ledger] region=' not in line:continue
    raw=dict(re.findall(r'(\w+)=([^\s]+)',line));region=raw['region']
    if region not in regions:errors.append(f'line {number}: unknown region {region}');continue
    values={k:int(raw[k]) for k in (*owners,'live','reserved','retired','peak_live','peak_committed','faults','events')}
    if sum(values[k] for k in owners)!=values['live']:errors.append(f'line {number}: owner sum mismatch')
    if not 0<=values['retired']<=values['live']<=values['peak_live']:errors.append(f'line {number}: invalid live/retired peak')
    if values['reserved']<0 or values['live']+values['reserved']>values['peak_committed']:errors.append(f'line {number}: invalid reservation peak')
    if values['faults']:errors.append(f'line {number}: ledger faults={values["faults"]}')
    if region=='heap':pending={}
    pending[region]=values
    if all(r in pending for r in regions):
        if len({pending[r]['events'] for r in regions})!=1:errors.append(f'line {number}: mixed snapshots')
        snapshots.append(pending);pending={}
if not snapshots:errors.append('No complete snapshots')
result={'log':str(a.log),'complete_snapshots':len(snapshots),'errors':errors,'last':snapshots[-1] if snapshots else None,'coverage':'Image CPU paths and Direct-owned GPU blocks only; media/font/codec/driver/scratch not fully tracked.'}
if a.out:a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2));raise SystemExit(bool(errors))
