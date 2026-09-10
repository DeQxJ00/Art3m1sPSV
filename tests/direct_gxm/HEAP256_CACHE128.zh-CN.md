# 256MiB堆与128MiB共享缓存试验

2026-09-11，用户要求整体内存直接申请256MiB，并同比扩大缓存。此前建议的104/32对照只完成核心测试，未编译、部署；本轮依用户追加要求改为组合试验。

相对已安装的104/40包：host-direct启动newlib堆192→256MiB（268435456字节）；核心ready+idle总保留额104→128MiB（134217728字节）；闲置纹理最大40→32MiB（33554432字节）。后台pending时既有5/6策略给ready请求111848107字节，剩余22369621字节，非固定双向队列。无pending时idle可用到32MiB，但仍受总额度限制。

idle不随堆增长：上一轮纹理已有6MiB从CDRAM分配失败后回退到USER_RW_UNCACHE；增加newlib堆不增加CDRAM，反而占用更多堆外普通内存。保留32MiB闲置上限，新增总额主要服务后台解码保留。活跃纹理、codec、线程栈和临时空间另计；128MiB不是应用总占用上限。

只改上述三个容量常量。shader、GPU同步、333MHz、队列64、压缩备份、单张解码上限16MiB及等待/取消策略不变。v1.20和v1.20-core标签、原始VPK保留。本轮同时改变三项，不能单独归因堆或总缓存。

核心a47c8a5：443项测试通过、14忽略（build/heap256-cache128-test.log），Vita核心编译通过（build/heap256-cache128-vita.log）。host源副本与canonical核验通过，编译日志build/heap256-cache128-host.log。待冻结和实机启动核验。

实机验收：先关性能浮窗以避免覆盖启动像素自检，安装后确认alpha/shared/retained/local_base/overlay自检与256MiB堆；通过后再打开浮窗。保持333MHz，SHUF00002人物+放射首次切入、重走一次、继续OP硬解并返回剧情及短快进。采样缓存总额、heap实际used/arena、CDRAM/uncached、ledger faults、分配失败与长帧；特别检查新增堆是否挤压媒体和堆外分配。启动或媒体失败时保留日志并恢复已备份包，不将启动成功等同于所有场景安全或帧率改善。

## 候选核验

候选build/direct-candidates/heap256-cache128-a47c8a5，主实现08c9434、核心a47c8a5。VPK SHA256 cf19cd08aa2101d8c1334fabb71e7ed09b8baae667c4e349330813ec56c32b30；SELF c002f8a5f626f28d1812be163d52826ac3b8788efe8b2a5954f2112023329720；core archive 1a9eac512a0f077207977837681da8c3682f3d500ad038a2a56be4f584fbec87。

host编译通过，有既有GNU-stack/约0.035秒WSL时差警告。新ELF/SELF/VPK CRC与嵌入eboot、SFO哈希核验通过；按ELF符号表定位_newlib_heap_size_user对应数据，读取为268435456。两个候选的sources.zip逐文件比较，唯一变更是host-direct/src/main.cpp，shader及其余host源码字节一致（heap-source-verification.json）。

安装前两次VitaCompanion version健康检查连接超时；未执行停止/上传/启动，仍等待用户关闭浮窗及实机连接恢复。不能据此认定256MiB申请失败：新包尚未在实机运行。

## 实机启动通过

用户关闭浮窗后网络恢复，deploy-20260911-054757完成version健康检查、旧SELF/SFO/log备份、临时文件和最终文件读回验证及launch。旧SELF b8c155d7...，新SELF c002f8a5...；kill返回cannot kill app，后续launch成功，不把该返回解释为已终止正在运行的游戏。

启动日志build/hardware-logs/20260911-054835-current/host.log，SHA256 73a3ae082046ab5360c5cb242144bbcc3d95fc34bcd66434d2bad99e83f2b451。build Sep 11 2026 05:45:09；5个alpha证书测试通过，shared max_delta=0/ok=1，retained/local_base/overlay均通过，clock arm333/bus222/gpu111/xbar111。结合实际安装ELF堆变量及已核验newlib整块初始化路径，256MiB堆在本次启动可用。

日志仍在选游戏阶段，未取得128MiB游戏内预算/峰值或OP验收数据。启动阶段3.31秒logic_menu长帧不能当作人物切入结果。当前未证明新包改善帧率，也未验证堆外媒体余量；接下来由用户测试同场景与OP。允许恢复性能浮窗。

## 放射效果实机复测

用户完成切入后，命令端口两次及FTP独立健康连接超时；等待后恢复，只读下载成功，未重启、换包或发送游戏按键。日志build/hardware-logs/20260911-055317-current/host.log，SHA256 be9771c0cbef8e3db62860486155bf9befd14d1cc6488e6eb5dfdb95a110fea1。派生cache-budget.json、ledger.json、heap256-cache128-analysis.json保存在同目录。

