# 01.02 上的第一轮优化：实机纹理上传

2026-09-09。分支 `perf/01.02-texture-latency`；可测试宿主为 **01.02 optB**，不是旧 01.03～01.05。仍链接经过哈希校验的原始 Opt2 core。没有修改 shader 源码或生成头，也没有取消 `sceGxmFinish`、修改三缓冲或改变时钟。

## 已落实并测量的修改

- `ecf5d37`：分开测量纹理分配、清零、复制和透明边界扫描。
- `6b8e1fb`：边界只会扩大，已被边界覆盖的更新不再扫描；每行只寻找最左、最右非零 alpha，跳过内部像素。保持原有透明裁剪及双线性采样结果。
- `4ba7f6e`：完整纹理只写一次，只有行尾 padding 清零；连续图像合并复制，使用系统 `sceClibMemcpy`。同时增加只读时钟、输入边沿诊断。

以下来自同一台 192.168.1.50 的实际游戏，不是 Vita3K 时间。表为 **960×540 上传分项的中位数**，单位毫秒；三次采集分别含 10、14、109 次同尺寸上传，场景范围不同，不能据此计算整段游戏的 FPS 提升。

| 分项 | 01.02 加计时 | optA | optB |
|---|---:|---:|---:|
| 内存分配 | 2.306 | 2.327 | 2.277 |
| 完整清零 | 3.993 | 4.089 | 0.001 |
| 像素复制 | 9.118 | 9.327 | 7.454 |
| 透明边界 | 63.754 | 0.211 | 0.196 |

优化前转场截图上传常需约 79 ms，其中约 64 ms 是透明边界扫描。optB 同尺寸上传通常约 9～12 ms。这里是上传分项，不能写成“换句只要 0.2 ms”。稀疏透明图像仍需扫描外部空白区域，尚未实现 SIMD 扫描。

## 开篇实机测试仍未达到目标

用户手动进入正文并连续换句，日志已保存到 `build/hardware-logs/20260909-013231-companion/host.log`。只读频率为 ARM 333 / Bus 222 / GPU 111 / Xbar 111 MHz。`trace-nextline.flag` 原本已存在，本轮沿用，core 性能采样有额外负担。

后段停住的窗口里没有纹理上传，整帧仍约 24 ms：逻辑约 3.3 ms、提交阶段约 7.5 ms、GPU 完成等待约 13.1 ms。提交阶段包含 core 构建绘制内容的时间，不能全算为 GPU 运算。当前仍有整个 DrawList 反复构建/提交的开销；不能声称已修复全部换句低帧。

首次加载 1920×1080 背景仍出现约 266～338 ms 的同步解码。背景加载、正文持续低帧、换句上传尖峰必须分别处理，不能全部归为字形缓存不足。下一步需要测量相邻帧绘制内容的实际变化，确定可保留的图层/文字绘制记录和失效条件；本轮尚未加入新的整帧或分层缓存。

## 五个原生 eboot 的本轮只读核对

五个 IDA 端口均先通过 `server_health`，再读取函数，没有修改 IDB。

| 端口 / 游戏 | 字形查询/生成函数 | surface 创建/复用函数 |
|---|---|---|
| 13337 / PCSG01084 | 0x81003D64 | 0x8102E6C0 |
| 13338 / PCSG01127 | 0x81003D64 | 0x8102EDC4 |
| 13339 / PCSG01201 | 0x81003D64 | 0x8102E8F0 |
| 13340 / PCSG01235 | 0x81003E2C | 0x81031D28 |
| 13341 / PCSG01297 | 0x81003E2C | 0x810328D8 |

五个字形路径均能确认：字体字符串、字符/样式字段及颜色参数参与查询；命中时复制保留的 surface 引用和矩形/度量；未命中时才生成，正常图集页为 512×512，预算不足另走独立 surface。五个 surface 函数均先比较尺寸、格式及标志，匹配时直接返回。它们支持“保留字形结果、保留 surface、按变化更新”的优化方向；不能由此推断原生一定缓存了整个背景合成层，也不能声称当前宿主已经完整复刻。

原始导出在 `build/01.02-optA/native/font-cache-13337.json` 至 `font-cache-13341.json`；surface 导出在 `build/native-five-audit/13337` 至 `13341`。参考录像仍为用户提供的《原版eboot开篇文字》《direct开篇文字》《direct双人文字》，已核对已有取帧图；截图中的叠加 FPS 不是逐帧统计。

## 验证和回退

- ASan/UBSan：80,000 次随机 alpha 更新与独立像素并集比较；629,326 次双线性采样比较。
- ASan/UBSan：438 次完整/局部像素更新，验证奇数宽度、padding 和分配边界。
- Vita3K：生产 GXM 探针像素比较通过，含混合、裁剪、rule 转场、批次边界、局部纹理更新；optA 的菜单、标题和开篇已查看。模拟器结果用于画面及流程，不用于实机性能结论。
- optB 已实际上传到实机，备份和安装后校验在 `build/direct-deploy/deploy-20260909-012755/`。尚未完成存档 2 的本轮分析及用户对画面流畅度的验收。

测试包：`build/01.02-optB/art3m1s-direct-01.02-optB.vpk`。清单及 SHA256：同目录 `manifest.json`。分项统计：`hardware-comparison.json`，可用 `scripts/summarize-direct-log.py` 重新生成。

