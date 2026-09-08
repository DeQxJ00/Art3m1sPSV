# 实施状态与继续工作依据

更新：2026-09-08。此记录区分原型验证与最终验收；完整移植目标保持未完成。

## 2026-09-08 撤回 async-present：实机启动黑屏

本轮结论更正：下段最初依据状态声称“可显示菜单”不成立。本次 retest/menu.png 经 view_image 检查实际为白屏；running/60FPS 不能作为显示通过。已向用户明确纠正，该轮不计为显示成功。前一次启动证据仍保留为历史，不覆盖本轮失败。

用户确认 async-present 在实体机启动即黑屏。重新用 Vita3K 安装同一个 149933… 包，可显示菜单；安装 eboot 和 VPK 内 eboot SHA256 均为 5449976A2720B8FDA21D3F1EB6126121D9807FF19E762EB27F32D85B09AC80D6。复测会话 0e705856-12b8-4744-b7eb-d7e03fe25848，截图 build/async-present/retest/menu.png。已请求关闭会话。

模拟器未覆盖这次实机失败，不能以其显示成功继续交付此方案。候选已撤回并加 CMake 禁用门，具体原因尚未通过实机日志确认；优先怀疑显示回调里的通知等待。恢复用户已确认不缺块的 safe-present（69D1B1…）作为实机基线；其性能问题仍未解决。不能把异步候选写成通过。

## 2026-09-08 实机完成保护有效，尝试按帧通知恢复并行

用户确认 safe-present 消除缺块但降低帧数，要求试优化。因此保留该包作为实机防闪基线，新增 ART3M1S_ASYNC_PRESENT 候选：每个默认显示缓冲槽在 EndScene 提交 fragment notification，由显示回调等待自己的通知后设置 framebuffer，避免主线程逐帧全局 Finish。保留原显示队列同步，初始化排空旧队列，通知缺失退回全局完成保护。未改顶点分配器；不能声称已确定具体资源复用根因。

候选 build/async-present/art3m1s_gxm-async-present.vpk，SHA256 149933415CC60D1BB6E7D91DCCA369CC44D2DCA95C1FB08BB2F4E8EF7005C46F。构建和生产函数 ASan/UBSan 顺序/回退测试通过；MCP 会话 fd574ce7-3442-40d7-b251-2acd21e0d517 确认 async 已启用、菜单/标题/存档 2 画面正常，已请求关闭。存档 2 哈希仍为 CBFCC2CB3379422AEB36A62BC3B0A71FE4E8E0BDAD16FC079211531A38834BE4。实机闪烁和帧数待对照，不以模拟器 60 FPS 证明性能。证据及操作说明在 build/async-present；普通构建脚本强制关闭候选选项。

## 2026-09-08 实机缺块闪烁：完成保护候选，尚未验收

用户提供 10:08 录像中约 5.8/9.77 秒缺块截图，并确认 PSV 屏幕也闪。已复制当前/上次实机日志至 build/hardware-logs/20260908-102055，保留校验值；当前日志与先前分析副本 SHA256 相同。不能仅归于 USB 采集，尚未证明具体根因。

新增可选 gxmSetPresentCompletionWait，在 endScene/common-dialog 后、显示队列提交前 sceGxmFinish。候选构建 ART3M1S_SAFE_PRESENT=ON；普通构建脚本强制 OFF。包 build/safe-present/art3m1s_gxm-safe-present.vpk，SHA256 69D1B1F8203B207ADE6AE952700400C203344D9245D2DCF8ABE1EDA928C7AB5E。真实生产函数顺序/错误路径 ASan/UBSan 检查通过，构建通过，MCP 会话 9fc8d51c-dec3-47d0-a09f-b9fe74624b8a 验证菜单正常显示与保护启用日志，随后测试游戏启动并请求 shutdown。实机闪烁消除和性能代价待复验；没有宣称修复完成，也没有混入音频/字体/场景复制修改。详细限制见 build/safe-present/README.zh-CN.md。

另：之前 nextline 诊断会话 d21f500d-7af0-427f-b1a8-29080e9162f5 在主动 shutdown 后，后续 session_status 返回终态 crashed、exitCode=3221226356；不能把它写成正常退出。本次启动前确认无 Vita3K 进程。

## 2026-09-08 追加：双人 A/B 完成，优先实机换句瞬时低帧

后续增加 scripts/analyze-nextline-log.py，将实机/模拟器来源显式区分，并保留不完整报告、解析错误、重叠窗口与报告自身开销；5 项解析测试通过，已解析当前模拟器日志的 30 份完整报告。没有修改已交付 VPK，也未根据模拟器计时更改运行逻辑。实机录像和日志尚待用户提供。

用户确认 MCP 空闲后，在会话 4f0afd2a-5bf1-46b7-8f7c-64c7fca64c44 完成相同图集、相同双人长台词的旧/新路径对照：最大 RGB 差 1，超过 3 的像素为 0；证据 build/image-quad-tests/ab-two-characters-resumed。存档 2 未改。此前跨运行字边差异未在本次严格对照中重现。

用户明确暂缓模拟器存档摘要/缩略图不一致，实机读档位置正确；不修复或覆盖现有存档。当前重点是实体机每句切换都会瞬时低帧，不能用模拟器速率归因。

新增可选 trace-nextline.flag：宿主每 5 秒汇总逻辑/图集准备峰值、已有图集区域复制与 GXM 等待，输出已有异步 core profiler 的分项统计，并记录报告自身开销。未改变字体尺寸、音频精度或游戏调度；未证明换句卡顿已解决。字体生成/排版尚未独立细分。