- 实际heap_limit=268435456/shared_budget=134217728/idle_limit=33554432/queue64；103份预算、52份完整账本校验无错误/fault。ready峰值96636073（92.16MiB），末次ready53342347+idle21056575=74398922（70.95MiB）。newlib最高used采样132266712（126.14MiB），该样本arena181608448；这不是未采样瞬时峰值，不能据此认定256MiB必需或无碎片。
- 所有账本uncached peak_live=0，没有上一104/40轮texture普通内存回退；CDRAM peak_live/committed99614720（95MiB），仍需验收OP媒体分配。容量变化和执行顺序均不同，不能单独归因idle下降。
- 首次人物/放射切入：2163行，at153545008，total169181us/logic103747/present65376。kun和line21/22/23均prefetch Pixels；kun PNG附加信息36469us，首次上传26080us，line21上传10056us。该次没有wipe_13读取，不能拿169ms直接除以上轮含遮罩402ms来声称同场景提速百分比。
- 后续重走时：2767行，at175980934，total307825us/logic61047/present246723。zbg27k再次Pixels命中，现场没有大背景重新解码；仍首次/再次GPU上传42962us。line21/22再次Pixels命中；wipe_13现场read73303/decode49952/publish24368us，三段合计147623us；其中底层file-read是read的子阶段，不重复相加。下一帧66806us，随后仍有约44–69ms暖机长帧。相比先前背景回访Encoded+重新解码，这是本次可见的缓存命中收益；并非GPU上传也被免除。
- 186300675至271385746共18个约5秒完整窗口，每窗300帧、最大18.593ms、over20ms=0，约90秒接近60帧。之后有用户按键/触摸和UI变动，尾部窗口259/285帧、max139/137ms，不把前段稳定结论推广为全程或最新画面始终60。
- logo.mp4硬解首帧成功，若干OGV已进入播放；尚无OP记录，也没有触发video-cache-reclaim/retry，不能宣称OP通过。

判断：本轮并非总缓存耗尽导致每次人物/放射重新解码；命中后PNG附加信息、首次GPU上传，以及未提前准备的转场遮罩仍造成进入长帧。保留当前配置继续OP验收；后续优先针对遮罩预载/附加信息读取和首次发布做独立优化，暂无依据再扩大容量就能消除这些阶段。本次只采样和记录，未修改运行代码。

### 当前双人＋放射画面占用

用户停在双人＋放射画面后，只读抓取055740-current，SHA256 fd4ca89e51cc289f86be6f08e988ed85516469381da43ccc3fd39789f25ea124。223份预算、104份完整账本校验通过。当前ready45095147（43.0MiB）、idle25497139（24.3MiB），合计70592286（67.3MiB）/128MiB，idle上限仍32MiB。CDRAM纹理58458112（55.75MiB，含活跃与闲置），加fixed19MiB与offscreen10MiB，总CDRAM84.75MiB；uncached为0。纹理总额与idle估算存在重叠和对齐差异，不能两项相加或相减直接推出活跃像素大小。

最后3个完整5秒窗口（at534401679、539406643、544411801）均300帧，max18.223/18.295/18.178ms，over20ms=0；对应图片decode/upload均0，retained每300帧命中600/重建0。仍有每窗300次missing资源查询，合计约28–30ms，不等同于300次图片解码。接近60帧的结论仅针对这约15秒稳定画面；之前有257帧/max98.691ms的变化窗口，不将稳定表现等同于切入瞬间无长帧。未改变设置或代码。

### OP有两次读取长等待，最终仍为硬解

用户报告OP卡顿，随后确认没有软解回退、显示60帧。只读060135-current日志SHA256 f44f9a1bc5274208145d59ab6080c145f6b4546aafa66988bd34af1c3bf3ff8c，仍为heap256/cache128原包，分项统计新代码尚未部署。

movie/kf5nz92d.mp4首次硬解6MiB CDRAM分配失败0x80024309。既有恢复机制释放22个闲置纹理、估算17.5MiB；驱动报告CDRAM free19→48MiB（包括失败decoder清理），第二次同NV12-direct模式first-frame OK、decoder=h264_vita。没有退回软解；此次验证了以前尚未在OP实际触发的释放重试路径，但首用分配失败仍有成本。

持续播放时出现两次整帧9383101us/9317781us，media分别9375611/9311847us；相应video-perf的读取平均耗时135436/311112us，而解码平均2363/2569us。host/video.c该read计时覆盖av_read_frame取目标流包，含底层I/O/调度，不是纯存储读时间。之后正常窗口300显示帧/约5秒，视频120帧/约5秒（约24fps源时间轴），解码约2.4–2.5ms；长等待后短时加快处理积压帧。不能把60fps显示循环等同于视频每秒60个新帧，亦不能把9秒等待误报为全程低帧软解。

开OP前已有bgm06_b.ogg archive-stream-read work9273547us、main查询black.ipt等待2562013us（互相重叠）；日志worker lifetime max_flush_refresh5453360us，也有长I/O操作。媒体读取长等待发生时heap used约59MiB，并无普通RAM分配失败证据。上述不能证明256MiB堆导致周期停顿，也不能仅凭低used排除堆外压力；存储/系统暂停/驱动阻塞需进一步证据区分。当前保留容量，未擅自调整媒体或shader。
