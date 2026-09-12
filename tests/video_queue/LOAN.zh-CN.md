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

## 分项计时与安全重排

计时包 VPK SHA256 `fb614ce4ac83a5ab0526ea363191721de10c1f06b62d6633b487a7c2914289d3`，SELF `a198b6f923a5ea248cadf4c19d8f05c3e36a7f87f43930faa8eedeb6059972e3`，清单 `deploy-20260912-214112/manifest.json`。

`profile-steady.log` 的 10 个实际消费窗口为 683 帧 / 50365 ms，即 **13.561 fps**。上传中位数 48.468 ms，其中等待旧显示 23.472 ms、准备资源 0.003 ms、平面复制 13.232 ms，另有约 11.6 ms 转换及完成等待。末窗口消费采样时钟与所选 PTS 仅差 20.372 ms；不能把生产者准备阶段的 late 指标当成当前显示时间戳延迟。

据此下一候选只重排：先复制私有 YUV 暂存，再等待旧显示，最后写 RGBA 输出。安全依据：每次转换在发布前都 `sceGxmFinish`，上次转换对 YUV 暂存的读取已经结束；普通显示只持有 RGBA 输出，且改写输出前仍保留 `wait()`。尺寸变化和 release 仍先等待再释放。不删除输出 fence，不声称已经做了纹理轮换。

GPU 探针额外开启 deferred display，让下次复制确实面对尚未完成的前次 RGBA 绘制；36 次动态更新后原尺寸屏幕对照最大差异 1、平均 0.0166015625、超过 1 的像素 0。MCP 会话 `09b18842-7dea-4675-ba41-a1b26a1d049c`；图片 `build/native-five-v125/yuva-overlap-screen.png`，数据 `build/video-queue-loan/probe-overlap-pixels.json`。屏幕捕获 helper 顺手复制的普通游戏 host.log 不属于该探针证据，应使用单独保存的 `probe-overlap-mcp.log`。

该 GXM 独立探针在像素对照完成、退出后，Vita3K 宿主报 0xC0000005；先前未重排版也有同样的退出异常。给探针增加正式 host 使用的显示队列排空与显式进程退出后，会话 `a785b413-62bd-4f2c-8e8b-68faba15a9ee` 仍在输出 `GPU and display drained` / `DONE` 后异常退出。其屏幕对照仍为最大误差 1，但不能宣称整个 GXM 探针退出流程通过。队列专用无绘图探针则正常 stopped；两者不混为一谈。

重排候选 `build/direct-candidates/ogv-yuva-queue-overlap/art3m1s_direct.vpk`，SHA256 `0201ff95924b4dcd3c08ebad156486ef59229bda58ebb4bc0040767015ba20da`。
实际部署清单 `deploy-20260912-214819/manifest.json`，SELF SHA256 `7c17c99678d130b36ce2c179f17cb501dedfd496f1f25b4f0d29758d80009b04`。传输前已主动停止游戏，旧日志完整备份；SELF/SFO 均回读校验，启动无 `ok=0`。

## 重排版实机初测

用户中途手动重启过，先前尚未到 OGV 的样本不用于性能比较。重启后确认 333 MHz、视频自检 RGB/alpha 最大误差均为 0。

`overlap-hang.log` 的文件名来自当时的疑似卡住反馈，**不代表已经证实死锁**。随后用户确认“好像进去了”“帧数有提高”，该现场日志也显示仍持续绘制：5 个消费窗口共 413 帧 / 25193 ms，**16.393 fps**，core 独立渲染窗口为 16.6 fps。相对计时包 13.561 fps 提高约 21%。等待旧显示中位数降到 10.088 ms，复制仍约 13.247 ms，主线程上传从 48.468 ms 降到 35.307 ms，符合复制与旧帧绘制重叠的预期。

收到最初“卡死”的反馈时已保存日志并 kill，但还没替换或回退；用户随即确认恢复且帧数提高，因此保留同包重新启动。不能把未定位的启动停顿宣称为已修复，也不据此认定新的稳态转换路径死锁。源动画仍为 30 fps，实际呈现约 16 fps，性能问题尚未完全解决。
