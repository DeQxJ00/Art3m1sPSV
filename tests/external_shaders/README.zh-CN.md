# 内置 Artemis PC shader 与外置缓存

安装与路径请参阅 [Shader 放置说明](../../SHADER_PLACEMENT.zh-CN.md)。51 项 demo 的 ZIP 内同时提供 `SHADER_PLACEMENT.txt`，可离线查看。

本轮以 Toshiue_Kanojo2 的 31 个文件为基准。otomeriron 的 20 个同名文件与它逐字节相同，因此 51 个来源文件只需 31 个预编译程序。

| 来源 | 文件数 | 与另一款相同 | 独有 |
|---|---:|---:|---:|
| otomeriron | 20 | 20 | 0 |
| Toshiue_Kanojo2 | 31 | 20 | 11 |

共有：add、blur_h、blur_v、cadd、cmul、compbr、compbrc、compdk、compdkc、gray、mosaic、mul、nega、radial、raster、reset、rgb、screen、sepia、sepia2。

新增：blur_k / blur_kx / blur_ky（三种 Kawase 采样）；dimhole / dimover / dimring（圆形扭曲、暗化与环形高亮）；noise（噪声位移）；trapezoid_dw / lt / rt / up（四方向梯形变形）。算法和参数保留源文件的定义；同名 shader 不自动视为同一实现。

## 加载顺序

1. 读取原 shader，按源码内容的 FNV64 标识查找内置 AGX1/GXP。共约 25 KiB GXP；只在脚本请求时注册，在游戏退出时释放。内置程序不依赖 libshacccg 或两个自动开关。
2. 未匹配内置版本：优先读取与源码、转换器版本匹配的 Cg 和参数布局缓存。
3. 缓存缺失且允许自动转换：将支持的固定 DX9 HLSL 包装转换为 Cg，并立即缓存，即使自动编译关闭也会保存。
4. 优先加载与 Cg、源码、编译设置和编译器版本匹配的 GXP。仅在缓存缺失且自动编译开启时调用 PSV 的 vitaShaRK/libshacccg。

两个开关在游戏选择界面的 START 设置中，默认均关闭、全局保存，下次进入游戏生效；已有用户设置仍按保存值读取。内置已覆盖这两个游戏的 51 个来源文件（31 种独立效果），一般无需开启。关闭不会删除已有缓存；不匹配、损坏的缓存不会作为当前源码的编译结果加载。

外置效果请准备转换好的 Cg 和配套的参数/校验元数据。Cg 是源码，并非编译结果：只有有效 Cg 转换缓存时，可以保持自动转换关闭，按需开启自动编译；两个开关均关闭时，需要匹配的完整 Cg/GXP 缓存。不能只放一个任意 `.cg` 文件就直接运行，也不能把 `.gxp` 改名为 `.cg`。

缓存保持 PFS 相对目录。例如：

```
games/<游戏>/shader-cache/system/shader/pc/example.hlsl.cg
games/<游戏>/shader-cache/system/shader/pc/example.hlsl.conversion.json
games/<游戏>/shader-cache/system/shader/pc/example.hlsl.gxp
games/<游戏>/shader-cache/system/shader/pc/example.hlsl.hash
```

保留 `.hlsl` 后缀是为了区分同名不同类型的源文件。旧的扁平缓存保留，但新路径不复用旧格式。转换元数据包含参数布局与源码/产物校验；手工修改 `.cg` 后不再视为有效的自动转换缓存。

## 修复与边界

- Cg 全局常量显式使用 `static const`。实机第一次测试发现灰度与两种褐色调读到零常量、呈现黑色，修复后通过像素验证。
- 核心与宿主统一接受 GXP 合法的四字节尾部对齐，拒绝截断、超长和不一致的包。
- 多层连续自定义滤镜保留每一次采样，通过两个交替使用的离屏目标执行。真实模糊不再当成普通拷贝；预编译减少的是加载成本，不能消除滤镜自身的 GPU 运算成本。
- 普通场景不会因“已注册某个 shader”而自动开启该效果。原有内置 sprite、转场、视频 shader 的程序字节不变。
- 自动转换仅支持已实现的固定顶点包装/像素效果子集；自定义顶点阶段、samplerBack 等不宣称支持。

