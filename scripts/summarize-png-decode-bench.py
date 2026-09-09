"""Summarize completed hardware probe output; never treat partial logs as a result."""
import argparse,json,re,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
m=json.loads((a.run/'manifest.json').read_text());assert m['completed'] and m['restored']
if m.get('performance_valid') is False:
    raise SystemExit('Rejected interrupted run: '+m.get('interruption','performance marked invalid'))
log=(a.run/'result.log').read_text();assert 'DONE failures=0' in log
result={}
for line in log.splitlines():
    if line.startswith('PIXELS '):
        parts=line.split();fields=dict(x.split('=',1) for x in parts[2:]);assert fields['equal']=='1'
        result[parts[1]]={'pixels_equal':True,'width':int(fields['width']),'height':int(fields['height']),
            'rust_peak':int(fields['rust_peak']),'libpng_peak':int(fields['libpng_peak']),'samples':{'rust':[],'libpng':[]}}
    if line.startswith('TIME '):
        parts=line.split();f=dict(x.split('=',1) for x in parts[2:]);assert f['ok']=='1'
        result[parts[1]]['samples'][f['decoder']].append(int(f['us']))
for name,r in result.items():
    for decoder,times in r['samples'].items():
        assert len(times)==10
        r[decoder]={'median_us':statistics.median(times),'min_us':min(times),'max_us':max(times)}
    r['libpng_time_reduction_percent']=100*(1-r['libpng']['median_us']/r['rust']['median_us'])
(a.run/'summary.json').write_text(json.dumps(result,indent=2))
for name,r in result.items():print(f"{name}: Rust {r['rust']['median_us']/1000:.3f}ms; libpng {r['libpng']['median_us']/1000:.3f}ms; reduction {r['libpng_time_reduction_percent']:.1f}%; peak {r['rust_peak']}/{r['libpng_peak']}")
