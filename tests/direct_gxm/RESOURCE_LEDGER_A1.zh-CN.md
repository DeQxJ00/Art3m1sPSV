# A1 资源账本首批接入

保留当前 Rust PNG 解码器、16MiB预载预算、单worker取消/等待协议及现有 shader/GPU 同步。该版本只记录资源事件，**reserved 是观察到的计划分配字节，不是已通过总预算准入**，没有启用 A2 回收或限制。

## 已覆盖

- CPU源文件：预载在已知大小时先记录reserved，读取成功转actual capacity；前台读取回调返回后登记实际容量。
- CPU解码输出：共享解码帮助函数仍原样；包装其可失败分配回调，记录reservation和Vec实际capacity。RGBA16缩为RGBA8后仍记完整capacity。
- ready、provider保留像素、GPU降级后的CPU缓存：移动记账令牌，不复制缓冲或重复记账；取消、替换、淘汰及销毁按存储所有权释放。
- Direct内存块：CDRAM和uncached分域，按256KiB对齐后的真实申请量计费。CDRAM失败先归还该域reservation，再记录uncached尝试；映射失败和释放失败不假装内存已归还。
- GPU纹理、离屏目标和固定渲染资源分组；三个显示缓冲和命令/顶点/索引/USSE内存包含在fixed。导入的外部纹理不再次记作本模块分配。
- 仍在GPU使用的已销毁纹理标为retired；retired是live子集。现有GPU完成等待之后实际释放，才扣live和retired。没有缩短fence或提前复用。

账本为固定大小数组，无逐分配HashMap或新增线程。只在分配、转移和释放发生时更新；未变化的容量不产生事件。锁内只做整数更新，快照先解锁再写日志，不在账本锁内IO、等GPU或调用回收。现有5秒heartbeat输出三个内存域的摘要，未增加每帧文件查询。

## 不能据此声称已完成的部分

- codec内部临时分配、FFmpeg/音频及OGV显式队列、字体CPU图集、readback/遮罩临时副本、容器和GXM驱动内部开销尚未逐项接入。
- 通过借用切片上传的数据由调用方持有，账本只记录provider真正保留的副本，不把借用当作另一次分配。回调读取过程、Vec重分配期间分配器内部新旧块重叠及分配器头/对齐不计入当前CPU峰值。
- 部分固定GPU/离屏对象按现有设计保留到进程结束；离开游戏回到选游戏菜单后，不应要求所有GPU字节归零。
- `heap live`应与newlib used做差核对；CDRAM/uncached不能直接从newlib used里减去。差额需要逐类解释，目前不能称为完整进程预算。
- A2预算阈值、抢占/回收，以及B未发布最终纹理存储直接写入尚未启用。禁止依据这次部分统计扩到96MiB。

## 版本与验证

- core `0b901cfedc0cdc315bdcbf6f9da384b81e573f01`；407项测试通过，13项既有测试忽略。
- 新测试覆盖：reservation取消归还、RGBA缓冲所有权往返不重分配、跨owner不翻倍、CDRAM失败转uncached、retired子集以及错误事件不破坏计数。原有异步取消/重绑/队列测试全部通过。
- C++生命周期测试 `resource_ledger_test.cpp` 用独立事件断言验证区域回退、retire幂等、实际释放和外部导入不重复计费；g++ -Wall -Wextra -Werror通过。
- Vita release核心和VPK交叉编译通过。候选 `build/direct-candidates/resource-ledger-0b901cf/art3m1s_direct.vpk`。
- eboot SHA256 `48ddce36d3f9f7c9f06f41240bb8fcce46e8d80bb692b98bd348c0b102d9774e`；VPK SHA256 `0770f78f3c41d25d1a53ce0a472f6f8b00757679d4d85af70bb9700ee95249ee`。
- host基于实际使用的9d17ae2隔离副本加入账本。与根目录现有main的后台诊断刷新/长帧日志有既有差异，未在此候选混入；精确源文件快照、CMakeCache和哈希保存在候选目录。稳定latest指针未更新。

## 实机对账流程

1. 333MHz，SHUF00002先测试；进入背景+文字场景停10秒，再切到头像/双人场景停10秒。
2. 连续切人物/背景，然后同一路线快进约20秒并停止，检查无卡死及缺块。
3. 播放/退出OGV效果后停10秒；有语音页面等语音结束。
4. 从游戏内可用入口回到选游戏菜单，停10秒。请勿用Select退出（Select仍为Auto），也不必为此覆盖存档。
5. 核对每个snapshot的owner之和等于live、retired≤live、faults=0，停句时reserved最终回0；对比进入/退出、重复换场景的CPU/GPU占用是否收敛。

