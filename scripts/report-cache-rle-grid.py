"""Validate and report the external FG RLE32 chunk/thread benchmark."""
import argparse
import csv
import hashlib
import html
import json
from collections import defaultdict
from pathlib import Path
from statistics import median

CHUNKS = [16, 32, 64, 128, 256, 512]
ROUTES = {1: 'staging', 2: 'staging2', 3: 'staging3', 4: 'staging4'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv', type=Path)
    p.add_argument('--sources', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    sources = json.loads(a.sources.read_text(encoding='utf-8'))['samples']
    names = {s['sample'] for s in sources}
    rows = list(csv.DictReader(a.csv.open()))
    assert len(rows) == len(sources) * 100
    groups = defaultdict(list)
    for r in rows:
        for k in r:
            if k not in ('sample', 'route'):
                r[k] = int(r[k])
        assert r['sample'] in names and r['ok'] == 1 and r['cpu_mhz'] == 333
        groups[r['sample'], r['codec'], r['chunk_bytes'], r['route']].append(r)
    metrics = ['total_us', 'decode_us', 'copy_us', 'alloc_us', 'seal_us', 'max_block_us']

    def get(name, codec, chunk, route):
        g = groups[name, codec, chunk, route]
        assert len(g) == 4 and {r['round'] for r in g} == {0, 1, 2, 3}
        measured = [r for r in g if r['round']]
        result = {k: median(r[k] for r in measured) for k in metrics}
        for k in ('raw_bytes', 'cached_payload_bytes', 'scratch_bytes', 'encode_us'):
            assert len({r[k] for r in g}) == 1
            result[k] = g[0][k]
        result['restore_max_us'] = max(r['total_us'] for r in measured)
        return result

    samples = []
    raw_ms = sum(get(name, 0, 0, 'raw')['total_us'] for name in names) / 1000
    for source in sources:
        assert source['category'] in ('character_body', 'portrait_fragment', 'sparse_layer')
        for kib in CHUNKS:
            for threads, route in ROUTES.items():
                v = get(source['sample'], 3, kib * 1024, route)
                samples.append(dict(sample=source['sample'], category=source['category'],
                                    width=source['width'], height=source['height'],
                                    chunk_kib=kib, threads=threads, **v))
    assert len(groups) == len(sources) * 25
    totals = []
    for kib in CHUNKS:
        for threads in ROUTES:
            g = [x for x in samples if x['chunk_kib'] == kib and x['threads'] == threads]
            totals.append(dict(chunk_kib=kib, threads=threads,
                               cache_mib=sum(x['cached_payload_bytes'] for x in g) / 1048576,
                               encode_ms=sum(x['encode_us'] for x in g) / 1000,
                               restore_ms=sum(x['total_us'] for x in g) / 1000,
                               decode_ms=sum(x['decode_us'] for x in g) / 1000,
                               copy_ms=sum(x['copy_us'] for x in g) / 1000,
                               alloc_ms=sum(x['alloc_us'] for x in g) / 1000,
                               seal_ms=sum(x['seal_us'] for x in g) / 1000,
                               max_batch_ms=max(x['max_block_us'] for x in g) / 1000,
                               active_scratch_kib=max(x['scratch_bytes'] for x in g) / 1024))
    for kib in CHUNKS:
        g = [x for x in totals if x['chunk_kib'] == kib]
        assert len({x['cache_mib'] for x in g}) == 1
    best = min(totals, key=lambda x: x['restore_ms'])
    report = dict(images=len(sources), rows=len(rows), failures=0, raw_copy_ms=raw_ms,
                  csv_sha256=hashlib.sha256(a.csv.read_bytes()).hexdigest(),
                  totals=totals, best=best, samples=samples)
    a.output.with_suffix('.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    parts = ['<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>FG RLE32 分块与线程实机对照</title>',
             '<style>body{font:16px/1.6 system-ui,sans-serif;background:#101725;color:#e2e8f0;max-width:1200px;margin:32px auto;padding:0 24px}table{border-collapse:collapse;width:100%;margin:16px 0}th,td{border:1px solid #344155;padding:8px;text-align:right}th{background:#22314b}td:first-child,th:first-child{text-align:left}a{color:#80bcff}summary{cursor:pointer}</style>',
             '<h1>FG RLE32：分块大小 × 解码线程数</h1>',
             f'<p>{len(sources)} 张相同 FG；{len(rows)} 条测量全部逐字节通过。CPU 333 MHz / GPU 111 MHz。每组合一轮预热、三轮测量，耗时为各图中位数之和，包含解压、纹理分配和上传，非游戏帧耗时。</p>',
             '<p>每块独立 RLE32。1～4 线程为主线程加 0～3 个持久工作线程，线程优先级 161，保持系统允许的亲和性；GPU 操作只在主线程。每轮轮换线程测试顺序。线程创建及压缩不计入恢复计时；压缩耗时为每图单次编码的合计，单独列出。</p>',
             '<p>缓存大小仅为压缩数据，不含块描述信息。中转空间列为所选解码线程使用的缓冲，不含线程栈；为了公平复用，测试程序另保留整个三工作线程缓冲池，最大 1.5 MiB，单线程时闲置缓冲也存在。参考原图与验证空间仅用于测试。</p>',
             f'<p>未压缩 RGBA 复制上传基线：<strong>{raw_ms:.2f} ms</strong>。最快合计：<strong>{best["chunk_kib"]} KiB / {best["threads"]} 线程，{best["restore_ms"]:.2f} ms</strong>。小幅差异应视为测试波动，不代表所有图片都更快。</p>',
             '<h2>恢复＋上传合计（ms）</h2><table><tr><th>分块</th><th>缓存 MiB</th><th>压缩 ms</th><th>1 线程</th><th>2 线程</th><th>3 线程</th><th>4 线程</th><th>活动中转 KiB（1→4）</th></tr>']
    for kib in CHUNKS:
        g = [x for x in totals if x['chunk_kib'] == kib]
        parts.append(f'<tr><td>{kib} KiB</td><td>{g[0]["cache_mib"]:.4f}</td><td>{g[0]["encode_ms"]:.2f}</td>' + ''.join(f'<td>{x["restore_ms"]:.2f}</td>' for x in g) + f'<td>{g[0]["active_scratch_kib"]:g}→{g[-1]["active_scratch_kib"]:g}</td></tr>')
    parts.append('</table><h2>耗时构成（ms）</h2><table><tr><th>分块 / 线程</th><th>解压与等待</th><th>复制</th><th>分配</th><th>封装</th><th>最大批次</th></tr>')
    for x in totals:
        parts.append(f'<tr><td>{x["chunk_kib"]} KiB / {x["threads"]}</td>' + ''.join(f'<td>{x[k]:.3f}</td>' for k in ['decode_ms', 'copy_ms', 'alloc_ms', 'seal_ms', 'max_batch_ms']) + '</tr>')
    parts.append('</table><p>“解压与等待”为墙钟时间，不能据此推算所有 CPU 核心的总工作量或耗电。最大批次覆盖该组同时解码的 1～4 块及后续复制，不等于单块耗时。</p><h2>逐图结果</h2>')
    for source in sources:
        group = [x for x in samples if x['sample'] == source['sample']]
        parts.append(f'<details><summary>{html.escape(source["sample"])} — {source["width"]}×{source["height"]} / {html.escape(source["category"])}</summary><table><tr><th>分块</th><th>1 线程 ms</th><th>2 线程 ms</th><th>3 线程 ms</th><th>4 线程 ms</th></tr>')
        for kib in CHUNKS:
            parts.append(f'<tr><td>{kib} KiB</td>' + ''.join(f'<td>{x["total_us"]/1000:.3f}</td>' for x in group if x['chunk_kib'] == kib) + '</tr>')
        parts.append('</table></details>')
    parts.append('</html>')
    a.output.write_text('\n'.join(parts), encoding='utf-8')
    print(json.dumps(dict(raw_copy_ms=raw_ms, totals=totals, best=best), indent=2))


if __name__ == '__main__':
    main()
