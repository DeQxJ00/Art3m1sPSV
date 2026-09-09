# 原生 Artemis 的缓存、离屏渲染与当前 Direct 对比

核对日期：2026-09-09。当前代码基点：`a8e1499`，01.02 Direct 分支。
这是静态分析与后续实施依据，不是新的实机性能验收结果。

## 结论与证据边界

原生确有资源缓存、字形复用、图层变化标记、可采样的离屏 surface 和三帧显示缓冲；目前没有证据把它们归纳成一套固定的“三级缓存”。三帧显示缓冲、三个 GPU 内存池、资源/绘制结果缓存是不同机制。

最值得采用的是：让未变化的图层尽量不重复构建数据；需要合成的图层使用持久的离屏对象；资源加载与回收有预算和生命周期。不能由“原生支持离屏”推导出“原生每个背景、立绘、文字页都预先画成一张缓存图”，也不能由“有变化标记”推导出“静止帧完全不提交 GPU”。

当前主要问题是换句低帧。因此实施优先级仍是 CPU 场景/文字绘制数据复用，其次才是转场捕获与选择性离屏合成。把所有图层无条件增加一次离屏 pass，可能进一步降帧。

本轮未改 shader、同步代码或实机安装文件。最新 CPU 修改的实机收益仍未验证；此前 optK 的实机数据与 optQ/optR 的模拟器数据不能混用。

## 样本与分析方法

每个 IDA MCP 在本轮读取前均完成健康检查，核对了实际输入路径。使用只读反编译、交叉引用和指令读取，没有重建函数、修改类型或改写 IDB。

| MCP 端口 | 本次实际载入样本 | surface 创建/调整尺寸入口 |
| --- | --- | --- |
| 13337 | PCSG01084 | `0x8102E6C0` |
| 13338 | PCSG01127 | `0x8102EDC4` |
| 13339 | PCSG01201 | `0x8102E8F0` |
| 13340 | PCSG01235 | `0x81031D28` |
| 13341 | PCSG01297，与既有 SHUF00002 原生 eboot 对应 | `0x810328D8` |

本次 IDA 实际载入的是上表五个样本，不把最初安装目录列表直接当成当前 IDA 样本列表。
下文未额外注明的地址均属于 13341；不能直接拿去查另一份 eboot。

本地原始导出位于 `build/native-offscreen-cache-audit/`，包括五份 `surface-*.c`、健康检查、BeginScene/显示队列指令窗口及单函数导出。商业二进制及大段反编译结果不纳入 Git。旧 `build/renderer-comparison-20260908/REPORT.zh-CN.md` 对“当前渲染器”的部分描述属于更早的 Native v1/Borealis，不适用于本报告的 Direct 基点。

IDA 对部分 ARM 函数边界识别不正确：例如请求反编译 `0x81225F30` 会得到包含前后函数的 `sub_81225CA8`。本报告使用虚表入口和原始指令核对边界；反编译文本中跨越栈保护失败分支拼接的“后续逻辑”，不能视为同一函数的正常执行流。

## “三级”分别指什么

| 机制 | 原生已确认的内容 | 当前 Direct | 能解决什么 |
| --- | --- | --- | --- |
| 三帧显示缓冲 | 三组显示像素、color surface、sync object；队列提交后索引轮换 | `gpu.cpp` 已有 `buffers[3]`，`back=(back+1)%3` | 协调 GPU 写入与显示读取；不会自动减少场景构建或字形绘制 |
| 三个 GPU 内存池 | 既有 `0x81030218` 分析为 64 MiB CDRAM、256 KiB CDRAM、128 MiB uncached RAM；分配入口 `0x81031234`、释放 `0x81031FC8` | 每个独立分配按 256 KiB 对齐，CDRAM 失败回退 uncached RAM | 降低小对象分配浪费、控制碎片与内存位置；不是三层贴图缓存，也不是可直接使用的图片容量 |
| 资源缓存 | 按资源身份保存引用，后台请求队列、预算计数和淘汰 | core 资源/纹理身份缓存与 host 纹理表已有实现，策略不等于原生 manager | 少重复加载、解码、上传；不能单独解决每帧遍历开销 |
| 字形与绘制数据复用 | 字形样式键、图集、持久图层对象、变换变化标记 | 文字缓存、消息相等性缓存、排序缓存、tween 优化已有；仍有全场景重建路径 | 减少换句和逐字显示的 CPU 峰值 |
| 离屏结果 | surface 同一像素存储同时绑定输出和纹理；上层可开启 intermediate rendering | 当前 01.02 host-direct 没有完整的 group render-target 路径 | 合成效果；静态复杂组在正确失效后可复用，但会增加 RT 切换成本 |

