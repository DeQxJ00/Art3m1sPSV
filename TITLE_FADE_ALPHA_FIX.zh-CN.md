# 标题淡出时按钮黑色矩形

2026-09-08。用户截图及 22:29:21 录像约 65 秒处：点击标题 Start 后，全屏变暗，底部各按钮透明边缘出现黑色矩形。原始 bt_start.png 有透明边缘；标题脚本 title_reset 删除标题图层、创建黑色背景并执行 uitrans。仅在工作区提取原始资源核查，没有改游戏脚本或贴图。

已确认的后端错误位于 NanoVG GXM 的片段程序混合配置：RGB 使用正确的 ONE / ONE_MINUS_SRC_ALPHA，但 alpha 使用 ONE / ONE_MINUS_DST_ALPHA。对不透明背景 Ad=1，旧式得到 Aout=As；透明按钮边缘 As=0 因而把 framebuffer alpha 写成 0，即使屏幕 RGB 看起来仍正常。显示快照作为 RGBA 纹理重新参与淡出后，这些区域露出黑色目标，解释了矩形为何在点击后出现。

修正实际片段程序配置与 fallback：Aout=As+Ad*(1-As)。此外，完成显示面的读回强制 alpha=255，保留原 RGB；转场和存档需要的是已显示的完整画面，不应继承 framebuffer 的图层覆盖值。此修改不把游戏源 PNG 的 alpha 改为不透明，也不影响其正常透明边缘。

验证范围：检查实际 GXM 混合参数、原按钮透明边缘与标题转场脚本；重新构建宿主/VPK，日志 `build/transition-alpha-build.log`。尚无此包的实体机或模拟器截图，不能宣称黑方块已由运行验证消失。需要复测标题 Start 淡出、弹窗背景淡化、正文存档缩略图。规则转场 shader 的缺失是另一项问题，本修复不补齐该功能。
