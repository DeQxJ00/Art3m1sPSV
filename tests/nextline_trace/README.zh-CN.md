# 换句日志分析

读取 host 的可选 nextline 计时，输出 summary.json 与中文 summary.md。工具仅用 Python 标准库，不修改日志或存档。

```sh
python3 scripts/analyze-nextline-log.py /path/to/host.log --source hardware --output build/hardware-nextline-analysis
python3 tests/nextline_trace/test_analyzer.py
```

必须显式标记 hardware、emulator 或 unknown。已有模拟器样本：build/nextline-trace/emulator-host.log，分析输出位于 build/nextline-trace/analysis。五项测试覆盖单位、尾部统计语义、缺失字段、截断 JSON、重启和无诊断数据。

分析纪律：

- 不把两个不同帧的峰值相加；核心统计窗口重叠，不累计为总耗时。
- core maximum 字段实际上是最慢 1% 的均值别名，工具使用明确的 one_percent。
- 文本生成/排版合计在 event_text_ms；不得直接宣布字形缓存不足。
- 报告输出自身成本单列；核心计时线程也有采样开销。
- 开场加载、读档、正常换句的窗口必须结合录像/原始日志区分。本次模拟器样本的最大文本尾值包含启动过程，不能拿它当每次换句耗时。
- 部分日志及解析失败会明确列出，不会继承上一份报告的数值。缺失字段显示破折号，不视为零。
- 模拟器样本只验证分析链路。实体机卡顿仍需实体机证据。