原始 01.02 安装包仍保留在 `build/direct-01.02/art3m1s-direct-01.02-original.vpk`，基线标签不变。未把 optB 标为性能目标已达成。

## 存档 2 手动测试后的分析与 optC

用户完成 optB 的存档 2 测试后，日志已取到 `build/hardware-logs/20260909-014643-companion/host.log`（SHA256 `6AF29E2B96B60231146924AAA618400007ABF914954E8D540C91FEFE12CC95B5`）。它包含同一启动会话的开篇和后续手动测试，不能把全文件统计当作单一双人场景。

最后两个无上传、无解码的五秒窗口都是 160 帧：逻辑约 5.47 ms，场景构建/提交约 11.4～11.5 ms，GPU 完成等待约 14.3 ms，整帧约 31.2～31.4 ms。对应 162 个 quad、26 次 draw、2 次 uniform；这些数据说明停留场景的持续低帧并非正在上传新字形。core 采样窗口有 640 次调用、320 次实际绘制，`frame_build_ms` 平均值应乘 2，约 10.36 ms/实际绘制帧；其中 `frame_text_ms` 约 1.14 ms。换句期间另有截图上传与场景/脚本尖峰，需要与持续开销分别处理。

optC 保留 optB 并增加一条宿主优化：检查完整 RGBA 数据，只有所有 alpha 都为 255 时才记录不透明证据；提交时还须满足四顶点 alpha=1、普通混合、没有实际 clip/rule shader，才能选择 GXM 禁用混合的 program 状态。shader 字节码和源码完全不变。半透明纹理、淡出、加法、规则转场、剪裁以及导入的视频描述符继续使用原路径。普通图片局部更新会保守撤销不透明证据；整张替换可以重新确认。PSV 使用 NEON 检查，并单独记录 `opacity_us`，必须核对其新增扫描成本是否小于 GPU 收益。

这来自当前代码确认的重复工作：背景与清屏此前也使用 `ONE / ONE_MINUS_SRC_ALPHA` 混合。它是语义可验证的优化，不等于已经复制原生引擎所有缓存，也没有新增整帧缓存。保留三缓冲、每帧 Finish、原始 Opt2 core 和时钟配置。

验证：ASan/UBSan 通过 156,224 次透明度判定/随机更新检查及混合条件检查；生产 GXM 探针 56 项像素检查通过，新增普通/淡出/加法/翻转着色四组与原混合路径对照，像素差均为 0。Vita3K MCP 离线后通过原有 Start-MCP.ps1 启动并先查询 session_status，未修改模拟器配置。实际性能仍需实机同场景对比，不能由探针的 60 FPS 推断游戏已达到目标。

### optC 实机结果与 optD 扫描修正

用户已确认 optC「已测试」「画面正常」。本轮实机日志是 `build/hardware-logs/20260909-020431-companion/host.log`，统计在 `build/01.02-optC/hardware-comparison.json`。

optC 暴露了需要修正的额外成本：286 次 960×540 上传中，`opacity_us` 中位数 **14.476 ms**，确认完全不透明时常见 14～15 ms。透明图片通常首块即可退出。因而不能把 optC 当作无条件性能改善，也不能只报道它末尾的 GPU 等待下降。末尾静止窗口约 22.4 ms/帧、GPU 等待约 6.28 ms，但内容是 148 quad / 22 draw / 0 uniform，与 optB 末尾的 162 / 26 / 2 不同，不构成严格同场景 A/B。

optD 只替换透明度扫描：使用连续 NEON load 和分块 AND 归并，每最多 1024 像素才取一次归并结果；原 optC 每 16 像素都提取结果分支，且使用交错载入。RGBA 数据和完整 alpha=255 判定语义不变，块尾和局部更新仍全量覆盖。shader、混合路径判定、同步、core 和时钟未再变动。

optD 验证：原 156,224 次 ASan/UBSan 检查通过；Vita3K 执行实际 ARM NEON 分支的 5,731 次单个透明像素位置检查通过，涵盖未对齐数据、64/1024 像素块边界和尾部；56 项像素检查通过。已备份、校验并部署实机，记录在 `build/direct-deploy/deploy-20260909-020951/manifest.json`，启动日志在 `build/hardware-logs/20260909-021056-companion/host.log`。这份启动日志只到游戏选择菜单，不能用于确认 960×540 扫描收益；需要下一次实际游戏采样。

### optD 收益不足，optE 取消大纹理全图检查

optD 本轮实际游戏日志：`build/hardware-logs/20260909-021551-companion/host.log`。247 次 960×540 上传的 `opacity_us` 中位数仍有 **12.722 ms**，只比 optC 小幅下降，不能称为解决。最后停留窗口约 26.7 ms/帧，102 quad / 26 draw / 1 uniform，和 optB、optC 末尾内容都不同，仍不提供跨场景 FPS 提升百分比。

通过 `scripts/run-upload-bench.py --host 192.168.1.50` 做了两次独立实机实验。脚本先备份当前 eboot 和日志，临时借用现有测试应用入口运行 `upload_bench.self`，完成后恢复原 eboot 并逐字节验证、重新启动游戏入口。没有改 SFO、存档、插件或时钟。记录分别在 `build/upload-bench/20260909-022034`、`20260909-022454`；两份 manifest 均 `restored: true`。探针输出 DONE 后已自行退出，随后 kill 返回「cannot kill」是它已退出，不是恢复失败。