前三者不能相互替代。显示缓冲的第三张空闲，不代表可以随意覆盖仍被转场纹理引用的像素；纹理生命周期还必须覆盖最后一次 GPU 采样。

## 资源缓存具体如何工作

`CSurfaceManager` 构造入口 `0x8101DC64`：

- `+40` 保存传入预算，`+44` 保存当前计数；构造时计数清零。
- `+48/+72/+96/+120` 是四组容器，不是三个缓存层。
- `+168` 一带维护待处理请求队列；`0x8123736C` 按请求名检查队列，`0x8123730C` 判断是否有待处理请求。
- 构造路径创建名为 `CSurfaceManager` 的工作线程，入口 `0x812376A0` 调用 `0x8101F6CC`。

工作线程在队列为空时等待。处理请求时，先取名称和 surface 引用，释放主锁，调用 loader，再重新加锁检查当前请求是否仍匹配。加载成功后：写入 `+48` 的按名记录；在 `+72` 保存反向名称关系；在 `+96` 保存附加关联；把 surface 虚方法 `+44` 返回值加入用量。这个结构支持后台加载与请求变化处理，不是每次绘制都读文件。

`0x812375A4` 在用量超过预算时，从淘汰队列头取得名称，在 `+120` 容器中找到对象，扣减用量、移除对象、弹出队列项；可淘汰容器为空时停止。确认的是**有预算、按队列次序淘汰**。尚未完整跟踪命中时是否调整队列位置，因此暂不称其为严格 LRU，也不把全部四个容器都称为贴图副本。

配置入口 `0x81201708` 中读取 `.ini.SurfaceCacheSize` 并传给 manager 创建路径。没有从静态代码确认此游戏实际运行时的配置值及单位，不能据此给当前版照填一个固定 MiB 数。

## 字形、图层与变化标记

既有原生分析定位了 `0x81003E2C` 的样式化字形缓存和 `0x81014C00` 的文字构建/引用复用；字形缓存键包含字体、字符和样式/颜色等信息，图集路径出现 512 尺寸。该结果不意味着所有游戏字体缓存都应强制设为 512，也不意味着扩大图集必然提高换句帧率。

本轮新核对 `0x81000D18`：保存上次输入的六个二维变换分量，逐项比较后设置变化位；其中部分矩阵分量变化会设置更强的标记。只有满足对应标记才调用虚方法 `+192` 更新，随后再进入绘制分派。说明原生区分“数据需要更新”和“继续使用对象绘制”。

`CGpuLayer` 的实际绘制入口 `0x8102F718` 使用对象保存的四顶点存储，仍写入坐标/UV。不能声称其每个 draw 都完全不写顶点。当前 Direct 也已是四顶点、邻接兼容批次合并，不应再拿早期六顶点/NanoVG 路径比较。

当前 optQ 已减少重复排序、optR 已减少 tween 的字符串转换与消息遍历、`a8e1499` 减少了完成 tween 扫描中的复制；但它们没有把整棵场景变成局部增量绘制。此前 optP 诊断中，完成文字的场景仍持续因 compositor 状态触发构建。因此下一步需要复用**局部绘制数据**，而不是看到 glyph cache 命中就认定文字的 CPU 工作已经结束。

## 离屏 surface 和目标切换

五份 surface 函数都能看到先比较宽、高、格式、flags；完全相同直接成功返回。这里证明的是**同一对象的分配复用**，尚不能称为全局 render-target 池。

13341 的 `CGpuSurface` 关键成员：

| 偏移 | 用途 |
| --- | --- |
| `+4` | 像素地址 |
| `+8/+12` | 请求尺寸 |
| `+16/+20` | 格式与 flags |
| `+24/+28` | 对齐后尺寸 |
| `+32` | 内存分配句柄 |
| `+36` | `SceGxmTexture` |
| `+52` | `SceGxmRenderTarget*` |
| `+56` | `SceGxmColorSurface` |

flags 的 bit 0 选择可渲染路径。创建 render target 后，分配像素，把它交给 `sceGxmColorSurfaceInit`，随后把**同一地址**交给 `sceGxmTextureInitLinear`。这样输出已经在 GPU 可采样的存储里，不需要仅为了下一次采样而先读回 CPU 再上传。该路径无 MSAA。

`0x81031CB8` 的 BeginScene 根据 renderer `+12` 判断目标：空值使用当前显示缓冲；非空使用上述 surface 的 render target 和 color surface。`0x81031EF4` 设置目标时，如果已有 active scene，会执行 EndScene → Finish → 重新 BeginScene。说明原生也支付目标切换和同步成本，不能把它简化为“原生没有等待”。

