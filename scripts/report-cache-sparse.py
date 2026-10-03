"""Report lossless transparent-span restores, including matched RLE GPU controls."""
import argparse
import csv
import hashlib
import html
import json
from collections import defaultdict
from pathlib import Path
from statistics import median

LABELS = {(0, 'raw'): 'RGBA 直接复制',
          (3, 'staging'): 'RLE32 / 1 线程中转',
          (3, 'staging3'): 'RLE32 / 3 线程中转',
          (3, 'gpu_spans'): 'RLE32 / 显存批量写入',
          (7, 'staging'): 'ZeroSpan32 / 1 线程中转',
          (7, 'staging3'): 'ZeroSpan32 / 3 线程中转',
          (7, 'gpu_spans'): 'ZeroSpan32 / 直接写显存'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv', type=Path)
    p.add_argument('--sources', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    sources = json.loads(a.sources.read_text(encoding='utf-8'))['samples']
    names = {s['sample'] for s in sources}
    rows = list(csv.DictReader(a.csv.open()))
    assert len(rows) == len(sources)*28
    groups = defaultdict(list)
    for r in rows:
        for k in r:
            if k not in ('sample', 'route'): r[k] = int(r[k])
        assert r['ok'] == 1 and r['sample'] in names and r['cpu_mhz'] == 333
        assert r['chunk_bytes'] == (0 if r['codec'] == 0 else 131072)
        assert (r['codec'], r['route']) in LABELS
        groups[r['sample'], r['codec'], r['route']].append(r)
    assert len(groups) == len(sources)*7
    items = []
    for source in sources:
        for (codec, route), label in LABELS.items():
            g = groups[source['sample'], codec, route]
            assert len(g) == 4 and {r['round'] for r in g} == {0,1,2,3}
            measured = [r for r in g if r['round']]
            result = dict(sample=source['sample'], category=source['category'], codec=codec, route=route, name=label)
            for k in ('total_us', 'decode_us', 'copy_us', 'alloc_us', 'seal_us', 'max_block_us'):
                result[k] = median(r[k] for r in measured)
            for k in ('cached_payload_bytes', 'raw_bytes', 'scratch_bytes', 'encode_us'):
                assert len({r[k] for r in g}) == 1
                result[k] = g[0][k]
            items.append(result)
    totals = []
    for category in ('all', 'character_body', 'sparse_layer'):
        for (codec, route), label in LABELS.items():
            g = [r for r in items if r['codec'] == codec and r['route'] == route
                 and (category == 'all' or r['category'] == category)]
            totals.append(dict(category=category, codec=codec, route=route, name=label,
                               images=len(g), cache_mib=sum(r['cached_payload_bytes'] for r in g)/1048576,
                               raw_mib=sum(r['raw_bytes'] for r in g)/1048576,
                               encode_ms=sum(r['encode_us'] for r in g)/1000,
                               restore_ms=sum(r['total_us'] for r in g)/1000,
                               decode_ms=sum(r['decode_us'] for r in g)/1000,
                               copy_ms=sum(r['copy_us'] for r in g)/1000,
                               alloc_ms=sum(r['alloc_us'] for r in g)/1000,
                               max_batch_ms=max(r['max_block_us'] for r in g)/1000,
                               active_scratch_kib=max(r['scratch_bytes'] for r in g)/1024))
    report = dict(rows=len(rows), images=len(sources), failures=0, totals=totals, samples=items,
                  csv_sha256=hashlib.sha256(a.csv.read_bytes()).hexdigest())
    a.output.with_suffix('.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    parts = ['<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>大面积透明 FG 专用编码对照</title>',
             '<style>body{font:16px/1.6 system-ui,sans-serif;background:#101725;color:#e2e8f0;max-width:1200px;margin:32px auto;padding:0 24px}table{border-collapse:collapse;width:100%;margin:16px 0}th,td{border:1px solid #344155;padding:8px;text-align:right}th{background:#22314b}td:first-child,th:first-child{text-align:left}summary{cursor:pointer}</style>',
             '<h1>大面积透明 FG：ZeroSpan32 与 RLE32</h1>',
             f'<p>{len(sources)} 张相同 FG，{len(rows)} 条测量全部通过逐字节比对。CPU 333 MHz / GPU 111 MHz，128 KiB 分块。每组一轮预热、三轮测量；耗时是各图片中位数之和，不是游戏帧耗时。</p>',
             '<p>ZeroSpan32 将全零 RGBA 区域记为空白长度，其他像素按连续字节保存；64 字节分类粒度合并细碎间隙。透明像素里的非零 RGB 原样保留。压缩无收益的块退回原始字节。</p>',
             '<p>直接写显存路径由主线程用 SDK 批量填充空白、复制数据，不读回显存，不需要整块 CPU 像素中转。RLE32 显存批量写入是公平对照：同样使用快速填充／复制，但编码保持原有 RLE32。逐字节验证在计时之外。</p>',
             '<p>缓存大小仅为数据载荷；中转内存不含线程栈与测试保留的参考图。两个工作线程的预留缓冲仍留在测试程序内，直接写入不使用它们。编码每图测一次，线程启动与编码不计入恢复耗时。</p>']
    for category, label in [('all','全部'), ('character_body','3 张大立绘'), ('sparse_layer','8 张大面积透明图层')]:
        parts.append(f'<h2>{label}</h2><table><tr><th>方式</th><th>缓存 MiB</th><th>压缩 ms</th><th>恢复＋上传 ms</th><th>活动中转 KiB</th></tr>')
        for r in totals:
            if r['category'] == category:
                parts.append(f'<tr><td>{r["name"]}</td><td>{r["cache_mib"]:.3f}</td><td>{r["encode_ms"]:.2f}</td><td>{r["restore_ms"]:.2f}</td><td>{r["active_scratch_kib"]:g}</td></tr>')
        parts.append('</table>')
    parts.append('<h2>逐图数据</h2>')
    for s in sources:
        parts.append(f'<details><summary>{html.escape(s["sample"])} — {s["width"]}×{s["height"]}</summary><table><tr><th>方式</th><th>缓存 KiB</th><th>恢复＋上传 ms</th><th>分配 ms</th></tr>')
        for r in items:
            if r['sample'] == s['sample']:
                parts.append(f'<tr><td>{r["name"]}</td><td>{r["cached_payload_bytes"]/1024:.2f}</td><td>{r["total_us"]/1000:.3f}</td><td>{r["alloc_us"]/1000:.3f}</td></tr>')
        parts.append('</table></details>')
    parts.append('</html>')
    a.output.write_text('\n'.join(parts), encoding='utf-8')
    print(json.dumps(totals, ensure_ascii=True, indent=2))


if __name__ == '__main__': main()
