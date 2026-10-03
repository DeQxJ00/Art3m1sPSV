"""Validate all-file physical benchmarks and report original PNG paths and timings."""
import argparse
import csv
import hashlib
import html
import json
from collections import defaultdict
from pathlib import Path
from statistics import median
from urllib.parse import quote

TIMES=['read_us','png_decode_us','select_us','encode_us','rgba_gpu_us','zsp_gpu_us',
       'rgba_alloc_us','zsp_alloc_us','rgba_write_us','zsp_write_us','rgba_seal_us','zsp_seal_us']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv',type=Path)
    p.add_argument('--sources',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args()
    sources=json.loads(a.sources.read_text(encoding='utf-8'))['samples']
    rows=list(csv.DictReader(a.csv.open()))
    assert len(rows)==len(sources)*4
    groups=defaultdict(list)
    for r in rows:
        for k in r:
            if k!='sample':r[k]=int(r[k])
        assert r['ok']==1 and r['cpu_mhz']==333 and r['gpu_mhz']==111
        groups[r['sample']].append(r)
    assert set(groups)=={s['filename'] for s in sources}
    items=[]
    for source in sources:
        group=groups[source['filename']]
        assert len(group)==4 and {r['round'] for r in group}=={0,1,2,3}
        measured=[r for r in group if r['round']]
        item={k:source[k] for k in ('filename','folder','width','height','png_bytes','raw_bytes','sha256')}
        for r in group:
            for k in ('png_bytes','raw_bytes','width','height'):assert r[k]==source[k]
            if r['raw_bytes']>=512*1024:assert r['estimated_bytes']==r['packed_bytes']
        for k in ('packed_bytes','eligible','zero_bytes','zero_runs','estimated_bytes'):
            assert len({r[k] for r in group})==1
            item[k]=group[0][k]
        for k in TIMES:
            item[k]=median(r[k] for r in measured)
            item[k+'_max']=max(r[k] for r in measured)
        item['gpu_saved_percent']=(1-item['zsp_gpu_us']/max(1,item['rgba_gpu_us']))*100
        items.append(item)

    def summarize(group):
        result={'count':len(group),'eligible':sum(x['eligible'] for x in group),
                'gpu_faster':sum(x['zsp_gpu_us']<x['rgba_gpu_us'] for x in group)}
        for k in ('png_bytes','raw_bytes','packed_bytes',*TIMES):result[k]=sum(x[k] for x in group)
        return result
    folders={name:summarize([x for x in items if x['folder']==name]) for name in sorted({x['folder'] for x in items})}
    totals=summarize(items)
    result=dict(images=len(items),rows=len(rows),failures=0,totals=totals,folders=folders,samples=items,
                csv_sha256=hashlib.sha256(a.csv.read_bytes()).hexdigest())
    a.output.with_suffix('.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    esc=html.escape
    parts=['''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>全部 FG 文件实机缓存对照</title>
<style>body{font:15px/1.65 system-ui,sans-serif;background:#101725;color:#e2e8f0;margin:28px}h1,h2{line-height:1.3}.note{background:#1b2940;padding:16px;border-radius:8px;max-width:1200px}.summary{max-width:1300px}table{border-collapse:separate;border-spacing:0;width:100%;font-variant-numeric:tabular-nums}th,td{border-bottom:1px solid #344155;padding:8px 10px;text-align:right;white-space:nowrap}th{background:#22314b}td:first-child,th:first-child{text-align:left}.scroll{max-height:75vh;overflow:auto;border:1px solid #344155}#detail th{position:sticky;top:0;z-index:2;cursor:pointer}#detail td:first-child,#detail th:first-child{position:sticky;left:0;background:#152033;z-index:1}#detail th:first-child{z-index:3;background:#22314b}tr:hover td{background:#243146!important}.fast{color:#8de1b4}.slow{color:#ffbc96}input,select{font:inherit;background:#18253a;color:#e2e8f0;border:1px solid #51627d;padding:8px;margin:8px 12px 12px 0;border-radius:4px}input{min-width:300px}a{color:#8bc6ff}small{color:#a4b1c6}</style></head><body>''',
           f'<h1>全部 FG 文件：PNG → ZeroSpan32 → 显存</h1><p><b>{len(items)} 张 PNG / {len(folders)} 个文件夹 / {len(rows)} 条实机测量，逐字节校验全部通过。</b></p><p><a href="{esc(a.csv.name)}" download>下载原始测量 CSV</a> · <a href="{esc(a.output.with_suffix(".json").name)}" download>下载汇总 JSON</a> · 点击原文件名查看对应 PNG。</p>',
           '<div class="note">CPU 333 MHz / GPU 111 MHz，128 KiB 分块。每张一轮预热、三轮正式测量取中位数。原文件名、相对路径与 PNG 文件字节保持不变。所有图片均强制编码作对照，“建议使用”仅表示候选阈值判断，未接入正式缓存。<br>PNG 读取与 PNG 解码分开计时；这里读取独立 PNG 文件，不含 PFS 查找／解包。读取可能命中文件系统缓存，不能当作冷存储速度。判断、压缩、上传独立计时；显存两列包含分配、写入和封装。透明像素的隐藏 RGB 保留，GPU 像素校验在计时之外。<br><b>ZeroSpan32 上传只代表压缩缓存已准备好的命中路径。首次使用还需承担判断和编码成本；本实验没有合并这两次扫描。</b></div>',
           '<h2>合计</h2><table class="summary"><tr><th>PNG 原文件</th><th>RGBA 解码数据</th><th>ZeroSpan32 缓存</th><th>PNG 解码</th><th>判断</th><th>压缩</th><th>ZeroSpan32 → GPU</th><th>RGBA → GPU</th></tr><tr>']
    for k in ('png_bytes','raw_bytes','packed_bytes'):parts.append(f'<td>{totals[k]/1048576:.3f} MiB</td>')
    for k in ('png_decode_us','select_us','encode_us','zsp_gpu_us','rgba_gpu_us'):parts.append(f'<td>{totals[k]/1000:.2f} ms</td>')
    parts.append(f'</tr></table><p>候选判断选中 {totals["eligible"]}/{len(items)} 张；ZeroSpan32 显存路径实测快于 RGBA 复制的有 {totals["gpu_faster"]}/{len(items)} 张。合计为逐图中位数之和，不是游戏帧耗时。缓存大小仅统计数据载荷，不含块描述对象、分配器开销及原 PNG 备份。</p>')
    parts.append('<h2>按文件夹汇总</h2><div class="scroll" style="max-height:none"><table><tr><th>文件夹</th><th>图片数</th><th>PNG MiB</th><th>RGBA MiB</th><th>ZeroSpan32 MiB</th><th>PNG 解码 ms</th><th>判断 ms</th><th>压缩 ms</th><th>ZeroSpan32 → GPU ms</th><th>RGBA → GPU ms</th></tr>')
    for name,group in folders.items():
        parts.append(f'<tr><td>{esc(name)}</td><td>{group["count"]}</td>')
        for k in ('png_bytes','raw_bytes','packed_bytes'):parts.append(f'<td>{group[k]/1048576:.3f}</td>')
        for k in ('png_decode_us','select_us','encode_us','zsp_gpu_us','rgba_gpu_us'):parts.append(f'<td>{group[k]/1000:.2f}</td>')
        parts.append('</tr>')
    parts.append('</table></div><h2>逐文件结果</h2><input id="search" placeholder="搜索原文件名或文件夹"><select id="folder"><option value="">全部文件夹</option>')
    for name in folders:parts.append(f'<option value="{esc(name)}">{esc(name)}</option>')
    parts.append('<select id="advice"><option value="">全部判断</option><option value="1">建议使用</option><option value="0">建议保留原方式</option></select><span id="count"></span><p><small>点击表头排序，左右滚动查看全部列。大小单位为 KiB，耗时单位为 ms。</small></p><div class="scroll"><table id="detail"><thead><tr>')
    columns=[('filename','原文件名 / 相对路径'),('dimensions','尺寸'),('png_bytes','PNG 原文件 KiB'),('raw_bytes','RGBA KiB'),('packed_bytes','ZeroSpan32 KiB'),('eligible','建议使用'),('read_us','读取 PNG ms'),('png_decode_us','解码 PNG ms'),('select_us','判断 ms'),('encode_us','压缩 ms'),('zsp_gpu_us','ZeroSpan32 → GPU ms'),('rgba_gpu_us','RGBA → GPU ms'),('gpu_saved_percent','上传耗时减少 %')]
    for i,(_,label) in enumerate(columns):parts.append(f'<th data-col="{i}">{label}</th>')
    parts.append('</tr></thead><tbody>')
    for x in items:
        parts.append(f'<tr data-folder="{esc(x["folder"])}" data-advice="{x["eligible"]}">')
        for k,_ in columns:
            if k=='filename':
                value=x[k];display=f'<a href="TEST_CPU_CACHE_THREADS/{quote(value)}" target="_blank">{esc(value)}</a>'
            elif k=='dimensions':value=x['width']*x['height'];display=f'{x["width"]}×{x["height"]}'
            elif k=='eligible':value=x[k];display='是' if value else '否'
            else:
                value=x[k];divisor=1024 if k.endswith('_bytes') else 1000 if k.endswith('_us') else 1
                display=f'{value/divisor:.3f}'
            cls=' class="fast"' if k=='gpu_saved_percent' and value>0 else ' class="slow"' if k=='gpu_saved_percent' else ''
            parts.append(f'<td data-value="{esc(str(value))}"{cls}>{display}</td>')
        parts.append('</tr>')
    parts.append('''</tbody></table></div><script>
const tbody=document.querySelector('#detail tbody'),rows=[...tbody.rows],search=document.querySelector('#search'),folder=document.querySelector('#folder'),advice=document.querySelector('#advice'),count=document.querySelector('#count');
function filter(){const q=search.value.trim().toLowerCase();let n=0;for(const r of rows){r.hidden=!(r.cells[0].textContent.toLowerCase().includes(q)&&(!folder.value||r.dataset.folder===folder.value)&&(!advice.value||r.dataset.advice===advice.value));if(!r.hidden)n++;}count.textContent=`显示 ${n} / ${rows.length} 张`;}
search.addEventListener('input',filter);folder.addEventListener('change',filter);advice.addEventListener('change',filter);filter();
let last=-1,ascending=true;for(const th of document.querySelectorAll('#detail th'))th.addEventListener('click',()=>{const col=Number(th.dataset.col);ascending=last===col?!ascending:true;last=col;rows.sort((a,b)=>{const x=a.cells[col].dataset.value,y=b.cells[col].dataset.value;return(ascending?1:-1)*(col===0?x.localeCompare(y):Number(x)-Number(y));});tbody.append(...rows);});
</script></body></html>''')
    # Close the folder select before the separate advice filter.
    document='\n'.join(parts).replace('<select id="advice">','</select><select id="advice">')
    a.output.write_text(document,encoding='utf-8')
    print(json.dumps({'images':len(items),'rows':len(rows),'totals':totals,'folders':folders},indent=2))


if __name__=='__main__':main()
