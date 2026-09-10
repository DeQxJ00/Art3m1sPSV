# 单份静态 surface 首版试验

用户要求试验原生的单份像素存储，同时保留已恢复的压缩备份。以ccaa7e0（内容等同7db4f17）为基线；压缩回退4MiB、闲置缓存32MiB、预载16MiB、shader和现有GPU安全等待均保留。

## 实现范围

1. 静态图resolve成功、像素数至少256×256时，确认host可提供有效surface像素视图后释放provider的独立RGBA副本，标记shared。透明度查询按真实stride读同一份像素；只有显式请求完整pixels或CPU合成mask时才临时复制，避免长期重复占用。动态视频、字体更新、截图目标与显式上传继续原路径。
2. 前台需解码、输出不超过16MiB、解码原始通道数据不大于RGBA8输出的图像，先在渲染线程分配新的未发布surface，直接解码到这块映射内存。封装时计算原有alpha／opacity信息，再把非对齐宽度的紧密行从后向前原地扩展为GXM stride，最后初始化descriptor并发布。width已8对齐时没有整图上传或行搬移。16位大于RGBA8的临时输出、分配失败等继续旧路径。
3. 后台预载线程未接入GXM、没有改变等待／取消／ticket协议。它仍产生CPU像素，主线程上传一次，随即释放重复像素。故此阶段实现单份**常驻静态像素**，不是所有后台解码均零复制，也不是完整原生加载架构。
4. private surface在解码／封装失败时由RAII释放，未发布对象从未被GPU引用。发布后的旧纹理沿用destroy/retired和既有完成等待；不能套用未发布对象的即时释放规则。CPU只读共享像素不改写在用纹理。
5. shared对象闲置时按一份GPU存储计费，淘汰时不制造新的CPU备份，压缩源继续保留。当前GPU预算仍是既有估算，实际分配账本有256KiB对齐，尚不是全局A2准入。

## 验证与限制

- core生产改动50ebf1b，随后仅增加失败解码回归。426项通过、14项忽略；日志build/shared-surface-test.log。Vita交叉编译build/shared-surface-vita.log通过。
- 新测试覆盖外部目标PAL8／RGBA32与保护字节、513宽逐像素读取、cache复用／淘汰后源回退、分配失败走旧上传、发布失败和半截PNG不发布并释放临时对象。原有文字、动画、透明证明、异步取消回归保持通过。
- host增加实际GXM启动对照：17×9源、24像素stride、半透明多彩图案，新旧texture分别绘制到屏幕并readback比较。shared路径仅在该项成功时启用，失败保持旧路径；其结果不决定原有shader是否启用。
- Shader字节与效果公式未改。CPU解码直接写CDRAM／uncached内存的速度可能低于写普通堆；必须观察总长帧、常驻内存、重复加载和画面正确性，不能预先认定零复制一定更快。
- 用户仍操作游戏。候选部署后先验证启动对照，再请复现多人／头像、背景平移与切换、OGV、短快进和读档。若出现缺块、崩溃或明显变慢，原idle32-7db4f17包可直接恢复。

## 首次实机部署

最终core 6d88e04、host e4deccf；最后一次Vita编译后重新链接，候选build/direct-candidates/shared-surface-6d88e04。VPK SHA256 c2386bddd17911b6adb2de8a678fbc6e15700e09d6946437f53a01b2d4002af4，eboot b478178ddcda6bf58bdfc5715da04edb3ae21f88e44bb681430ab599015a68c7。host最初clean构建通过，最后重链有0.016秒WSL时钟偏差警告；新ELF标记、源码一致性、VPK CRC、VPK内eboot一致及SFO核对均通过。

deploy-20260911-013205完成备份、读回与启动。启动日志20260911-013333-current（SHA256 05393378bc186a147c09961d4d4c44872488ef3efb7d778c10d788fe56436a87）：`shared-surface-self-test odd_stride=24 alpha=128 max_delta=0 ok=1`，新路径通过真实GPU像素对照并启用；retained/local_base/overlay原检查均通过，333MHz。此时只完成启动验证，不能宣称游戏中的延迟、内存或稳定性改善。待用户按同组场景测试并采集shared-surface-decode、cpu_released、heap/CDRAM和frame-spike记录。

## 首轮游戏低帧与发布整理修正

用户复现后的日志：build/hardware-logs/20260911-013718-current/host.log，SHA256 aef7abca19f046129157477df0910eab4b7d4c5bdb3fa48f73f514417d24e3f7。42份完整资源账本均通过一致性校验、faults=0；末次CDRAM live=79167488、uncached texture=8388608字节。这证明单份surface工作，不代表所有分配都留在CDRAM。

最后一次切换at_us=214307337出现693694us长帧：kun_z2a0100（1020×1008）从压缩备份命中，decode=79531us、publish=240525us；line21 decode=45566、publish=19437us；wipe_13读盘75349us、decode=49661、publish=25990us。随后line22/23继续前台解码，出现约110/111ms帧。末尾两个5秒窗口均300帧、max约17.4ms，故本次主要是载入阶段长帧，不能说停句持续只有低帧。