该流程用于确认生命周期记账与当前A0候选的稳定性，不是宣告帧率提升。待对账后继续补媒体等差额，再决定A2额度；实际长帧收益还要后续整条加载路径对比。

## 首次实机启动记录

已部署并读回校验eboot；param.sfo与原版一致。启动的 retained/local-base/overlay 像素自检全部通过，333/222/111MHz。菜单稳定窗口302帧，平均present 16.483ms，最大17.233ms，>20ms为0；不把启动初始化3.34秒窗口算作稳定帧率，也不据此声称游戏内收益。

首次3组完整snapshot均 faults=0、reserved=retired=0。CDRAM 31,719,424bytes，其中fixed 19,922,944、texture 1,310,720、offscreen 10,485,760；离屏包含刚执行自检所保留目标。菜单尚无游戏图像CPU存储，因此账本heap=0，新lib used约9.49MB属于当前未覆盖的菜单字体/宿主等；这已经说明账本尚非完整堆用量。

记录：[启动日志](evidence/resource-ledger-20260910/startup.log)、[对账汇总](evidence/resource-ledger-20260910/startup-summary.json)、[部署](evidence/resource-ledger-20260910/deployment.json)、[候选哈希](evidence/resource-ledger-20260910/candidate.json)。脚本 `scripts/summarize-resource-ledger.py` 校验每域owner合计、retired子集、reserved/peak、faults以及同一snapshot的事件序号。

游戏内场景切换、快进和OGV生命周期对账仍待实机操作完成。当前没有启用A2。

## 游戏内测试及平移反馈

用户已完成本轮头像/双人、连续切换和OGV测试，随后报告当前平移仍明显丢帧。首批账本未启用优化策略，不能据此宣称帧率问题已修复。游戏内最新日志见 [账本汇总](evidence/resource-ledger-20260910/gameplay-summary.json) 和 [耗时/日志哈希](evidence/resource-ledger-20260910/gameplay-observations.json)。未发现账本计数错误，但完整进程覆盖和退出菜单对账仍未完成。

捕获到当前隔离host仍同步查询诊断开关：overlay-cache.off约38–43ms、full-cover.off约59ms、trace-nextline.off约49ms；同一后段窗口无图片解码或纹理上传，最长帧约75ms。根目录已有后台StatusCache实现，隔离构建未带入。先同步canonical main/diagnostic_io/status_cache并验证；不改shader、GPU实现或0b901cf核心库。

OGV窗口帧准备约425–518ms，其中遮罩约147–244ms（已经包含于prepare，不能重复相加）。相关媒体计数扩展草稿保存于build/ledger-media-wip，尚未部署或混入平移查询修复。下一步补OGV显式队列和字体CPU存储，然后才决定A2准入。

## 平移查询路径修复构建

首个gates打包发现源码copy2保留旧mtime，增量构建没有重编译main；虽然源码核对通过，ELF/eboot却与首个A1相同。该产物已标REJECTED，部署在应用停止后中断，未取得成功替换验证。随后clean-first重编译，校验ELF含max_flush_refresh_us及frame-spike标记、eboot确实变化、核心静态库逐字节不变。之后只使用gates-v2候选，不继续部署被拒绝产物。

`build/direct-candidates/resource-ledger-gates-v2-0b901cf`：eboot SHA256 `0f311841800d8d7253d50099bac57708184aef6f1726a0cc41658cc7a4dfadab`，VPK `27ea39370c29f6a1c9d6f9f4d4caa63597f753612a7d40fe2a2ea948e44daa24`。core仍0b901cf，静态库SHA256 `c5056952963ac940583416286e99b43132944f157d83cc79107cd69ede821a59`。

新增 `scripts/check-direct-candidate-source.py` 对照canonical源文件，可用--elf检查编译标记、--self和--previous-self拒绝本应变化却未变化的eboot。StatusCache现有阻塞探针测试再次通过。该修复只针对诊断查询造成的长帧；仍须同一平移场景实机对照，不能声称所有平移问题都已解决。

