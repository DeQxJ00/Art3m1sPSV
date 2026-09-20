# 压缩资源 OGV 提前停止

2026-09-20，用户反馈 Lua 优先预载版没有明显变差，但 SHUFFLE OGV 播放过短。资源来源为 `F:/WorkSpaceAI2/art3m1s_test_rom/SHUFFLE_episode2_psv`。

## 证据

- `build/hardware-logs/20260920-151556-companion/host.previous.log` 确认运行 SHUF00002，加载器标记 `priority=lua-first-v1`。进场和退场 OGV 多次 `queued=1 presented=1`，Theora 报 `error in unpack_block_qpis`，约 225–289 ms 就关闭。
- 同次 `host.log` 是后来的 Toshiue 会话，`flare05.ogv` 也有同样错误，但不能用该文件代替 SHUFFLE 的现场证据。
- 从用户指定的 PSV PFS 中仅提取四个 OGV 到 `build/ogv-short-20260920`。所有 Ogg 页 CRC 正确；Windows FFmpeg 8.0 单线程和双线程解码均报同样错误，排除了该现象必须依赖本次 Lua 调度或实机并发才能发生。
- 对照未压缩原包 `F:/WorkSpaceAI2/art3m1s_test_rom/SHUFFLE_episode2`，同名彩色和遮罩均可完整解码。

| 资源 | 原包 | PSV 压缩包可解码帧数 |
|---|---|---|
| アイキャッチ_01_in.ogv | 15 帧 / 0.5 秒 / 30fps | 3，解码报错 |
| アイキャッチ_01_in_m.ogv | 15 帧 / 0.5 秒 / 30fps | 5，解码报错 |
| アイキャッチ_02_out.ogv | 18 帧 / 0.6 秒 / 30fps | 3，解码报错 |
| アイキャッチ_02_out_m.ogv | 18 帧 / 0.6 秒 / 30fps | 5，解码报错 |

host/video.c 在解码错误时结束播放，因此实机只呈现一帧；不是脚本预载主动缩短 OGV 时长。这不证明所有 OGV 异常都有相同原因，也没有逐字节核对实机 PFS 与用户本地包。

## 独立资源修复候选

从原包重新解码缩到 960×540，再由 WSL Linux libtheora 编码；彩色与遮罩均保持 YUV420、30fps。使用质量 8、全关键帧，确保尾部重复画面的帧数和时长不被当前编码链路缩短。文件变大，PSV 解码成本仍需实测，不能宣称性能已验收。

四个结果分别为 15/15/18/18 帧，0.5/0.5/0.6/0.6 秒，Windows FFmpeg `-xerror` 全帧解码无错误。原 PFS、游戏安装目录及播放器代码均未修改。

修复包：`build/releases/SHUFFLE-episode2-ogv-repair-20260920.zip`，SHA-256 `0d7067f211f127e7e8dc3342ec8af4832cca54e73d9f386dde712b1e58f746dd`。包含四个修正资源、说明与校验清单，未包含其他解包数据。尚未安装到实机。

完整证据：`build/ogv-short-20260920/resources.json`、`decode-report.json`、`original-report.json`、`fixed-report.json`、`repair-validation.json`。商业资源与中间数据只放在忽略的 build 路径，不提交仓库。
