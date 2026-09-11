# 2026-09-11 OGV YUV444 转换与循环后台播放

目标是 otomeriron 的 `movie/sakura.ogv` 和自动配对的 `sakura_m.ogv`：
两者均为 960×540、30 fps、10 秒、Theora YUV444。游戏
`system/ui/title.lua` 将其放在 `500.z.mv` 并请求循环播放。

旧实机日志 `build/direct-deploy/deploy-20260911-131316/host.log` 中，
樱花区段约 5.2 秒处理 11 帧，prepare 约 416 ms/帧，内含 mask 约
136 ms/帧，上传约 32–40 ms/帧。这些是该区段的耗时，不代表全部 OGV。

## 实现

- 在 CPU 上用 ARM NEON 转换未缩放的 YUV444，保留旧 swscale 默认的
  BT.601 limited 系数。遮罩 Y 经 limited→full 映射后作为 alpha，与
  RGBA 一次写出；不能直接把原始 Y 字节当 alpha。
- 其他像素格式继续走 swscale。第一次使用时用实际链接库执行内存像素
  对照；RGB 误差超过 1/255 或 alpha 有差异则退回 swscale。
  该自检不读屏幕，不受性能浮窗影响。
- 静音循环 Theora 也启用现有后台队列；三个排队帧及一个消费帧共
  7.91 MiB（960×540 RGBA），整体仍有 16 MiB 上限。
  循环累计输出时间戳，遮罩使用循环内时间；退出先停止并 join 工作者。
- 增加颜色转换耗时；后台的 upload 指标是等待队列及复制，实际主线程
  上传总耗时在退出时的 video-async 行记录，不能混为 GPU 上传耗时。
- 原资产、Direct shader、核心库、堆及图片缓存额度均未改。

## 验证及限制

- `tests/video_convert/run.sh`：ASan/UBSan、边界、全部合法 YUV 值、
  256 种遮罩亮度通过。樱花前 30 帧 RGB 最大误差 1/255，平均误差
  0.000020，alpha 完全一致。输入素材留在 build 目录，未覆盖游戏文件。
- `tests/video_async/run.sh`：正常播放、消费者长暂停后追帧、阻塞队列
  取消，以及跨两轮以上循环的单调时间戳和退出回收通过。
- `tests/video_queue/run.sh`：时间戳、末帧、容量上限、停止/join 通过。
- Vita3K 会话 `8736692b-d2c8-4b35-ad97-a9777ca43072`：实际 ARM NEON
  自检 `enabled=1 rgb_max=1 alpha_max=0`；标题樱花区段 150 帧/5019 ms，
  prepare 8381 μs/帧。截图及日志在 `build/native-five-v125/ogv-title-first.*`。
  这是模拟器结果，不能当作 PSV 实机帧率。日志在约 6.9 秒后记录该层关闭，
  所以此场景本身未覆盖循环边界；循环边界由独立测试覆盖。

包 `build/direct-candidates/ogv-yuv444-1/art3m1s_direct.vpk`：
SHA-256 `ba44453268861263a221fa81f6654725d03aa1b47155d50ceab491a2babc3550`；
SELF `2ab24d6cbc3fc8aa3c4749d8ccbe39c95cdde006ebb71d14caee8b1b020ebf68`。
13:46 已部署 192.168.1.50，回读验证通过，原包备份及清单位于
`build/direct-deploy/deploy-20260911-134612/`。版本号不变。

实机启动日志已取到；等待用户进入 otomeriron，自然播放标题入场樱花，
再记录色彩、遮罩、上传耗时和可见效果。尚未宣称实机达到 30 fps。
H.264 OP、其他非 YUV444 视频和游戏文字属于后续回归检查。

## 实机卡住反馈与对照（同日后续）

用户反馈实机在 Artemis 静态标志之后、标题之前卡住。首份日志已保存到
`build/ogv-convert/hang-135559/host.log`，完整文件及原 SELF 另见
`build/direct-deploy/deploy-20260911-140014/`。MP4 已正常结束，最后主线程
记录约 431.996 秒；之后音频线程仍有记录，但没有 `playing sakura.ogv`。
不能据此认定 Theora 队列死锁，也不能把这份包当作实机验收通过。

尝试恢复旧包时 FTP 拒绝重命名/删除仍存在的 eboot，原程序未被删除。
重启 PSV 后，14:04 成功恢复 native-compat-7，回读 SELF 为
`c5f102f5f8a0e83fe28e3b68d3f17cb41401033da1a758d03313196b391135a7`；
清单 `build/direct-deploy/deploy-20260911-140443/manifest.json`。
随后用 VitaCompanion 的成对 press/release 进入 otomeriron。旧包日志也在
Artemis 标志附近停止刷新，最后约 98.829 秒，仍无 sakura 播放记录；
已询问用户是否一直处于应用前台，确认前不能把旧包结果判为同一死锁。

准备了独立 `otome-hang-watch` 诊断候选：沿用旧 video.c，增加独立内核线程
记录最后主线程阶段、状态、等待类型/ID 和运行时钟到 `hang-watch.log`。
源码和重建脚本位于 `build/ogv-convert/hang_watch.hpp`、
`prepare-hang-probe.py`；只修改临时构建副本，打包后恢复了 main.cpp 和
CMakeLists.txt。不修改生产 shader 或资产，不把诊断包作为性能候选。
Vita3K 验证后才可部署；实机目前保留 native-compat-7 对照包。
