# 预载额度挤掉已解码背景：实机对照

## 2026-09-20 05:38:28 剧情复测

录像 `Z:/Record/OBS/2026-09-20 05-38-28.mp4`，实机日志保存为
`build/dialogue-stall-20260920/retest-0538-host.log`。

上一轮灰度重建优化已生效：四人画面建立缓存的整帧由约 210 ms 降至
101.580 ms，离屏切换由 23 次降至 13 次。但资源切入仍有 1.1–1.7 秒长帧。
bg01fs_s、bg03 等背景此前确实预载成功，随后缓存回收记录显示
`decoded_kept=false`，返回时再次同步解码；不能把这些停顿都归因于 shader。

## 修正

旧 `CacheBudget::request_ready(true)` 一有异步任务就申请总额度的 5/6：
192 MiB 中先预留 160 MiB，把 provider 闲置缓存限额从 64 MiB 压至 32 MiB。
即使 ready 只实际占四五十 MiB，也会提前淘汰已用过的解码结果。

现在改为 `min(实际 ready + 16 MiB, 总额度的 5/6)`，每次发布刷新申请，
没有异步任务时撤销申请。总缓存仍为 192 MiB，provider 闲置上限 64 MiB，
GPU 闲置上限 32 MiB，堆请求 320 MiB；申请不等于可以使用尚未释放的字节。
实际发布仍受同一账本、原来的准入及回收规则约束。没有移除压缩备份，
没有改变 shader、字体和 GPU 同步。较大资源仍可能因容量不足而降级，
此改动不承诺所有资源一直保留像素，也不增加常驻资源。

核心基线 a721bfb。可重放补丁 `patches/progressive-prefetch-reservation-core.patch`。
500 项核心测试通过、17 项忽略，包含申请增长、取消、真实压力下 LRU 回收与
ready/idle 不重复使用同一份额度的检查。

## 实机实验

脚本 `fixtures/cache_reservation.iet`：预载并显示 first，连续显示 18 张不同
960×540 填充图，让 first 从 GPU 淘汰但保留 CPU 解码结果，再启动 8 张图的
异步准备，1 秒后返回 first。需要另行提供 `assets/first.png`、`fill0..17.png`
及 `ahead0..7.png`；ahead 为 first 的独立路径副本。BOOT 指向该脚本，960×544。
资源仅用于本地验证，不提交游戏资源。保存截图引起的写盘长帧不计入结果。

两轮日志均为 CPU 500 MHz / ES4 222 MHz（设备当时的实际时钟），因此这些数值
只用于本实验 A/B；不能与之前 333/111 的灰度性能直接比较。

| 项目 | 旧策略 | 渐进申请 |
|---|---:|---:|
| 异步任务启动后 first 解码缓存 | 被回收 | 保留 |
| 返回 first | 压缩备份命中后重解码 | 已解码缓存命中 |
| 解码／上传 | 解码 59.208 ms + 发布 0.103 ms | 上传 8.561 ms |
| 提交场景耗时 | 78.438 ms | 27.688 ms |

新旧 first、return 两个截图全图逐像素对比均为零差异。新包启动像素自检通过。
新 eboot SHA256：`d67704447464a85ba7cedada70b794a2fa249d07a76846f489200c49e24278df`。
证据：`build/dialogue-stall-20260920/reservation-baseline-final/`、
`reservation-after/`、`reservation-pixel-comparison.json`；首轮旧实验没有制造足够
压力，不用它作为回收问题的对照。

还需实际剧情复测：相同路线切背景／人物，尤其重复回到旧背景时，确认解码命中率
和长帧改善。冷资源首次加载、灰度合成重建、换句逻辑仍有独立成本，不能据此
宣称所有卡顿已消除。字体边缘问题按用户要求留作下一项。
