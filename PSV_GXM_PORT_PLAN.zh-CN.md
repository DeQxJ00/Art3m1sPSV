# art3m1s → PSVita 原生 GXM 移植计划

调研日期：2026-09-07。本文是实施计划；目前已构建并运行 GXM 宿主原型，T01 已显示完整标题按钮、进入开场并返回资料库，正文颜色修复正在复测，音视频和完整流程尚未通过验收。实施证据与已知缺口见 [当前实施状态](IMPLEMENTATION_STATUS.zh-CN.md)。下文性能指标仍为待验收目标。

## 1. 推荐路线与首版范围

**保留 art3m1s-core 的 Rust 解释器、场景模型和文本逻辑；使用 Borealis PSV/GXM 的 C++17 宿主 UI 与显示平台层；新增共享 GXM context 的 Artemis 剧情渲染后端；媒体采用带 Vita 补丁的 FFmpeg，H.264 优先 NV12 直接供 GXM 采样。**

已核查 xfangfang/borealis 的 `wiliwili` 分支，提交 `5f08b286f3df737f3321d2247a6fe633fcead03c` 与本次 Wiliwili 子模块一致。推荐复用资料库/设置/暂停菜单控件、PSV 输入/输入法/平台事件及 GXM 初始化/三缓冲；游戏排版、图层效果和媒体语义继续由 core/MediaService 实现。详细接入顺序、源码证据和风险见 [Borealis 接入方案](/F:/WorkSpaceAI2/art3m1s-psv-gxm/BOREALIS_INTEGRATION.zh-CN.md)。

