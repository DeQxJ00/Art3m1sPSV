# Direct GXM 宿主

新的 PSV 宿主入口，选择菜单与游戏均直接调用 SceGxm。构建不包含
Borealis、NanoVG、Yoga、GLFW 或它们的资源。`host-gxm` 和旧 vendor 目录
保留用于历史回归对照；新宿主只复用其中与 UI 框架无关的游戏目录扫描、
媒体桥接和数学兼容源文件。

## 构建

当前固定为 01.02／Opt2 基线，详见 [基线说明](../baselines/01.02/README.zh-CN.md)。
`scripts/build-direct-all.ps1` 使用当时归档的 core 和现有媒体库、shader 头构建宿主，
不会自动重编译当前 core 或 shader，也不会重新生成媒体库。

依赖已构建时，在 WSL 项目目录运行 `bash scripts/build-direct-host.sh`。
修改 shader 后先运行 `scripts/build-direct-shaders.ps1`；仓库中的 `shaders.hpp`
是离线生成产物，运行时不需要 Sony 编译器或 vitaShaRK。

输出：`build/direct-01.02-host/art3m1s_direct.vpk`，Title ID `ART3DIR01`。
这是独立安装项，名称 **art3m1s Direct GXM**。
仍读取 `ux0:data/art3m1s-gxm/games` 和原来的 `saves`，无需重新复制游戏。

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

用 `-DDIRECT_GXM_PROBE=ON` 配置 CMake 可构建 `direct_probe.vpk`，Title ID
`ART3DPR01`。探针直接使用生产 `gpu.cpp` 和 shader，不读取游戏、不操作存档。

`tests/direct_gxm/check_pixels.py` 比较 11 组历史渲染画面，并独立验证透明混合、
加色、硬裁剪、规则渐变、非对齐纹理宽度和局部更新。历史旋转 NanoVG scissor
不等同于 core 提供的轴对齐 stage clip，因此明确排除该组历史对比，另测硬裁剪。

MCP 和像素探针验证功能。帧率、短帧和闪烁的结论须以同场景实体机录像及日志为准。
