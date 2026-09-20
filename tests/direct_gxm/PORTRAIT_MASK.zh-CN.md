# 头像黑底：局部灰度遮罩与缓存

2026-09-15。实机 333 MHz、ES4 111 MHz，沿用 host-direct shader。

## 复现场景与资源检查

已保存至游戏内 **No.01-04**，实际文件 `savedataHD/save0003.dat` 与 `save0003.png`。原有 10 个存档相关文件先备份，再新增测试档；备份及保存后的副本均在 `build/toshiue-portrait-alpha/saves-before/`、`saves-after/`。

只读对比原版 `F:/WorkSpaceAI2/art3m1s_test_rom/年上彼女2` 与压缩版 `E:/EmuGame/vita3k_data/ux0/data/art3m1s-gxm/games/toshiue-kanojo2`。两套归档各 4540 条索引。只提取相关头像、表情和遮罩到项目 `build/toshiue-portrait-alpha/{original,compressed}/`，没有往游戏运行目录解包，也未修改／替换游戏归档。

- `ren_nob0200.png`：原版 240×252 RGBA，压缩版 180×189 RGBA；两者透明度均包含 0～255，压缩版有 9892 个完全透明像素。
- `ren_noa0200.png` 及当前表情零件亦保留透明度，尺寸缩到 75%。
- `pc/ja/mw/maskface.png`：原版 300×300、压缩版 225×225，均为 8 位灰度 PNG（颜色类型 0）。普通 RGBA 解码后的 alpha 全为 255，真正的遮罩值在灰度通道中。

因此本场景不是压缩版把头像透明像素统一变成黑色。当前存档的 `1.80.mw.1.fa` 位于 `(0,315)`，设置 `intermediate_render=2`、`intermediate_render_mask=:ui/mw/maskface.png`，其内部位置分组再次开启模式 2。

## 修正

原 compositor 把模式 2 的结果强制成不透明，再把灰度遮罩当作全屏 RGBA alpha 纹理采样。两项叠加将头像周围的透明空间变成一块 225×225 黑底。

此次限于带遮罩的子树：保留自身和嵌套中间合成的覆盖率；未带遮罩的模式 2 沿用既有行为，不把尚未完全还原的原生 1/2 模式语义扩展到所有场景。

遮罩复用已有 `resolve_with_mask(mask, mask)` 灰度到 alpha 转换，并以图层世界坐标、原尺寸绘制到遮罩目标，正确继承位移／缩放。组合纹理名加入 scene 保活集合，避免每帧解码或上传。没有修改 shader 源码／字节码。

第一版透明结果正确，但遮罩分组被排除在缓存外，实机约 19 帧，未作为最终交付。最终版允许稳定的局部遮罩组缓存，烘焙时包含实际 mask render target；头像命令、遮罩位置或纹理内容变化时失效。缓存仍共用已有 4 个槽，不新增无限容量的队列。

## 验证

- 466 项核心测试通过，15 项既有测试忽略。
- 新测试覆盖嵌套透明度、局部遮罩位置与缩放、组合纹理保活、稳定遮罩只烘焙一次、遮罩纹理及变换变化后失效。
- 实机启动像素自检通过：`retained=1 local_base=1 overlay=1`。
- 从 LOAD 明确选择 No.01-04，读回相同文字及头像姿势：黑底消失，浮窗 60 帧。`before.jpg`、`after.jpg` 为实机投影截图。
- × 隐藏／恢复文字框，头像消失／恢复正常；随后换句、眨眼后未复现黑块。
- 最终两个 10 秒停句窗口为 59.9、60.0 FPS；5 秒组缓存统计分别 729／727 次命中、19 次重建。仍有个别超过 20 ms 的帧，不代表完全没有波动。无脚本错误或离屏分配失败。
- 之前的 SAVE 返回修正也在此次存档后验证：× 立即恢复剧情，无需再次切换背景。
- 这不是全游戏性能或所有原生游戏兼容性验收；加载峰值与停句帧率分开看。

日志位于 `build/native-command-port/psv-portrait-*.log`；压缩／原始资源统计为 `build/toshiue-portrait-alpha/comparison.json`。

## 版本复现

core 工作树提交：`d3698e6` → `03c3a1f`，补丁为 `patches/masked-portrait-core.patch`。在已含旧 native-commands 和 queued-wait-return 补丁的 core 上检查后应用；已有提交不得重复应用。

宿主改动位于 `host-direct/src/gpu.cpp` 的 `group_end_cached`，将生成的遮罩目标加入缓存烘焙，并保留透明属性。构建入口仍为 `scripts/build-native-commands-core.ps1` 与 `scripts/build-native-commands-host.sh`。

最终包：`build/toshiue-portrait-alpha/art3m1s-portrait-mask-cached.vpk`。

VPK SHA256：`d26b460fbeba20f70a92afd2f22560fdf17a27c0d931c409425fc5f7356210c3`。

SELF SHA256：`a2d3a55d2d63760cad425f8c74d27a4182703a753924478ed622b643c1201005`。

实机逐文件回读校验与最终包一致，记录于 `build/direct-deploy/deploy-20260915-031314/manifest.json`。该记录初始包哈希在备份期间遇到构建完成而过时，已另附 `verified_installed_package` 保留准确最终包证据；实际 staged／installed 文件字节校验均通过。