另外五份的帧末同步并不完全一致：三个较早样本在 BeginScene 前有 Finish；两个较后样本在显示队列提交、轮换之后有 Finish。当前 optK 安全基点的等待不能仅凭“三帧缓冲已做”删除。之前缺块和黑屏的回归仍是必须守住的边界。

## 上层 intermediate rendering：已追到哪里

脚本解析路径（IDA 归入 `0x811A373C`）把 `intermediate_render` 分为启用和模式两个调用：字符串 `0` 关闭；`1` 传内部模式 2；`2` 传内部模式 1。不能仅把它们视为同一个 bool。

通过 RTTI `0x8140A7F0` 确认这里使用 `CArtemisLayer`，对应虚表 `0x8140A284`：

- `+300 → 0x81225CA8`：启用时按条件创建对象引用，保存在 `+88/+96`；初始两处可以指向同一对象；关闭时释放引用，发送变化标记 4。两处引用本身不是两张独立像素缓存的证明。
- `+304 → 0x81225F30`：原始指令确认比较旧模式 `+104`；变化时更新它、清理相关对象所持状态并发送变化标记 4。不是每帧无条件重建。
- `0x810134B0`：按状态选择两组引用/子节点集合，并检查 `+116` 的不同位，必要时调用更新检查，再分派相关对象及子节点。它证明有状态与失效控制，但尚未把每个位、每个虚调用完整还原成“重画纹理”或“只更新属性”。

尚待闭合：具体游戏转场走哪条 capture/合成路径；离屏像素更新的全部触发条件；子层变化如何传递到上层；命中后跨帧保留多久；模式 1/2 对清底与透明度的完整语义。不能把当前 core 的 mode-2 opaque 参数倒推成原生已经验证的语义。

## 原生也有 CPU 截图路径

renderer 虚表 `0x813A9484` 的 `+40 → 0x81031F38` 会取得 960×544 普通 surface，逐行调用复制 helper：每行 3840 字节，源 stride 4096，源从第 543 行向上遍历。它是 CPU 帧缓冲复制，不是前面的离屏 surface 绘制路径。

尚未完成虚调用点追踪，不能声称这个截图接口只用于存档，也不能声称所有原生转场都绕过 CPU。

当前 01.02 `scripts/build-core.ps1 -NativeRenderer` 仅启用 `gl-backend,gxm-native-renderer`，没有启用 `gxm-builtin-effects`。实际路径是：

1. core 请求 `art3m1s_gxm_capture_previous`；未就绪时保持旧绘制列表。
2. host 在完成帧后读回，按请求尺寸产生 CPU 像素。
3. core 得到像素，再经纹理 provider 上传转场纹理。

1280×720 RGBA 的一份 CPU 输出约 3.52 MiB；复制、缩放和上传会产生额外流量，但这发生于捕获需求，不是每句必然发生。优化它有助转场峰值，不能用它解释所有有语音/无语音或文字多少的稳态差异。

代码中的 `native_effects.rs` 与纹理捕获接口存在于另一 feature 路径；当前 host-direct 未提供完整 group/capture-texture 实现。不能通过简单打开 feature 就认定完成迁移，更不能混入旧 01.04/01.05 的整套宿主。当前基础桥接也未消费全部 ShaderGroup，grayscale/negative 参数在这个桥接入口没有实际应用；这是功能接线边界，不能归咎于已经验证的 shader 字节码。

## 接下来的实施顺序与验收

### A. 优先减少换句引起的 CPU 绘制数据重建

在 core compositor 边界缓存局部布局、纹理引用、裁剪和局部绘制片段；文字 reveal 只更新变化的文字部分，立绘位置/透明度动画只更新受影响的变换与参数。可继续每帧正常提交画面，先减少构建工作。

失效必须覆盖公开 `Layer` 可变字段、父层变换/透明度、子节点顺序、资源内容换代、文字 reveal、mask/effect、viewport、删除/改名/读档。当前公共 `get_mut` 不能绕过；仅比较 layer ID 或纹理 ID 不足以安全命中。先比较完整 DrawList 与原实现，再测 CPU 构建耗时。不能为了命中缓存停掉箭头 OGV、光效、tween 或逐字显示。

### B. 独立做 GPU 捕获和离屏对象候选

在 GXM 后端与 host 的接口处实现，不把 GXM 资源管理塞进脚本。复用现有已经验证的采样 shader；原生 surface 的“同一像素同时作为输出与输入描述”可以借鉴。