修复包已通过FTP读回校验并启动：[部署记录](evidence/resource-ledger-20260910/gates-v2-deployment.json)。[新启动日志](evidence/resource-ledger-20260910/gates-v2-startup.log)确认max_flush_refresh_us输出存在，查询刷新由后台日志线程执行；菜单稳定窗口约300帧/5秒，最大约17.3ms，>20ms=0。这轮没有再次运行像素自检，因此菜单离屏分配为0，不与前一轮自检后保留10MiB目标的菜单内存直接比较。shader/GPU源码未变。

新包的同一平移场景对照已请求，尚待用户回到该场景；仅凭菜单日志不能确认平移改善。原有计划继续，媒体账本草稿需在本次查询修复验证后恢复；当前不扩缓存、不接libpng。


## 普通启动遗漏能力初始化导致的低帧回退

用户反馈 gates-v2 平移更慢。`20260910-084251-current` 末尾连续窗口约26.1FPS，平均总帧约38.4ms；logic约3.8–4.2ms，present约34.5ms，其中begin等待约20.8ms。该段无图片解码/纹理上传，每帧有一次copy、一次composite、一个离屏group，retained hits/builds均为0。见 [修正前证据](evidence/resource-ledger-20260910/startup-capability-before.json)。这不是与此前录制严格匹配的同场景A/B，不能据此计算提升比例。

源代码原因：`localBaseAllowed`、`overlayAllowed`、`neutralSingleAllowed` 默认false，只有 `retained_self_test()` 的像素验证才能开启。旧main只有删除 `retained-probe.once` 成功才调用该函数。首次A1部署带此标记，gates-v2未带；标记会被消费，因此旧版即便第一次成功，下一次普通启动也丢失这些能力。后台诊断缓存本身不负责开启它们。

修正：每次Direct初始化后、进入菜单前调用现有像素验证，仍按各路径实际验证结果开启能力，不直接把默认值改为true；兼容消费旧标记。新增 `[render-capabilities] startup_validation=1 retained=... local_base=... overlay=... elapsed_us=...` 日志。未改核心、GPU实现、shader、预算、同步边界或分配策略。成本是每次启动增加验证时间和验证画面，游戏循环不执行此验证。

构建使用clean-first；核心archive SHA不变，源文件与ELF新日志标记检查通过，VPK CRC、eboot一致性、SFO一致性均通过。候选 `build/direct-candidates/resource-ledger-startup-0b901cf`，eboot `590cf946a9f20ba88e46b724e18f4654ffe0905fae00d4395e2a761ca3a82ef7`。不带 `--retained-self-test` 部署，用于验证普通启动。实机启动验证及同场景效果另行记录；不把编译通过当作帧率恢复。


实机不带标记部署成功，随后确认 `retained-probe.once` 不存在并再启动一次。两次均打印 `retained=1 local_base=1 overlay=1`，验证耗时分别1,829,541us和1,828,435us；局部合成像素差0，overlay各背景最大差≤1。首次启动菜单稳定窗口300帧/5秒、>20ms为0，但尚待用户回到之前约26帧的平移画面进行匹配验证。证据：[首次启动](evidence/resource-ledger-20260910/startup-capability-first-launch.log)、[再次启动](evidence/resource-ledger-20260910/startup-capability-second-launch.log)、[无标记检查](evidence/resource-ledger-20260910/startup-capability-restart-check.json)、[部署](evidence/resource-ledger-20260910/startup-capability-deployment.json)。stable latest指针保持原样。


## A1扩展：媒体显式缓冲、剧情字体与加载分段（86a820c）

保持用户确认已修复的平移路径，以及每次启动的能力验证。本轮没有改shader/GPU实现、现有loader等待协议、16MiB ready/idle预算或PNG解码器。不是宣称已经消除了切换长帧。

- ledger v2增加media/font owner，并通过新版本符号使新host不能静默链接旧8-owner核心。
- 媒体覆盖OGV三槽队列、async_pixels、RGBA工作缓冲和alpha mask。av_malloc记录显式请求量（含调用者padding），不含分配器/codec内部额外占用。部分槽分配失败及worker创建失败均释放已记录资源；队列消费不扣除仍保留的存储。
- 字体覆盖剧情FontVec源数据的Vec capacity和字形atlas CPU像素容量。LoadedFont同时持有FontArc及共享账本令牌，named/active引用不重复计费；仅最后持有者释放后归还。ab_glyph 0.2.32的FontArc包装FontVec，owned_ttf_parser 0.25.1保留原Vec，已核对本地依赖源码。字体解析器、轮廓、容器及宿主菜单字体仍不在该字段完整覆盖范围。
- 新增有界load-block记录：文件大小/内容读取、归档媒体读取、stream-open、保存写入各自的锁等待和锁内工作；demux-open、stream-info、video-codec-open及video-worker-join独立计时。只有≥20ms的操作才输出，每个编译单元最多64条，均在释放资源锁之后输出。嵌套阶段重叠，不得直接相加。
- 原有surface-prefetch demand-wait及纹理decode/upload计时继续保留。当前是后台预载+前台按需等待/回退+主线程上传；并非全同步，也并非整个加载路径完全不阻塞。