## 验证与复现

### 可交互的 51 页 demo

51 页 demo 已预置在 Direct VPK 内；安装后在启动器选择 **TEST SHADERS 51**，两个自动开关均可关闭，无需额外复制资源。程序从 `app0:/demos/TEST_SHADERS_51/` 读取；保存数据仍在 `ux0:data/art3m1s-gxm/saves/TEST_SHADERS_51/`。数据目录存在旧版同 ID demo 时优先使用内置版，菜单不重复显示。

维护资源时运行 `python scripts/prepare-shader-gallery.py`，同时生成提交到仓库的 `host-direct/assets/TEST_SHADERS_51/` 和独立导出包 `build/shader-gallery/TEST_SHADERS_51.zip`。普通构建直接打包已提交资源，不需要本地原游戏或重新生成字体。CMake 缺少 demo 时会报错，避免产出只有菜单入口、没有资源的包。

- 第 1–20 页标为 game1，第 21–51 页标为 game2；每个原始 HLSL 文件单独注册、单独展示，不合并重复项。页面、日志标识、manifest、资源路径与包内说明均不包含真实游戏简称。
- 左原图、右效果，附来源、名称、中文说明及固定参数。混合类使用额外半透明四色纹理；棋盘底用于观察透明区域；reset 预期与原图一致。
- 按 ○ 下一项，51 页后循环；按 □ 可在菜单选择退出游戏。全部资源位于独立 TEST 目录，不修改真实游戏。
- Vita3K 已在转换/编译均关闭时走完 51 页，并验证回到第一页、打开 □ 菜单；日志确认 51 个内置注册且无 shader 加载错误。51 张截图及 `host.log` 保存在 `build/shader-gallery/`，按三张 contact 图片检查全部页面。
- 这是固定参数的视觉演示，不代替实机性能测试或原版所有参数的逐像素对照。

内置 demo 更新验收：Vita3K 从 VPK 应用目录启动，两个自动开关关闭，51 次注册均命中内置；按 ○ 完整走过 51 页、回到第 1 页，再从 □ 菜单退出成功。保留数据目录里的旧 demo 做去重对照，新入口使用应用资源及独立可写缓存路径。`tests/game_library/test.cpp` 另验证数据目录尚不存在时仍列出 demo、旧版去重、原游戏优先级和选择记忆。包内 62 个文件（含 51 个 HLSL）均扫描确认无真实游戏简称。日志和页面截图位于 `build/shader-gallery/bundled-check/`。

安装包：`build/shader-gallery/art3m1s-bundled-shader-demo.vpk`，SHA256 `1893203a1e9977ea6198602e18d5a0a857028b5bd39fdee99d4fa62d13b1ad33`。本轮验证为 Vita3K，未将此 demo 包替换到实机。

### 渲染器与缓存验证

- 核心测试：482 通过，15 忽略（含转换、缓存失效、实际 31 个 GXP 包的读取回归）。
- `tests/external_shaders/cache_test.cpp`：四种开关组合、冷/热缓存、不同游戏隔离、源码变化、损坏恢复、目录穿越拒绝、设置保存。
- `bundled-shader-probe.once` 是显式启动诊断开关，平常不执行。实机 31/31 程序通过两组颜色/遮罩/裁剪像素断言；每个程序另记录一组空间效果图。空间图用于观察，不等价于所有参数范围的原版逐像素比较。
- `scripts/prepare-shader-bundle-test.py` 生成独立 TEST_SHADERS_31 与 TEST_SHADER_CACHE，全部测试资源在 build 下；安装也只放独立 TEST 游戏目录。
- 实机完整脚本验收：关闭转换/编译，51 次注册全部命中内置，无 shader 加载错误；灰度、反色、褐色、模糊、波浪、梯形页面成功保存并查看。停留页采样约 60 FPS。
- 外置实机测试：修改注释生成不匹配内置哈希的灰度 shader，首次转换/编译成功（约 1.91 秒）；关闭两个开关并重启后命中 Cg/GXP 缓存，没有再次加载编译器。首次核对 libshacccg 文件指纹仍有 I/O 成本，本次缓存读取总计约 1.34 秒；内置 31 项不经过这个路径。
- 真实游戏所有剧情组合和高负载多级滤镜仍需继续回归，不能由上述单项测试推断全程性能。

