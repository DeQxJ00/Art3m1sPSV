"""Check shared retention snapshots and report decoded/encoded first-use evidence."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
snapshots=[];ready=[];errors=[];hits=[];demotions=[];lanes=[]
for i,line in enumerate(a.log.read_text(encoding='utf-8',errors='replace').splitlines(),1):
    fields=dict(re.findall(r'(\w+)=([^\s]+)',line))
    if '[image-cache-budget]' in line:
        row={k:int(fields[k]) for k in ('ready','idle','total','limit','idle_limit')};row['ready_goal']=int(fields.get('ready_goal',0));snapshots.append(row)
        if row['ready']+row['idle']!=row['total'] or row['total']>row['limit'] or row['idle']>row['idle_limit']:errors.append(f'line {i}: shared retention mismatch')
        if not 0<=row['ready_goal']<=row['limit'] or row['idle']>row['limit']-max(row['ready'],row['ready_goal']):errors.append(f'line {i}: idle did not honor requested headroom')
        if 'parts_version' in fields:
            keys=('ready_decoded','ready_encoded','ready_proof','idle_decoded','idle_gpu_est','idle_encoded','idle_proof')
            if fields['parts_version']!='1' or any(k not in fields for k in keys):
                errors.append(f'line {i}: unsupported or incomplete cache breakdown')
            else:
                parts={k:int(fields[k]) for k in keys};row.update(parts)
                if min(parts.values())<0:errors.append(f'line {i}: negative cache component')
                if sum(parts[k] for k in keys[:3])!=row['ready']:errors.append(f'line {i}: ready component sum mismatch')
                if sum(parts[k] for k in keys[3:])!=row['idle']:errors.append(f'line {i}: idle component sum mismatch')
                if 'fixed_mask_decoded' in fields or 'fixed_mask_proof' in fields:
                    if not all(k in fields for k in ('fixed_mask_decoded','fixed_mask_proof')):errors.append(f'line {i}: incomplete fixed mask subset')
                    else:
                        row.update({k:int(fields[k]) for k in ('fixed_mask_decoded','fixed_mask_proof')})
                        if not 0<=row['fixed_mask_decoded']<=parts['ready_decoded'] or not 0<=row['fixed_mask_proof']<=parts['ready_proof']:errors.append(f'line {i}: fixed masks exceed ready components')
        if 'history_reserved' in fields:
            row.update({k:int(fields[k]) for k in ('history_reserved','history_surfaces','history_limit')})
            if not 0<=row['history_reserved']<=row['limit'] or row['ready']>row['limit']-row['history_reserved']:errors.append(f'line {i}: ready consumed history reserve')
            if row['history_surfaces']>row['history_limit']:errors.append(f'line {i}: history exceeds surface window')
    if '[surface-prefetch] ready ' in line:
        used=int(fields['cache_bytes']);limit=int(fields['cache_budget']);ready.append(used)
        if used>limit:errors.append(f'line {i}: ready exceeds available budget')
        if 'kind_bytes' in fields and int(fields['kind_bytes'])>int(fields['kind_limit']):errors.append(f'line {i}: prefetch lane exceeds cap')
    if '[image-prefetch-lanes]' in line:
        keys=[f'{kind}_{part}' for kind in ('mask','animation') for part in ('decoded','encoded','proof','limit')]
        if any(k not in fields for k in keys):errors.append(f'line {i}: incomplete lane breakdown')
        else:
            row={k:int(fields[k]) for k in keys};lanes.append(row)
            if min(row.values())<0:errors.append(f'line {i}: negative lane component')
            for kind in ('mask','animation'):
                if sum(row[f'{kind}_{p}'] for p in ('decoded','encoded','proof'))>row[f'{kind}_limit']:errors.append(f'line {i}: {kind} cap exceeded')
            if snapshots:
                for part in ('decoded','encoded','proof'):
                    if row[f'mask_{part}']+row[f'animation_{part}']>snapshots[-1].get(f'ready_{part}',0):errors.append(f'line {i}: lanes exceed ready {part}')
    if '[surface-prefetch] demote ' in line:demotions.append(fields)
    if 'GXM prefetch-hit ' in line or 'GXM prefetch-encoded-hit ' in line:
        if any(x in fields.get('name','') for x in ('kun_z2a0100','tor_z2a0100','line21','line22','line23','zbg27k')):
            hits.append({'name':fields['name'],'kind':'encoded' if 'prefetch-encoded-hit' in line else 'pixels'})
if not snapshots:errors.append('No shared retention snapshots; cannot validate candidate')
result={'log':str(a.log),'snapshots':len(snapshots),'errors':errors,'ready_peak':max(ready,default=0),'last':snapshots[-1] if snapshots else None,'demotions_logged':len(demotions),'target_hits':hits,'scope':'Retained ready+inactive objects; active scene, alignment, decoder scratch and GPU retirement are excluded.'}
# Old binaries report a combined total: never invent a zero encoded/decoded split.
last=result['last']
result['prefetch_lanes']=lanes[-1] if lanes else None
result['prefetch_lanes_note']='Subset of ready retention, never add again; active/idle GPU assets are outside these prefetch lane caps.'
result['fixed_mask_subset']=None if not last or 'fixed_mask_decoded' not in last else {
    'decoded_cpu_bytes':last['fixed_mask_decoded'],'alpha_proof_bytes':last['fixed_mask_proof'],
    'note':'Included in ready and total already; never add these bytes again.'}
result['last_breakdown']=None if not last or not all(k in last for k in ('ready_decoded','ready_encoded','ready_proof','idle_decoded','idle_gpu_est','idle_encoded','idle_proof')) else {
    'decoded_cpu_bytes':last['ready_decoded']+last['idle_decoded'],
    'gpu_retained_est_bytes':last['idle_gpu_est'],
    'encoded_bytes':last['ready_encoded']+last['idle_encoded'],
    'alpha_proof_bytes':last['ready_proof']+last['idle_proof'],
    'total_bytes':last['total'],
    'note':'Allocated capacities. CPU pixels and GPU estimates are separate charges, not unique image bytes. Compressed backups may accompany decoded pixels. GPU block alignment and active resources excluded.'}
if a.out:a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2));raise SystemExit(bool(errors))
