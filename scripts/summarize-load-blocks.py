"""Summarize bounded slow-load samples; nested durations are not additive."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
pattern=re.compile(r'\[load-block\] op=(\S+) path=(.*?) tid=(-?\d+) wait_us=(\d+) work_us=(\d+) at_us=(\d+) result=(-?\d+)')
events=[];groups={}
for line in a.log.read_text(encoding='utf-8',errors='replace').splitlines():
    m=pattern.search(line)
    if not m:continue
    op,path,tid,wait,work,at,result=m.groups()
    event=dict(op=op,path=path,tid=int(tid),wait_us=int(wait),work_us=int(work),at_us=int(at),result=int(result))
    event['total_us']=event['wait_us']+event['work_us'];events.append(event)
    group=groups.setdefault((op,int(tid)),dict(op=op,tid=int(tid),recorded_samples=0,max_wait_us=0,max_work_us=0,max_total_us=0))
    group['recorded_samples']+=1
    for name in ['wait_us','work_us','total_us']:group['max_'+name]=max(group['max_'+name],event[name])
report={'log':str(a.log),'recorded_samples':len(events),'groups':list(groups.values()),
        'longest':sorted(events,key=lambda e:e['total_us'],reverse=True)[:20],
        'limits':'Only >=20ms operations, max64 records per translation unit. No all-operation percentiles or total blocked time can be inferred. Nested stages overlap; a worker operation is not proof of a blocked render thread.'}
if a.out:a.out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=True,indent=2))
