# Direct GXM 宿主

游戏外置 shader 的目录、配套缓存文件与开关用法见 [Shader 放置说明](../SHADER_PLACEMENT.zh-CN.md)。

新的 PSV 宿主入口，选择菜单与游戏均直接调用 SceGxm。构建不包含
外部 UI 框架或它们的资源。游戏目录扫描、媒体桥接、
数学兼容代码及接口头文件均位于 `host-direct/src/`。旧 `host-gxm`、
`host-direct-effects` 后端及其专用构建入口已删除，需要历史对照时从 Git 历史提取。

## 构建

当前版使用 `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1`，重编译根目录 `core/`、构建宿主并生成 VPK。产物位于 `build/releases/`，每次构建保留独立文件和 SHA256 清单。手动配置 CMake 时默认链接本次核心输出 `build/native-command-port/libart3m1s_core.a`。

版本自动跟随当前分支最近的正式 Git tag；未标记改动会注明开发版本。完整版本与 PSV 系统字段的映射、环境要求和分阶段构建方式见 [构建与交付说明](../docs/BUILD.zh-CN.md)。

旧 `build-direct-all.ps1` 是历史基线入口，不用于当前版本交付。Title ID 仍为 `ART3DIR01`，现有游戏、存档和资源目录不变。

## 绘制与生命周期

- `gpu.cpp`：960×544、三个 stride=1024 的显示缓冲、无 MSAA、无深度/模板附件。
  每个矩形保留四顶点，通过六索引合并相邻兼容矩形，画序不变；premultiplied
  source-over 与 additive 混合。普通、裁剪、规则渐变、渐变＋裁剪使用四种
  专用片段 shader，普通路径没有片段 uniform。颜色与透明度按顶点保留。
- 显示：EndScene → PadHeartbeat → DisplayQueueAddEntry → 轮换 → Finish。
  回调 SetFrameBuf(NEXTFRAME) 后 WaitVblankStart。保留上一版已能实机运行的同步语义，
  没有启用曾导致实机黑屏的异步通知方案。
- 每帧 Finish 后才能复用顶点或更新纹理。场景内被替换/删除的纹理延迟到 Finish 后释放。
- `bridge.cpp`：保持 art3m1s_core 的 GXM FFI，承接图片、字形图集、视频与截图回读。
  游戏脚本与资源缓存仍由 core 负责。不会因为去掉 UI 框架就自动获得原生 eboot 的所有缓存算法。
- `menu.cpp`：stb_truetype 栅格化随包字体，512×512 菜单图集。只在 scene 外准备字形；
  游戏加载成功后释放字体文件、CPU 图集、GPU 图集和字形表；返回菜单时重新加载。
- `main.cpp`：原生按键/触摸、加载工作线程和进度条、游戏循环、缓冲日志。
  圆圈确认/下一句，叉取消；Select 无返回菜单绑定。选择菜单里的叉退出应用。

日志仍是 `ux0:data/art3m1s-gxm/host.log`，启动轮换上一份为 `host.previous.log`。
每 5 秒输出帧耗时分项；保留 `trace-nextline.flag` 开关和 core profiler 诊断。
Opt1 新增 `[gxm-perf]`：逻辑矩形数、实际 draw 数、uniform 数、普通路径矩形数，
并分别记录提交、显示队列与 Finish 等待。原来的总 present 耗时不能视作纯 GPU 时间。
`readback.hpp` 对 960 像素宽输出按行复制，其他尺寸先读入 CPU 行缓存再按原规则缩放。

GXM context、环形缓冲和显示缓冲属于整个进程，退出前排空 GPU 与显示队列，
随后由 `sceKernelExitProcess` 交给系统统一回收。`prepare_process_exit` 不能
当作进程内销毁并重建渲染器的接口。当前 Vita3K 的应用退出清理仍有已复现的
宿主崩溃，详见构建目录中的测试记录；没有修改模拟器配置或二进制。

## 验证

VPK 不附带 demo。需要验证时，将 `assets/TEST_SHADERS_51/` 复制到 `ux0:data/art3m1s-gxm/games/`，在游戏选择界面进入 **内置 Shader 演示 demo**；51 个来源文件按完整字节去重为 31 项，相同 shader 仅保留一份源码和一个演示页。每页标注 game1/game2 来源，manifest 保留全部来源映射。按 ○ 下一项、31 项后循环，按 □ 呼出菜单退出。无需开启自动 shader 转换/编译。

也可将 `assets/TEST_SHADERS_EXTERNAL/` 复制到同一 games 目录，运行 **外置 Shader 演示 demo**：五个内置库之外的新效果，加一个预期拒绝的 samplerBack 回退用例。先显示使用说明，○ 开始/下一项，六项后循环，□ 退出。首次需在启动器 START 设置中开启 Shader 自动转换与自动编译，并安装 `ur0:/data/libshacccg.suprx`；演示自身不修改全局设置。完成缓存后可关闭两个开关再进入，缺少匹配缓存且未开启时只显示原图。

外置资源位于 `ux0:data/art3m1s-gxm/games/TEST_SHADERS_EXTERNAL/`，缓存单独写入 `ux0:data/art3m1s-gxm/games/TEST_SHADERS_EXTERNAL/shader-cache/`。普通构建不打包演示资源，更新工具为 `scripts/prepare-external-shader-gallery.py`；所有源码与演示画面不含实际游戏简称。

启动加载时另显示 Shader 处理进度：读取、内置效果、Cg 准备、缓存检查与实际编译，附当前文件及当前批次的完成/失败数量。分母来自已派发的 Shader 请求，不预测后续脚本；单个编译器调用内部没有伪造百分比。快速请求限频刷新，实际编译前和批次完成后会提交进度画面。进度帧保留当前编译器，批次结束后的普通帧再释放；首个脚本更新和游戏画面完成后关闭加载状态并释放菜单字体。

内置效果 demo 的源码保留在 `assets/TEST_SHADERS_51/`，资源更新工具是 `scripts/prepare-shader-gallery.py`。

用 `-DDIRECT_GXM_PROBE=ON` 配置 CMake 可构建 `direct_probe.vpk`，Title ID
`ART3DPR01`。探针直接使用生产 `gpu.cpp` 和 shader，不读取游戏、不操作存档。

`tests/direct_gxm/check_pixels.py` 比较 11 组历史渲染画面，并独立验证透明混合、
加色、硬裁剪、规则渐变、非对齐纹理宽度和局部更新。历史旋转 NanoVG scissor
不等同于 core 提供的轴对齐 stage clip，因此明确排除该组历史对比，另测硬裁剪。

MCP 和像素探针验证功能。帧率、短帧和闪烁的结论须以同场景实体机录像及日志为准。