独立探针在相同 960×540 RGBA 内存上比较系统复制、NEON 全图检查、32 位 AND、16 KiB 分块复制后检查、经 scratch 中转，以及整图复制后检查。分块方式依然约 12 ms（复制+检查），整图复制后检查约 16～20 ms；不能靠这轮扫描微调消除成本。游戏的典型静止窗口里，不混合覆盖面积为 1,044,488，接近两次 960×544 清屏的 1,044,480；不能声称大量立绘或背景已经走到不混合路径。

**optE** 对可选的不透明证据检查设置 **1024 像素上限**，尺寸更大的纹理直接保守视作未知，不读取其 alpha，不进入不混合优化。局部/完整更新也执行同一上限。小纹理仍须全量证明 alpha=255，提交时仍检查 tint/clip/rule/blend。这样保留纯色清屏等低成本收益，去掉 optC/optD 对转场截图新增的 12～15 ms 全图扫描；大图继续沿用 optB 的混合路径。shader、同步和 core 没有变化。

第二轮实机探针的 method=6 是新的大图拒绝路径，四次均为 1 µs、结果为 opaque=0；这是「跳过检查」，不是「全图扫描只要 1 µs」。ASan/UBSan 检查新增了向大图检查传入仅 1 像素有效缓冲的测试，确认它不会读取整图；1024/1025 边界与随机更新检查通过。56 项生产像素比较通过。尚未宣称 optE 完整游戏稳定 60 FPS。

Vita3K 的 optD 探针在画面验证通过后，退出时曾报告宿主访问冲突（0xC0000005）；不把它记作干净退出。后续 optE 探针重新启动并完成画面检查，实机独立探针正常产出 DONE 并完成程序恢复。

## 有语音页面在播放结束后仍低帧：optF 诊断

用户新观察：有语音句子的页面比无语音句子低帧，且语音播完仍然如此。现有 optE 日志已复制到 `build/hardware-logs/20260909-023643-companion/host.log`。当前不能直接归因于解码器或音频精度，也不能以不同句子的帧率推断是声音本身造成；名字框、立绘、按钮/动画和文字量也可能不同。

发现原 Direct `av_log_set_level(AV_LOG_WARNING)` 隐藏了宿主已有的 `[audio-detail]`、`[audio-perf]`、`[thread-perf]` INFO 信息；播放/结束的 sceClibPrintf 也不写入 host.log。**optF voice trace** 保留 optE 的全部渲染、core、shader、音频格式/块长/解码和混音算法，只修正日志过滤并记录生命周期：

- play/close：进程时间、id、generation、channel、loop、源文件；`voice_hint` 只用于诊断，涵盖通过 SE 播放的 `:vo/` 文件，不改变通道或音量。
- notify：是否仍为当前 generation、是否实际转发 core、从解码结束到通知的间隔和回调耗时。
- audio-detail：当前活动轨道/语音提示数，重采样耗时；保留原解码、混音、阻塞输出错误和线程运行计数。
- frame-perf：增加同一进程时钟的 `at_us`，用于把音频生命周期与帧窗口对齐。

测试：`tests/audio/run.sh` 的 ASan/UBSan 回归通过，覆盖 44.1/48 kHz 单/双声道 PCM 比较、EOF/loop、淡入淡出/声像、双缓冲所有权、延后完成回调、旧 generation 过滤和流释放。Vita3K 先查询 session_status 再启动实际宿主，日志已验证 `voice_hint=1` 的轨道 close、notify forwarded=1，随后 active_voice_hint=0；记录在 `build/01.02-optF/emulator-media.log`。这验证诊断路径可用，不代表已排除实机问题，也不用于比较实机帧率。

已备份并部署 optF，记录在 `build/direct-deploy/deploy-20260909-024134/manifest.json`。实机需要同一句语音的重播前/中/后停留，再与无语音句子比较；特别检查语音结束后解码工作是否恢复、是否仍有图层重建或额外 GPU 工作。此版本是定位用版本，尚未宣称修复语音页面持续低帧。

## optG：有上限的后台 Ogg 准备

原生依据见 [NATIVE_AUDIO.zh-CN.md](NATIVE_AUDIO.zh-CN.md)。用户授权加入压缩数据预载；本轮不调整渲染、shader、core、时钟、音质和 2048 帧双输出缓冲。

对非循环、非视频的 SE/voice 播放请求，输出线程先保留待播放轨道，独立准备线程完成路径解析、压缩文件读取和 Vorbis 打开，随后由输出线程接管解码器并开始混音。不是通过执行未来脚本来预测下一句。由于语音也可能走 SE，该条件按音频命令/循环属性判断，不依赖某个游戏文件路径。BGM、循环和视频音频保留原路径。

- 压缩数据单文件上限 2 MiB、全部准备中/待接管/播放中总上限 8 MiB；准备任务总数上限 16。按 32 KiB 分段读取、检查取消；不把一整份文件读取放进实时输出线程。
- 超限、压缩数据分配失败或预载读取不完整，释放预载占用并退回原有流式读取；不发布半份压缩文件。非 Vorbis 文件保留 FFmpeg 回退。
- 同 ID 替换、停止及 stop_all 使用 generation 使旧任务失效；输出线程接管时再次核验。停止尚未开始的轨道即使带 fade_ms，也不会让准备完成后突然开声。
- 换游戏/退出时先停止并 join 输出与准备线程，再释放队列与压缩缓冲，然后宿主才关闭游戏文件。预载成功的 Vorbis 关闭底层文件句柄，后续播放与重播只读取内存；结束时释放压缩数据。
- 日志 `[audio-preload]` 记录进程时间、id/generation、取消状态、实际驻留/总占用字节、准备耗时及回退状态。prepare_us 是后台准备耗时，不是主线程卡顿或首声延迟。