包 build/nextline-trace/art3m1s_gxm-nextline-trace.vpk，SHA256 0A014CCF5660AC1D001F38899EE0CA7976D7E2F05C7EF50AAE80760F654FF25F，AB 编译选项 OFF。构建成功；会话 d21f500d-7af0-427f-b1a8-29080e9162f5 已验证启动、存档 2、四次换句输入和日志；30 份可解析报告，未丢样，区域更新/等待/文字事件均有非零采样。会话已主动关闭，模拟器临时开关已删除；用户实体机的分项日志与录像待提供。详细操作及限制见 build/nextline-trace/README.zh-CN.md。旧 D1D5E9…包未额外复测，当前复测的是含可选采样的 0A014C…包。

## 2026-09-08 最新：图片矩形合批候选版

普通 RGBA 矩形增加直接 UV 顶点路径，GXM 合并相邻且完整材质一致的三角形，保持原覆盖顺序、四方向描边和阴影。新片段 shader 与旧图片填充的预乘透明度和 scissor 公式一致；规则转场/不适用格式/分配失败保留旧路径。没有改 core、1024² 图集尺寸或音频精度，也没有新增 GPU 等待。

存档 2 同一双人台词：普通包初版实测 252→129 次绘制提交/flush（约少 49%），顶点量有所增加；模拟器 CPU flush 均值 289→170us，不等同于实机 FPS 提升。CPU 几何/队列/分配失败测试、原混合/菜单字体/区域更新检查通过；12 组 GPU 图片探针与高 UV 的 1024² 图集细线探针通过，最大 RGB 差 1。同次运行、同图集的首段文字 A/B 亦通过。

最终 OFF 构建 build/art3m1s_gxm-image-quad-final.vpk，SHA256 D1D5E9C0E7D0F65E107BD421E679F438E2293660F9E5F22684C3BD71D0796B04；构建与诊断关闭检查通过，尚未单独启动复测。双人长台词同次运行 A/B 在共享模拟器切换至 STS2VITA0 后中断，不能记为通过或 art3m1s 崩溃。不同运行的目标文字截图仍有少量边缘差异需严格对照，实机性能待测。完整证据与下一步见 build/image-quad-tests/REVIEW.zh-CN.md。存档 2 未覆盖。

## 最新进展：NanoVG GXM 应用每次绘制的混合因子

确认原后端虽然记录 call.blendFunc，但 flush 的应用调用被注释，所有彩色程序固定 source-over。现使用 GXM fragment program 变体应用四个混合因子，按基础 shader 与因子缓存复用，深度/stencil 的禁用颜色程序保持原路径；等待 GPU/显示队列完成后释放变体，再注销基础程序。此项使宿主已经请求的 NVG_LIGHTER 真正生效。core 的 Multiply/Screen/NativeReverseSubtract 等模式仍未完整映射，不能视为所有 Artemis 混合模式完成。

宿主/VPK 构建成功。直接提取生产缓存选择函数、以模拟 patcher 执行 ASan/UBSan 检查通过：500 次命中不重复创建、不同 shader/alpha 因子隔离、深度绕过、分配/patch 失败后重试。GPU 探针扩展为六个规则区域加六个 Porter-Duff 区域、36 个采样点，已构建、未运行。首次出现新 shader/混合组合仍需创建变体，实际耗时和画面待实机测量。

## 最新进展：GXM 舞台坐标裁剪

GXM 原先忽略 DrawCommand.clip_bounds，现匹配 GL 的舞台矩形相交语义，跳过空区域/非有限值，并经 FFI 传至 NanoVG scissor。宿主先在舞台缩放下设置 scissor，再施加图层自身变换；nvgRestore 隔离各次绘制。没有按游戏或界面硬编码。329 项 core 测试通过、5 忽略，覆盖原坐标保留、部分越界、完全越界、负尺寸及非有限值。Vita core 编译通过；画面验收仍待设备。此项不等同于 stencil/离屏组蒙版完整支持，也不能直接归因此前 Start 黑底问题。

## 当前进行中：规则转场 GXM shader

新增独立 GPU 像素探针 ART3GRP01（`tests/rule_shader/render.cpp`）：生产 shader 同帧绘制六组不同参数，结束场景后读显示缓冲，比对 18 个采样点并输出 result.log / output.ppm。已构建，尚未运行，不能作为 GPU 验证通过证据；不替换游戏运行器。编译器对照探针 ART3GRC01 继续保留。

规则转场现已接入测试运行器：Rust 分派 rule-trans，将 mask/progress/vague 送入 bridge；NanoVG GXM 使用专用双纹理片元程序，每个延迟 call 独立保存 mask 与 uniform 参数。普通图片不受该分派影响；规则 shader/纹理不可用时有日志并退回交叉淡化。328 项 core 测试通过、5 忽略，Vita core/宿主/VPK 构建通过，链接 ELF 中确认包含完整离线 GXP。新包 `build/art3m1s_gxm-rule-transition.vpk`；尚未实机确认中心/扇形展开、alpha、方向和帧率。本段覆盖下方历史“尚未接入”的记录。

离线编译已打通：用户提供的 SDK-3_570_021-patch.zip 包含可运行的 psp2cgc，已放到 `.tools/sony-shader-3.570`；vitaShaRK 探针按用户要求保留。`scripts/build-rule-shader.ps1` 生成 724 字节 GXP，psp2cgnm 验证 frag[11]、oldFrame 槽 0、ruleMap 槽 1。编译参数、工具/源码/GXP 哈希保存在 build/rule-shader-offline/manifest.json。固定相对路径参数后重建与首次输出哈希一致。尚未接入游戏绘制或取得 GPU 画面证据。下述探针“未运行”状态仍有效，但“未获得成功 GXP”仅指其 vitaShaRK 路径。