对齐宽度的tor_z2a0100（984×993）publish=158202us，cinema11（960×540）publish=154475us，而1920×1080背景publish约1.3ms。宽度搬移不是唯一因素；首版直接在mapped像素上逐字节扫描透明边缘有明显嫌疑，但首版只有seal总计时，不能把241ms全部归到alpha扫描。

修正仅改host发布整理：透明边缘以4KiB缓存scratch分块读取再扫描，仍保留不透明边缘直接探测；非对齐行倒序经同样上限scratch搬移，各次memcpy无重叠。没有第二份常驻像素，没有更改shader、GPU寿命、Rust解码器、压缩回退或异步等待。日志新增bounds_us/opacity_us/pack_us。312组边界/稀疏透明/随机alpha、1..4096宽、跨scratch边界、padding和前后保护字节测试，以独立逐像素oracle及原始紧密像素作对照，在WSL ASan+UBSan下通过。

该诊断候选启动时还会对960×540、984×993、1020×1008的合成mapped surface执行新旧bounds与pack计时，对比bounds及所有逻辑像素/padding；与已有GPU像素测试共同决定shared开关。合成计时只能证明该子步骤的成本变化，不能代替同一游戏场景的总帧时间复测。

候选host 5efa0eb，core仍6d88e04；build/direct-candidates/shared-stage-5efa0eb/art3m1s_direct.vpk，SHA256 b2349f0eec512bed2391121c73d04889c1dd4039b2414f05bf6a764c627acb78，eboot 379d5f24e47c553378ad6b45619e058159c52c41b7ee8d1fe7cddb65c37d0588。clean host构建通过（仅工具链crtn.o既有GNU-stack警告），源码一致、VPK CRC、eboot和SFO检查通过。第一次部署在命令health时超时；用户回复后第二次health成功，但FTP读取旧文件备份时超时，尚未kill或替换任何文件。随后health仍成功而FTP欢迎响应超时，实机验证待FTP恢复，不得把这份候选报告为已安装或已加速。

## 恢复连接后部署及实机子步骤对照

deploy-20260911-015204完成旧SELF/SFO/log备份、新文件读回校验与启动，new_sha256与候选一致。kill命令返回应用不可kill（当时未成功终止任何应用），随后launch返回Launched；新启动日志确认实际新程序已运行。

build/hardware-logs/20260911-015330-current/host.log，SHA256 a5d03cbd2057319ee739a1e35b9bb40c8c5546c610caf31999519eaaeb552bcc。333/222/111/111 MHz；shared GPU像素对照max_delta=0、ok=1；retained/local_base/overlay均通过。三组合成mapped surface的逻辑像素/padding与bounds均一致。

| 尺寸/模式 | 原bounds us | 新bounds us | 原pack us | 新pack us |
| --- | ---: | ---: | ---: | ---: |
| 960×540全透明 | 204339 | 36220 | 3 | 1 |
| 984×993中心矩形、边缘透明 | 269582 | 87042 | 20 | 2 |
| 1020×1008中心矩形、边缘透明 | 271746 | 92143 | 65297 | 63653 |

前两组宽度已对齐，没有真正row pack工作。扫描改动在这三项确实降低耗时；非对齐行搬移仅65.3→63.7ms，不能称明显加速。合成测试是固定顺序原→新、不同于游戏具体图片，不直接外推总帧时间或稳定60帧。仍需用户复现同一人物出现/背景切换/转场场景，记录新bounds_us/opacity_us/pack_us与frame-spike；不能把启动自测约3.4秒计入普通游戏加载性能。

## 游戏复测与稀疏动画回退

build/hardware-logs/20260911-015622-current/host.log，SHA256 ebb91797dbbc2cb3242b0d2f497925275374da486cc3d2e3087cee41b311079d。31份完整账本通过、faults=0；末次CDRAM live=88342528、uncached=0。

- kun_z2a0100：decode=77597、publish=179846us，其中bounds=107302、pack=72365us。首版publish=240525us。对应最后切换frame=648640us（前轮693694us），仍明显长帧，不作严格同输入A/B百分比承诺。
- tor_z2a0100：publish=104696us，其中bounds=104564us；首版158202us。cinema11/01从154/155ms降到约27/28ms。
- line21/22/23却回退至publish=37233/38143/37450us（首版约19/18/21ms），主要全在bounds。4KiB分块对稀疏线条过度复制。后续动画frame=129892/127468us（首版约110/111ms）。
- wipe_13 read=77156、decode=49749、publish=24747us（opacity=24005）。其他rule也有约24ms opacity；1920×1080背景zbg27k仍需约298ms前台解码。扫描/搬移不是全部载入瓶颈。

下一修正仅针对shared surface CPU整理：Vita用NEON每次提取16像素alpha，只在有非零alpha的块里定位首尾；不再复制4KiB整行。行扩展使用倒序NEON块，整块源先读进寄存器再写目的地，尾部通过4字节临时值处理，避免重叠memcpy。非ARM保留原便携实现。主机312组ASan+UBSan仍通过，但只验证非NEON分支；Vita NEON分支必须以实机启动像素对照验收。启动probe另增960×540稀疏斜线、1025×9周期透明像素，检查bounds、逻辑像素与padding；任何失败均关闭shared路径。