`build/01.02-optG/audio-tests.log`：ASan/UBSan 全部通过。新增覆盖压缩内存与流式 PCM 逐块一致、播放/重播零文件读取、2 MiB/8 MiB 边界、超限回退、读取失败回退、取消释放、后台读取被阻塞时输出仍继续、待播停止/同 ID 替换、FFmpeg 回退、stop_all、退出重启及无资源泄漏。原 44.1/48 kHz 单/双声道、混音、循环、双输出缓冲和结束通知回归保持通过。

宿主构建通过，VPK 为 `build/01.02-optG/art3m1s-direct-01.02-optG-audio-preload.vpk`。本轮没有宣称解决语音播完后的持续低帧；预载首先针对起播/播放期间的文件开销，最终收益须在相同实机场景、相同时钟下比较。

最终包 Vita3K 会话 `29574fc8-5a4e-4c02-a24f-80c3b0010771`：完成启动、标题、SE 通道系统语音预载/播放/结束通知，以及开篇连续切句；日志 `build/01.02-optG/emulator-media.log`。游戏运行中没有报告音频输出错误；视频打开仍可产生原路径的工作块超时，不宣称所有超时消失。MCP 关闭模拟器时进程报 0xC0000374，不能计作干净退出。

再用最终包会话 `88089f98-2176-4a67-9f94-0135fc62f603` 走游戏 Exit：资源释放并返回选游戏菜单；再按叉退出宿主，日志依次出现 `game resources released`、`GPU and display drained for process exit`、`direct host clean exit`。Windows 模拟器进程随后仍报 0xC0000005。因此能确认宿主退出路径走完，但模拟器进程退出异常尚未定位，不把整项退出测试标为通过。相关记录在 `emulator-clean-exit.log`、`emulator-clean-status.json` 和 `emulator-shutdown-error.log`。

实机重新联网后，已备份 optF 的 eboot/SFO/日志，再部署 optG 并校验回读字节。部署记录 `build/direct-deploy/deploy-20260909-031831/manifest.json`；启动日志 `build/hardware-logs/20260909-031934-companion/host.log` 确認为 optG。没有覆盖存档或调整时钟。实机音频/帧率收益尚待用户同场景测试。

### optG 实机语音测试结果

用户完成测试后，日志复制到 `build/hardware-logs/20260909-034935-companion/host.log`，对应检索记录 `build/01.02-optG/game-retrieval.json`。启动时只读时钟为 ARM 333 / bus 222 / GPU 111 / xbar 111 MHz。真实语音与 SE 的压缩预载约 24～134 ms（后台准备耗时，不是主线程阻塞时间），正常发布、停止和完成通知均已采到。语音播放的所列音频窗口没有 output_errors 或 over_budget，完成后的 active_voice_hint 回到 0。

例如进程时间 1801737993 µs 的音频窗口仅剩 1 条轨道、语音 0，118 个工作块平均 4335 µs、最大 22752 µs；1803084982 µs 的帧窗口 208 帧，media 60 µs、logic 3642 µs、present 20355 µs，合计约 24.06 ms/帧。对应 166 quad / 25 draw / 1 uniform，submit 8615 µs、Finish 11697 µs。因此预载没有消除语音结束后的持续低帧。不同句子的文字/图层数量不同，此处不与旧版不同画面的窗口计算提升百分比。音频 work_wall_permille 也不是 CPU 使用率。

### optH：同画面详细统计开关（诊断版本）

原 host 在存在 `trace-nextline.flag` 时始终启用 core 详细 profiler。为测量统计本身的成本，optH 仅增加运行中暂停开关：启动游戏时仍由原 flag 决定是否启用诊断；若已启用，每秒检查 `trace-nextline.off`，存在则暂停详细 profiler，删除后恢复。没有原 flag 时不持续查询。`[profile-state]` 记录切换时刻和只读时钟；宿主 `[frame-perf]` / `[gxm-perf]` / 音频指标仍保留。丢弃跨切换边界的窗口，不能把统计开销提前当作已确认根因。

`scripts/profile-direct-ab.py` 在用户选好的同一静止画面采样 on/off/on，各 30 秒；不发送输入、不重启、不改时钟，备份并在 finally 中恢复暂停文件原始内容或不存在状态。失联恢复失败会保留明确的 manifest 错误，不能当作已恢复。

构建后的实际 optH 包已通过 Vita3K MCP 启动与标题验证：会话 `1c9dd97b-937d-4f32-b365-1d2940ba2cc8`；日志 `build/01.02-optH/emulator-profile-gate.log` 确认 enabled=1→0→1，暂停段 13 个宿主帧窗口仍输出、详细 core 快照为 0，恢复后快照重新出现。测试临时 flag/off 文件已移除、恢复原不存在状态。此项只验证开关有效，不用模拟器 FPS 推断实机收益。optG 音频、optE 渲染、固定 Opt2 core、shader、同步和时钟均未修改。