离线重建：`compile-external-shaders.py` 使用本机 psp2cgc（O1/no-fastmath/bestprecision）生成 AGX1；`bundle-artemis-shaders.py` 生成内置资源表和实机诊断数据。运行期未知 shader 使用 libshacccg；没有把这 31 个程序改成启动时现编译。

核心提交 `86c63dd`，补丁 `patches/artemis-bundled-and-external-shaders-core.patch` 基于核心 `c939398`。内置二进制与表同时保存在 `shaders/artemis-pc`，完整核心补丁也包含它们。当前 VPK 为 `build/external-shaders/art3m1s-bundled-31.vpk`，SHA256 `61bdcb0a70e2fc864f653fb51e50fca0674d825fc3e2a9901a7fd85b17f03739`。

本轮日志：`build/native-command-port/bundled-31-static2.log`（31 项像素）；`build/external-shaders/fixture-device.log`（51 次注册和六个页面）；`build/native-command-port/shader-cache-cold2.log` / `shader-cache-warm2.log`（外置缓存）。文件均已复制到电脑，不依赖实机日志继续保留。

默认关闭后续更新：`build/external-shaders/art3m1s-bundled-31-default-off.vpk`，SHA256 `4c974af02702c6123ca1cf2134d7a2b887d2648a20049cb001517e8a9eea0b17`。本次仅改默认值和菜单说明；ASan/UBSan 缓存与设置测试通过，Vita3K 验证无设置文件时两个开关均关闭，两页说明完整显示。截图为 `menu-default-off-convert.png` / `menu-default-off-compile.png`，位于 `build/external-shaders/`。模拟器测试后恢复原设置文件；本次尚未部署到实机。

### 51 项以外的新效果与编译器内存归属（2026-09-15）

`python scripts/prepare-external-shader-test.py` 生成 `build/external-shaders/custom-physical/game/`。依赖前述 gallery 图案、外置测试字体和提取后的 Toshiue HLSL 包装；输出仅放独立 TEST 游戏目录。新增五个实际算法：双色映射、色差偏移、暗角、带羽化边界的第二纹理混合、数组参数曲线。第六项使用不支持的 `samplerBack`，用于验证拒绝和原图回退。所有源码哈希均不匹配内置表，并非只改文件名或注释。

首次实机第三轮出现系统错误。已保留转储，主线程在文字栅格化的 `calloc` / `_malloc_r` 内访问损坏的堆链表。核对实际 VitaSDK `libvitashark.a` 后发现宿主重复释放编译结果：`shark_compile_shader_extended()` 返回编译输出持有的 `programData`，宿主却先 `free(program)` 再 `shark_clear_output()`。现在只由 `shark_clear_output()` 释放；注册函数在此前已复制 GXP。修改测试桩以模拟真实归属后，ASan 在旧代码稳定复现 double-free，修复后 ASan/UBSan 测试通过。

修复版在实机 CPU 333 MHz 上使用新目录 `TEST_SHADER_EXTERNAL6_FIXED` 完成：

| 模式 | 新编译 | GXP 命中 | 结果 |
|---|---:|---:|---|
| 首次进入，转换/编译均开启 | 5 | 0 | 六页及完成标记全部取得 |
| 重启，转换/编译均关闭 | 0 | 5 | 六页与首次截图逐字节一致 |
| 保留 Cg/元数据，删除本测试的 GXP/hash；转换关、编译开 | 5 | 0 | 六页与首次截图逐字节一致 |

