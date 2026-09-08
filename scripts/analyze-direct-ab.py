"""Summarize only complete steady windows from bounded Direct on/off/on logs."""
import json,re,sys
from pathlib import Path
root=Path(sys.argv[1]); report=json.loads((root/'manifest.json').read_text(encoding='utf-8'))
state_marker='[layout-cache-state]' if report.get('mode')=='layout' else '[profile-state]'
def records(path):
    frames=[]; gxm={}; marker=0
    for line in path.read_text(encoding='utf-8',errors='replace').splitlines():
        nums={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
        if state_marker in line: marker=nums['at_us']
        if '[gxm-perf]' in line: gxm=nums
        if '[frame-perf]' in line: frames.append(dict(nums,gxm=gxm,marker=marker))
    return frames
last=records(root/'before.log')[-1]['at_us']
result=[]
for phase in report['phases']:
    allframes=records(root/phase['log']); selected=[]
    phase_marker=int(re.search(r'at_us=(\d+)',phase['state'])[1])
    for prev,f in zip(allframes,allframes[1:]):
        # Exclude boundary windows, including one second for polling/settling.
        if f['marker']==phase_marker and prev['at_us']>=max(last,phase_marker+1000000): selected.append(f)
    total=sum(f['frames'] for f in selected)
    entry={'phase':phase['name'],'state':phase['state'],'windows':len(selected),'frames':total}
    if total:
        for key in ['media_avg_us','logic_menu_avg_us','direct_present_avg_us','capture_avg_us']:
            entry[key]=round(sum(f[key]*f['frames'] for f in selected)/total,1)
        entry['total_avg_us']=sum(entry[k] for k in ['media_avg_us','logic_menu_avg_us','direct_present_avg_us','capture_avg_us'])
        entry['over20ms']=sum(f['over20ms'] for f in selected)
        entry['gxm_ranges']={key:[min(f['gxm'][key] for f in selected),max(f['gxm'][key] for f in selected)] for key in ['quads_avg','draws_avg','uniforms_avg','submit_avg_us','finish_avg_us']}
        entry['windows_raw']=selected
    result.append(entry);last=allframes[-1]['at_us']
(root/'comparison.json').write_text(json.dumps(result,indent=2))
for entry in result: print(json.dumps({k:v for k,v in entry.items() if k!='windows_raw'},ensure_ascii=False))
