# GXM 规则转场 shader 接入记录

状态：`rule_transition_f.cg` 已用用户提供的离线工具编译成 724 字节 GXP，验证 uniform 与双采样器绑定，并接入 core→bridge→NanoVG GXM 延迟绘制队列。`build/art3m1s_gxm-rule-transition.vpk` 已构建，链接 ELF 中找到与离线输出完全相同的 GXP。**没有实机渲染通过的证据，仍待图像验收**。Cg 源码开头的“Not wired”注释来自准备阶段，本段描述当前集成状态。

## 离线编译与 vitaShaRK 对照

用户提供 `SDK-3_570_021-patch.zip`，已解压到工作区 `.tools/sony-shader-3.570`，不在游戏运行目录。编译器自报版本 SDK 3.5.0 build 13284（Mar 10 2016），不要仅凭压缩包名将其报告成其他版本。

运行 `./scripts/build-rule-shader.ps1` 可重建，或通过 -CompilerDirectory 指定工具目录。使用 sce_fp_psp2、O1、nofastmath、bestprecision、nofastint。输出及参数检查结果、工具/源码/GXP SHA256 在 `build/rule-shader-offline/`。本次 GXP SHA256 为 `7FE6EC355D1063AAFCEA872D946872FDC251726847CB2B2D924BAB945BDFA99B`。

按用户要求保留 `tests/rule_shader` 的 vitaShaRK 探针，之后用同一源码比较两种编译器。二进制不同不等于画面错误；仍须比较 GPU 输出及性能。离线 SDK 工具不打包进 VPK。

## 已核对的接口

- core 的 `render_pipeline/transition.rs::overlay_old_frame` 给规则转场附加 `rule-trans`、mask_texture、progress、vague，旧帧 opacity=1；不是脚本未发出渐变。
- 已修复的缺口：GXM renderer 原先不分派 ShaderEffect，bridge 只调用普通 NanoVG image fill。现将 rule-trans 的 mask/progress/vague 传入宿主专用规则填充；普通 shader 路径不受此分派影响。
- Borealis 非 USE_VITA_SHARK 路径使用内嵌 GXP；此次已生成规则 shader 的 GXP，可继续接入该生产路径。
- 新 shader 复用 NanoVG fill 顶点输出 TEXCOORD0/1 和 frag[11] 布局。旧帧占 TEXUNIT0，规则图占 TEXUNIT1；frag[9].z/w 在此专用程序中分别承载归一化 progress/vague。不改变普通 image fill 的 uniform 含义。
- UV 从 paintMat、extent 生成，规则图与旧帧共用 UV；匹配 core 的现有 GL rule shader。宿主必须保留 capture_clip 的 UV 翻转与缩放。
- 灰度阈值及 smoothstep 公式与 core 内建 shader 相同；GL 输出直 alpha，而 NanoVG 输出预乘 alpha，因此这里先令 RGB 乘纹理 alpha，再连同 alpha 乘 keep 和预乘后的 innerCol。不能仅修改 alpha，否则旧 RGB 会残留。
- 此 shader 用于全屏矩形转场，不包含 NanoVG 轮廓边缘抗锯齿函数；保留 scissorMask。不得替代普通图片或文字 shader。

## 接下来必须完成

1. 已完成离线 Cg→GXP 编译、可复现脚本、工具版本及哈希记录和参数绑定检查；vitaShaRK 运行对照尚未完成。
2. 已增加专用 fragment program，运行时使用当前 fill vertex program 创建链接，销毁时释放。实际链接是否成功仍须查看设备日志。
3. 已实现每个 call 保存 rule image、独立 uniform allocation 保存 progress/vague，flush 时绑定槽 1。临时录制状态在 nvgFill 返回后清空，不用于 flush。
4. 已实现 Rust GxmRenderer 到 bridge 的专用参数分派；使用 core 的 transition retained_files 保活旧帧和规则图。没有游戏名硬编码。328 项 core 测试通过，5 忽略；包括参数传递、边界/非有限值和非规则 shader 隔离测试。
5. 验证 progress=0/1、黑/白/渐变规则、vague=1/255 和 32/255、规则图不同分辨率、旧帧 alpha 和 UV 翻转。需要 GPU 输出图像证据；仅 CPU 公式测试不足。
6. 优先对 SHUF00002 与用户 Steam 录像中的中心展开、扇形展开作实机对照，并采样转场期间帧时间。当前异步旧帧截图及隐藏文字捕获问题另行验收，shader 本身不会自动修复它们。

本次没有切换全局 USE_VITA_SHARK 开关。生成头文件同时保留预编译 GXP 和同源 Cg，供后续 vitaShaRK 对照；主运行器保持离线 GXP 路径。规则不可用时记录 `[gxm-rule] ... unavailable; using crossfade`；首次成功入队记录 `[gxm-rule] dual-texture rule draw queued`，后者只证明入队，不证明实际 GPU 画面正确。不可用时使用交叉淡化，不能据此算规则效果通过。
