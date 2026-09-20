# v1.2.8 实机验收

用户确认刚才平移优化包实机测试通过，无问题。该版本不含正在开发的外置 shader。

原录像 `Z:/Record/OBS/2026-09-15 04-11-20.mp4`；日志 `build/native-command-port/psv-current-slow-scene.log`。`toshi_03.ast` 的 block_00127 同时平移背景、立绘，五层未实现的 blur_k 仍创建离屏复制，约 6 FPS、每帧目标切换约 132 ms，且发生嵌套目标分配失败。

修正仅消除当前 identity fallback 的中性合成边界，并通过已证明中性的子组传递背景不透明覆盖证据。实际模糊算法未在这个版本实现；灰度、遮罩、透明度与其他未知 shader 不被强行忽略。

474 项核心测试通过；实机启动像素自检通过，333 MHz。用户实机验收通过，不把通过测试等同于所有时刻均 60 FPS。

已安装包 `build/toshiue-scene-render/art3m1s-neutral-blur-routing.vpk`，SHA256 `2633014b5c75e81f8eea2dd0d44e2c9dd416d094eb3243361b5eb6c9401e37b5`。安装记录 `build/direct-deploy/deploy-20260915-042808/manifest.json`。

核心补丁 `patches/neutral-blur-routing-core.patch`，基于 `60a372e`。此 tag 沿用 VPK 内部版本 01.10，发布版本为 v1.2.8。