新增独立 ART3GRC01 编译探针 `tests/rule_shader` 和可复现脚本 `scripts/build-rule-compiler.sh`，使用 VitaSDK 中 vitaShaRK 在设备上编译 Cg，检查 frag 与双采样器绑定后输出 GXP。已构建约 70 KiB 的独立 VPK，并检查其打包 shader 与源文件一致；未运行探针、未获得成功 GXP。当前游戏运行器 VPK 未更新。详见该测试目录 README。

已新增 `host-gxm/shaders/rule_transition_f.cg` 和同目录接入说明，按 core 的规则阈值公式、NanoVG fill 顶点接口和预乘 alpha 输出准备双纹理片元 shader。尚未编译 GXP、接入 shader 分派或验证 GPU 输出；当前 VPK 的规则转场仍未实现。现有 Borealis 生产路径使用内嵌 GXP，不能把新增 Cg 源码等同于生效。接入说明列明逐 call 参数保存、纹理绑定/保活及实机图像验收要求。

## 最新进展：GXM render-only 上传不再保留重复 CPU 像素

GXM provider 现在遵守 `upload_rgba_render_only` 的接口语义，不扫描不透明度、不复制/保留调用方已经持有的 RGBA。文字 renderer 的 atlas 原始像素仍保留，因此每个 1024×1024 atlas 可减少 provider 中 4 MiB 重复 CPU 存储；GPU 分配和全页上传尚未减少。普通图片、视频、截图的可读上传路径维持原行为。render-only 纹理保守报告非全不透明，保持 alpha 混合；pixel_alpha 返回 None，与可选 CPU 查询语义一致。内存统计分别计算 CPU 字节与对齐后的 GPU 估算字节。

327 项常规测试通过、5 忽略。新增测试覆盖可读→render-only→可读切换、释放容量、纹理身份及 revision、保活、alpha 查询与 GPU 对齐估算。没有在活动 GXM 场景内新增等待或原地改写纹理；尚未证明实机 FPS 提升。

## 最新进展：缓存字形度量

`rasterize_glyph` 原先在像素缓存查询之前每次执行 outline_glyph、px_bounds 和度量计算。现将字形度量与图集坐标一起缓存，保持字体 generation、glyph ID、精确字号位模式组成的键；共享 glyph ID 的字符仍保留各自文字身份。缓存内不保存字符字符串内容。仅命中路径跳过轮廓提取，未改绘制外观、描边、字号或音频精度。每项缓存增加少量度量存储，图集仍为 1024×1024 RGBA，首次加入新字形仍整页上传，尚未实现局部更新。

验证：326 项常规测试通过、5 忽略；新增外部字体检查通过（通过 ART3M1S_TEST_FONT 指定 host/assets/menu.ttf，显式运行 cached_glyph_metrics_match_font_outlines_and_preserve_atlas），覆盖多个字号、拉丁/日文/共享缺字字形、缓存命中后的原字体度量一致性、图集像素与分配不变、字体 generation 隔离。Vita release 核心编译通过。此项作用于重复排字，不能据此声称解决持续低帧率；实机效果待测。

## 最新进展：实机日志与纹理耗时分项

用户 01:55:18 的 host.log 已证明开场 H.264 使用 h264_vita / NV12-direct；普通场景存在约 32–43 FPS 窗口、852 ms 绘制长帧及另一处约 5.14 秒逻辑长帧。音频不是当前首要嫌疑；没有 OGV 箭头播放证据。分析见 `PHYSICAL_LOG_REVIEW_20260908.zh-CN.md`。

新增 GXM texture-perf 五秒汇总：读取次数、缺失次数、成功解码、解码错误、上传次数/失败/字节数，以及读取/解码/上传墙钟耗时。超过 50 ms 的读取、解码和成功上传另记资源名；上传覆盖文字图集等直接上传，并拆出宿主调用耗时与包含 CPU 副本维护的总耗时。汇总在 retain 时输出，长帧可使间隔超过五秒；计时包含等待，不等同于 CPU 利用率。没有改变失败重试及游戏资源行为，没有按 dummy 路径硬编码处理。326 项 core 测试通过、4 忽略；Vita release 核心编译通过。新统计仍待实机采样，不能宣称帧率提升。

## 最新进展：标题淡出黑色按钮底框

按用户截图定位 NanoVG/GXM 的 alpha 混合错误：ONE_MINUS_DST_ALPHA 会在透明按钮覆盖不透明背景时破坏帧缓冲 alpha。已改为 ONE_MINUS_SRC_ALPHA，并将完成显示面的读回 alpha 固定 255，防止转场截图带入透明孔洞。详见 `TITLE_FADE_ALPHA_FIX.zh-CN.md`，运行验收待实机复测；不将此项与未实现的规则转场 shader 混为一谈。

## 最新进展：放射线 PNG 动画重复加载

新 12 秒录像显示对话期间白色放射线效果约 5 FPS；形状与原包 line2.ipt 的三张 PNG 循环相符。已修复 GXM retain 每换图立即删除上一帧的缓存抖动：来源图片使用 16 MiB 非活动 CPU＋GPU 估算预算的近期缓存，保护活动集合，动态上传资源沿用原生命周期。生产 provider 回归中 300 次轮换由 300 次读取变为 3 次读取／上传；325 项 core 测试通过，4 忽略。详见 `PNG_ANIMATION_PERFORMANCE.zh-CN.md`，实机 FPS 尚未验证。本节覆盖旧记录中“尚无近期 GPU 纹理缓存”的描述，但不代表多级场景渲染缓存或首次加载优化已全部完成。

