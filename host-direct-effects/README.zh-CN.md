# Direct GXM 内置效果测试版

更新：用户已要求合回主版本，现已合入 `host-direct` 01.03，主构建也启用
`gxm-builtin-effects`。本目录和下文保留作为独立测试版记录，当前主版本结果见
`build/effects-merged/REPORT.zh-CN.md`。

本目录从正在优化的 `host-direct` 分出，用于补齐 core 的内置 shader 语义。
主版本继续使用 `gxm-native-renderer`；本版本额外启用 `gxm-builtin-effects`。
新 FFI 仅在该 feature 下调用，旧宿主 ABI 与构建脚本保持兼容。

## 已实现

- simple-sprite / sprite：颜色乘算 → 灰度（0.299/0.587/0.114）→ 反色 → 不透明度。
- Native E-Mote：四角颜色双线性插值、UV 矩形、模型空间裁剪、alpha wipe、倍亮、
  native 3/4 预乘、native 5 反色、低 alpha discard；支持四边形和展开三角网格。
- rule-trans：按规则图红通道阈值及 vague 软边溶解，保持 core 的计算顺序。
- alpha-mask：对预乘组纹理的 RGB 和 alpha 一起应用遮罩，避免二次预乘和黑边。
- group-composite：解预乘、颜色过滤、opaque、组 alpha、遮罩、重新预乘。
- 分组：保持 core 的范围/嵌套选择顺序，独立渲染 mask_commands，再合成到父组。
- 10 种 BlendMode 均分别映射：Alpha、Add、Multiply、Screen、PremultipliedAlpha、
  PremultipliedAdd、NativeAdd、NativeReverseSubtract、NativeMultiply、NativeScreen。
  native 模式保留目标 alpha；另有内部 Copy 模式用于完全清空离屏目标。

普通图片保留原来的 4 条轻量片段路径。`builtin_f.cg` 仅用于附加颜色效果、
分组/遮罩、E-Mote 材质和扩展混合。所有 shader 离线编译，运行时无需编译器。
本版本包含 5 类在用片段 shader、19 个已链接片段程序（含内部 Copy），一个共用顶点程序。

## 边界

游戏自行提供的 HLSL `[lyshader]` 源码不属于 core 的内置 shader 清单。
`register_hlsl_shader` 仍明确返回尚未转换错误；本版本没有运行时 HLSL 编译器，
不能声称游戏自定义效果均已兼容。

离屏目标固定为 PSV 显示分辨率 960×544，无 MSAA/深度/模板。按嵌套深度懒分配，
并在游戏之间保留供复用；每个颜色或遮罩目标约占 2 MiB 外加 GXM 元数据。
分组切换使用 Finish 确保目标绘制完成后再采样，不使用 CPU 回读合成。
这会增加实际启用组效果时的绘制和同步成本；实机性能尚未验证。

## 构建和测试

1. PowerShell：`scripts/build-direct-effects-core.ps1`
2. PowerShell：`scripts/build-direct-effects-shaders.ps1`
3. WSL Ubuntu-24.04：`bash scripts/build-direct-effects.sh`

输出：`build/shader-completion/host/art3m1s_direct.vpk`，Title ID `ART3EFX01`，
应用名 **art3m1s GXM Effects**。这是独立安装项，仍读取
`ux0:data/art3m1s-gxm/games` 和原有存档。日志为
`ux0:data/art3m1s-gxm/host.effects.log`，上一份为 `host.effects.previous.log`。

测试探针：`effects_probe.vpk`，Title ID `ART3EFP01`，不访问游戏或存档。
`tests/direct_effects/check_pixels.py` 使用独立的 GL 混合方程进行截图像素对照，
同时测试分组复用、嵌套、蒙版、FFI 参数、网格和 UV 翻转。

详细验证结果、已知测试环境问题与包校验值见
`build/shader-completion/REPORT.zh-CN.md`。
