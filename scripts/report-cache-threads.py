"""Compare paired one/two decoder measurements without conflating encoding costs."""
import argparse,json,html
from pathlib import Path

NAMES={1:'zlib',2:'LZ4',3:'RLE32',6:'LZW'}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('summary',type=Path);p.add_argument('--sources',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--append-to',type=Path)
    a=p.parse_args();r=json.loads(a.summary.read_text());s=json.loads(a.sources.read_text())['samples']
    assert r['failures']==0 and r['rows']==len(s)*36
    lookup={(x['sample'],x['codec'],x['route']):x for x in r['samples']}
    items=[]
    for source in s:
        for codec in NAMES:
            one=lookup[(source['sample'],codec,'staging')];two=lookup[(source['sample'],codec,'staging2')]
            assert one['valid'] and two['valid'] and one['cached_payload_bytes']==two['cached_payload_bytes']
            items.append(dict(sample=source['sample'],category=source['category'],codec=codec,name=NAMES[codec],
                              width=source['width'],height=source['height'],raw_bytes=one['raw_bytes'],
                              single_ms=one['total_us']/1000,dual_ms=two['total_us']/1000,
                              single_decode_ms=one['decode_us']/1000,dual_decode_ms=two['decode_us']/1000,
                              single_scratch_kib=one['scratch_bytes']/1024,dual_scratch_kib=two['scratch_bytes']/1024))
    totals=[]
    for codec in NAMES:
        group=[x for x in items if x['codec']==codec]
        one=sum(x['single_ms'] for x in group);two=sum(x['dual_ms'] for x in group)
        totals.append(dict(codec=codec,name=NAMES[codec],single_ms=one,dual_ms=two,speedup=one/two,
                           saved_percent=(1-two/one)*100,
                           single_decode_ms=sum(x['single_decode_ms'] for x in group),
                           dual_decode_ms=sum(x['dual_decode_ms'] for x in group)))
    report={'images':len(s),'rows':r['rows'],'failures':0,'totals':totals,'samples':items}
    a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    if a.append_to:
        block=f'<h2>单／双线程对照（{len(s)} 张代表图）</h2><p>同一压缩数据，交替测试顺序。单线程为主线程解压；双线程为主线程＋一个持久 CPU 工作线程，GPU 操作仍全在主线程。64 KiB 中转增加至最多 128 KiB，不含线程栈和解码器工作区；线程创建与编码不计入。数字为逐图中位数之和。</p>'
        block+='<table><tr><th>方案</th><th>单线程恢复＋上传 ms</th><th>双线程恢复＋上传 ms</th><th>加速比</th><th>耗时减少</th></tr>'
        for x in totals:block+=f'<tr><td>{x["name"]}</td><td>{x["single_ms"]:.2f}</td><td>{x["dual_ms"]:.2f}</td><td>{x["speedup"]:.2f}×</td><td>{x["saved_percent"]:.1f}%</td></tr>'
        block+='</table><details><summary>展开逐图线程对照</summary><table><tr><th>图片</th><th>尺寸</th><th>方案</th><th>单线程 ms</th><th>双线程 ms</th></tr>'
        for x in items:block+=f'<tr><td>{html.escape(x["sample"])}</td><td>{x["width"]}×{x["height"]}</td><td>{x["name"]}</td><td>{x["single_ms"]:.3f}</td><td>{x["dual_ms"]:.3f}</td></tr>'
        block+='</table></details>'
        document=a.append_to.read_text(encoding='utf-8');anchor='<h2>逐图明细</h2>';assert anchor in document
        a.append_to.write_text(document.replace(anchor,block+anchor),encoding='utf-8')
    print(json.dumps(totals,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