对象按尺寸、格式、用途匹配；保留显示缓冲和纹理的独立使用生命周期。任何仍被 GPU/显示读取的存储都不得提前改写或回收；不允许同一 pass 采样自己的输出。维护分配次数、存活用量、目标切换、等待和 readback/upload 字节计数。捕获应来自已经完成且方向、逻辑尺寸正确的帧，不能仅把 front 指针长期当作转场纹理。

先覆盖截图/转场，再做必要的 group 合成。保持可单独回退，不动 shader 源码和字节码。旧 shader 功能验收与当前 host 接线验收分开记录。

### C. 有选择地缓存复杂静态组

只有组内绘制结果在多帧间不变，并且减少的子 draw 成本足以抵消合成、存储和切换，才使用持久像素缓存。优先实验已完成的文字组和复杂静态 UI；单张背景或单张立绘本来就是一次贴图，额外离屏不一定有收益。第一次合成仍可能形成峰值，需要与换句分配/上传峰值一起测。

离屏渲染与屏幕外剔除分别统计。当前 Direct 已有透明、裁剪、范围剔除与透明边界修剪；不能把这几项算作新优化。

### D. 内存池候选后置

先记录小纹理实际字节与 256 KiB 分配粒度的差额，再决定是否引入子分配器。原生固定池数值与当前资源规模、字体设计不相同，不直接照抄 64/128 MiB。池化的收益主要是分配/碎片；不能先承诺它会消除每句低帧。

### 验证标准

333 MHz、相同实机、相同存档/页面，分别记录短/长句、无语音/语音结束、连续换句、双人和放射效果、OGV 箭头、转场。至少比较帧时间 p50/p95/p99、超过 16.7/33.3 ms 的帧数、换句最大峰值，以及 CPU build/GPU submit/等待分项。短句与长句本身不同，不能作为唯一 A/B 对照。

同时检查渐变、mask、grayscale、透明边缘、嵌套组、存档缩略图、显示方向与缺块闪烁。Vita3K 用于功能对比；其速率不作为实机 FPS 结论。实机长期稳定接近 60 帧仍是未完成目标。


## 2026-09-10：换人物/场景首次上传，原生 PNG 到 surface 的路径

本轮逐次先server_health、再只读py_eval，端口13341实际输入PCSG01297。原始证据在build/native-five-audit/20260910-transition-*.json，未将反编译正文纳入Git。

- 初始化0x81201708通过0x8132C310创建loader并存到+8；0x8132BB74把它传给CSurfaceManager构造函数0x8101DC64，manager保存到+32。
- loader虚表0x813A4880的+20指向0x8100D334。IDA将它合并进0x8100D0F4，不能把前一函数的开头当作真实入口；其加载分派需要考虑格式插件。
- PNG插件工厂0x812EBFD4设置虚表0x813A6DF0，+12为0x81016EB4。代码识别PNG/IHDR、初始化libpng 1.6.21，配置格式，然后调用surface +8分配/调整尺寸；调用+48，逐行调用+52取得地址，把行地址数组传入解码，最后调用+68。默认工厂字段+4=0，使用流读回调；不是所有格式都预读整文件。
- CGpuSurface两张表0x813A97C4/0x813A9848的+8=0x810328D8，+48=0x81232A10（空函数），+52=0x81032BC0，+68=0x8101D818。+52反编译也被合并，另导出入口原始指令验证：RGBA行地址=surface[+4]+4*surface[+24]*row；单通道为+4+stride*row。
- +68按逻辑尺寸与对齐尺寸补最后一列/行的边缘像素，没有完整图像拷贝。结合此前+4同时交给sceGxmTextureInitLinear的证据，PNG目标实际是CGpuSurface时可直接写纹理存储，不必再整张RGBA上传。这个结论不代表所有运行时资源都走相同具体surface类型，也不证明所有GXM API可以并发调用。

与当前版差异：surface_loader后台产生image::RgbaImage；provider首次resolve仍调用host分配、整图复制和alpha/opacity分析，最后core还检查整体opaque。应优先设计独立未发布纹理存储的准备/写入/发布生命周期，资源预算覆盖排队和取消结果，显示线程仅接受已完成对象。保持GXM上下文单线程、旧纹理GPU引用寿命及取消ticket校验，不直接在线程上调用现有upload入口；原生证据不支持删除安全等待。首先用单次切换frame-spike数据验证峰值分项，随后测试预热/冷加载、连续快速换人、取消/读档和内存压力，不能拿静态60FPS代替转场验收。


## 2026-09-10：用户调整优先级后的异步加载专项

