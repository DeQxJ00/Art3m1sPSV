# 当前 137 quads 页面约 43 FPS 的耗时

真实 PSV，同一 optL-reuse 进程。两次只读读取日志，最终证据 `build/hardware-logs/20260909-165231-companion/host.log`，SHA256 `3163d00cc30633d87461977be0a5eab0d607e35e5b915874d9c44af340767022`。没有换包、输入或改时钟。解析结果为同目录 `current-scene.json`。

最终日志最后 12 个 frame-perf 窗口（约一分钟，2586 帧）按帧数加权：media 0.0635 ms、logic/menu 4.2581 ms、direct present 18.8991 ms、capture 0.001 ms，合计 23.221 ms，即约 43.06 FPS。最后三个窗口绘制量一致：137 quads、26 draws。

同步窗口 GXM begin 平均等待约 9.70 ms，调用路径为 `finish_pending(WaitSite::Begin) -> sceGxmFinish`，包括随后的 collect。它是上一轮 GPU 完成与缓冲安全复用的同步成本，不能称为当前帧纯 shader 执行时间，也不能直接删除。

最后六段 core profile 经过 `sample_count/rendered_frames` 归一化，每次实际绘制的 frame_build 约 8.03–8.20 ms，其中 backlog 同步 3.81–3.89 ms、text commands 0.67–0.69 ms、scene 构建 2.90–2.95 ms、retain 0.52–0.53 ms；GPU 命令提交的 CPU 路径约 0.78–0.80 ms。这些分项属于 present 内部，不应与 present 总值再次相加。core profile 是滚动窗口，边界与 host 窗口不同，因此仅作近似拆分。

源码以实际候选的 `build/heap-audit/optL-source/` 为准。`runtime/render_gxm.rs` 在 rebuild 时调用 `sync_backlog_snapshot -> build_bound_scene`。前轮 DrawList 修改只复用 Vec 存储，仍然清空并重新生成内容，不能理解为整个绘制结果命中缓存。

`runtime/text.rs:BacklogInputs::update` 已缓存再现标签序列化，但每次仍逐页深比较历史及各消息层标签；`sync_backlog_snapshot` 还刷新文本度量。因 profiler 把二者合计，不能将 3.8 ms 全部精确归因于历史深比较。`active_layer_text_metrics` 即使命中排版缓存，仍遍历字形位置计算宽高。

现有脏标记按整个场景判断，动画、事件、文字状态变化均可触发重建；当前日志证实重建耗时持续存在，但没有记录具体触发原因，不能武断认定一定是等待箭头。后续需区分脏标记来源，再将不变的历史、文本、静态层与动画更新解耦。

最后三段纹理统计均 decoded=0、uploads=0；audio-detail 均 active_tracks=1、active_voice_hint=0、resample_us=0。当前持续低帧不是大量新图解码/上传造成，也无正在播放语音；后台 BGM 仍消耗 CPU，不能称音频完全没有影响。每帧仍有一次 missing texture 查找，约 13–14 ms/5秒，即约 0.06 ms/帧，不是本页主耗时。

要到 60 FPS，需要从约 23.2 ms 缩到 16.7 ms。可测的优化方向首先是不变 backlog/文本度量按内容版本更新、静态绘制内容缓存，以及在保持资源生命周期安全的前提下改善 CPU/GPU 重叠。不能仅凭这些耗时保证删除某项后必达 60 FPS；需同页 333 MHz 实机 A/B。
