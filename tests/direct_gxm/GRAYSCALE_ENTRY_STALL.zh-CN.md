# 灰度画面切入卡顿：实机隔离记录（2026-09-20）

前半部分记录诊断阶段；后半部分是随后实现、实机对照通过的灰度重建优化。
实际剧情还混有资源冷加载和逻辑长帧，不能标为全部卡顿已修复。

## 来自实际剧情的证据

录像 `Z:/Record/OBS/2026-09-20 04-45-46.mp4`，对应抓取日志
`build/dialogue-stall-20260920/new-host.log`：

- 稳定画面可连续 300 帧命中 retained 合成结果，builds=0。不是完全没有效果缓存。
- 有些仅换句长帧总计 40–49 ms，主要是 logic 31–40 ms，present 8–10 ms。
- 场景切换仍有背景读取、解码导致的长帧；不能全部归因于 shader。
- 切入后的第二帧有 present 80–200 ms，原有五秒平均值不足以拆分该帧成本。

## 排除图片冷加载的实机实验

独立包先加载一张 960×540 背景和两个半透明色块。随后只切换父组
`intermediate_render=2` / `grayscale`，或移动一个子层。所有图片已经驻留。
每个阶段静止 1100 ms 后保存截图，再进入下一阶段。

| 操作 | 整帧 | GPU 提交场景内 | 离屏切换等待 | 结果 |
| --- | ---: | ---: | ---: | --- |
| 首次开启灰度后的缓存建立帧 | 55.457 ms | 31.689 ms | 31.034 ms，3 次切换 | builds=1，allocations=0 |
| 灰度下改变子层位置后的缓存建立帧 | 55.306 ms | 31.632 ms | 30.903 ms，3 次切换 | builds=1，allocations=0 |
| 返回先前灰度及位置 | 未触发 ≥40 ms 整帧记录 | 未触发 ≥20 ms 场景记录 | 未发生上述重建尖峰 | 已有结果复用 |

首次开启和移动后的尖峰均没有新离屏分配。代码在第一次不同的画面直接绘制，
连续第二帧相同才建立最终合成缓存，因此峰值出现在缓存建立帧。
约 55 ms 的整帧不能全部算作 shader 运算；31 ms 是包含 flush/EndScene/Finish 的
目标切换耗时，其余包括前一帧 GPU 完成等待等。

截图保存本身约 0.6–0.75 秒的同步编码／写盘不计入效果切换成本；这些发生于前一阶段
末尾。灰度首次结果、重复、移开再返回、关灰度再开启的截图，排除性能浮窗后逐像素一致；
灰度区域 RGB 三通道差异为 0。

证据：`build/dialogue-stall-20260920/gray-device/host.log`、`comparison.json`、`phase0.png`–`phase6.png`。
本地生成／操作脚本 `build/dialogue-stall-20260920/gray_probe.py`。
临时设备测试目录已移到 `ux0:data/art3m1s-gxm/validation/gray-entry-20260920`，原游戏选择已恢复。

## 新诊断记录

`[effect-frame-spike]` 每五秒最多八行，仅记录 ≥20 ms 的提交场景：
目标切换次数／耗时、离屏分配次数／耗时、灰度绘制数、合成结果命中与重建数。
结合 `[frame-spike]` 区分资源准备、Begin 前等待与离屏重建。
命中缓存时 gray_draws=0 是正常的，最终灰度图已存在，直接复制即可。

部署：`build/direct-deploy/deploy-20260920-045715/manifest.json`。
eboot SHA256：`e99593c8083a080437733b65b88667ba60082b42d590fede9cb16b7ba4c0c250`。
shader 字节、320 MiB 堆、192 MiB 总缓存、字体均未修改。

后续优化必须降低合成结果建立时的重复绘制／同步，保持半透明人物、嵌套组和蒙版正确。
不能直接删除 `sceGxmFinish`，也不能将组透明度随意分摊到子图层。
需在原剧情复现并对照新慢帧记录，确认该路径占实际卡顿的比例。

## 05:06:38 实际剧情复测和优化

新录像 `Z:/Record/OBS/2026-09-20 05-06-38.mp4`，日志
`build/dialogue-stall-20260920/effect-retest-host.log`：四人场景切入后建立缓存的一帧
209.970 ms，其中离屏切换 23 次、178.238 ms；没有离屏分配。
单人切入后的缓存建立帧约 85–116 ms，也伴随多次离屏等待。

实际游戏注册的是源码匹配的内置 `gray.hlsl`，通过 external 程序接口执行；
不是第一轮实验的属性 `grayscale=1`。诊断项 `gray_draws` 仅统计后者，所以游戏中
该项为 0 不代表没有灰度 shader。不是临时编译 shader 引起停顿。

优化只在缓存重建遍历中移除透明、无颜色／透明度／遮罩变化的中性父组，
允许其包含经过源码指纹确认的内置 gray 子组。子组 shader 仍执行，外层最终合成
缓存仍保留，不拆成多个长期缓存槽，不修改 `sceGxmFinish` 安全边界。
仅名称相同的未知 shader 不算匹配；gray 有额外 alpha／mask／user texture，
或者父组有非中性效果时，仍用保守路径。同 ID 重注册会清除旧的源码证明。

### 对照条件

独立脚本 `fixtures/grayscale_entry.iet`，准备 `bg.png` 和原游戏的 `gray.hlsl`，
BOOT 指向该脚本，VITA 960×544。四组重叠半透明色块模拟人物和表情，分别应用 gray；
背景也应用 gray。先全部载好，再移动一组，避免把资源解码算作灰度成本。
脚本保存截图的同步编码／写盘长帧不计入渲染优化数据。

实机 CPU 333 MHz、GPU 111 MHz，堆和缓存额度不变。

- 改前：重建帧约 167.6–168.2 ms，提交场景约 149.4–149.9 ms，目标切换 19 次。
- 初版候选：重建帧约 78.7–78.8 ms，提交场景约 70.8–70.9 ms，目标切换 11 次。
- 7 个阶段的新旧截图逐像素一致，未排除任何区域。
- 核心回归：498 通过、17 忽略。覆盖缓存命中、未知 shader、父组效果、遮罩、alpha 和同 ID 重注册失效。

证据 `build/dialogue-stall-20260920/wrapper-before/`、`wrapper-candidate/`、
`wrapper-comparison.json`。后续最终包收紧为源码匹配的 gray 白名单，同一脚本再次验证。
代码重放补丁 `patches/grayscale-neutral-rebuild-core.patch`，基于活动 core 的 d550a03。

最终白名单版：两次重建整帧 78.731 / 78.825 ms，提交场景 70.824 / 70.837 ms，
7 张截图对比仍为零差异（`wrapper-device/`、`wrapper-final-comparison.json`）。
core 提交 a721bfb；最终部署 `build/direct-deploy/deploy-20260920-052143/manifest.json`。
eboot SHA256：`2ff97ed576369fc1dfe0ccf3c1679af571199d9dee64d86397fd918d019c92d5`。
临时包已移到 `ux0:data/art3m1s-gxm/validation/gray-wrapper-20260920`，恢复原游戏选择。

本项仍有离屏重建成本，且真实剧情的背景冷解码与换句逻辑成本尚存，不能据独立测试
宣称实际剧情已全程 60 FPS。字体修复仍单独安排。