顺序：先完成bind_surface_async与原生异步加载逻辑对照及测试，然后切人物/场景长帧诊断，之后纹理管理。新纹理存储接口与预算原型已移出活动源，保存在build/paused-texture-staging/20260910；实机未部署。后台诊断快照、frame-spike也尚未部署，不将它们混入异步专项实机测试。

新增只读证据20260910-async-manager-table/methods/bind-asm.json：manager表0x813A735C的+8指向0x8101E478；该入口无法直接反编译，使用原始指令。R3低字节控制异步分支：0x8101E940检查标志，非零经renderer(+24所指对象)虚方法+56创建surface引用，再把名称与引用放入+168附近队列并唤醒worker；零标志路径0x8101ED78同样创建对象，再直接调用loader+20。缓存命中、已排队等待、引用记录分别处理，不能把全部绑定都重新解码。+20的0x81236B8C按名移除排队请求或转到已完成对象解绑；+44的0x81237144清空队列。worker先解锁加载、重锁核对请求再发布，先前证据仍成立。

当前异步实现新增两项确定性并发回归：取消保留已完成缓存、队列取消后绝不调用loader；shutdown唤醒等待消费者，不能在活动loader返回前结束，停机后bind无效且重复shutdown安全。完整393通过/13忽略，async-native-lifecycle-tests.log。旧实机日志052424-current确认后台ready记录，但不包含新专项实机验证。CPU缓存预算下Pixels可能降级Encoded，故loading=false仅证明请求后台阶段结束，不能声称已等价于原生可直接使用surface；此边界仍待处理，未宣布专项完成。


异步专项候选：Encoded缓存再次bind时安排持久worker重解码，真正take时若仍是可识别图像压缩数据，也只安排一次后台重解码并等待；不重新读文件。用新的ticket与原队列、取消和shutdown同步。优先像素结果去掉压缩备份，并先回收旧缓存，避免刚重解码就立即降回Encoded。仍保持16MiB就绪CPU缓存预算；未知/坏图只回退一次，超过解码限制的图仍可能走同步fallback，因此不声称所有资源完全异步。首次回归暴露无效数据不应排队等待，已加快速格式识别后重跑。新增真实PNG像素/单次读取、坏PNG有限回退、重复bind主动重解码测试，完整396通过/13忽略（async-redecode-tests-final.log）；Vita核心通过async-redecode-vita-core.log。未包含纹理管理改动。


异步专项实机部署：build/direct-deploy/deploy-20260910-054120/manifest.json，host源逐文件与9d17ae2相同，core d0368c2，候选信息build/async-loader-host/candidate.json。独立host构建目录避免混入此前的status-cache/frame-spike或已暂停的纹理准备接口。054152-current启动像素自检PASS，overlay背景误差1/1/0，333/222/111/111；实际游戏异步加载验证待用户推进剧情，尚未宣称此专项完成。latest包/元数据同步到专项包，实机eboot b2a87580a9101bace4b4033ec9615dbcf5f482333bb14d91a45124eec6b88fde。


054311-current实机（SHA256 d6d234f7630b39bbee959b24fabf21b1bbf18c30d348c1042f15de086cf34dad）首次确认pc/ui/ja/mw/mw的redecode=true、pixels=true，解码13.197ms、前台等待333.389ms。前面背景预载单项599–695ms，说明仅移到同一个worker会有队头阻塞；不能把解码线程变化当作等待延迟已解决。

后续候选core e46a35e：保留普通文件读取/解码worker，增加一个仅接受demanded+redecode的持久紧急线程（priority170，普通180），它不持有source回调、不读文件、不调用GPU。仍共用64项物理队列与16MiB就绪CPU缓存；解码并发最多2，额外一个512KiB栈和一个解码工作集，不能声称峰值内存完全不变。退出会停止并join两个线程，第二线程创建失败时关闭第一个后返回错误。需求升级后notify_all，避免已有排队rebind换到紧急通道却没有唤醒工作线程。

针对实机问题的确定性回归：阻塞大背景source，分别直接take压缩文字框和先rebind再take；两种情况下文字框像素都必须在释放背景之前返回，source总调用仍2次。失败分支先释放source，避免测试析构join卡住。完整397通过/13忽略（async-demand-lane-tests-final.log），Vita核心编译通过。这是根据当前CPU压缩缓存设计作出的调度适配，原生证据没有证明存在两个相同worker；未修改纹理管理。此候选尚未部署，实机仍d0368c2，继续保留用户测试。


### 2026-09-10: stalled-device incident and synchronous rebind regression