## 最新进展：菜单字体按生命周期释放

游戏加载成功后卸载菜单字体数据、字形记录和扩大图集，仅保留默认空 atlas。返回菜单前先关闭游戏/媒体、等待 GPU，再恢复菜单字体；加载及错误页面保留字体。共享 GXM 和 core 游戏字体不受影响，轻量菜单 View/列表保留。20 次卸载/恢复、字形图集扩容回收、字体 ID/文字宽度、游戏纹理保留及分配失败回滚的 ASan/UBSan 测试通过。详见 `tests/menu_fonts/README.zh-CN.md`。这不是实机内存或 UI 验收；此前性能版本哈希为历史记录，新包仍输出到相同 VPK 路径。

## 最新进展：录像反馈的第一轮性能修改

本轮 VPK SHA256：`71A9831C9DC47BAC0E862926EDCDBE4996B3F50E6A7DED76AB46289BFA60ABA3`，文件 `build/gxm-host/art3m1s_gxm.vpk`（15574537 bytes）。以下旧章节的包哈希与“尚未实施”描述保留为历史记录，以本节和 `PERFORMANCE_CHANGES.zh-CN.md` 为准。

已接通 bindSurface 预取字节到纹理加载，减少重复资源读取；静默、非循环 Theora 图层改为后台彩色/遮罩解码、三个 CPU 帧槽有界排队、主线程按 PTS 上传，丢弃过时呈现并跳过过时帧的转换。相同尺寸的视频纹理复用，更新前按需等待 GPU。音频质量保持不变，新增主线程、音频、Theora 协调线程及音频重采样耗时日志。包含前轮加载进度条和按需存档截图修改。

桌面 core 测试 323 通过、4 忽略；音频、视频队列、真实 Theora＋遮罩 ASan/UBSan 回归通过。960×540、30 FPS、0.5 秒合成片段呈现 15 帧；人为停顿 300 ms 时呈现 9 帧、队列丢弃 2 帧、跳过转换 4 帧，约 501 ms 结束。这是桌面生命周期与时序验证，不是实机性能数据。Rust Vita 与宿主构建成功，宿主有 WSL 时钟偏差警告。

32560 端口再次检查仍拒绝连接，未安装或运行最终包。T01 实机帧率、加载画面、缩略图回归与独立空白全局状态的章节跳过仍待验收。bindSurfaceAsync 仍同步；GPU 场景 LRU、GPU 转场快照尚未实现，不将第一轮修改称为场景卡顿已解决。

## 最新进展：实机录像与加载页面

用户提供 232 秒实机录像，明确为存档图片修复前的版本；录像低帧不能归因于后来新增的截图等待。具体时间点、原生 IDA 缓冲复用证据、core 同步预加载且缓存未接入纹理消费的缺口、章节跳过的全局已读条件、双路 Theora YUV444P 箭头瓶颈，见 `PHYSICAL_RECORDING_REVIEW.zh-CN.md`。

本轮实现了加载阶段进度条和后台 PFS 读取：GameSurface 构造不再阻塞打开档案，工作线程更新已完成/总资源包数，主线程随后初始化 core；退出时 join 工作线程。GXM 留在主线程。脚本初始化仍是同步阶段；缓存重构、OGV 线程化和章节逻辑修改尚未实施。新的 VPK 已构建，SHA256 `ED58682A637800E38D651D3B2A069F70EF70C98EF66DDA1A04D9A9918C8D4F85`。MCP 32560 仍拒绝连接，新加载流程未运行验收、未实机验证。

## 最新进展：GXM 存档缩略图（实体机优先）

修复 `core/src/runtime/save_io.rs` 的 GXM `takess` 分支：原实现创建全零 RGBA，生成透明 PNG。现在宿主在 Borealis scene 结束后等待 GXM 完成，记录上一帧 front buffer；core 在 takess 时按 stride 读取该帧，再交给既有 savess 缩放、PNG 写入及纹理加载流程。存档截图固定 alpha=255，匹配显示器实际显示的 RGB。切换游戏时清除读回引用，不使用 MCP 截图，也不改游戏脚本或运行目录资源。

Vita3K 全局配置原有 `disable-surface-sync: true`，导致修复后的真实 CPU 读回仍是全零。已仅为 ART3GXM01 创建应用专属配置 `E:/EmuGame/vita3k_mcp/config/config_ART3GXM01.xml`，设为 false，保留全局配置。会话 `9e6e3347-eb0f-4771-8fa3-93675ba1bad3` 的 T01 第 2 格已出现进入菜单前的夜空缩略图，PNG 120×67、11132 bytes、2132 种颜色；第 1 格原存档保留。该轮 PNG alpha 检查发现部分透明像素，已补固定 alpha 后重新构建，最终版复测结果待补。

用户确认可见缩略图，但反馈帧数明显下降。旧修复每个完成的游戏帧执行一次场景外 sceGxmFinish，且模拟器表面同步打开后会增加读回开销；会话状态曾报 21 FPS，此单点值不是严格对照基准。现改为主循环在 Borealis mainLoop 前推进 core 逻辑，draw 仅提交渲染，takess 因此能在场景外按需等待 GXM；正常帧只记录 front buffer 地址，不调用 sceGxmFinish。转场捕获仍按需等待。输入在 draw 收集、下一轮逻辑消费；退出时清除 active_game 指针。MCP 随后连接被拒绝，最终版本运行、透明度、重启和帧率复测仍待完成，不能宣称性能已恢复。实体机验收仍未完成；操作及 PNG 检查见 `tests/save_thumbnail/README.zh-CN.md`。旧透明 PNG 无法自动恢复，需要重新保存槽位。