art3m1s 主仓库是 Flutter 宿主，实际脚本与渲染核心位于独立的 art3m1s-core。PSV 版重新实现资料库、输入、文件和媒体宿主即可，移植 Flutter/Dart/ANGLE/mpv 整套环境不列入主线。[宿主架构](https://github.com/Alphaly2K/art3m1s/tree/995cd627a346fa5be4c6b6ed1d6468518492a370)

首个验收对象固定为 **T01：用户自行从 Steam 版移植到 PSV 的 SHUF00002 / SHUFFLE EP2 Artemis 游戏**。用户已明确确认该移植包能在实体机运行，将它作为资源兼容性与画面/行为对照基线。第一版目标是该样本在新 art3m1s GXM 运行时完整可玩：启动、标题、正文、选择肢、语音/BGM/SE、视频、存读档、返回资料库。其他原生 Artemis 游戏在 T01 之后测试。PC 高分辨率资源、复杂自定义 shader、E-Mote/Eluna 分别扩展验收。

首版必须有可操作的游戏选择菜单：启动先进入本地游戏列表，支持手柄/前触摸选择、确认启动和退出游戏后返回列表；不得将入口硬编码为单个游戏。列表展示游戏标题，无法读取标题时显示目录名或 Title ID；显示封面（缺失时用占位图）和资源路径，提供重新扫描。SHUF00002 作为首轮测试默认焦点，其他样本仍可选择，测试执行顺序按下文安排。

菜单使用独立的 UI 字体配置和缓存，与游戏正文/注音字体分开。建议将默认菜单字体打包在 `app0:/resources/fonts/menu.ttf`，并允许用户在设置中选择 `ux0:/data/art3m1s-gxm/fonts/` 内的 TTF/OTF 字体；菜单设置保存为 `ui.font_path`，独立字号保存为 `ui.font_size`。自定义字体缺失或加载失败时回退到内置菜单字体，保持设置页可操作；选择支持中文、日文的默认字体并检查缺字回退。修改菜单字体只影响资料库、设置、宿主暂停菜单和弹窗，不修改游戏字体、脚本或存档；游戏自己的菜单仍由游戏资源/运行时绘制。菜单字体设置需重启后保留，游戏退出返回列表后仍有效。

其余首版功能包含目录/PFS 挂载、游戏字体与音量设置、手柄和前触摸；在线翻译、VNDB、复杂桌面 UI 放到后续。游戏资源与运行时 VPK 分开存放。

### 测试样本与固定顺序（用户指定）

| 顺序 | 样本 | 已知状态 | 测试安排 |
|---|---|---|---|
| 1 / T01 | 用户自移植 Steam → PSV 的 SHUF00002 / SHUFFLE EP2 | 用户已确认现有移植包可在实体机运行；新 art3m1s GXM 运行时尚待验收 | 默认首选；P0 基线、P3 基本可玩、P4 媒体/效果、P5 性能和 P6 完整流程均优先用它 |
| 2 / T02 | 原生 PSV Artemis：PCSG01297 | 用户提供的已安装游戏，已确认本机目录和 eboot 存在；新运行时未测试 | T01 之后首个原生样本 |
| 3 / T03 | 原生 PSV Artemis：PCSG01201 | 用户提供的已安装游戏，已确认本机目录和 eboot 存在；新运行时未测试 | T02 之后 |
| 4 / T04 | 原生 PSV Artemis：PCSG01107 | 用户提供的已安装游戏，已确认本机目录和 eboot 存在；新运行时未测试 | T03 之后 |
| 5 / T05 | 原生 PSV Artemis：PCSG01084 | 用户提供的已安装游戏，已确认本机目录和 eboot 存在；新运行时未测试 | T04 之后 |
| 6 / T06 | 原生 PSV Artemis：PCSG01127 | 用户提供的已安装游戏，已确认本机目录和 eboot 存在；新运行时未测试 | T05 之后 |

用户指定目录：`E:/EmuGame/vita3k/_data/ux0/app/SHUF00002`。本轮检查该拼写路径不存在，当前可访问的同 Title ID 目录为 `E:/EmuGame/vita3k_data/ux0/app/SHUF00002`。同时保留指定路径与当前发现路径，执行测试前核对 ini、PFS/补丁、脚本和视频清单，固定资源哈希，避免仅凭 Title ID 把不同版本当成同一样本。

现有移植包的实体机可运行结论来源是用户确认；新 GXM 版的通过记录单独建立。使用该已转换资源集接入新运行时，首轮不重复执行 Steam 原版缩图/转码，也不覆盖该可用包或其存档。对照运行使用现有 eboot，新运行时验收使用独立应用 ID 和数据/存档目录。

每次修复先复测 T01 的相关场景，再测试其他原生游戏；T01 出现阻塞时优先定位，不以其他游戏通过替代。样本登记见 `test-samples.json`。

2026-09-08 用户指定 **T01 存档 2** 为多人立绘与效果测试起点。测试只读取此槽，不覆盖；原档、上一版和缩略图已备份至 `build/save2-baseline/`，哈希登记于 `test-samples.json`。读档后从“我的后背冷汗直冒。”推进，可抵达校门外男性角色与女仆重叠、带表情符号的画面。固定相同台词、文字/UI 显示状态、设备频率和版本进行前后对照。Vita3K 用于画面、逻辑及提交次数诊断，流畅度和性能验收以实体机为准。

T02–T06 按用户列出的顺序安排；首轮完成 T01 完整流程后，再依次测试这五个原生游戏。五个原生游戏的指定目录均为 `E:/EmuGame/vita3k/_data/ux0/app/<TitleID>`，本轮实际找到的目录均为 `E:/EmuGame/vita3k_data/ux0/app/<TitleID>`；两种路径分别登记，测试前再次检查实际来源。已安装状态不等于已通过新运行时或本轮真机验收。

**用户已授权测试时自行复制这些已安装游戏，无需再次询问复制许可。** 到对应样本开始测试时，由执行者将其安装内容复制到 `F:/WorkSpaceAI2/art3m1s-psv-gxm/test-data/<TitleID>/source/`，生成文件清单和哈希并核对完整性；需要解包、转换或适配时另建 `prepared/` 副本。先检查可用空间；已有副本先核对版本，版本不一致时另建版本目录。原安装目录与现有存档保留，运行时使用每样本独立的测试数据/存档目录。游戏资源不纳入源码版本控制或 VPK。P6 的扩展验收须逐项记录 T02–T06，出现失败要留下原因和复现步骤，不能静默跳过。

## 2. 已查明的基础与约束

| 项目 | 已确认的事实 | 对计划的影响 |
|---|---|---|
| core 架构 | 已有 `Renderer` / `TextureProvider` 和 `DrawList`，但 `CoreRuntime` 直接持有 GL context/FBO/GlRenderer/GlTextureProvider | 先解耦运行时，再接 GXM，不能只实现一个 trait |
| feature | `runtime`、大量运行时 C ABI 和 profiler 被 `gl-backend` 控制；PNG 解码依赖也挂在该 feature 上 | 单纯 `--no-default-features` 得不到可玩的无 GL runtime |
| 脚本 | 非 iOS 默认 Lua 5.1，mlua 使用 vendored Lua；Rust edition 2024 | 固定 nightly、VitaSDK、Lua 交叉编译补丁及依赖提交 |
| FFmpeg | Switchfin 提供 n6.0-3 Vita 配方及完整补丁，包含 `h264_vita`、`aac_vita`、`mp3_vita` | 以此固定版本起步，按游戏实际格式补齐 decoder/demuxer |
| GXM 视频 | 两个参考项目都有 mpv GXM 路径；Wiliwili CI 明确提供 GXM 与 GLES2 两种 Vita 构建 | 参考 GXM 帧布局、上下文与同步；正式宿主优先直接用 libav* |
| 本机旧成果 | `F:/WorkSpaceAI2/art3m1s-psv-2` 已有 Rust 构建、流式 IO、音频、NV12 与游戏兼容修改 | 审核后按模块复用，保留来源、差异及测试记录 |

源码依据：[core feature 配置](https://github.com/Alphaly2K/art3m1s-core/blob/e54a5f9495febbce2b6c992d61ed588c787c99df/Cargo.toml)、[运行时](https://github.com/Alphaly2K/art3m1s-core/blob/e54a5f9495febbce2b6c992d61ed588c787c99df/src/runtime.rs)、[feature 门控](https://github.com/Alphaly2K/art3m1s-core/blob/e54a5f9495febbce2b6c992d61ed588c787c99df/src/lib.rs)、[绘制接口](https://github.com/Alphaly2K/art3m1s-core/blob/e54a5f9495febbce2b6c992d61ed588c787c99df/src/render_pipeline/draw.rs)、[Switchfin FFmpeg 配方](https://github.com/dragonflylee/switchfin/blob/500f659bea07f4eb2a507a52f2b81a7a76295bba/scripts/vita/ffmpeg/VITABUILD)、[Wiliwili Vita CI](https://github.com/xfangfang/wiliwili/blob/88e5876bea9502d06f46a8656e3530684d3aaf7d/.github/workflows/build.yaml)。

旧工程文档记录了运行、真机反馈和性能修复，但本轮没有重新跑这些测试。因此将其视作可复用候选和回归线索，不将历史记录直接算作新 GXM 版通过。优先核查 `host/files.c`、`media_io.c`、`audio.c`、`video_direct.c`、构建脚本、Lua 补丁和配套测试。视频旧实现仍用 vitaGL 纹理包装与 `glFinish`，需要改为 GXM 原生所有权和同步。

Rust Vita target 为 `armv7-sony-vita-newlibeabihf`，Tier 3，无预编译标准库，动态链接不受支持，`std` 部分支持。输出 staticlib，由 VitaSDK 链接 SELF/VPK，使用 `-Z build-std=std,panic_abort`；本机已存在 nightly-2026-08-28 / SDK 2026.08 工具链，可先复现再固定配置。[Rust 官方说明](https://doc.rust-lang.org/rustc/platform-support/armv7-sony-vita-newlibeabihf.html)

## 3. 目标模块边界

```text
VitaHost：Borealis 游戏列表/设置/输入/显示平台 + 文件/存档/计时
  ├─ CoreRuntime：Lua/ASB、游戏状态、文本、场景/合成指令
  │    └─ GxmBackend：借用共享 context；游戏纹理、材质、离屏目标和绘制
  ├─ GameSurfaceView：显示游戏 stage 纹理，叠加宿主菜单，由 Borealis 交换显示
  └─ MediaService：共享 demux、音视频解码、混音、PTS、完成事件
       └─ VideoFrame：像素格式/stride/crop/颜色信息/保活句柄
```

建议目录：`core/` 固定版本的核心副本，`host/` Vita 宿主，`host/gxm/` 平台资源，`core/src/backend/gxm/` Rust 适配，`shaders/` 自有 shader 源与生成规则，`tests/` 场景及媒体探针，`scripts/` 构建/资源审计/打包，`research/` 调研证据。

Rust 负责把 `DrawList` 解释为后端操作，C ABI 薄层操作 SceGxm。避免每个 sprite 跨 FFI 反复查询状态：统一提交一批顶点与材质命令，资源使用句柄。C ABI 采用明确宽度、`repr(C)` 布局、版本号和销毁函数，不直接传 Rust 容器。

同一个 runtime 只在逻辑/渲染线程串行调用；解码和 IO worker 通过有界队列传递结果。渲染线程中的 Borealis video context 拥有唯一 GXM context 与显示队列，core 借用 context/shader patcher，媒体线程不自行 BeginScene/EndScene。游戏离屏渲染安排在 Borealis 显示帧外；GameSurfaceView 在正常 UI 绘制阶段采样游戏 stage 纹理，避免嵌套 BeginScene。退出顺序为停止任务、停止解码、移除显示包装并排空 GPU/显示引用、释放媒体/游戏纹理/后端和 runtime，最后关闭 Borealis context。

### 必需的 core 重构

1. 把 runtime/profiler 门控与 `gl-backend` 拆开，新增 `gxm-backend`；PNG 解码和通用资源缓存独立于 GL。
2. 抽象显示 surface、离屏目标、转场捕获、截图、resize、纹理 revision/保活/更新、damage 区域；现有 `Renderer::render()` 一个方法不足以涵盖运行时需求。
3. 逐项清理 `runtime/render.rs`、文本、视频上传及销毁中的 GL 专有调用；通用逻辑只使用不透明句柄。
4. 为视频增加导入外部帧接口，携带格式、plane、pitch、可见尺寸、crop、PTS 和释放回调；保留 RGBA 软件帧入口。现有 `video_gl_*` ABI 仅服务 GL 后端。
5. 明确 capability：复杂 shader、CPU 像素读取、压缩纹理、E-Mote 等。不支持的必需效果输出资源名和原因；不能通过静默忽略蒙版或特效来通过验收。
6. 桌面 GL 保留为语义对照。先证明相同脚本产生的事件/DrawList/存档一致，再评估 GXM 输出。

## 4. eboot.bin 的 GXM 参考结果

实际路径为 `E:/EmuGame/vita3k_data/ux0/app/SHUF00002/eboot.bin`，与用户原路径相比，数据目录为 `vita3k_data`。文件大小 1,892,950 字节；SHA-256 为 `F560C569E4E1480800F518BA166D7D1EF20AB9A1243AB30A6344293B34D834EF`。

已核对旧分析 SELF 与该文件哈希一致，复制现有 IDA 数据库到本工作区，用 IDA 9.1 / ARM Hex-Rays 批处理复核七个函数，导出汇编和伪代码。数据库中的 `native_*` 名称来自此前人工标注，不能当作原始符号；地址、汇编调用与常数是核查依据。本轮复用了分析数据库与缓存伪代码，没有从零重新分析整个文件。

| 地址 | 静态证据 | GXM 版对应工作 |
|---|---|---|
| `0x81031568` | 创建 context/render target/sync object；960×544、stride 1024；循环注册 192 项 shader 程序表 | 建立显式初始化及材质缓存；首版只实现已覆盖效果组合 |
| `0x8102F718` | 纹理槽 0 和可选槽 1、按模式选 fragment program、设置 uniform、提交绘制 | 实现单纹理/双纹理蒙版材质，减少可合并的离屏 pass |
| `0x81031CB8` | 分别进入显示或纹理目标的 BeginScene | 统一 render target 接口，支持必要离屏合成 |
| `0x81031D64` | EndScene、DisplayQueueAddEntry、索引模 3、Finish | 三缓冲与同步作为基线；记录等待耗时再优化 |
| `0x81031EF4` | 活跃场景切目标时 EndScene/Finish，再启动目标 | 正确处理目标依赖，测量 pass 切换成本 |
| `0x810328D8` | 尺寸/格式/标志相同提前返回；否则重建线性纹理/目标 | 建立纹理和离屏目标池，避免逐帧分配 |

证据位于本工作区 `research/ida-gxm/probe.json` 及相邻的地址命名 `.asm` / `.c` 文件。192 是程序表项数，不是每帧 draw 数；静态分析没有证明全局批处理方案、实际 GPU 耗时或原生所有效果语义。原生也使用离屏目标与 Finish，不能把二者存在直接视为低帧原因。实现参考其组织方式，自行编写 shader，不依赖打包原游戏 eboot 或提取出的 shader 二进制。

### GXM 渲染实现顺序

先复用并验证 Borealis 的清屏/三缓冲，接通 GameSurfaceView 外部纹理呈现，再完成游戏纹理 quad、alpha、缩放/旋转/裁剪和文字，随后完成双纹理蒙版、混合模式、转场/效果组、网格。游戏 shader 源经固定工具链生成 GXP，并缓存 shader patcher 结果和 uniform 索引；第一阶段就验证编译工具能实际产出并在真机加载，不把它留到最终集成。Borealis 内置 NanoVG shader 只覆盖宿主 UI，不能替代游戏效果实现。

首版以原有字形/排版语义为准，缓存字形或文本页，后续依据计时选择 atlas；无文字变化时不能重建整页。PNG 解码后按资源用途决定是否保留 CPU alpha 数据，以满足像素命中测试和 `lyedit`，不能为了省内存一概丢弃。

保持透明层顺序，只批处理相邻且状态兼容的绘制；禁止按纹理全局重排。转场使用 GPU 目标快照，常规路径避免整屏 CPU readback。必要的离屏组有预算及复用池。

初版使用完整画面保证正确。优化局部更新时必须处理三缓冲内容版本：前一显示帧不等于当前可写缓冲的内容，可用持久 stage 纹理合成后呈现，或跟踪每个缓冲的 damage 历史。直接将 GL 的单 FBO 局部更新搬到轮换显示缓冲会产生旧画面残留。

## 5. FFmpeg 硬解与音频方案

### 视频主路径

```text
目录/PFS 流 → AVIO → 一次 demux → 有界 packet 队列
  ├─ H.264：h264_vita → 带引用计数的 NV12 帧池 → GXM 纹理采样
  └─ 音轨：解码/重采样 → 混音/PCM 队列 → SceAudioOut
                                  └─ 已播放音频时钟 → 视频 PTS 调度
```

从 Switchfin `ffmpeg.patch` 的 `vitadec_video.c`、自定义像素格式和 buffer allocator 入手；从两项目 mpv 的 `gxm.patch` 参考纹理格式、shader 与 target 约定。正式版本直接接 libavformat/libavcodec/libavutil/libswresample；libswscale 留作软件帧适配。mpv GXM 可作为独立解码/显示对照，不承担游戏音频混音或默认接管显示。[FFmpeg Vita 补丁](https://github.com/dragonflylee/switchfin/blob/500f659bea07f4eb2a507a52f2b81a7a76295bba/scripts/vita/ffmpeg/ffmpeg.patch)、[Switchfin GXM 补丁](https://github.com/dragonflylee/switchfin/blob/500f659bea07f4eb2a507a52f2b81a7a76295bba/scripts/vita/mpv/gxm.patch)、[Wiliwili GXM 接入](https://github.com/xfangfang/wiliwili/blob/88e5876bea9502d06f46a8656e3530684d3aaf7d/wiliwili/source/view/mpv_core.cpp)。

必须验证的细节：

- `AV_PIX_FMT_VITA_NV12` 是补丁定义的格式，必须使用同一补丁版本的头文件和静态库；对应参考路径是 `SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0`，不能凭命名猜测 U/V 顺序。
- 自定义 `get_buffer2`、显存分配/映射/对齐、连续 plane 布局需满足该 decoder 的约束。960×540 可见图像与 960×544 分配高度分开，UV 偏移按分配高度，采样按可见区域；测试带 crop 的帧和边缘颜色。
- NV12 少一次 RGBA 转换/上传不等于完全无拷贝或已达实时。记录实际 decoder、输出格式、拷贝字节数、decode/submit/wait 耗时。
- 保持 AVFrame/AVBuffer 引用直到 GPU 不再读取；帧槽经过 free → decoding → queued → GPU in-flight → free。先用可证明正确的等待建立基线，再实现按提交完成回收；单纯 `av_frame_unref` 不能证明 GPU 已完成。
- 验证 AVCC/Annex B、SPS/PPS、参考帧数量、分辨率变化、B 帧、seek 后 flush、延迟帧排空、EOS、跳过及连续播放。1080p 只列能力探测，首版目标是已审计的 960×540 约 30fps 样本。
- 以已播放音频样本数为时钟并计入输出排队延迟；无音轨时使用单调时钟。处理迟到帧和暂停/恢复，不能按每次主循环固定增加 16ms，也不能让 24/30fps 视频拖慢游戏逻辑。
- BT.601/709、limited/full range、黑位、肤色、UV 次序均用色条/灰阶样本验证，固定 CSC 配置覆盖不了的格式增加转换路径。
- 硬解能力在打开文件与分配前检查。可返回的失败允许释放资源并重开为 RGBA 硬解/软件路径；原生崩溃不会自动回退。超出软解预算时提供具体不支持原因或独立预转换方案。

全屏视频先落地，再把同样的外部帧作为游戏图层纹理接入。配对 alpha 视频必须按 PTS 配对；默认只承诺一个活跃硬解视频实例，多路、Theora/OGV 或遮罩流分别评估，不能把 H.264 硬解能力外推到所有视频格式。

### 音频与 IO

复用 BGM/SE/voice 的 ID、循环段、淡入淡出、声像与结束通知语义；完成事件携带 generation，旧声音结束不能终止同 ID 新声音。48kHz 混音加有界 PCM 队列，decode/mix 与阻塞输出分离；记录队列欠载和各阶段 CPU 时间。

Vorbis 首先审核旧工程 Tremor 定点路径，保留 FFmpeg 兼容回退；其本质仍是 CPU 解码。AAC/MP3 的 Vita 包装器涉及全局库与 stream 生命周期，先限定单个视频音轨，明确资源所有者后才扩展并发。

AVIO 使用持久流句柄，支持 offset、64 位 seek/size、EOF、取消；同一媒体的音视频共享 demux，避免双重读取。PFS 挂载优先级固定，区分独立 PF8 包和真正跨卷归档，不能只凭 `.000` 后缀拼接；共享 seek 状态必须同步。游戏资源、补丁、存档按稳定项目 ID 隔离，写存档采用临时文件和成功替换。

## 6. 资源、坐标与内存预算

显示固定 960×544，游戏逻辑舞台由 ini/脚本决定。通过统一 viewport/projection 和逆变换映射输入；960×540 内容可上下各留 2 像素。PC 资源缩图时保留逻辑尺寸与缩放元数据，避免脚本已有缩放与工具再次缩放；点击区、字体、裁剪、shader 参数必须使用一致坐标系。

先审计图片尺寸、总像素、shader 清单、视频格式和 PFS 结构，再决定按需解码、预转换或压缩。源资源不原地修改，转换结果具备 manifest、来源哈希、参数和验证记录。大于纹理能力的图片采用离线处理或分块，不等到上传失败才发现。

以 IDA 参考中的 stride 1024 估算，3 张 1024×544×4 显示缓冲约 6.38MiB；两张同布局 stage/转场纹理约 4.25MiB。6 张 960×544 NV12 有效分配数据约 4.48MiB，尚需分配粒度和 decoder 内部缓冲。1920×1080 RGBA 单图约 7.91MiB，可见大图缓存不能无限增长。

初始工程预算：显示/目标/命令区约 16–24MiB，视频输出与 codec 工作区先预留 24–32MiB，图片/文字缓存先限制 32–48MiB。它们是可调整预算，不是驱动保证；分别记账 CDRAM、普通内存、USSE 和解码器工作区，并以实际可分配空间与峰值为准。视频工作区应优先预留，再扩大图片缓存，防止图形分配挤掉硬解内存。采用 Borealis 后显示缓冲共用并只记一次；其默认 stride 960、4×MSAA 与 NanoVG 字体/几何资源需要实测重新核算，不能直接沿用上述 IDA stride 1024 估算，也不能无测量承诺额外 UI 开销可忽略。

缓存按字节设上限，统计活动/闲置/待 GPU 释放占用；切游戏、seek、异常退出后都检查回收。32 位地址空间下同时检查尺寸乘法、包偏移与长度转换，资源读取不使用整包进内存策略。

## 7. 实施阶段和验收闸门

以下为一名熟悉 Rust/C/Vita 的开发者估算，具备可持续真机测试条件。P0 后根据 shader 工具链、旧成果质量和样本复杂度重新估算。

| 阶段 | 预计工作日 | 主要交付 | 通过条件 |
|---|---:|---|---|
| P0 基线与复用审计 | 1–2 | 版本锁、旧工程差异清单、T01 自移植包目录/资源哈希及相同场景基线、shader 清单、shader 编译探针 | 知道复用什么；计时可信；自有 GXP 能生成/加载；T01 版本和场景可重现 |
| P1 runtime 解耦 | 3–5 | runtime/backend 分离、Vita staticlib、GL 适配回归、基础宿主 | 无 GL 构建保有 runtime；脚本/文本/存档回归通过；SELF 启停正常 |
| P2 GXM 与硬解探针 | 3–5 | 清屏/quad/三缓冲、纹理池、独立 H264→NV12→GXM 播放器 | 真机颜色/stride 正确；循环与退出不泄漏；日志证明实际硬解 |
| P3 基本可玩 | 5–8 | 游戏选择菜单及独立菜单字体；sprite/文字/蒙版/混合/基础转场、输入、文件/存档、BGM/SE/voice、全屏视频 | 从列表选择 T01，在新运行时启动→标题→正文→选择→存读档→返回列表全部走通；菜单字体配置可保存且不改变游戏字体 |
| P4 完整媒体与效果 | 4–6 | 共享 demux/A-V 时钟、图层视频/alpha、复杂效果组、样本必需 shader | 片头/片尾/跳过/seek/恢复正确；与基线画面对齐，无静默遗漏 |
| P5 性能与内存 | 3–5 | 场景/材质缓存、相邻批处理、目标复用、帧槽同步优化、预算策略 | 达到下表真机性能与内存目标；没有以禁用效果换取通过 |
| P6 回归与发布 | 5–8（原估算；五个原生样本审计后重估） | 先完成 T01 完整路线和连续运行，再依次复制并测试 T02–T06；可复现构建、VPK/符号/说明/依赖声明 | T01 首先通过完整验收；五个原生样本分别记录结果；发布包、资源版本和测试记录可对应 |

依赖顺序为 P0 → P1 → P2 → P3 → P4 → P5 → P6。P0 加入 Borealis GXM demo/资源基线，P1 接入 C++17 UI 宿主，P2 复用显示基础设施并验证外部纹理与菜单覆盖。P2 内的 GXM 静态图与媒体探针分别验证，最后组合；不必等待复杂游戏 shader 才开始暴露硬解风险。

**预计 3–5 周得到首个样本基本可玩版，6–10 周形成较完整且经过真机回归的版本。** 合计开发工作约 24–39 个工作日，日历估算另留调试余量；未知游戏 shader、E-Mote 深度兼容及新资源格式作为扩展，不包含“所有游戏通用兼容”的承诺。

新增的五个原生样本纳入 P6；上述日期保留为原始估算，完成各包资源/效果审计后，按实际兼容修复量更新 P6 和总工期，不预设五个样本都无需额外适配。

## 8. 验收矩阵与性能目标

| 类别 | 验证内容 | 目标 / 判定方式 |
|---|---|---|
| 游戏选择菜单 | 多游戏列表、手柄/触摸导航、确认启动、重扫、空目录/坏资源提示、退出后再次选择 | 默认进入列表；能选择不同项目；错误后仍可操作；按项目隔离配置与存档 |
| 独立菜单字体 | 内置字体、自定义 TTF/OTF、重启保持、文件缺失/损坏、中文/日文标题、进出游戏 | 宿主菜单字体正确切换或回退；正文/注音及游戏自带菜单字体保持原配置；反复切换无悬空引用或缓存持续增长 |
| 基础显示 | 色条、透明叠加、缩放/裁剪、双纹理蒙版、文字/ruby、目标切换 | 对固定截图比较；混合/字形边缘另设容差，不能只看是否有画面 |
| 逻辑 | 选择肢、自动/快进/章节跳过、语音等待、同 ID 音效替换 | 事件顺序与停止条件一致，跳过不永久等待或黑屏 |
| 存档 | 多项目隔离、保存/覆盖/读取、缩略图、重启读取、写入失败 | 状态正确，失败不破坏原存档 |
| 视频 | H264 + AAC、无音轨、不同帧率、B 帧、crop、EOS、seek、跳过、连播 | 目标 960×540/30fps 连播 10 分钟，无持续掉帧；稳定 A/V 偏差目标 ≤80ms |
| 游戏性能 | 同资源/同存档/同操作/同频率设置下 60 秒采样 | 普通剧情目标 60Hz，活动画面 p95 帧间隔 ≤20ms；复杂效果目标 p95 ≤33.3ms |
| 音频 | BGM+voice+多 SE、循环段、声像/渐变、44.1/48kHz | 无可闻断裂；记录输出错误、欠载、CPU 超预算及延迟 |
| 内存/生命周期 | 连续切场景/切游戏、20 次视频开始停止、睡眠恢复 | 无单调增长、悬空纹理、双重释放；工作区不足能报告失败 |
| 完整游戏（首要） | T01 用户自移植 Steam → PSV SHUFFLE 至少一条完整流程/路线及回到标题 | 新 GXM 版独立验收；标题或一段剧情通过不算全流程通过，保存日志与关键节点证据 |
| 原生游戏扩展（后续） | T01 完整流程通过后，依次测试 PCSG01297、PCSG01201、PCSG01107、PCSG01084、PCSG01127 | 执行者测试时自行复制；沿用显示/逻辑/存档/媒体矩阵，五个样本独立记录结果，不替代 T01 验收 |

计时统一使用 Vita 单调计数器，分别记录 IO、解码、场景遍历、draw-list 构建、GPU 提交、GPU/显示等待和总帧间隔。CPU 调用耗时不等于 GPU 执行耗时；没有 GPU 定时证据时不把 submit 时间标成 GPU 时间。统计逻辑 ticks、实际 repaint 和视频 displayed frames，静止页保持已有画面不能虚报高重绘率。

桌面侧运行相关 Rust 语义回归、IO/音频测试与截图对照；Vita3K 验证安装、控制流和基础画面；真机判定硬解、颜色、同步、显存与性能。固定 CPU/GPU 频率、设备/固件、资源哈希、构建哈希和日志开关。需要同步等待的诊断版本与性能版本分开记录。

## 9. 优先风险与处置

| 风险 | 尽早发现的办法 | 处置 |
|---|---|---|
| GL 耦合比 trait 表面更深 | P1 构建无 GL 完整 runtime 并跑桌面对照 | 把 capture/target/texture 生命周期一并抽象，避免局部绕过 |
| shader 编译链或动态效果不完整 | P0 编译自有 GXP；按样本列出必需效果 | 常用效果做离线变体；复杂脚本 shader 逐个移植，必要项未覆盖则样本不通过 |
| 硬解/渲染争抢显存 | P2 独立视频探针及分配日志 | 预留 codec 区，限制缓存，查返回值与失败清理 |
| NV12 花屏、卡顿或重复帧 | 色条/crop/连续播放，记录帧引用与等待 | 修正 pitch/plane/CSC/同步；不凭 decoder 名称宣告成功 |
| GXM 后仍然低帧 | 同一存档测 CPU 构建与 GPU/显示等待 | 处理实际瓶颈，保留已有场景/文本缓存改进 |
| PC 缩图破坏 UI | 保留逻辑坐标，验证边缘点击及弹窗 | 使用转换 manifest 和每游戏适配，禁止全局盲改 ini |
| 模拟器通过、真机失败 | P2 即开始真机验收 | 真机反馈进入阶段闸门，不把硬解验证拖到发布前 |

## 10. 开始实施后的第一批具体任务

### 本机 Vita3K MCP 与测试部署

用户提供的服务地址为 `http://127.0.0.1:32560/`；已通过实际 `tools/list` 和 `session_status` 请求确认 MCP 端点为 `http://127.0.0.1:32560/mcp`。根路径返回 404 不代表服务不可用。服务配置指向 `E:/EmuGame/vita3k_mcp/Vita3K.exe`；该模拟器 `config.yml` 的 `pref-path` 为 `E:/EmuGame/vita3k_data/`，普通 `E:/EmuGame/vita3k/config.yml` 也指向这一数据目录。用户书写的 `E:/EmuGame/vita3k/_data` 当前不存在，部署应读取有效 pref-path，而不是新建同名空目录。

部署分为以下内容：

- **游戏资源**：测试时由执行者自行复制到有效数据目录下 `ux0/data/art3m1s-gxm/games/<TitleID>/`，由新运行时按 `ux0:/data/art3m1s-gxm/games/` 扫描并列入选择菜单。目录/PFS、补丁、脚本、媒体及 ini 保持相对结构；改动资源时沿用独立 prepared 副本。已有原生安装目录 `ux0/app/<TitleID>/` 保留作对照。
- **新运行时应用**：生成独立 Title ID 的 VPK，通过 MCP `launch_app` 的 `contentPath` 安装并启动；接口也支持 ZIP/解包目录。已经安装的应用通过 `titleId` 启动。普通游戏资源目录不是可安装运行时；已安装应用中的 `eboot.bin` 也不会自动成为新 art3m1s 的启动入口。
- **存档/字体**：新测试运行时使用独立的 `ux0:/data/art3m1s-gxm/saves/<TitleID>/` 和 `ux0:/data/art3m1s-gxm/fonts/`，避免与已存在的旧工程 `ux0/data/art3m1s/` 混用；复制与部署均记录资源版本。

首轮连接只读取接口及会话状态，没有启动、停止或安装应用。后续按既定测试顺序执行复制、安装、启动、输入和日志/截图采集；MCP 操作记录须对应本次构建与样本，不将旧会话结果计为新包通过。

1. 固定本次 `source-lock.json` 对应的源码，建立独立 core/host；比较旧工程改动与当前上游，挑选构建、IO、音频和兼容修复，避免整目录覆盖。
2. 提取 runtime 中所有 GL 专有职责清单，确定 `RuntimeBackend` 和外部 `VideoFrame` 资源契约；先保持桌面 GL 行为。
3. 在 Borealis GXM 宿主中制作自有纹理/蒙版 GameSurfaceView 演示，以及 960×540 H264/NV12 探针，使用独立应用 ID 和数据目录；验证菜单覆盖、场景时序、输入路由、纹理销毁，并收集真机图像、耗时和显存日志。
4. 上述闸门通过后，优先接入 T01 用户自移植包的实际 PFS、文本和媒体，直接使用其已转换资源；按 P3 验收路径推进到第一条完整可玩流程，之后再测其他原生 Artemis 游戏。

本轮调研产物还包括 `research/ida-gxm/`、`scripts/ida_gxm_plan_probe.py` 和包含 Borealis 的五个 `refs/` 源码快照。IDA 数据库/反编译参考仅作为本地研究材料；后续版本管理默认排除它们，发布包保留自有实现和依赖所需声明。