该次 MCP shutdown 最终进程退出码为 0，仅代表本次未重现旧的退出异常。已备份并部署实机，记录 `build/direct-deploy/deploy-20260909-035847/manifest.json`；回读启动日志 `build/hardware-logs/20260909-035935-companion/host.log` 确认为 optH，ARM 333 / bus 222 / GPU 111 / xbar 111 MHz。原 trace flag 保留。等待用户在语音结束的静止句子停留后进行同画面采样；此时尚没有 optH 的实机性能结论。

### optH 同画面实测：详细统计不是主要瓶颈

用户进入语音结束的页面并确认「好了」后，执行了 `python scripts/profile-direct-ab.py --host 192.168.1.50 --seconds 30`。原始日志和恢复记录在 `build/profile-ab/20260909-040511/`，manifest 的 `restored=true`，原暂停文件不存在且已恢复该状态。没有发送游戏输入。三段时钟均为 333 / 222 / 111 / 111 MHz，保留窗口均为 96 quad / 25 draw / 2 uniform。

分析脚本 `build/01.02-optH/analyze.py` 只纳入当前 phase 的 profile-state、前一帧窗口末尾已超过切换时刻 1 秒的完整窗口；不把 off 尾部尚未切换的窗口计入恢复段。结果见 `comparison.json`：

| 详细统计 | 有效窗口/帧数 | 平均整帧 | logic/menu | present（含等待） |
| --- | --- | --- | --- | --- |
| 开，第一段 | 6 / 1349 | 22.186 ms | 3.920 ms | 18.201 ms |
| 关 | 5 / 1135 | 22.026 ms | 3.953 ms | 18.009 ms |
| 开，恢复段 | 5 / 1120 | 22.306 ms | 4.020 ms | 18.219 ms |

关闭统计仅相差约 0.16～0.28 ms，约 1%，不足以解释低帧或达成 16.67 ms。每段 Finish 平均范围仍为 10.58～10.61 ms；提交阶段约 7.33～7.69 ms，且包含 core 的帧构建。不能把删除统计当作本轮性能修复。

本轮还核对了**实际链接的固定 core**，避免只按较新源码推断：`build/01.02-optH/symbols.txt` 和 `flush-events*.asm` / `render-cache.asm` 来自本次 ELF。`present_gxm` 在 0x811C5430 清除 runtime+2697 的 dirty 字节；`flush_host_events` 在 0x811C488C～0x811C48B8 将「收集的事件非空」合入 runtime+2697/2698；`render_current_frame` 的 0x811C5540～0x811C5574 检查 dirty、已有 draw list 和纹理 revision 后决定是否进入重建。因此固定 core 确实存在绘制列表缓存，但事件/动画可以使其失效。尚未识别本页具体是哪项持续使其失效，不能直接跳过事件或动画。

同一静止窗口另有 reads=227、missing=227、read_us=13571（总计五秒窗口），约 60 µs/帧；没有纹理解码或上传。重复缺失查询值得清理，但其已测成本不足以解释 22 ms。下一阶段应量清缓存失效来源，并检验 CPU 场景准备与 GPU 执行的重叠机会；任何等待位置调整都必须继续保护纹理更新/释放、顶点复用、视频表面及截图读取，不能重现先前缺块或黑屏。

### 字数对持续帧耗时的影响

用户观察到字多和字少页面也有帧率差异。核对实际 optH ELF 的 `GlyphTextRenderer::build_text_commands`：0x811EBB52 调用 `layout_message_layer`，后续 0x811EC690、0x811EC76C、0x811EC828、0x811EC8CC、0x811EC974 等调用 DrawCommand::clone 并向命令向量加入内容，反汇编在 `build/01.02-optH/text-build.asm`。对应现有源码 `core/src/text/glyph.rs`：字形图集缓存命中后，构建阶段仍进行排版及逐字命令生成；描边有四个偏移副本，正文一个，启用阴影再加一个。即有描边时通常每字 5～6 个 quad，**不等于每字 5～6 次 GPU draw call**，宿主还有相邻批处理。

因此仅扩大字形图集不能消除随字数增长的重建和绘制成本。尚未进行同背景/同效果、只改变字数的实机定量实验，不将跨句差异全部归因于文字。优化优先检查已完成文字的布局和命令复用；如进一步把静态描边/阴影预合成进字形缓存，须覆盖颜色、alpha、每字符缩放/旋转、link hover、ruby 等语义，动态情况保留正确路径，且保持 shader 不变。当前未实现该优化、未宣称达到 60 FPS。

## optI 候选：共享排版结果缓存

新增 `core/src/text/glyph/layout_cache.rs`，绘制、click-wait 图标位置、链接区域以及文字尺寸查询共享布局结果。按实际字符字符串、字形宽度/advance、页面宽度、全部 TextLayoutConfig、不可拆分区间和对齐方式逐项比较，不依赖可能漏更新的页 generation，也不以散列相同作为命中证据。图集位置、颜色和逐字时钟不影响纯排版，它们仍由原绘制逻辑逐帧应用。

最多 8 项、总计 4096 个字形键；单项字符串总字节上限 8192、keep 区间上限 1024。命中使用 Arc 共享坐标，不再复制排版向量；LRU 淘汰不使在用结果失效。超限完整走原排版，不截断文本。该步只复用布局，尚未缓存整个 DrawCommand 列表，也未减少每字的描边/阴影 quad 数。

