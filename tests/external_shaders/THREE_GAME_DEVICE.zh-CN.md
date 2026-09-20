# 三款资源的非内置 Shader 实机验收

测试日期：2026-09-19。只使用安装目录的原始 PFS 索引及 shader 条目，不执行 PC 游戏 EXE，不改变游戏资源或 PFS。解包分析文件位于 `build/three-game-shaders/originals/`；实机只新增三个独立 TEST 游戏目录。

2026-09-20 更新：下面的首次验收数字保留历史含义；新版加载页只把 HLSL 计入进度及失败。Toshiue 现在显示 HLSL 32 项（含 reset 对照）、失败 31、其他格式跳过 31；每份 Stella 为 HLSL 27 项（含 reset）、失败 23、其他格式跳过 44。跳过项不再尝试转换，DX11 HLSL 仍明确失败。

新版 demo 的自动校验使用“左：CPU参考效果”和“右：PSV着色器效果”；自动截图完成后，手动演示改为“左：原图”和“右：PSV着色器效果”，按 ○ 切换下一项。Stella 默认停在 blend 效果页，便于直接观察变化。reset／零强度等对照页可能两边一致；Toshiue 没有本轮可编译的非内置 DX9 项，仅保留 reset 并明确提示。更新没有修改原 shader 或 GXP 算法。

本轮已更换加载统计修正版 executable（SHA256 `1f61be9a682a33deec6008b2f64013f4b7a895fd416709e6822ac4df033b2f00`）。491 项核心测试及宿主进度计数 ASan/UBSan 测试通过。核心提交 `ae6bc7b`，可重放补丁为 `patches/hlsl-only-shader-progress-core.patch`。本轮设备证据独立保存在 `build/shader-progress-hlsl/device/`，不覆盖下面 2026-09-19 的历史记录。

实机复验命令：`wsl -d Ubuntu-24.04 -- python3 /mnt/f/WorkSpaceAI2/art3m1s-psv-gxm/scripts/analyze-hlsl-progress-device.py`。检查三个 demo 的 HLSL 总数／失败数／跳过数、精确错误路径和缓存字节，并只对绘制区域比较历史像素，避免新增标签影响判断；另验证手动页左侧等于原图、右侧等于 shader 输出，且两边存在可见差异。

复验全部通过：19 页自动截图的效果区域与历史基线完全一致；手动 blend 页左侧与 reset 原图一致、右侧与自动 shader 输出一致，左右 104000 个像素中有 100372 个不同。用户原 shader 开关及游戏选择已恢复、启动器自检通过；三个新脚本和字体均已回读核对。摘要和实机原图／效果截图位于 `evidence/shader-progress-hlsl-20260920/`。

## 原始资源范围

按 `.pfs`、`.pfs.000` 等覆盖顺序建立有效文件表，保留 shader 原始字节；按当前内置表的源码 FNV64 判断命中，不按文件名判断。三个包共 271 个 shader 文件，69 个来源文件命中内置，202 个不命中。

| 原始目录 | Shader 文件 | 内置命中 | 非内置 DX9 HLSL | 非内置 DX11 / GLSL |
|---|---:|---:|---:|---:|
| toshiue-kanojo2 | 93 | 31 | 0 | 62 |
| Stella_of_the_End_PC | 89 | 19 | 3 | 67 |
| Stella_of_the_End_Android | 89 | 19 | 3 | 67 |

Toshiue 的 31 个 `system/shader/pc/*.hlsl` 均已内置。剩下 31 个 DX11 HLSL 和 31 个 Switch GLSL 不在当前自动转换器支持范围。

两份 Stella 包的 **89 个 shader 文件逐字节相同**，包括 Android 包中的 PC HLSL 副本。各自 3 个未内置的 DX9 文件：

- `system/shader/pc/blend.hlsl`：将 RGB 平均亮度作为透明度，乘遮罩透明度和图层 alpha。
- `system/shader/pc/blend2.hlsl`：根据采样红色通道的 0.78 阈值决定透明度。原代码把 `float3` 赋给 `float`；libshacccg 会警告隐式截断，测试保留原代码，没有擅自改成平均亮度。
- `system/shader/pc/radial.hlsl`：径向模糊。与内置版本的差别是累加器写成 `float4 fadd = 0`，而不是显式 `float4(0,0,0,0)`；源码哈希不同，算法相同，仍必须按外置流程验收。

每份 Stella 另外有 23 个 DX11 HLSL、22 个移动端 GLSL、22 个 Switch GLSL。它们作为明确拒绝项测试，不能因为回退原图仍然可见而记为编译成功。

## 实机结果

七轮均完成，总计 55 张截图。所有测试使用 Vita 平台，外置效果的原始路径和缓存均保持 `system/shader/pc/...`。

| 资源 | 首次：转换开／编译开 | 热缓存：转换关／编译关 | 仅 Cg：转换关／编译开 |
|---|---|---|---|
| Toshiue | 62 个其他平台代码逐项拒绝；reset 内置对照正常 | 无可编译的非内置 DX9 项，不重复测空集 | 同左 |
| Stella PC | 3 项真实编译，9 页绘制完成 | 3 次 Cg / GXP 命中，0 次编译 | 3 次 Cg 命中，3 项重新编译 |
| Stella Android | 3 项真实编译，9 页绘制完成 | 3 次 Cg / GXP 命中，0 次编译 | 3 次 Cg 命中，3 项重新编译 |