## 最新进展：原生 NV12 验证与模拟器硬解接口

按需截图版本已完成 Rust release 与 GXM 宿主/VPK 构建，日志为 `build/core-thumbnail-ondemand-build.log`、`build/gxm-thumbnail-ondemand-build.log`。最新 `build/gxm-host/art3m1s_gxm.vpk` SHA256：`A9AC7E8B49DD2A72039C41FF4CABB9C7060FFCBB5E431CA6EC5F3DB6844D2EC8`。对象反汇编 `build/thumbnail-finish-host-frame.asm` 确認普通帧记录地址后返回，只有捕获请求分支进入读回；这不是运行性能验证。MCP 端口 32560 仍未连通，未安装此最终包。

本机 MCP 配置的源码目录为 `E:/WorkSpaceAI/psv_vita3k/Vita3K-mcp`。只读核查 `vita3k/modules/SceVideodec/SceVideodecUser.cpp:595` 的 sceVideodecQueryMemSizeInternal 和同文件 424 行的 sceAvcdecQueryDecoderMemSizeInternal 均返回 UNIMPLEMENTED；SceAvcodec 的内部同名接口也未实现。这与实际查询后 codec_buf_size=0 的失败现象一致。未修改模拟器，也未给 FFmpeg 填造假的内存大小。此证据说明当前模拟器路径无法证明实体机硬解，不能证明实体机也会失败。

新增 `tests/video_gxm`，使用生产 GXM/NV12 导入和解码缓冲池代码。独立应用 `ART3GN012` 的 MCP 会话 `bc5c47a8-e4fa-4413-8c99-3c7d0ac501c2` 已完成 600 帧、五次池生命周期，每次只使用两个 786432 字节 CDRAM 槽；未观察到 pool exhausted/live frame/unmap failed。截图 `build/test-artifacts/gxm-nv12-bars.png` 通过八色条顶部/中部/底两行检查，没有填充颜色渗漏。Cross 后日志有 cleanup complete，会话 stopped、crash=null。

测试 VPK SHA256：`7C90227FE04AE587DD0146C1B0627613E3DA72D0387C004E4FC689EAAC0BE6A5`。该包不读取游戏数据、不经过 H.264 解码，不能替代硬解、AV 同步和实体机验收。主移植 VPK 保持前一节所列媒体版；未因测试而更改游戏运行资源。

## 最新进展：媒体宿主接入

已在本工作区构建 FFmpeg Vita、Tremor 和整合后的 GXM VPK。媒体版 SHA256：`1BB5C8744FEACEA0F53C34683092AF3387EC17EEAAAA111C950973C720BBD04C`。构建日志：`build/ffmpeg-build.log`、`build/tremor-build.log`、`build/gxm-media-build.log`。新增 `scripts/build-gxm-all.ps1` 串联 Rust 核心、FFmpeg、Tremor、宿主构建；本轮各组件命令实际通过，组合脚本尚未整条重跑。

core 的媒体命令进入宿主队列，Borealis 帧间执行音频轮询、视频解码及 GPU 资源替换；GameSurface 绘制时仅提交全屏视频图像。退出游戏后先在帧间关闭视频、停止音频 worker，再关闭文件/PFS，避免工作线程引用已关闭文件。前触摸和确认键可请求跳过允许跳过的视频。软件 RGBA 输出和原生 NV12 导入均共享 Borealis 的 GXM context。修正 NanoVG/GXM 外部图像删除逻辑：回收描述符注册槽，保留外部分配的像素内存所有权。

`tests/audio/run.sh` 通过：44.1/48 kHz、单双声道、循环/EOF、淡入淡出/声道平衡、重采样、完成通知代次与缓冲区所有权检查，ASan/UBSan 未报错；结果在 `build/audio-regression.log`。旧 NV12 probe 使用 vitaGL，不作为本次 GXM 导入路径已验收的证据。

MCP 会话 `ce9352a3-3c27-442e-866d-85f93c607f3a` 的日志证明 T01 BGM 经 Tremor 解码并向 Vita 音频输出提交，采样区间 output_errors=0、clipped_samples=0；未采集音频波形或进行听感验收。`movie/logo.mp4` 的 NV12 和 RGBA 硬解均在 query codec buffer size 阶段失败（查询后大小为零，首帧返回 -1313558101），随后软件 H.264 解码成功上传首帧，并记录 20 帧提交。未捕获到本轮视频播放截图，不能称为视频视觉/同步验收通过；硬解明确未通过。

游戏正文仍可运行，截图 `build/test-artifacts/t01-media-integrated.png`；Select 返回资料库已截图验证 `build/test-artifacts/t01-media-return.png`。继续核查 Vita 内部解码器内存查询 API、原生 GXM NV12 独立测试及实际视频同步。完整 T01、T02–T06 和实体机验收保持未完成。

## 最新进展：转场捕获与标题按钮

会话 `09ccb802-28be-4771-b644-d6d4b5796078` 验证了转场捕获补全后 T01 的完整标题按钮显示、Start 进入开场、Cross 推进到下一张背景、Select 返回资料库。截图分别为 `build/test-artifacts/t01-capture-enabled.png`、`t01-start-after-capture.png`、`t01-dialogue-cross.png`、`t01-return-library.png`。对应 VPK SHA256：`9A53A1B805EEDEEF5E52F2FE8DBA2E9BFA7A32E736058A08BC58B1F1E6467C4A`。测试使用现有 PFS，未解压或覆盖运行目录。