验证记录 `build/01.02-optI/`：

- `text-tests.log`：44 项通过，1 项字体 fixture 测试当时按默认忽略；随后用现有 menu.ttf 单独执行并通过（`font-fixture-tests.log`）。新增 500 组混合文字/禁则/缩进/对齐/注音区间对照，以及实际输入修改、缓存复用、淘汰和超限检查。
- `layout-benchmark.log`：桌面 release，5000 次查询；40/120/360 字原排版 1276/3809/11245 ns，命中查询 151/400/1121 ns。仅为排版函数微基准，不代表实机帧率。
- `test-all.sh` 实际运行：core、Lua 5.1/Luau、两个 E-Mote crate 等累计 950 项通过，随后独立 pf8 crate 的 Windows 路径断言失败（实际反斜杠、断言期望正斜杠，文件未修改）；余下 pfs-upk 单独补跑 5 项通过。不能称整个 test-all 通过。
- `cargo check --all-features` 被已有缺失 bin `tools/game-probes/src/bin/emote_parity_probe.rs` 阻止；`--all-features --lib` 通过。`cargo fmt --check` 报出现有广泛格式差异，未批量格式化无关文件；新缓存模块经 rustfmt 格式化。

**这是明确采用当前源码重编译 core 的候选，不是历史 1.02 core 的精确重建。** 库放在 `build/01.02-optI/libart3m1s_core.a`，独立 CMake 目录 `build/direct-optI` 显式选择该库及 `DIRECT_TEXT_LAYOUT_CANDIDATE=ON`，默认构建仍链接固定 Opt2 库。启动 banner 明示 REBUILT current core。原 GXM 渲染目标文件与 optH 逐字节相同，shader、Finish、音频和时钟没有修改。

候选包 `art3m1s-direct-01.02-optI-layout-candidate.vpk` 的 Vita3K 会话 `a2d9507b-07c6-4b61-aef8-ad4ec309ecd0` 已进入标题、开篇短句及自动换行长句，截图 `after-circle.png` / `opening.png` / `longer-text.png`、日志 `emulator-game.log`。开场曾截到黑色过渡帧，继续后正常进入标题，未将该帧误判为持续黑屏。尚未全面验证重编译 core 相对固定库的行为差异，**没有部署实机，实机仍为 optH**。下一步需要同一 core 中开/关缓存的性能对照，以及和固定基线的兼容性/实机场景对照，不能把较新 core 的其他变化都计为此缓存收益。

### optI 同一 core 的缓存开关与实机部署

为隔离排版缓存本身的收益，增加 per-renderer 开关，经 `art3m1s_runtime_set_text_layout_cache_enabled` 由 runtime 所属线程调用。默认启用；关闭走原布局计算，仍返回相同类型的布局结果，恢复时继续用实际输入核验旧缓存。重新加载项目创建的新渲染器默认启用。候选宿主每秒检查 `text-layout-cache.off` 并记录 `[layout-cache-state]` 及只读时钟；固定 Opt2 构建不引用新接口。详见 FFI_REFERENCE.md。

`scripts/profile-direct-ab.py --host 192.168.1.50 --mode layout --seconds 30` 只临时改变这个开关，保留 profiler 状态不变；原有 `--mode profile` 仍兼容 optH。`scripts/analyze-direct-ab.py <采样目录>` 按 manifest 的模式选择对应 marker，排除跨切换边界的窗口；已用 optH 既有三段日志回归，数值不变。

新开关文字回归 45 项通过，2 项默认忽略。全测试脚本累计 951 项通过，仍仅在未修改的 pf8 Windows 路径断言处失败；补跑 pfs-upk 的 5 项通过。`--all-features --lib` 和 PSV core/host 构建通过；此前缺失 bin 与全库格式检查问题未修复，不声称全检查通过。

最终对照包 `build/01.02-optI/art3m1s-direct-01.02-optI-layout-ab.vpk` 链接 `libart3m1s_core-gate.a`。Vita3K 会话 `428d1bfd-e2ca-4847-8f4a-b8018298532c` 验证 enabled=1→0→1，关闭时进入长句页面、保持该句恢复后换行和位置一致，退出码 0；日志 `emulator-gate.log`。文字区域截图的原始像素不是完全相同（不同采样时刻、背后的动画透过半透明对话框），没有将其记为像素严格一致测试；纯布局坐标的严格对照由单元测试覆盖。临时关闭文件已删除，恢复原不存在状态。GPU 目标文件仍与 optH 逐字节相同。

已备份 optH 的 eboot/SFO/日志并部署这个**重编译 core 的 optI 对照版**，记录 `build/direct-deploy/deploy-20260909-044344/manifest.json`，回读程序字节校验通过。未改存档、shader、音频、同步或时钟；原始 1.02 和 optH 包继续保留。此时等待实机长句页面的缓存开/关/开采样，尚无实机性能改善结论。

### optI 实机长页同画面 A/B：单独缓存排版收益不足

用户确认页面准备好后，执行 layout 模式各 30 秒的开/关/开采样。原始日志、完整窗口与恢复证据在 `build/profile-ab/20260909-045012/`，`manifest.json` 确认 `restored=true`，恢复 `text-layout-cache.off` 不存在。没有发送输入、改时钟或安装程序；实机继续运行 optI，缓存开启。