第五项后仍继续绘制文字，第六项按预期拒绝，三轮均完成，未再出现系统错误。关闭转换时第六项报缺少匹配 Cg；开启转换时明确拒绝 `samplerBack`。这不代表任意 HLSL 都受支持，也不代表所有参数组合均经过原版像素对照。当前包装要求全局参数声明位于 `vs` / `ps` 函数之前。

首次冷编译约 1.83 秒（含编译器文件指纹及初始化），随后四项各约 0.61–0.64 秒。关闭开关重启后的首个缓存读取约 1.23 秒（仍需文件指纹 I/O），后续约 17–18 毫秒；这是加载耗时，不是每帧开销。

证据位于 `build/external-shaders/custom-physical/`：`crash/` 为旧版错误现场和 ASan 复现；`fixed-cold/`、`fixed-warm/`、`fixed-cg-only/` 各含完整日志、六张截图及缓存；`fixed-results.json` 为计数与截图一致性断言，`fixed-contact.jpg` 为效果总览。早期 `cg-only/` 属于失败轮次，不能作为通过证据。

已部署修复包 `build/external-shaders/art3m1s-external-shader-ownership-fix.vpk`，SHA256 `23c42d866014c5f06b8630f05b79b58f26cb5f945886270318f686b6d21c2cd6`；对应 ELF 保存在 `custom-physical/fixed.elf`。结束后恢复原先选中的 PCSG01297、删除测试临时 shader 设置（原文件不存在，恢复默认双关），返回启动器。真实游戏资源和存档未修改。

### 启动 Shader 进度（2026-09-15）

加载页按当前事件批次显示 Shader 文件名、完成数/总数、失败数和进度条，状态区分读取、内置、Cg 准备、缓存检查、实际编译。失败请求也结束当前一项；分母不是整款游戏所有分支的预测数量。实际编译仍同步执行，在每项编译之前提交加载画面，没有伪造单个编译器调用内部的进度。

只在首个游戏画面之前启用此回调，且仅在主线程、GXM scene 外绘制；不递归调用核心，不在回调中处理按键。空运行时的初始帧不再提前结束加载，必须先执行脚本更新。快速阶段最多每 100 ms 更新，首次、编译前、批次结束立即更新；进度帧不释放当前编译器，正常游戏帧恢复原有释放行为，避免每画一次进度就重新加载 libshacccg。

使用 `python scripts/prepare-shader-progress-test.py` 生成独立测试资源，五个新效果及一个预期拒绝项集中在启动批次。C++ ASan/UBSan 验证计数、失败、限频、重置及缓存/设置逻辑；核心全功能测试 483 通过、15 忽略。Vita3K 冷启动验证 0/6 到 6/6、失败 1，五项均真实编译，批次只释放一次编译器；关闭两开关后五项命中缓存、没有编译，六张结果截图与冷启动逐字节一致。截图及日志位于 `build/shader-progress/emulator/`，`intermediate-*` 为排查阶段结果，不作为最终通过证据。

内置 demo 同样验证 51/51、失败 0、没有编译，完成后正常显示效果页；整个注册批次只提交了 3 次进度画面，没有为每个内置项强制等待一帧。测试后恢复模拟器原先的 Shader 设置和上次选择记录。

核心提交 `e4c8660`；`patches/shader-loading-progress-core.patch` 基于 `86c63dd`，须与含新进度 ABI 的 Direct 宿主一起构建。安装包 `build/shader-progress/art3m1s-shader-progress.vpk`，SHA256 `b60ee10b2b3eb4fe9e4e7ee10e617c95f368d44d7a9ed21989aaf6aef29fc93d`，继续包含 game1/game2 的内置 51 页 demo。本轮为 Vita3K 验证，未部署到实机。