根因证据：`render_pipeline/transition.rs` 的 `is_in_progress` 在 needs_capture 为真时持续返回真；只有 capture_texture/capture_gpu_texture 才清除此标记并设置开始时间。此前 GXM render_current_frame 没有捕获逻辑，原 GL 入口有。新实现请求捕获时重绘旧提交，在 Borealis mainLoop 完成、GXM 场景结束后调用 sceGxmFinish，按显示缓冲 stride 读取 RGBA，再由 core 注册转场纹理。跳过转场及退出游戏时取消未消费的捕获。

当前是 CPU 读回的功能基线，仍须改进为舞台离屏纹理路径：显示分辨率到舞台的重采样会有损失；refresh_transition_source_frame 尚未按 GL 行为重建已隐藏文本；规则溶解 shader 尚未实现。此版本不代表转场全部效果通过。

正文截图存在白色描边糊成一片的问题；已发现 host-gxm/gxm_bridge.cpp 丢弃 core 的 color.multiply，新增 NanoVG image paint 颜色乘数。着色修复已构建，复测会话为 `620f51d7-5333-4b36-8ee2-97898a061561`；其视觉结果仍待后续记录。音视频仍未接通。

着色复测结果：同会话 `build/test-artifacts/t01-tint-dialogue.png` 已显示可读中文正文“我掏出手机，看了看屏幕。”及深色描边，Cross 后背景切换正常，状态 running、crash=null。相比 `t01-dialogue-cross.png` 中白色字形与白色描边糊在一起，颜色传递修复有效；字号、间距、抗锯齿与原版的一致性尚未验收。最新 VPK SHA256：`BF84D9D04AA4C6E967AD7F8E3A8F2935BA46B3223FEEF1FAC034E8B579AE14A5`。本轮未修改字体文件、脚本或游戏运行目录资源。

## 当前已验证

- Rust GXM 核心静态库与 Borealis GXM 宿主已经构建，输出 `build/gxm-host/art3m1s_gxm.vpk`，应用 ID 为 `ART3GXM01`。
- 2026-09-07 本轮执行 `wsl -d Ubuntu-24.04 -- bash -lc 'cd /mnt/f/WorkSpaceAI2/art3m1s-psv-gxm && bash tests/math_compat/run.sh && bash scripts/build-gxm-host.sh'`，测试及构建均成功。构建有 WSL/Windows 时间差与 GNU-stack 链接警告。
- 修正宿主强符号 `fmod`：使用有界二进制整数除法，避免旧实现的大商循环和浮点减法舍入错误。676 组边界组合及 200000 组确定性随机 binary64 输入与系统 libm 一致，包含带符号零、次正规数、无穷及 NaN；UBSan 无报错。此测试验证返回值，不验证 errno/浮点异常状态。
- Vita 目标对象反汇编为标量整数余数运算；检查目标为 `build/gxm-host/CMakeFiles/art3m1s_gxm.dir/src/math_compat.c.obj`。
- 本轮 Vita3K MCP 会话 `334adf04-55dd-4aa1-9358-dcb089038e7b` 已安装新版 VPK，960×544 菜单正常显示中文，默认选中 T01。截图 `build/test-artifacts/gxm-math-menu.png`。会话状态采样为 61 fps，仅证明菜单运行，不代表游戏性能。

## T01 尚未通过

本轮会话 `334adf04-55dd-4aa1-9358-dcb089038e7b` 触摸选中 T01 后，截图 `build/test-artifacts/gxm-math-t01.png` 已显示 SHUFFLE Episode 2 标题背景与标志；再次查询会话确认应用 ID 为 `ART3GXM01`，运行中、无崩溃。标题按钮未显示。当前 VPK SHA256 为 `1A526092B3ECE170700471EAE4D93067F63C54B2D9824AF096C44DEFF89C86F2`。此次只改了 fmod，但尚未做同等待时间的旧包对照，不能把进入标题背景归因于该修复。

先前会话 `466a9278-d03e-47cb-8c3a-a4594b0af79b` 的游戏画面黑屏，截图 `build/test-artifacts/t01-overlay-first-scene.png`。此前加载过运行时、两张 2×2 纹理并提交绘制，不能据此判断视频已正常开始或资源已完整解析。

测试资源在 `E:/EmuGame/vita3k_data/ux0/data/art3m1s-gxm/games/SHUF00002`。该目录是实验副本：复制基础资源后，又覆盖复制了原安装包 `unpack/root` 和 `unpack/root.pfs.002.unpacked` 中的散文件。原安装包未修改，但测试目录中的 system.ini 和脚本可能被覆盖，尚未建立逐文件资源清单，不能称为与实体机参照包完全一致。补充散文件后仍黑屏，资源缺失不是已证实的根因。

## 接下来必须完成

用户明确要求：禁止在游戏运行目录解压资源或放入解包散文件，这会干扰运行。若必须提取诊断文件，仅放工作区独立目录；修复目标是 art3m1s-core 的 GXM 后端及宿主适配，不通过改游戏资源绕过后端问题。

本轮重新检查，运行目录只有 movie、root.pfs、root.pfs.000、root.pfs.001、root.pfs.010、root.pfs.011、system.ini 和 title.txt，先前散文件覆盖已不在。只读索引确认 root.pfs 含六个 `pc/ui/ja/title/bt_*.png` 按钮。新加 GXM 纹理失败诊断区分最终资源源缺失、解码失败和上传失败，每个名字只报告一次；不把宿主散文件 fallback 的 MISS 当成最终资源缺失。

