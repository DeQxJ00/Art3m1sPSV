"""Summarize optional Vita next-line diagnostics without inferring GPU/FPS causality."""
import argparse
import json
import re
from pathlib import Path

FIELDS = ("interpreter_ms", "event_text_ms", "event_media_ms", "event_runtime_ms",
          "audio_media_ms", "frame_text_ms", "frame_build_ms", "gpu_submit_ms",
          "host_ffi_ms")


def numbers(line):
    return {key: int(value) for key, value in re.findall(r"\b(\w+)=([0-9]+)\b", line)}


def parse_log(text, source):
    reports, issues = [], []
    current = None
    session = 0
    for line_no, line in enumerate(text.splitlines(), 1):
        if "[nextline-trace] enabled;" in line:
            session += 1
            current = None
        if "[nextline-trace] logic_max_us=" in line:
            current = {"line": line_no, "session": session,
                       "timestamp": line.split(" [", 1)[0],
                       "tick": numbers(line)}
            reports.append(current)
        elif "[nextline-atlas]" in line:
            if current is not None:
                current["atlas"] = numbers(line)
            else:
                issues.append({"line": line_no, "reason": "atlas report without tick header"})
        elif "[nextline-core]" in line:
            try:
                value = json.loads(line.split("[nextline-core] ", 1)[1])
                if not isinstance(value, dict) or not isinstance(value.get("one_percent"), dict):
                    raise ValueError("missing one_percent object")
                if current is None:
                    raise ValueError("core report without tick header")
                current["core"] = {k: value.get(k) for k in (
                    "enabled", "session_ms", "sample_window_ms", "sample_count", "dropped_samples")}
                current["core"]["one_percent_ms"] = {
                    key: value["one_percent"].get(key) for key in FIELDS}
            except (ValueError, IndexError) as error:
                issues.append({"line": line_no, "reason": str(error)})
        elif "[nextline-trace] report_cost_us=" in line and current is not None:
            current["report_cost_us"] = numbers(line).get("report_cost_us")
            current = None
    for report in reports:
        report["complete"] = all(key in report for key in ("core", "atlas", "report_cost_us"))
    return {"source": source, "reports": reports, "issues": issues,
            "limits": [
                "Overlapping core windows must not be summed; one_percent is a tail average, not a maximum.",
                "Tick maxima and core category tails may refer to different frames; categories overlap.",
                "Atlas stats cover existing-page region updates, not initial full-page allocation.",
                "Report cost is diagnostic overhead. No input timestamp or exact sentence mapping is available.",
                "Emulator timing cannot establish physical Vita performance; no automatic root-cause verdict."]}


def display(value, scale=1):
    return "—" if not isinstance(value, (int, float)) else f"{value / scale:.3f}"


def markdown(data):
    lines = ["# 换句诊断日志摘要", "", f"采样来源：{data['source']}。",
             f"报告 {len(data['reports'])} 份；不完整 {sum(not r['complete'] for r in data['reports'])} 份；解析问题 {len(data['issues'])} 项。",
             "", "全部耗时单位为 ms。核心列为最慢 1% 样本均值，各列不可相加或认定来自同一帧。",
             "图集等待属于准备阶段的子项；报告输出开销单列。模拟器数据不用于判定实机卡顿。", "",
             "| 行号/时间 | 逻辑峰值 | 图集准备峰值 | 区域更新等待峰值 | 文本事件尾值 | 脚本尾值 | 媒体事件尾值 | 报告开销 | 完整 |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |"]
    for row in data["reports"]:
        tick, atlas = row["tick"], row.get("atlas", {})
        core = row.get("core", {}).get("one_percent_ms", {})
        vals = [f"{row['line']}/{row['timestamp']}",
                display(tick.get("logic_max_us"), 1000), display(tick.get("atlas_prepare_max_us"), 1000),
                display(atlas.get("wait_max_us"), 1000), display(core.get("event_text_ms")),
                display(core.get("interpreter_ms")), display(core.get("event_media_ms")),
                display(row.get("report_cost_us"), 1000), "是" if row["complete"] else "否"]
        lines.append("| " + " | ".join(vals) + " |")
    lines += ["", "不能仅凭此表判断字体缓存不足：排版与字形生成尚未细分，首次分配、场景资源变化也需要联合查看原始日志及录像。",
              "", "## 解析问题", ""]
    lines += [f"- 第 {issue['line']} 行：{issue['reason']}" for issue in data["issues"]] or ["无。"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--source", choices=("hardware", "emulator", "unknown"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = parse_log(args.log.read_text(encoding="utf-8", errors="replace"), args.source)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "summary.json").write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    (args.output / "summary.md").write_text(markdown(data), encoding="utf-8")
    print(json.dumps({"reports": len(data["reports"]), "issues": len(data["issues"]),
                      "source": args.source, "output": str(args.output)}))


if __name__ == "__main__":
    main()