- Device still ran core d0368c2 / host 9d17ae2 when the user reported a persistent stall. Captures `build/hardware-logs/20260910-054819-current` and `055155-current` have identical SHA256 8abf63b3280e3d25f1a9f9149cf626d2036e5e6386d2a59ceb4d50da2af26ed6. Last heartbeat: 344959350 us; last cinema01 decode completed in 20242 us. FTP and commands respond; port 1234 unavailable; no fresh crash dump found. The exact blocked call is NOT established.
- Extended the blocked-background regression to synchronous rebind as well as take and async rebind. It failed before the fix (`async-syncbind-before.log`), because synchronous wait did not mark retained decoding urgent. Core b7f4c26 fixes that lane selection and wakes both workers; ordinary first-load cache policy remains unchanged.
- Waiting consumers now emit a once-per-timeout (1 s) pending ticket / queue / redecode snapshot outside the cache lock. Timeout does not fabricate completion, drop requested pixels, or start duplicate I/O.
- `async-syncbind-after.log`: 397 passed, 13 ignored. Vita core compiled successfully. These checks prove the exercised queue behavior, not resolution of the device stall or frame-rate parity. Keep texture management and transition diagnostics deferred until async real-device testing passes.

Post-stop evidence: `build/direct-deploy/deploy-20260910-055727/host.log` contains a longer tail: main frame heartbeat stops at 344959350 us while audio continues through 669883488 us. Earlier equal FTP snapshots were incomplete evidence of logging activity. This supports a main-thread stall, not total process suspension.

Candidate core b7f4c26 deployed with verified eboot SHA256 eaac023c6df545767959fcc3cf9e26eb7ee98bfd74e4a7b58dd7ed1a1c54c3b3. Deployment manifest: `build/direct-deploy/deploy-20260910-055727/manifest.json`; startup log `build/hardware-logs/20260910-055807-current/host.log` passes retained self-test at 333/222/111/111 MHz. In-game reproduction remains pending.

### 2026-09-10: native worker lifecycle recheck and ongoing device observation

Read-only MCP 13341 (health before each call), manager table 0x813A735C: slot +36 -> 0x8101DB70 starts CSurfaceManager worker with stack 0x40000 and signals when queued work exists; slot +40 -> 0x8101E3EC sets stop byte +20, signals the condition, waits for thread termination then destroys thread. This supports stop/wake/join ordering already used by our loader; it does not prove all native cancellation behavior matches. Raw private evidence: `20260910-async-status-table.json` / `20260910-async-status-methods.json`.

Live capture `build/hardware-logs/20260910-060011-current/async-observation.json`: both new workers report priority setup success; zero one-second pending-wait diagnostics. Several static windows have 300 frames / 5 seconds, but latest action windows contain 281567 and 634891 us long frames. Async stall reproduction and transition performance are still unverified; do not promote this observation to overall success. No additional deployment or device input was performed during this observation.

### 2026-09-10: queued decode input lifetime; withdraw timed-wait diagnostic

Core 7f92738: cache publication previously evicted encoded input owned by queued redecodes. A blocked-source/pressure regression deterministically observed two reads of one image instead of one (`async-pending-payload-before.log`). Excluding pending entries from eviction fixes this without raising the 16 MiB retained-cache budget; 398 tests pass. This is async input ownership, not deferred GPU texture-management work.

Live log `060208-current` produced 1506 waiting messages despite some matching demand operations completing in under one second. Therefore the b7f4c26 timed-wait diagnostic is not trustworthy on the current Vita runtime; its scheduling/logging overhead may perturb the measurement. Core 625b916 removes wait_timeout entirely and restores condition waiting, adding one job-begin event per worker task instead. No assertion is made about the underlying pthread timeout cause. The old d0368c2 stall predated this diagnostic and is still unresolved.

`async-event-trace-tests.log`: 398 passed, 13 ignored. Vita core rebuild passed. Shader/render host source remains the isolated 9d17ae2 version.

### 2026-09-10: read-only lock-order audit for original stall

Inspected core `surface_loader.rs`, `runtime/project.rs`, `ffi.rs`, and host `files.c`: worker source reads and decode run after dropping loader State mutex; foreground prefetch takes resolved path before entering loader wait; read_chunk clones FILE_READER before calling host_read; host_read holds files_mutex across disk/PFS access but does not invoke loader wait. No direct State -> files_mutex -> State cycle was found in these inspected paths. This is a limited static result, not proof that no deadlock exists elsewhere. OS I/O and allocator waits remain possible; no corresponding stack was captured.

The device remains on b7f4c26 pending user confirmation of whether the original stall location was passed. Immutable replacement package (625b916) is at `build/direct-candidates/async-625b916`; it has not been deployed. Do not use the device's timed-wait warning count as elapsed seconds.