诊断版本已执行 `scripts/build-core.ps1 -Gxm` 和 `scripts/build-gxm-host.sh` 构建成功。MCP 会话 `2e92b609-acdd-4ef9-b3ec-93590c0fabbb` 已启动并选择 T01；随后会话终止，状态为 stopped、crash=null，日志含 `MainWindow::on_game_closed`，截图请求没有返回图像。不能将本次会话当成按钮修复或崩溃证据；下一次测试前先确认会话状态。诊断代码位于 `core/src/backend/gxm/provider.rs`，失败仍允许后续重试，动态视频纹理在首帧上传前不误报资源缺失。

1. 核对 T01 包文件和配置，建立可重复的资源准备清单，遵守上述禁止运行目录散文件覆盖的约束。
2. 诊断标题按钮缺失、绘制命令、过渡、纹理颜色和脚本等待；GXM 当前仅有基本纹理四边形，尚未实现效果组、遮罩、网格及过渡目标。早期黑屏截图不能证明永久卡死，也不能直接归因于缺少视频。本轮日志出现 `pc/ui/ja/title/bt_extra.png`、`bt_exit.png` 的散文件查找失败；仍须结合 PFS 最终解析结果判断资源缺失。
3. 接通音频与 FFmpeg Vita 视频。当前 GXM GameSurface 未注册媒体回调；旧宿主媒体代码仍包含 GL 路径，须改为使用 Borealis 唯一 GXM context。NV12 外部纹理的 AVFrame 持有与 GPU 完成同步需要明确生命周期。
4. 完善游戏字体、菜单字体配置、输入、存读档、退出返回列表与资源释放。
5. 优先完成 T01 全流程验收，再依次测试 T02–T06。六个样本的实体机性能和完整流程目前均未由新运行时证明。

构建通过、菜单截图和微测试均不替代上述验收。

### 2026-09-08 MCP 性能测试配置纠正
用户确认同版本真机和其他机器正常。发现 MCP 的 config_ART3GXM01.xml 曾为存档缩略图 CPU 读回验证设置 disable-surface-sync="false"，普通实例无此应用覆盖、全局为 true。已停止 MCP 会话、备份配置到 build/rule-gpu-evidence/config_ART3GXM01.surface-sync-enabled.xml，恢复该项 true 后重启；未修改游戏包。新会话 20810077-1257-481a-98c7-aac0cab68264，菜单测得 61 FPS。此前游戏低至 8 FPS 的数据不可作为实机性能结论，仍需相同游戏场景对照。关闭表面同步后，模拟器 CPU 读回缩略图可能再次失效，与实体机结论分开。

## 2026-09-08 文字时机逆向补充与纠正

已核对 build/native-text-timing-side/FINDINGS.zh-CN.md 和当前 glyph.rs。原版普通 print 同步拆块、构造字符并查询/生成字形，wait/@ 为已有文字安排显示效果；布局溢出可能保留剩余文本。当前 core::push_text 同样先对 content.chars() 调用 rasterize_glyph 收集结果，再写入 text_buffer，逐字显示由 reveal_index/reveal_clock 控制。不能将二者差别描述为“原版提前生成整句，移植版显示一个才造一个”。没有证据确认原版等待时后台读取下一条 print；onClickWaitIn/Out 的实际脚本回调尚未穷尽，不能全程序排除。

已确认优化对象仍是 GXM dirty 图集整页4MiB上传与图片重建，对照原版512²持久surface的局部写入。另需单独测量当前正文+四向描边+阴影最多每字六份core绘制命令的开销；这不等于六次已测GPU draw call。原版部分样式在字形位图生成阶段处理。以上静态证据不证明录像尖峰的唯一原因，下一句后台预热只能作为后续设计，不能作为原版既有机制。

用户已确认 shader 实机验证OK，具体探针覆盖范围不扩大推断。本轮侧任务未改源码、游戏、模拟器配置或会话；主任务仅整合记录。多人立绘路径尚未完成与原版的对应分析，不能以文字结果代替立绘结论。

## 2026-09-08 文字区域更新接口（第一阶段，尚无性能收益）

core TextureProvider 增加 upload_rgba_render_only_region：数据为完整紧密RGBA图像，region为[x,y,w,h]；首次创建必须初始化完整图片，失败返回None保留重试。默认回退原render-only整页上传，桌面及现有GXM行为保持兼容。glyph Atlas记录并合并脏矩形，成功后清除，失败后的新增写入继续合并；未改图集尺寸。

验证：build/text-region-core-tests.log，330 passed / 5 ignored。新增测试覆盖跨行矩形合并、失败重试、旧像素保留、成功后范围重置以及无变化不重复上传。GXM区域写入、在途帧同步、512/1024尺寸对照和实机性能验证尚未实现；本阶段不发布性能修复VPK，不把接口测试作为优化完成证据。

## 2026-09-08 GXM文字区域更新接入（待运行验证）

已接入GxmTextureProvider区域更新：首次图集完整创建，后续复用同一NanoVG图片和GPU分配，逐行复制合并脏矩形，统计实际区域字节数。数据仅用于生成的render-only图像；图集仍为1024²，桌面后端默认整页回退不变。

文字prepare_textures在GameSurface::tick的脚本推进后、Borealis beginFrame前执行。宿主显式font_update_phase限制区域写入只能在此阶段；首个实际区域更新sceGxmFinish一次，该批后续更新复用等待结果，干净帧无额外等待。此保守同步基线尚非最终无等待方案。晚于准备阶段产生的字形更新会返回失败保留dirty，次帧重试，需关注首帧样式/链接白块等迟生成路径的表现。

