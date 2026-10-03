"""Summarize on-device cache benchmarks; round zero is retained but excluded from medians."""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
from statistics import median


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    groups=defaultdict(list)
    rows=list(csv.DictReader(a.csv.open()))
    for row in rows:
        for k in row:
            if k not in ('sample','route'):row[k]=int(row[k])
        if row['round']==0:continue
        chunk='whole' if row['chunk_bytes']==row['raw_bytes'] else str(row['chunk_bytes'])
        groups[(row['sample'],row['cpu_mhz'],row['codec'],row['route'],chunk)].append(row)
    items=[]
    for (sample,cpu,codec,route,chunk),measurements in groups.items():
        if len(measurements)!=3 or {r['round'] for r in measurements}!={1,2,3}:
            continue
        b=measurements[0]
        item={k:b[k] for k in ('scene','raw_bytes','png_bytes','cached_payload_bytes','scratch_bytes','gpu_alloc_bytes')}
        item.update(sample=sample,cpu_mhz=cpu,codec=codec,route=route,chunk=chunk,
                    cache_fraction=b['cached_payload_bytes']/b['raw_bytes'],
                    valid=all(r['ok'] for r in measurements),repeats=len(measurements))
        for metric in ('encode_us','read_us','alloc_us','decode_us','copy_us','seal_us','total_us','max_block_us'):
            item[metric]=median(r[metric] for r in measurements)
        item['total_max_us']=max(r['total_us'] for r in measurements)
        items.append(item)
    scene_groups=defaultdict(list)
    for item in items:
        # Active picture = base + fixed facial layer + first expression.
        if item['sample'].endswith('layer3'):continue
        scene_groups[(item['scene'],item['cpu_mhz'],item['codec'],item['route'],item['chunk'])].append(item)
    scenes=[]
    for (scene,cpu,codec,route,chunk),group in scene_groups.items():
        if len(group)!=3:continue
        scenes.append(dict(scene=scene,cpu_mhz=cpu,codec=codec,route=route,chunk=chunk,
            raw_mib=sum(i['raw_bytes'] for i in group)/1048576,
            cache_mib=sum(i['cached_payload_bytes'] for i in group)/1048576,
            encode_ms=sum(i['encode_us'] for i in group)/1000,
            restore_ms=sum(i['total_us'] for i in group)/1000,
            decode_ms=sum(i['decode_us'] for i in group)/1000,
            copy_ms=sum(i['copy_us'] for i in group)/1000,
            seal_ms=sum(i['seal_us'] for i in group)/1000,
            max_block_ms=max(i['max_block_us'] for i in group)/1000,
            scratch_mib=max(i['scratch_bytes'] for i in group)/1048576,
            gpu_mib=sum(i['gpu_alloc_bytes'] for i in group)/1048576))
    report={'rows':len(rows),'failures':sum(not r['ok'] for r in rows),'samples':items,'scenes':scenes,
            'method':['Single-thread wall times at reported actual CPU frequency; not power measurements.',
                      'PNG file reads may hit filesystem caches; not cold-storage benchmarks.',
                      'Compression input is exact decoded RGBA, including RGB beneath alpha zero.',
                      'All paths reuse the same opacity proof generated outside timing.',
                      'Cache size is payload only; excludes descriptors, allocator overhead and saved PNG copies.',
                      'Scratch is explicit decode buffer only, excluding codec workspace and retained validation reference.',
                      'Direct route includes stride repacking in seal_us; staging writes final GPU row layout.',
                      'DDS baseline is already lossy input: ok means upload succeeded, not equality with original PNG.',
                      'Scenes sum per-layer medians; these are estimated serial cold uploads, not measured game frame times.']}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2))
    print('rows',len(rows),'failures',report['failures'])
    for s in scenes:
        if s['cpu_mhz']==333 and s['chunk'] in ('0','whole','262144'):
            print('scene={scene} codec={codec} {route}/{chunk} cache={cache_mib:.3f}MiB encode={encode_ms:.2f}ms restore={restore_ms:.2f}ms decode={decode_ms:.2f}ms scratch={scratch_mib:.2f}MiB'.format(**s))


if __name__=='__main__':main()