### 2026-09-10: linked pthread clock mismatch explains unreliable diagnostic

Inspected the exact WSL linker SDK archive `/home/qxj00/ae3-vitagl-build-20260830/vitasdk/arm-vita-eabi/lib/libpthread.a`; disassembly saved privately as `build/direct-builtin-shader/pthread-audit-disassembly.txt`. `pthread_condattr_setclock` stores the selected clock in the attribute (+4), but `pthread_cond_init` checks only pshared (+0) and does not copy the clock. `pthread_cond_timedwait` passes the absolute deadline unchanged into `sem_timedwait`, which calls `pte_relmillisecs`; that helper compares against `ftime` (wall time). The selected Rust std source initializes condvars with CLOCK_MONOTONIC and builds absolute monotonic deadlines. This static call chain explains immediate timeouts and excessive warnings in the withdrawn diagnostic. No SDK archive was modified. Ordinary unbounded condition waits do not use a non-null deadline.

Potential adjacent exposure: enabled profiler aggregate uses mpsc recv_timeout (`core/src/profiler.rs:447`), while disabled profiler uses recv. It requires a separate measured check of the selected std channel/parking backend before claiming runtime overhead; no profiler changes made during async-first work. The original d0368c2 stall predates the newly added diagnostic and remains unresolved.

### 2026-09-10: user confirms real-device freeze regression resolved

User explicitly reports the freeze fixed after testing installed b7f4c26. This is real-device reproduction feedback; the exact old blocked instruction remains uncaptured. It clears the reported persistent-stall regression for that test, not all scene-transition latency or full native frame-rate parity.

Async cleanup core 625b916 then deployed from immutable package `build/direct-candidates/async-625b916/art3m1s_direct.vpk` (queued-input ownership fix, timed-wait diagnostic removed). Manifest: `build/direct-deploy/deploy-20260910-060933/manifest.json`. Next requested work is character/scene transition long-frame diagnosis; GPU texture management remains later.

Cleanup package startup evidence: `build/hardware-logs/20260910-061015-current/host.log` retained self-test PASS, clocks 333/222/111/111. Full in-game latency remains to be measured.

### 2026-09-10: FAST-SKIP STALL REOPENS async acceptance

User reports fast-skip freeze on deployed cleanup core 625b916. The earlier normal-progression confirmation is insufficient for overall async acceptance. Captured `build/hardware-logs/20260910-061329-fastskip/host.log`; command health initially timed out then recovered on the next check. Last visible jobs 71-76 completed, including foreground cache redecodes. Buffered tail still prevents identifying the blocked instruction. This version has no wait_timeout diagnostic, so that withdrawn feature cannot explain this incident.

Core 0080f04 restores only surface_loader.rs to d86d8ce (pre retained-byte redecoding / urgent second lane), leaving other core and renderer improvements intact. This is a causal A/B control, NOT a proven fix or task completion. The 10482a0 per-source timing build was never installed. Prior implementations remain in git for comparison. Shader/render host remains exact isolated 9d17ae2. Fast-skip comparison takes priority over further transition/texture optimization.

Fast-skip control deployed: `build/direct-deploy/deploy-20260910-061608/manifest.json`, eboot SHA256 6b47933c1a8847b3052e970375804189604f4075b6e3044b0da3fe1cc2611c4e. Startup `061703-current` retained self-test PASS, 333/222/111/111. Device fast-skip A/B still pending. The saved pre-stop log ends at gxm-wait around 83.4s with the last visible async begin/ready pairs completed; no exact blocking call identified.

### 2026-09-10: bounded logger storage-stall regression

Added `tests/direct_gxm/log_queue_test.cpp`: block the sink, concurrently submit 4000 messages from four producers, request flush/read stats, verify producers finish while storage remains blocked, queue stays bounded and records drops, then release and drain/stop. g++17 ASan+UBSan on WSL passed. This tests storage backpressure logic only; it does not certify Vita pthread behavior or rule out formatting/allocator/kernel synchronization stalls. No host logging implementation or current device build changed. Fast-skip control real-device result remains pending.

### 2026-09-10: resolve native synchronization imports (not guessed from stubs)

MCP 13341 read-only import-table lookup + local VitaSDK NID database resolves: 0x813369E0 = SceLibstdcxx `_Mtx_lock` (B0705E73); 0x813367B0 = `_Mtx_unlock` (1C4A8E64); 0x813368E0 = `_Cnd_broadcast` (69E27281); 0x81336B50 = SceLibKernel `sceKernelWaitThreadEnd` (DDB395A9). Private evidence: `20260910-async-sync-resolved.json`. The raw stub bodies return -1 before loader import patching and must not be interpreted as native runtime behavior.