验证：331 core测试通过、5忽略，新增区域更新ID复用、初次完整初始化字节记账、后续区域字节数及非法边界测试；Vita release core与宿主VPK构建成功。构建日志text-region-core-tests.log、text-region-vita-core.log、text-region-host.log。包build/art3m1s_gxm-text-region.vpk。MCP 32560端口拒绝连接，尚无本包模拟器/实机画面或性能证据，不宣称换句卡顿已解决。下一步T01同段验证无漏字/闪烁，比较texture-perf上传字节和尖峰，再处理图集尺寸及样式绘制成本。

### 文字区域更新：链接高亮时机回归修复

首次非空正文在prepare_textures时预先分配4×4白块，避免首次hover在draw输入阶段生成白块导致旧图集区域更新被阶段限制拒绝、该页当帧文字消失。新增回归确认空场景不上传，首次正文准备白块并上传，再次取白块/prepare不重复上传。

332 core测试通过（5忽略）。tests/text_region/test_host_update.py提取生产C++函数，在ASan/UBSan下验证非8对齐宽度的stride、边界保护、区域外像素不变、非法区域/活动帧拒绝和每批只等待一次；这是CPU模拟接口测试，不能证明GPU同步/性能。Vita core与宿主重新构建成功，产物build/art3m1s_gxm-text-region-hover.vpk，保留之前包用于对照。MCP仍拒绝连接，本轮无运行画面证据。

### 2026-09-08 原版立绘路径静态对照

记录build/native-sprite-review/REVIEW.zh-CN.md。原版0x8102F718的普通图片路径一次四顶点draw；当前NanoVG flags=0，普通凸矩形也单次draw，排除“默认AA多画一遍”假设。原版直接绑定持久surface并索引shader表；当前存在NanoVG路径/矩阵与线性纹理查找准备开销，但未测量其占比。原版显式裁剪字段与完整尺寸回退已见，尚无自动alpha边缘裁剪证据。完整场景调用次数/缓存/离屏链路待动态计数，不能认定多人低帧根因。

### 2026-09-08 实际GXM提交统计接入

NanoVG GXM新增上下文统计：flush次数/命令数、sceGxmDraw提交尝试数/顶点数、纹理查找/比较数量、flush CPU墙钟总量与峰值。宿主finish_host_frame每5秒通过Logger写入host.log（覆盖菜单和游戏上下文），不增加GPU等待。时间不包括EndScene/显示与GPU完成，不把draw_attempts当作已验证GPU成功调用。

产物build/art3m1s_gxm-submit-perf.vpk包含文字区域更新与hover预分配修复。最终宿主构建exit0，日志build/gxm-submit-perf-final-build.log。测试步骤和字段解释build/native-sprite-review/PERF_TEST.zh-CN.md。MCP复查仍拒绝连接，暂无新包实际运行日志；此项提供多人立绘/文字分离诊断，不宣称已经找到或修复立绘低帧根因。

### 2026-09-08 MCP恢复与T01正文复测

已通过现有Start-MCP.ps1自行恢复服务。submit-perf包运行T01，正文circle推进、新中文完整显示，状态约61FPS，无crash。区域更新窗口总上传2154800bytes（2次，无错误），低于旧图集单次4MiB；不能把包含其他纹理的总数全部归文字。普通正文实测120draw尝试/flush，flush CPU平均149us；均为模拟器证据，不替代实机结论。证据build/text-region-runtime/REVIEW.zh-CN.md与截图/host-raw.log/session-status.json。

重要：本次host.log尾部残留旧内容、存在新旧拼接残行，统计只采本轮完整窗口。main.cpp日志build时间也仅代表该对象编译日期；版本识别用安装包hash。MCP当前会话1e0da489-fd39-46de-b177-95cb5f1abb84仍运行，可继续验证。

### T01固定立绘场景文字/UI对照

同一人物特写画面：显示正文与UI时112draw/flush，隐藏后6draw/flush；模拟器CPU flush均值166us→28us。注意隐藏动作同时隐藏正文及底部UI，不能全归文字，也不等于实机GPU耗时。证据build/text-region-runtime/batch7.png、sprite-hidden.png、sprite-text.log、sprite-hidden.log。多次换句/背景与道具图显示未崩溃；多人立绘和实机性能仍待验证。

### 2026-09-08 用户指定存档 2：已建立多人测试起点

T01 存档 2 已备份并登记于 test-samples.json，测试只读不覆盖。正常读档恢复“我的后背冷汗直冒。”，继续推进抵达校门外男性与女仆重叠的画面。列表摘要仍显示开头“小时候——”，与实际保存场景不一致，需后续排查元数据；此前据列表摘要判断槽位不对的推断已撤回。

同一双人画面稳定状态：文字及底部 UI 显示时 252 draw 尝试/flush，隐藏后 8；模拟器 CPU flush 均值 289us 与 37us。人物、表情符号、背景保留；两种状态邻近完整统计窗口均无纹理上传。这是提交量诊断，不代表实体机耗时或已证明低帧根因；移除内容包含头像和底部 UI，不能全归正文。多人/特效流畅度后续统一从存档 2 做实机对照。证据 build/save2-baseline/REVIEW.zh-CN.md。

本轮已安装并运行 build/art3m1s_gxm-log-rotation.vpk（SHA256 2F37F0576A4977BE1A0BD84B37408889848069DC64E556B94310CB3FE72A6A87），包含现有文字区域更新与提交统计。新增启动日志轮换已在模拟器确认：新 host.log 单独创建，host.previous.log 哈希与轮换前备份完全一致；启动日志的 main_object_build 已明确仅指 main 对象时间。新会话 3e940220-47c8-491f-bc7d-e5a59576a543，原会话已退出；未修改模拟器配置。
