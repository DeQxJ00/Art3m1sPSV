# OGV 的 GXM 颜色与透明度转换试验

基线是 `ogv软解初步版`（606a72e）。原始 OGV、Theora 解码、游戏的 sprite / builtin shader 保留；本次只新增视频专用 YUV444 + mask Y → RGBA shader。

## 数据与生命周期

- 解码线程推进颜色与 mask 到相同 PTS，打包四个 8 位平面。队列同时传递帧种类，避免 CPU 回退帧被误认为平面数据。
- 主线程在渲染场景外复制平面到可供 GXM 读取的暂存区，绘制到可复用的 RGBA 纹理。覆盖暂存区与输出前等待旧 GPU 读取完成，发布前等待转换完成。
- core 的 shared-video 接口不扫描或保留 CPU 像素副本；host 同步接管实际完成的 RGBA 纹理。已有游戏绘制与特效继续使用该 RGBA 纹理。
- 只在有新视频帧时转换。关闭视频后释放平面暂存、render target、sync 与 CPU 回退区；输出纹理由 core 原有删除回调释放。
- 首次打开视频做私有离屏像素自检，RGB 最大误差须 ≤1，alpha 必须完全一致。失败自动使用原 CPU 路径。`ux0:data/art3m1s-gxm/video-yuva.off` 存在时也使用 CPU，开关仅在打开视频时读取。
- 本候选仅处理符合宽度对齐限制的 YUV444；其他格式保持 CPU 路径。仍有原队列的整帧复制，没有宣称完整零复制或 Theora 硬解。

## 构建与 core 来源

实际 core 源码为 `build/heap-audit/controls-source`，基点 `5cb27c4`，本次提交 `0c9c373`。根目录 `core/` 不是当前包的构建来源。可用 `patches/video-yuva-core.patch` 在对应基点重放。

1. `powershell -File scripts/build-video-yuva-shader.ps1`：使用本机 psp2cgc，独立生成 video shader。
2. `powershell -File scripts/build-video-yuva-core.ps1`：生成 `build/video-yuva-gxm/libart3m1s_core.a`。
3. 同步 `host-direct/src` 到既有 `build/async-loader-host-source/src` 构建镜像，配置 CMake 的 `ART3_DIRECT_CORE_LIBRARY` 为上述 archive，再构建 `art3m1s_direct.vpk-vpk`。本次实际命令保存在 `build/video-yuva-gxm/build-host.sh`。

本次 core archive SHA256：`ed72c5e7f2b3fa295e84943cb7847d6edfeb26338c22d3d540f20f64cc129265`。

## 自动检查与独立 GPU 探针

- core：457 passed，0 failed，15 ignored；启用 `gl-backend,gxm-builtin-effects`。新增验证共享视频无 CPU 副本、ID/revision、普通上传回退、无效长度不改变状态。
- `tests/video_async/run.sh`：ASan/UBSan，包括两路循环、PTS、取消、GPU 回调必须在主线程，以及旧 CPU 路径。
- `tests/video_queue/test.c`：ASan/UBSan 并发压力验证帧种类与像素/时间戳配对，不因丢帧错配。
- `tests/direct_gxm/video_yuva_probe/` 构建独立 ART3YUV01，未替换普通 Vita3K 游戏。MCP 每次调用前健康检查。
- 原尺寸 96×64 CPU/GPU 并排截图：通道最大差异 1，平均差异 0.0166015625，超出 1 的像素为 0。截图 `build/native-five-v125/yuva-native-screen.png`，数据 `build/video-yuva-gxm/probe-native-pixels.json`。
- 放大过滤对照最大差异 2，8 个像素超过 1；不把此结果伪报为严格逐像素通过。原尺寸对照用于隔离过滤舍入误差。
- Vita3K 的离屏显存 CPU 回读不可靠，读出零值；不能作为转换像素的正确性证据。运行时自检会因此保守回退，实际 GPU 转换通过独立探针屏幕截图验证；实机自检另行记录。

## 部署候选

`build/direct-candidates/ogv-yuva-gxm-1/art3m1s_direct.vpk`

VPK SHA256：`17d4c1567e488419206334505fe8ef728b70791fb13af30359c163d0b0f9cfdc`。
备份及逐文件回读记录：`build/direct-deploy/deploy-20260912-210729/manifest.json`。

`video-perf color_us_per_frame` 在新路径下是平面打包时间，不再表示 CPU 颜色转换；GXM 时间见 `video-yuva-gxm`。比较需同时看视频输出 FPS、上传耗时、准备耗时，不能只拿单项替代总帧率。

## 实机第一轮

2026-09-12，CPU 333 MHz，GPU 111 MHz，CapUnlocker 开启。启动检查通过，专用视频自检 `rgb_max=0 alpha_max=0 enabled=1`，实际樱花日志确认打包 YUV444 + mask Y。

`hardware-gpu-final.log` 的 10 个完整窗口共 668 帧 / 50306 ms，13.279 fps；解码墙钟耗时中位数 41.476 ms/帧，准备 15.827 ms/帧。GXM 暂存及前置等待中位数 24.586 ms，新转换绘制与等待 11.654 ms。这些阶段可与后台解码重叠，不能简单相加当作总帧时间。

两路压缩缓存磁盘读取仍为 4/4，循环命中持续增长。颜色与 alpha 自检正确不等于全游戏视觉验收；仍需用户观察实际边缘和循环。当前尚未达到源文件 30 fps。

同包 CPU 对照：设置 `video-yuva.off` 后重启，并核对日志 `enabled=0 flag_result=00000000`；333 MHz。`hardware-cpu-final.log` 的 8 个窗口为 390 帧 / 40433 ms，即 9.646 fps，准备阶段中位数 46.685 ms。相对首轮 GXM 约提升 37.7%，但这是短时顺序对照，仍需恢复 GXM 复测验证。CPU 转换约 31 ms，mask 约 15 ms；额外 GXM 暂存/等待使最终 FPS 收益小于 CPU 准备阶段的降幅。

VitaCompanion 的 `Killed/Launched` 回复不等于程序已经完成重启。首次 CPU 切换的启动紧跟 kill，未取得新启动日志，因此丢弃该次样本；再次单独启动后才开始有效 CPU 轮。恢复脚本已在 kill/launch 间等待 4 秒，仍以新日志为准。

恢复 GXM 的第三轮确认新启动、333 MHz、自检 RGB/alpha 误差均为 0、`enabled=1`。`hardware-gpu2-final.log` 的 5 个完整窗口为 333 帧 / 25236 ms，即 13.195 fps，复现首轮提升。准备中位数 16.348 ms、解码 43.446 ms，GXM 暂存/前置等待 24.205 ms，转换/等待 11.663 ms。用户实机观察为“高了2帧”，与此前已接受的约 10–11 fps 基线相比收益有限；不能只选择较慢 CPU 对照轮宣称大幅提升。

结束时实机保留 GXM 开启的新包，CPU 对照标志已删除。后续优先减少平面打包→队列→消费缓冲→GPU 暂存的整帧复制，再研究输出轮换减少等待；这轮尚未实现这些改造，也没有证明等待可直接删掉。
