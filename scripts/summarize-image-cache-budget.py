"""Check shared retention snapshots and report decoded/encoded first-use evidence."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
snapshots=[];ready=[];errors=[];hits=[];demotions=[]
for i,line in enumerate(a.log.read_text(encoding='utf-8',errors='replace').splitlines(),1):
    fields=dict(re.findall(r'(\w+)=([^\s]+)',line))
    if '[image-cache-budget]' in line:
        row={k:int(fields[k]) for k in ('ready','idle','total','limit','idle_limit')};row['ready_goal']=int(fields.get('ready_goal',0));snapshots.append(row)
        if row['ready']+row['idle']!=row['total'] or row['total']>row['limit'] or row['idle']>row['idle_limit']:errors.append(f'line {i}: shared retention mismatch')
        if not 0<=row['ready_goal']<=row['limit'] or row['idle']>row['limit']-max(row['ready'],row['ready_goal']):errors.append(f'line {i}: idle did not honor requested headroom')
    if '[surface-prefetch] ready ' in line:
        used=int(fields['cache_bytes']);limit=int(fields['cache_budget']);ready.append(used)
        if used>limit:errors.append(f'line {i}: ready exceeds available budget')
    if '[surface-prefetch] demote ' in line:demotions.append(fields)
    if 'GXM prefetch-hit ' in line or 'GXM prefetch-encoded-hit ' in line:
        if any(x in fields.get('name','') for x in ('kun_z2a0100','tor_z2a0100','line21','line22','line23','zbg27k')):
            hits.append({'name':fields['name'],'kind':'encoded' if 'prefetch-encoded-hit' in line else 'pixels'})
if not snapshots:errors.append('No shared retention snapshots; cannot validate candidate')
result={'log':str(a.log),'snapshots':len(snapshots),'errors':errors,'ready_peak':max(ready,default=0),'last':snapshots[-1] if snapshots else None,'demotions_logged':len(demotions),'target_hits':hits,'scope':'Retained ready+inactive objects; active scene, alignment, decoder scratch and GPU retirement are excluded.'}
if a.out:a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2));raise SystemExit(bool(errors))
