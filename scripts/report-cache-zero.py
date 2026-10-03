"""Validate paired PNG row-count measurements and extend the original file report."""
import argparse
import csv
import hashlib
import html
import json
from collections import defaultdict
from pathlib import Path
from statistics import median
from urllib.parse import quote


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--previous', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    reference = json.loads(args.reference.read_text(encoding='utf-8'))
    previous = json.loads(args.previous.read_text(encoding='utf-8'))
    groups = defaultdict(list)
    raw_rows = list(csv.DictReader(args.csv.open(encoding='utf-8')))
    routes = ('production', 'row_plain', 'row_count')
    for row in raw_rows:
        for key in row:
            if key not in ('sample', 'route'):
                row[key] = int(row[key])
        assert row['ok'] == 1 and (row['cpu_mhz'], row['gpu_mhz']) == (333, 111)
        assert row['total_us'] == row['read_us'] + row['decode_us']
        assert row['route'] in routes
        groups[row['sample']].append(row)
    assert len(raw_rows) == len(reference) * 18
    assert set(groups) == set(reference) == {s['filename'] for s in previous['samples']}
    samples = []
    for original in previous['samples']:
        item = dict(original)
        name = item['filename']
        group = groups[name]
        expected = reference[name]
        assert expected['source_sha256'] == item['sha256']
        assert len(group) == 18
        assert {(r['route'], r['round']) for r in group} == {(v, n) for v in routes for n in range(6)}
        for r in group:
            assert all(r[k] == item[k] for k in ('width', 'height', 'png_bytes', 'raw_bytes'))
            assert r['zero_pixels'] == expected['zero_pixels'] and r['pixels'] == expected['pixels']
        item.update(expected)
        item['zero_percent'] = 100 * expected['zero_pixels'] / expected['pixels']
        item['format'] = 'PAL8' if expected['png_type'] == 3 else 'RGBA8'
        item['pixel_dimensions'] = item['width'] * item['height']
        for route in routes:
            measured = [r for r in group if r['route'] == route and r['round'] != 0]
            for key in ('read_us', 'decode_us', 'total_us'):
                item[route + '_' + key] = median(r[key] for r in measured)
            item[route + '_decode_spread_us'] = max(r['decode_us'] for r in measured) - min(r['decode_us'] for r in measured)
        item['counter_extra_us'] = item['row_count_decode_us'] - item['row_plain_decode_us']
        item['production_extra_us'] = item['row_count_decode_us'] - item['production_decode_us']
        item['total_extra_us'] = item['row_count_total_us'] - item['production_total_us']
        samples.append(item)
    sum_keys = ('zero_pixels', 'pixels', 'png_bytes', 'raw_bytes', 'packed_bytes',
                'production_read_us', 'production_decode_us', 'production_total_us',
                'row_plain_decode_us', 'row_count_read_us', 'row_count_decode_us', 'row_count_total_us',
                'counter_extra_us', 'production_extra_us', 'total_extra_us',
                'select_us', 'encode_us', 'zsp_gpu_us', 'rgba_gpu_us')

    def summary(items):
        result = {key: sum(i[key] for i in items) for key in sum_keys}
        result['count'] = len(items)
        result['zero_percent'] = 100 * result['zero_pixels'] / result['pixels']
        return result

    formats = {f: summary([i for i in samples if i['format'] == f]) for f in ('PAL8', 'RGBA8')}
    totals = summary(samples)
    data = dict(images=len(samples), rows=len(raw_rows), failures=0, totals=totals, formats=formats,
                samples=samples, csv_sha256=hashlib.sha256(args.csv.read_bytes()).hexdigest(),
                previous_csv_sha256=previous['csv_sha256'])
    args.output.with_suffix('.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    esc = html.escape
    parts = ['''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>PNG 解码同步统计全零 RGBA</title><style>
body{font:15px/1.65 system-ui,sans-serif;background:#101725;color:#e2e8f0;margin:28px}h1,h2{line-height:1.3}a{color:#8bc6ff}.note{background:#1b2940;padding:16px;border-radius:8px;max-width:1300px}table{border-collapse:separate;border-spacing:0;font-variant-numeric:tabular-nums;width:100%}th,td{padding:8px 10px;white-space:nowrap;text-align:right;border-bottom:1px solid #344155}th{background:#22314b}th:first-child,td:first-child{text-align:left}.scroll{overflow:auto;border:1px solid #344155}#detail th{position:sticky;top:0;cursor:pointer;z-index:2}#detail th:first-child,#detail td:first-child{position:sticky;left:0;background:#18253a;z-index:1}#detail th:first-child{z-index:3;background:#22314b}tr:hover td{background:#243146!important}.fast{color:#8de1b4}.slow{color:#ffbc96}input,select{font:inherit;background:#18253a;color:#e2e8f0;border:1px solid #51627d;padding:8px;margin:8px 12px 12px 0;border-radius:4px}input{min-width:300px}small{color:#a4b1c6}</style></head><body>''',
             f'<h1>PNG 解码时统计全零 RGBA</h1><p><b>{len(samples)} 张原始 PNG / {len(raw_rows)} 条实机测量：像素和全零计数全部验证通过。</b></p>',
             '<p><a href="cache-zero.csv" download>本轮原始 CSV</a> · <a href="comparison.json" download>完整 JSON</a> · <a href="../cache-files-study/comparison.html">上一轮压缩／显存报告</a></p>',
             '<div class="note">CPU 333 MHz / GPU 111 MHz；每张每条路径一轮预热、五轮正式测量取中位数，轮换路径顺序。<b>全零比例严格指 R=G=B=A=0，不是只有 A=0。</b>PC 独立解码计数与实机计数相互校验。<br>原路径＝现有正式 PNG 解码器。按行不计数＝实验性按行解码输出。按行计数＝在相同输出循环内统计：PAL8 在展开调色板时统计，RGBA8 在复制解码行时统计，不另做整图扫描。实验只覆盖这批非交错 8 位 PNG，未替换正式解码器。<br>“计数增量”＝按行计数 − 按行不计数，隔离计数开销；“相对原解码增量”同时包含解码路径变化；负数表示本次测得更快。逐文件增量由各路径中位数相减，可能受测量波动影响。<br>读取与解码分别计时；读取为独立 PNG 文件的温缓存读取，不含 PFS。总耗时包含读取＋解码。显存、判断、压缩数据沿用上一轮结果；所有校验均在计时之外。</div>',
             '<h2>按源格式合计</h2><div class="scroll"><table><tr><th>格式</th><th>数量</th><th>全零比例</th><th>原解码 ms</th><th>按行不计数 ms</th><th>按行计数 ms</th><th>计数增量 ms</th><th>相对原解码增量 ms</th><th>原读取＋解码 ms</th><th>计数读取＋解码 ms</th></tr>']
    for label, item in [*formats.items(), ('全部', totals)]:
        parts.append(f'<tr><td>{label}</td><td>{item["count"]}</td><td>{item["zero_percent"]:.3f}%</td>')
        for key in ('production_decode_us', 'row_plain_decode_us', 'row_count_decode_us', 'counter_extra_us', 'production_extra_us', 'production_total_us', 'row_count_total_us'):
            parts.append(f'<td>{item[key]/1000:.3f}</td>')
        parts.append('</tr>')
    parts.append('</table></div><p>合计是逐图中位数之和，不是单帧耗时。</p><h2>逐文件完整结果</h2><input id="search" placeholder="搜索原文件名或文件夹"><select id="format"><option value="">全部格式</option><option>PAL8</option><option>RGBA8</option></select><span id="count"></span><p><small>点击表头排序，左右滚动查看各列。所有大小单位 KiB、耗时单位 ms。原文件名可点击查看图片。</small></p><div class="scroll" style="max-height:75vh"><table id="detail"><thead><tr>')
    columns = [('filename','原文件名'),('format','源格式'),('pixel_dimensions','尺寸'),('zero_percent','全零 RGBA %'),
               ('png_bytes','PNG KiB'),('raw_bytes','RGBA KiB'),('packed_bytes','ZeroSpan32 KiB'),
               ('eligible','此前候选判断'),
               ('production_read_us','原路径读取 ms'),('row_count_read_us','计数路径读取 ms'),
               ('production_decode_us','原路径解码 ms'),('row_plain_decode_us','按行不计数 ms'),
               ('row_count_decode_us','按行计数 ms'),('counter_extra_us','计数增量 ms'),
               ('production_extra_us','相对原解码增量 ms'),('production_total_us','原读取＋解码 ms'),
               ('row_count_total_us','计数读取＋解码 ms'),('total_extra_us','读取＋解码增量 ms'),
               ('select_us','此前独立判断 ms'),('encode_us','此前压缩 ms'),
               ('zsp_gpu_us','ZeroSpan32 → GPU ms'),('rgba_gpu_us','RGBA → GPU ms'),('gpu_saved_percent','上传耗时减少 %')]
    for i, (_, label) in enumerate(columns):parts.append(f'<th data-col="{i}">{label}</th>')
    parts.append('</tr></thead><tbody>')
    for item in samples:
        parts.append(f'<tr data-format="{item["format"]}">')
        for key, _ in columns:
            value = item[key]
            if key == 'filename':
                display = f'<a href="../cache-files-study/TEST_CPU_CACHE_THREADS/{quote(value)}" target="_blank">{esc(value)}</a>'
            elif key == 'format':display = value
            elif key == 'pixel_dimensions':display = f'{item["width"]}×{item["height"]}'
            elif key == 'eligible':display = '建议使用' if value else '保留原方式'
            else:
                divisor = 1024 if key.endswith('_bytes') else 1000 if key.endswith('_us') else 1
                display = f'{value/divisor:.3f}'
            cls = ' class="fast"' if key.endswith('extra_us') and value < 0 else ' class="slow"' if key.endswith('extra_us') else ''
            parts.append(f'<td data-value="{esc(str(value))}"{cls}>{display}</td>')
        parts.append('</tr>')
    parts.append('''</tbody></table></div><script>
const body=document.querySelector('#detail tbody'),rows=[...body.rows],search=document.querySelector('#search'),format=document.querySelector('#format'),count=document.querySelector('#count');
function filter(){let n=0;const q=search.value.trim().toLowerCase();for(const r of rows){r.hidden=!(r.cells[0].textContent.toLowerCase().includes(q)&&(!format.value||r.dataset.format===format.value));if(!r.hidden)n++;}count.textContent=`显示 ${n} / ${rows.length} 张`;}
search.addEventListener('input',filter);format.addEventListener('change',filter);filter();
let last=-1,asc=true;for(const th of document.querySelectorAll('#detail th'))th.addEventListener('click',()=>{const col=Number(th.dataset.col);asc=last===col?!asc:true;last=col;rows.sort((a,b)=>{const x=a.cells[col].dataset.value,y=b.cells[col].dataset.value;return(asc?1:-1)*(col<2?x.localeCompare(y):Number(x)-Number(y));});body.append(...rows);});
</script></body></html>''')
    args.output.write_text('\n'.join(parts), encoding='utf-8')
    print(json.dumps(dict(images=len(samples), rows=len(raw_rows), totals=totals, formats=formats), indent=2))


if __name__ == '__main__':
    main()