两份 Stella 每轮的 67 个其他平台代码均按预期拒绝，错误路径集合与原始清单一致。首次共有 6 次外置编译，仅 Cg 轮又有 6 次重新编译。热缓存和仅 Cg 轮的 36 张完整截图与各自首次截图逐像素相同；PC / Android 的编译缓存四文件也逐字节相同。

生成的 GXP 大小分别为 blend 532 字节、blend2 572 字节、radial 1340 字节。首次第一项约 1.96 秒（包含编译器指纹读取和初始化），后两项约 0.28 / 0.64 秒。热缓存首项仍约 1.36–1.39 秒，后两项约 16–18 毫秒：缓存命中不代表消除了首次编译器文件指纹 I/O，这些都是加载时间，不是逐帧时间。

与独立 CPU 公式对照的图像区域，平均单通道差值约 0.002–0.279；blend2 的 4 个阈值边缘像素除外，其他最大差值不超过 5。该边缘例外保留在机器结果中，不把它描述成原生 PC 像素完全一致。

用户原先没有 shader-settings 文件，结束后删除本次临时设置，恢复默认双关；原上次游戏选择和备份文件状态已回读确认。启动器自检通过、停在游戏选择界面。三个独立 demo 和有效缓存留在实机，可直接选择查看，不必再开启转换/编译。

## 独立 Demo

生成：

```powershell
python -X utf8 scripts/prepare-three-game-shader-test.py
```

默认从 `E:/EmuGame/vita3k_data/ux0/data/art3m1s-gxm/games/` 读取，也可通过 `--games` 指定来源。脚本复用只读 PFS 提取器、当前内置 manifest 和 DX9 转换边界；用 WSL fontTools 从已有分析字体提取 ASCII 子集。

输出包：`build/three-game-shaders/three-game-shader-demo.zip`。

实机目录：

```text
ux0:data/art3m1s-gxm/games/
  TEST_SHADER_TOSHIUE_REAL/
  TEST_SHADER_STELLA_PC_REAL/
  TEST_SHADER_STELLA_ANDROID_REAL/
```

三个 demo 的 `platform.txt` 都是 `vita`，只提供 `[VITA]` 的 `system.ini`。shader 仍按原来的 `system/shader/...` 路径加载，没有改注释、改算法来强行绕过内置匹配，也没有借用真实游戏的缓存。

进入后自动运行一遍、保存截图，随后停留；按 ○ 手动逐页查看，□ 打开菜单退出。左图是独立 CPU 公式生成的参考纹理，右图使用实机 shader。Toshiue 只有内置 reset 对照页，用来确认拒绝其他平台代码后仍能正常绘制；它不存在本轮可编译的非内置 DX9 项。

每份 Stella 共 9 页：reset 对照，以及 blend、blend2、radial 的不同参数和 alpha=255/128。radial 包含 size=0 的原图等价情况，以及非中心径向采样。测试 shader 使用独立 ID，避免按名字误用其他内置效果。

## 实机流程和证据

使用现有 ART3DIR01 程序，没有替换 executable，也没有修改渲染器或转换器。实机 eboot SHA256：

```text
320840e79ad1c1bc057cdfed6f916c1e61bb1482278b105b8ddec14b5c0c5b3a
```

VitaCompanion 1.06；启动日志确认 ARM 333 MHz、GPU 111 MHz，Vita 平台入口。测试前保存用户的 shader 开关、上次选择及备份文件。上传逐字节回读验证；缓存清理只针对本次独立 TEST 目录，并要求与已导出的缓存字节一致。

```powershell
python -X utf8 scripts/test-three-game-shaders-device.py upload
python -X utf8 scripts/test-three-game-shaders-device.py start --game 1 --round cold
# 等启动自检结束，再按一次圆圈进入已选中的 demo
python -X utf8 scripts/test-three-game-shaders-device.py press
python -X utf8 scripts/test-three-game-shaders-device.py collect --game 1 --round cold
```

`--game 0/1/2` 分别对应上述三个目录。`cold` 开启转换和编译；`warm` 关闭两者；`cg-only` 关闭转换、开启编译。执行 `cg-only` 清理动作时，只删除本测试中已回读验证过的 GXP/hash，保留 Cg/元数据，然后以同名 round 启动。

结束后必须运行 `restore` 恢复原偏好并返回游戏选择界面。demo 和它们自己的缓存保留，供用户手动查看；不发布到全局 shader-cache，也不覆盖真实游戏缓存。

证据位于 `build/three-game-shaders/device/`：完整日志、每页截图、四种缓存文件、上传校验、偏好备份与恢复记录。`preliminary-old-font/` 是修正 demo ASCII 字体前的初测，不作为最终截图证据。

分析：

```powershell
wsl -d Ubuntu-24.04 -- python3 /mnt/f/WorkSpaceAI2/art3m1s-psv-gxm/scripts/analyze-three-game-shaders.py --require-complete
```

分析同时检查真实编译/命中计数、Vita 入口、完成标记、拒绝数量、缓存的源码/Cg/GXP 校验和页面像素；详细结果写入 `build/three-game-shaders/results.json`。

参考比较使用每页 400×260 的左右绘制区域。blend2 在硬阈值附近的 4 个边缘像素存在先采样后阈值与预生成纹理再采样的差异，明确单列，不宣称逐像素完全一致。其余效果要求单通道最大差值不超过 5，区域平均差值不超过 1.5。缓存复用和重新编译轮次则与同一程序的首次实机截图作精确像素比较。

这验证的是当前运行时编译、缓存读取及上述采样/透明度参数，不替代任意参数范围、原生 PC 全流程或性能验收。
