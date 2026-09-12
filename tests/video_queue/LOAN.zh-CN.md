# 视频队列预留写入与借用读取

基于 GXM 转换首版（根仓库 `9a3e680`，实际 core `0c9c373`）。本轮不改 shader、GPU 等待、core 或游戏素材。

## 所有权规则

原链路：解码平面 → 打包暂存 → 复制到队列 → 复制到消费暂存 → 复制到 GXM 暂存。

本轮：解码平面 → 直接打包到预留队列槽 → 主线程借用该槽 → 复制到 GXM 暂存。

- 单生产者、单消费者、三个槽。预留的尾槽在 commit 前不可见，消费头部不会改变它对应的尾索引。
- acquire 后的头槽仍计入占用，主线程完成同步上传后才 release。上传期间不持有队列 mutex，生产者可以填其他空槽。
- stop 唤醒等待空间的生产者，尚未提交的写入不再发布；已经借出的数据在 release 前仍有效。
- 上传失败也先 release，再 join/关闭；GPU 仍等待转换完成后返回，未引入异步 GPU 借用。
- CPU RGBA 路径仍需复制入队，但消费端同样借用。原 copy push/take 包装保留，用于兼容和混合路径检查。
- 删除独立消费暂存（960×540×4 = 2073600 字节），队列由原统计 4 帧内存降为 3 帧。原 RGBA 回退区仍保留，不宣称完全零复制。
- `color_us_per_frame` 在预留写入路径包含等待空槽的时间，不再是纯粹打包成本；最终比较以视频输出帧率和整段主线程耗时为准。

## 验证

- 桌面 ASan/UBSan：20×10000 帧并发压力，交替 copy push / reserve write，消费端持有指针并多次让出执行仍不能被覆盖；检查 PTS、kind、丢帧、EOF、stop/join。
- 独立检查未 commit 的帧不可见、预留期间消费早帧后仍正确发布、stop 取消写入但不撤销已借出的帧。
- `tests/video_async/run.sh` 全套通过：两路 Theora 循环、颜色/alpha、主线程上传、时间戳、追帧、取消、缓存以及遮罩 EOF。
- Vita 线程探针 `tests/video_queue/vita_probe` 使用 ART3VQ001；4×1000 帧并发、上述边界检查通过。MCP 会话 `7d3f44cf-477e-4624-a905-d2e56f443468`，最终 stopped，无 crash。日志 `build/video-queue-loan/probe-mcp.log`。探针没有绘制画面，因此 fps=0 正常，不是游戏性能指标。
- 探针最初 stdout 重定向停在日志写入，改为独立文件打开/关闭；忙轮询版本线程调度过慢，加入空队列短暂休眠，并限制 Vita 探针压力规模。未把这些未完成轮次报告为通过。正式游戏仍使用原有帧循环/优先级。

构建使用原 `build/video-yuva-gxm/libart3m1s_core.a`；根 `host/video.c` 与 `host/video_queue.h` 是 CMake 直接引用的源码，没有其他 host 镜像需要同步。

## 候选包

`build/direct-candidates/ogv-yuva-queue-loan/art3m1s_direct.vpk`

SHA256 `b2ecd571e74936f763a92ed711b902f616a1422c74fcbc522366408654bb5f52`。
备份及安装清单 `build/direct-deploy/deploy-20260912-213201/manifest.json`。
实机 SELF SHA256 `99c42101d94d91190ae4db8f34f3532f3c3ddb4f05337b34005d10ef325e47b8`。
首次 FTP 覆盖被拒绝，旧文件仍在；重新 kill 后，以旧文件哈希和暂存文件逐字节校验继续安装，最终 SELF/SFO 回读通过。恢复脚本 `build/video-queue-loan/resume-deploy.py`。启动无 `ok=0`，otomeriron 确认 333 MHz。

安装前即时日志 `baseline-before-install.log` 是 MP4 播放片段，统计约 30 fps，**不能当作樱花 OGV 基线**。应参考上一轮已确认的 GXM 樱花 13.28 / 13.20 fps，或重新取得同场景对照。

## 第一轮实机结果与计数口径

`hardware-steady.log` 确认 `loan=1`、队列 6220800 字节、GXM 自检成功。7 个 producer 窗口共提交 1050 帧 / 35128 ms，约 29.89 fps；但 GXM 实际转换仍约 68 次 / 5 秒，即约 13–14 fps。主线程追不上时会丢弃已过时队列帧，因此 **29.89 是生产者吞吐，不能报告为实际显示 FPS**。

时间戳落后保持约 0.18–0.22 秒，较旧版持续累积数秒明显改善；循环磁盘读取仍 4/4。GPU 暂存与前置等待合计中位数 36.652 ms，转换及等待 11.554 ms。暂存/等待合项不足以决定是否需要缓冲轮换，因此下一包仅增加分项计时，以及 `video-consumer` 的实际完成上传数，先定位而不撤销同步保护。
