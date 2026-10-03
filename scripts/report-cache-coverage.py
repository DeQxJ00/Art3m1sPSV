"""Join hardware timing summaries with corpus provenance into a local comparison table."""
import argparse
import html
import json
from pathlib import Path

NAMES={0:'RGBA 原像素',1:'zlib level 1',2:'LZ4',3:'RLE32',6:'LZW',4:'内存 PNG 解码'}
CATEGORIES={'background':'背景','scene_art':'场景／特效图','character_body':'立绘主体',
            'portrait_fragment':'裁剪表情／部件','sparse_layer':'高透明表情层','interface':'界面素材','grayscale':'灰度／纯色素材'}
ORDER=[0,1,2,3,6,4]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('summary',type=Path);p.add_argument('--sources',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    summary=json.loads(a.summary.read_text());sources=json.loads(a.sources.read_text())
    lookup={x['sample']:x for x in sources['samples']};data={}
    for x in summary['samples']:
        expected='raw' if x['codec']==0 else 'png_ram' if x['codec']==4 else 'staging'
        if x['cpu_mhz']==333 and x['route']==expected:
            assert x['valid'] and x['repeats']==3
            data[(x['sample'],x['codec'])]=x
    assert summary['failures']==0 and len(data)==len(lookup)*len(ORDER)
    assert summary['rows']==len(lookup)*28
    def aggregate(names):
        raw=sum(data[(n,0)]['raw_bytes'] for n in names)
        result=[]
        for codec in ORDER:
            rows=[data[(n,codec)] for n in names]
            result.append(dict(codec=codec,name=NAMES[codec],count=len(rows),raw_mib=raw/1048576,
                               cache_mib=sum(r['cached_payload_bytes'] for r in rows)/1048576,
                               encode_ms=sum(r['encode_us'] for r in rows)/1000,
                               decode_ms=sum(r['decode_us'] for r in rows)/1000,
                               restore_ms=sum(r['total_us'] for r in rows)/1000,
                               max_block_ms=max(r['max_block_us'] for r in rows)/1000,
                               scratch_kib=max(r['scratch_bytes'] for r in rows)/1024))
        return result
    totals=aggregate(list(lookup));categories={c:aggregate([n for n,x in lookup.items() if x['category']==c]) for c in CATEGORIES if any(x['category']==c for x in lookup.values())}
    report={'rows':summary['rows'],'failures':0,'images':len(lookup),'totals':totals,'categories':categories,
            'samples':[{**x,'timings':aggregate([n])} for n,x in lookup.items()]}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.with_suffix('.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    def table(rows):
        out='<table><thead><tr><th>方案</th><th>缓存数据 MiB</th><th>节省</th><th>首次压缩 ms</th><th>解压／解码 ms</th><th>恢复＋上传 ms</th><th>最长分块 ms</th></tr></thead><tbody>'
        for r in rows:
            saved=100*(1-r['cache_mib']/r['raw_mib'])
            out+=f"<tr><td>{r['name']}</td><td>{r['cache_mib']:.3f}</td><td>{saved:.1f}%</td><td>{r['encode_ms']:.2f}</td><td>{r['decode_ms']:.2f}</td><td>{r['restore_ms']:.2f}</td><td>{r['max_block_ms']:.3f}</td></tr>"
        return out+'</tbody></table>'
    body='<h1>PSV FG 图片 CPU 缓存压缩对照</h1>' if set(categories)<= {'character_body','portrait_fragment','sparse_layer'} else '<h1>PSV 图片 CPU 缓存压缩对照</h1>'
    body+=f'<p>{len(lookup)} 张图片 · {len(categories)} 类 · CPU 333 MHz / GPU 111 MHz · {summary["rows"]} 次测量 · 校验失败 0</p>'
    body+='<h2>全部样本合计</h2>'+table(totals)
    body+='<p>各图片先取 3 次测量中位数，再相加；不是游戏帧耗时。第 0 轮预热不计入。首次压缩与命中恢复分别统计。压缩方案为 64 KiB 独立分块，在普通内存解压后复制到显存。</p>'
    body+='<p>容量只含缓存数据，不含管理结构、原 PNG 备份、解码器工作区、最大 64 KiB 中转缓冲或显存。所有图片统一解码为 RGBA，完整保留透明像素的 RGB；灰度行不代表生产环境单通道纹理的占用。内存 PNG 行不含文件读取，文件读取对照保存在原始 CSV。zlib 使用 flate2 1.1.9 / miniz_oxide，LZ4 使用 lz4_flex 0.11.6，RLE32 为逐 RGBA 像素游程。LZW 使用 weezl 0.1.12、8 位字节字母表、最高 12 位码和 LSB 位序，每块重建字典。以上是当前实现结果。</p>'
    for c,rows in categories.items():body+=f'<h2>{CATEGORIES[c]}（{rows[0]["count"]} 张）</h2>'+table(rows)
    body+='<h2>逐图明细</h2>'
    for x in report['samples']:
        label=f"{x['sample']} · {CATEGORIES[x['category']]} · {x['width']} × {x['height']} · {x['mode']} · 全透明 {x['transparent_fraction']*100:.1f}%"
        body+=f'<details><summary>{html.escape(label)}</summary><p>{html.escape(x["source"])}</p>'+table(x['timings'])+'</details>'
    css='body{font:16px/1.6 system-ui,sans-serif;max-width:1200px;margin:32px auto;padding:0 20px;color:#203044;background:#f6f8fb}h1,h2{color:#143a5b}table{border-collapse:collapse;width:100%;background:white;margin-bottom:18px}th,td{padding:9px 12px;text-align:right;border-bottom:1px solid #dce3eb;white-space:nowrap}th{background:#163e60;color:white}td:first-child,th:first-child{text-align:left}details{background:white;margin:8px 0;padding:12px;overflow:auto}summary{cursor:pointer;font-weight:600}p{color:#485a6d}h2{margin-top:36px}@media(max-width:800px){body{font-size:13px}table{display:block;overflow:auto}}'
    a.output.write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>PSV 缓存压缩测试</title><style>'+css+'</style>'+body+'</html>',encoding='utf-8')
    for r in totals:print(r)


if __name__=='__main__':main()