Native surface-manager synchronization uses Sony C++ library imports, whereas the current Rust backend goes through Vita pthread. This establishes an implementation difference only; it neither proves a pthread deadlock nor identifies the original blocked call. Do not substitute assumed lightweight-kernel-condvar details for the unresolved Sony library internals.

### 2026-09-10: USER CONFIRMS ROLLBACK BASELINE

User instruction: keep rollback. Do not restore the retained-byte redecoding / urgent second lane extensions without addressing the reported regression. Deployed baseline remains host 9d17ae2 + core 0080f04 (loader restored to d86d8ce), application version 01.10. Fixed package: `build/art3m1s-direct-01.10-async-rollback.vpk` with adjacent JSON; VPK SHA256 05839517cdadfde4626726291c10bb2da39adbcc2d70a98d9ebd86daf6a01093. Root checkout still contains prepared, UNDEPLOYED diagnostic status snapshot/frame-spike changes; root tag is a provenance/documentation checkpoint, not a claim that these are in the device package. Continue next performance diagnosis from the rollback core, preserve shader/render behavior. Full stability coverage and native frame-rate parity remain unproven.

### 2026-09-10: profiler A/B and neutral-parent cache traversal

Real-device same-scene on/off/on profiling, archived under `build/hardware-logs/20260910-062709-profile-ab`, stayed around 30.2 FPS with identical 139 quads / 33 draws. Original `trace-nextline.off` absence was restored and verified (manifest `restored:true`). The follow-up `063249-current` snapshot confirms roughly 5.9 ms logic and 27.3 ms presentation, including roughly 12.4 ms begin wait. Every frame has one copy/composite pair, retained hits/builds both zero, and no texture decoding/upload. These timings are wall time, not independent additive GPU costs. Profiling itself is not the main cause of this scene's sustained slowdown.

Core e369f5e fixes a reproduced traversal omission: a proven neutral parent previously descended through the uncached renderer, preventing its masked child from reaching the existing retained cache. Cache-aware traversal now descends through neutral boundaries with bounded command/group scopes. Non-neutral parents keep the original isolated path. Shader code, mask math, GPU fences and rollback loader are unchanged. Regression fails before the fix (masked child recomposited instead of reused); after the fix 395 tests pass / 13 ignored, including equal-range nesting, moving background, child invalidation, and preserving outer grayscale. Vita core and isolated host 9d17ae2 build successfully. Candidate `build/direct-candidates/nested-cache-e369f5e` records exact hashes. Actual scene speedup and visual correctness still require hardware comparison; log history alone does not prove the current offscreen group has this exact nesting.

Deployment verified: `build/direct-deploy/deploy-20260910-063626/manifest.json`, installed eboot SHA256 `1e6575f709731bb1c45ed5b6074ba22657b32dc3ba3486dd367d39b915fb2725`. Startup snapshot `build/hardware-logs/20260910-063730-current/host.log` reports retained self test PASS and 333/222/111/111 MHz. This validates the existing GPU cache pixel probe, not the new traversal in the user scene. Awaiting the same low-FPS scene for frame/cache-hit comparison.

### 2026-09-10: native intermediate update predicate, exact ARM entry

Read-only MCP 13341 health checked before each query. CArtemisLayer vtable +292 resolves to 0x81309CF0. Exact ARM 0x81309CF0..0x81309D1C checks object +132: nonzero returns true; otherwise it calls 0x8101204C with the low byte of the branch selector and returns its boolean result. Hex-Rays merged this entry into 0x813097CC, so that enclosing decompilation is NOT the predicate body.

Exact ARM 0x8101204C..0x81012320 confirms a recursive child-state check: object +12 nonzero returns false; otherwise branch 0 selects the child container via +80, branch 1 via +84. It queries child virtual +100 (branch 1 first obtains the alternate object via +272), then child +292 with the same branch selector, short-circuiting when either is true. This closes the previously unknown recursive call beneath 0x810134B0: the intermediate layer's state can depend on descendants, not only its own properties. The precise semantic bits of +132 and +100, and the eventual GPU repaint operation, remain unresolved. This evidence does not prove a fixed three-tier cache or that all static frames skip GPU submission.

Private evidence: `build/native-five-audit/20260910-intermediate-update292-asm.json` and `20260910-intermediate-update-8101204c-asm.json`. Do not infer later functions from the merged 0x8101204C decompilation. Current implementation conservatively validates child draw commands, overlapping effects, masks and texture content revisions; native evidence reinforces the need to preserve those dependencies when caching child groups.