三段均为 ARM 333 / bus 222 / GPU 111 / xbar 111 MHz；保留窗口稳定为 405 quad、29 draw、2 uniform，语音活动数为 0，仅剩一条音轨。没有纹理解码或上传；每帧约一次缺失资源查询，约 65 µs。此处和 optH 的 96 quad 页面内容不同，不能直接比较两版 FPS。

| 排版缓存 | 有效窗口 / 帧数 | 平均整帧 | logic/menu | present（含等待） |
| --- | --- | --- | --- | --- |
| 开，第一段（包含异常） | 5 / 698 | 40.977 ms | 14.124 ms | 26.782 ms |
| 关 | 4 / 635 | 31.579 ms | 4.493 ms | 27.016 ms |
| 开，恢复段 | 4 / 639 | 31.318 ms | 4.492 ms | 26.756 ms |

第一段进程时间 339783917 µs 结束的窗口含一次 **6741665 µs** 单帧停顿，同期音频工作最大值 **6023852 µs**，不是持续的普通渲染耗时。原因未定位，不能归因于缓存、FTP、休眠或某个插件。完整结果保留该窗口；仅作敏感性分析，另列剔除此窗口后的第一段 636 帧平均 **31.435 ms**。原始分析没有静默过滤异常。

关与恢复开启的完整稳定段仅相差 **0.261 ms（约 0.8%）**，一次三段实验不足以确认这么小的差异具有统计显著性；当前仍约 32 FPS，不能声称解决低帧。

`interpretation.json` 保留相关音频、纹理和 core 分项。core 每宿主帧记录 advance 与 present 两个样本，因此须以 sample_count/rendered_frames 归一到一次实际绘制，不能直接把混合样本均值当每帧成本。最后稳定窗口换算约：帧构建 12.611 ms，其中 backlog/文字度量同步 2.374 ms、文字命令 2.930 ms、场景组装 5.690 ms、纹理保留集合 0.573 ms；提交约 2.046 ms；宿主 Finish 约 11.68 ms。core 为滚动十秒窗口，宿主为约五秒窗口，分项不宣称与宿主逐帧精确相加一致。

源码复核：`render_gxm.rs` 重建时会同步 backlog、生成全部文字命令、组装场景；`glyph.rs` 的字形 atlas 和排版缓存不会缓存每字正文/描边/阴影命令。静态页被事件、动画或其他绘制状态标脏后仍可走此路径。现阶段没有确定此页的具体持续失效来源，也没有做同背景只改变字数的控制实验，不能把所有差异归于字数。

后续优先为已完成文字复用绘制命令，再处理静态场景部分的重复组装。必须验证直接状态修改、字体/图集更换、链接悬停、ruby、逐字显隐和整页变换的缓存失效，避免仅按 generation 判断。GPU 等待仍是独立成本，不能把删 Finish 作为快捷修复。本次只完成实测和记录，未额外修改或部署渲染代码。

## optJ：已完成消息层绘制命令缓存

在 optI 重编译 core 候选上新增 `text/glyph/command_cache.rs`，不是固定 Opt2 库的精确重建。每个消息层单独缓存已经完成揭示的绘制命令；仍在 reveal_pending 的层走原路径。命中后仍复制命令向量交给现有场景管线，**没有减少 quad 数、改变混合顺序、提前合成描边或修改 shader**。

缓存逐项核验实际 glyph（包含字符、度量和 atlas 坐标）、字体描述、页面位置/尺寸、reveal index/clock/hidden、scetween 配置、links、rubies 和 open ruby。全局排版、字体世代、解析后的 atlas 纹理句柄、链接开关和白块变化清除旧条目。atlas flush 仍先于缓存查询，失败/恢复会通过解析结果重新核验。没有仅依赖页面 generation；backlog 标签、历史和字体栈不复制进键。

最多 8 个消息层条目、4096 个缓存命令，消息层键的估计数据预算 256 KiB（不含分配器开销和共享环境副本），按 LRU 淘汰。超过预算的层仍完整构建并绘制，不截断文字。`text-command-cache.off` 每秒查询一次，由 runtime 所属线程调用新 FFI；开关保持排版缓存、动画时钟和全部 GPU 路径不变。A/B 脚本新增 `--mode commands`，继续备份并恢复临时控制文件。

验证产物在 `build/01.02-optJ/`：

- `differential.log` 使用实际 menu.ttf，逐项比较缓存路径与原路径的完整 DrawCommand 列表；覆盖 80 次位置/尺寸/颜色/alpha/旋转/缩放/描边/阴影/atlas/字形修改、排版变化、字体世代、纹理句柄更换和临时不可用、注音、两类链接悬停及开关、逐字入场/退场与清页。另有容量、淘汰、直接编辑和开关回归。
- `command-benchmark.log`：桌面 release 每种 2000 次，40/120/360 字命令构建原路径约 15.2/38.8/184.5 µs，缓存命中约 7.4/11.1/31.1 µs；包含命令向量复制，不是实机 FPS。
- `all-tests.log` 中 952 项通过、19 项忽略，随后仍因未修改的 pf8 Windows 路径断言失败；单独补跑 pfs-upk 5 项通过。all-features lib 检查通过；完整 all-features 仍被既有缺失 emote_parity_probe 源文件阻断，fmt 全库检查仍报告现有格式差异，新模块已 rustfmt。没有将这些检查记成全绿。
- PSV core/host 实际构建完成，包 `art3m1s-direct-01.02-optJ-command-cache.vpk`，SHA256 `8bad416f0d197f5a526da438d2ffaf953dd61d2226530ab6414d0dda4def9e19`。GPU 目标文件仍为 `01075f1feff40f257b61d4f00d323bfa5ebc72e25580f38ea9cbf3d518a19afd`，与 optI/optH 相同。
- Vita3K MCP 首先检查服务，发现端口拒绝连接，用现有 Start-MCP.ps1 恢复服务，未修改配置。会话 `70b7a849-22b8-4d34-b790-2c91ace2c58c` 完成菜单、标题、开篇及换行长句，command-cache-state 为 1→0→1；截图 long-off/long-on 的文字位置、换行与描边一致，背景动画时刻不同，不宣称截图逐像素相同。`emulator-command-gate.log` 留档；原控制文件不存在且已恢复，关闭会话 exitCode=0。