验证：核心407项回归通过；额外真实菜单字体夹具的共享所有权和字形像素/布局测试2项通过；无渲染feature编译通过。媒体队列ASan/UBSan并发、stop/join测试通过，新故障注入覆盖第1/2/3槽失败回滚和可重复释放令牌。Vita core/host clean build通过，ELF含新版账本和load-block标记，VPK CRC及SELF/SFO一致性校验通过。core提交86a820c，候选build/direct-candidates/resource-ledger-media-86a820c。实机生命周期对账另行记录；A2尚未启用。

后续切换优化按证据分流：预载未及时完成→保留完成结果并安排准备/发布阶段；编码态回退→避免将解码重新推到显示线程；上传/opacity扫描→最终存储准备与分块工作；文件锁等待→缩短共享锁范围或独立读取对象；媒体open/join→拥有明确取消和释放协议的异步准备。每项仍需像素、快进取消和实机长帧验证，不能简单让take返回None造成缺图。


新包已部署并读回核对。启动确认账本version=2 owners=10，retained/local_base/overlay像素自检通过且全部开启，333MHz；菜单9组完整snapshot计数无误。菜单尚未进入剧情，media/font为0是预期（宿主菜单字体不属于本次剧情字体覆盖），不能当作游戏内生命周期验证。见 [启动日志](evidence/resource-ledger-media-20260910/startup.log)、[账本](evidence/resource-ledger-media-20260910/startup-summary.json)、[部署](evidence/resource-ledger-media-20260910/deployment.json)。已请求用户连续切背景/人物、OGV进入退出后停句测试，等待实机对账。

### 本轮游戏内结果（09:27读取）

用户完成上述实机测试。76组完整快照owner合计正确、faults=0；观察到media非零3次、从非零回到0两次，采样最大10,886,528字节。最后media=0、font=18,051,088、heap已记账39,250,391字节；字体仍被游戏使用。5秒快照会漏过短生命周期及实际峰值，不代表FFmpeg内部无泄漏。退出到选游戏菜单的字体释放、重复3轮及A2仍未验收。

日志揭示四张1920×1080背景zbg04a/zbg03a/zbg07a/zbg05a都曾以pixels完成后台预载，但显示时变成prefetch-encoded-hit，前台再解码260,696–293,921us，然后上传99,089–107,496us。这与loader完成缓存超过16MiB后降级像素的代码路径吻合；保留源文件避免了重读，却未避免显示时重新解码。不能把所有bind引用都当作近期需求保护，因为游戏可能一次绑定许多后续背景。

OGV进入帧at_us=241289273耗759,761us，其中media=722,090us；相邻demux-open和codec-open确在main线程执行。仍有其他逻辑及播放中上传长帧，不能认为只异步open即可解决全部媒体问题。启动11.608秒读取对应字体sourcehansans-medium.otf，不属于普通背景切换。最后停句窗口约60FPS，>20ms=0，仅为当前画面结果。

原文件日志64条/进程额度被启动耗尽，无法分析后续文件锁细节。后续补丁改为每编译单元每5秒最多16条、使用32位原子且最多8次CAS重试，旧时间戳不重开前一窗口；诊断竞争时抑制样本而不等待。额度更新、过期调用、回绕及8线程并发测试在ASan/UBSan下通过，Vita VPK构建通过。该补丁尚未部署，实机仍为86a820c本轮包。

证据：[完整日志](evidence/resource-ledger-media-20260910/gameplay.log)、[账本结果](evidence/resource-ledger-media-20260910/gameplay-summary.json)、[资源级关联与限制](evidence/resource-ledger-media-20260910/gameplay-analysis.json)、[加载分项](evidence/resource-ledger-media-20260910/gameplay-load-summary.json)。日志SHA256为89707fcdd31cd8773c6c71d5017fe45fb4357aef817ca0ae7618acef1c5894bc。