本候选尚需同一实机场景的开/关/开测量，不能用桌面微基准或 Vita3K FPS 宣称接近原生 60 FPS。场景组装、backlog 同步、命令复制和 GPU Finish 仍是后续工作。

实机已备份 optI 并部署 optJ，`build/direct-deploy/deploy-20260909-050826/manifest.json` 记录旧/新程序哈希，FTP 回读验证一致；SFO 字节未变。启动日志 `build/hardware-logs/20260909-050937-companion/host.log` 确认 optJ、ARM 333 / bus 222 / GPU 111 / xbar 111 MHz。未改游戏数据、存档或时钟。等待用户进入长句页面，后续用 commands 模式实测。

### optJ 实机同画面结果：每帧减少约 1 ms

用户确认「好了」后执行 commands 模式开/关/开各 30 秒，产物 `build/profile-ab/20260909-051414/`。manifest 确认 restored=true，原 `text-command-cache.off` 不存在并已恢复。实机始终为 optJ；下述省略 damage key 的本地修改没有部署，不参与这次比较。

三段完整窗口均为 ARM 333 / bus 222 / GPU 111 / xbar 111 MHz，310 quad / 29 draw / 2 uniform，语音数 0，仅一条剩余音轨；没有解码或纹理上传。每帧约一次缺失资源查询、约 60 µs。该页面与 optI 上轮 405 quad 页面不同，不据此比较跨包 FPS。

| 命令缓存 | 完整窗口 / 帧数 | 平均整帧 | logic/menu | present（含等待） |
| --- | --- | --- | --- | --- |
| 开，第一段 | 6 / 1132 | 26.547 ms | 4.056 ms | 22.428 ms |
| 关 | 5 / 904 | 27.717 ms | 4.181 ms | 23.474 ms |
| 开，恢复段 | 4 / 748 | 26.759 ms | 4.132 ms | 22.563 ms |

开启比关闭减少约 **0.96～1.17 ms/帧（3.5～4.2%）**，仍约 37～38 FPS，未达到 60 FPS。三段 Finish 平均范围均约 11.34～11.36 ms，quad/draw 未变，不能把收益记为减少 GPU 工作。

`interpretation.json` 记录按 sample_count/rendered_frames 归一的 core 滚动统计。各段最后一份稳定统计的文字命令构建分别为 **1.242 / 2.356 / 1.272 ms/绘制**，与宿主整帧节省约 1 ms 的方向和量级一致。场景组装仍约 4.8 ms，backlog/文字度量同步约 2.0 ms，提交约 1.47 ms；这些滚动十秒分项与宿主五秒窗口不作逐帧精确加总。保留命令缓存，后续继续减少场景组装和等待成本。

## 后续本地候选：Direct 省略每个命令的 damage key

`compositor/build.rs` 原来对每个正文/描边/阴影 quad 调用 `DrawList::push_layer`，分配一份图层 ID String。`runtime/render.rs` 的桌面局部重绘比较会消费这些 key；`backend/gxm` 和 `runtime/render_gxm.rs` 全目标重绘不消费它们。

新增显式 `RenderPipeline::without_command_keys()` 路径，仅由 GXM runtime 选择。原公共构建入口仍默认生成 key，不改变桌面 damage 语义；Direct 使用匿名 push，command_keys 保留等长 None 列表。没有省略 DrawCommand、蒙版、shader 参数、分组范围或修改绘制顺序，组级标识仍保留。stencil 组的可选身份 key 可以为空，其 mask_range、效果和绘制内容保持原样。

`build/01.02-keyless/parity.log`：128 个父变换时刻，包含两个不同原始字符串 ID（1.80 / 1.8）、80 个注入文字命令、前置内容、蒙版和 intermediate-render 效果组，规范化可选 damage key 后整个 DrawList 严格相等。桌面 release 微基准 `benchmark.log` 每种 2000 次，200/600/1800 个注入命令分别从约 44.1/59.4/184.4 µs 到 20.2/32.0/99.1 µs；是含命令复制的 CPU 帧构建微基准，尚无实机收益结论。

完整测试脚本 953 项通过、20 项忽略，仍在未修改的 pf8 Windows 路径断言失败；补跑 pfs-upk 5 项通过，all-features lib 检查和 PSV core 构建通过。完整 all-features 的既有缺失 bin、全库格式差异仍存在。新 core 仅归档到 `build/01.02-keyless/libart3m1s_core.a`，**没有打包、没有装机，实机继续保持已测的 optJ**。
