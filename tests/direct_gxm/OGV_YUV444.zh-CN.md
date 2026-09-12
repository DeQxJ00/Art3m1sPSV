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

诊断候选已通过 Vita3K 启动及独立日志持续落盘检查；会话
`456d4800-286e-4c12-ad89-cfba150193d4`，证据
`build/ogv-convert/hang-watch-mcp.log`。写入后关闭文件以避免模拟器
文件缓存使运行中读到空日志。尚未部署实机，等待前台状态确认。

## 2026-09-12 诊断包实机部署

用户要求安装实机：已部署 `otome-hang-watch` 并启动，SELF 回读
`6b782e574f383bbe72df015aae4d464598818e7aff4869f523132e263ff63e69`。
备份及清单 `build/direct-deploy/deploy-20260912-171726/manifest.json`。
启动 retained/local-base/overlay 自检通过，333 MHz；独立
`hang-watch.log` 持续记录主线程阶段和等待状态，启动器 epoch 正常前进。
证据为同目录 `startup-host.log`、`startup-hang-watch.log`。
已开启 VitaCompanion nosleep；等待复现 Artemis 标志后的卡点，尚不宣称修复。

## 2026-09-12 用户澄清：OGV 只有约 2 fps

用户明确说明并非卡死，而是 OGV 极低帧率。本轮按播放性能继续处理。
实机完整日志 `build/ogv-convert/real-ogv-20260912-174026/host-full.log`
实际包含持续播放的 sakura；49 个完整采样窗口的中位数：2.1219 fps，
主视频解码 299 μs/帧，读包 659 μs/帧，prepare 416794 μs/帧，
上传 30318 μs/帧。prepare 内含配对遮罩的解码及灰度转换，不能把全部
prepare 时间都归为 Theora 熵解码。统计在同目录 `sakura-baseline.json`。

用户确认安装后，已将此前通过 MCP 实际 ARM 像素自检的同一
`ogv-yuv444-1` 包装回实机，未改包：部署清单
`build/direct-deploy/deploy-20260912-174245/manifest.json`，SELF 回读
`2ab24d6cbc3fc8aa3c4749d8ccbe39c95cdde006ebb71d14caee8b1b020ebf68`。
普通版 Vita3K 当时正在运行游戏，因此未再次启动 MCP 游戏占用同一目录。
启动自检通过；最初为 500 MHz，用户调回 333 MHz，进入 otomeriron 后
`profile-state` 已确认 arm=333、gpu=111。等待这份包的樱花采样，
不能把新包最初的 500 MHz 数据与旧版 333 MHz 直接比较。

## 2026-09-12 实机追帧瓶颈与第二、三轮候选

第一轮快速转换确实启用了 NEON（实际库的 CPU 像素 oracle RGB 最大误差 1，
alpha 完全一致），但 333 MHz 下仍只有约 1–2 个新视频帧/秒：
`build/ogv-convert/fast-steady.log` 中 8 帧 / 6416 ms 的窗口，
颜色转换约 43.8 ms，mask 约 426.4 ms。decode/prepare 均按输出帧归一化，
包含追赶时解码的多帧，不能解释成单个压缩帧的解码成本。

第二轮 `ogv-yuv444-2`：
- 仅配对 Theora 遮罩设置 AV_CODEC_FLAG_GRAY，VP3 render_slice 跳过未使用的
  U/V 重建；保留所有熵解码和 Y。未全局开启 CONFIG_GRAY，未改变其他 codec。
  通用 FFmpeg 初始化仍可能打印配置未启用 gray 的提示，局部 VP3 分支已生效。
- Theora 内部 frame worker 的默认 Vita pthread 优先级 191 调整为 159，
  与输出线程一致；保持 CapUnlocker 的已有亲和性策略和音频线程设置。
- `tests/video_convert/mask_decode.c` 对原始 sakura_m.ogv 全部 300 帧比较
  普通与 GRAY 路径的 Y 和 PTS。桌面以及使用当前 patched Vita libavcodec
  的独立 MCP 应用 ART3MSK01 均完全一致。MCP 会话
  ecc16e7b-f211-43a2-893d-20238920d5d3 自然结束，无崩溃。
  日志 `build/ogv-convert/mask-gray-mcp.log`。独立应用只读游戏素材，
  不修改普通 Vita3K 正在运行的应用、日志或存档。
- 修改后的 `vendor/ffmpeg/libavcodec/vp3.c` 已重新构建至
  build/ffmpeg/libavcodec/libavcodec.a，再复制到 build/media-sdk/lib/ 链接。
  原归档保存在 build/ogv-convert/libavcodec.before-mask-gray.a。
- 实机 SELF b78e5e92ca381e34fdbfa1672b82fb5bc9f6b429385257303577244849ed4c27，
  VPK 5a9a670196260b9bae7bb155a432e138bfa89cda01d17dff187ca6fba923b54f。
  部署清单 build/direct-deploy/deploy-20260912-175430/manifest.json；
  启动像素自检通过，profile-state 确认 arm=333。
- 结果仍未解决：candidate2-fourth.log 中多窗口约 1.6–2 fps，
  mask 每输出帧约 180–236 ms，颜色转换约 38 ms。
  logo.mp4 实机 h264_vita 约 30 fps；不能把渲染循环 FPS 当作 OGV 更新率。

第三轮 `ogv-yuv444-3` 继续修正追帧：跳过过期颜色帧时，同时推进配对遮罩
解码，但不转换、不上传；避免遮罩落后后集中补帧，又令颜色落后的循环。
时间戳、片源、分辨率、透明度公式不变。保留最后遮罩帧供下一次实际转换。
新增变化遮罩测试比较逐帧转换与每三帧延迟转换的结果，并覆盖 EOF，
与正常、停顿追帧、循环、取消测试一起通过 ASan/UBSan。
测试日志 build/ogv-convert/paired-catchup-tests.log。
第三轮性能须以实机部署后日志判定，尚未验收。

第三轮 VPK SHA-256：
`c799b30d713d3bd2a8b6a3f78060f755678ea641811085c670b9221b6619ac03`。
本轮 patched libavcodec.a SHA-256：
`f5e284f6014486095afcc641de4181cb2fa201e111e936574dc43213c60a70ce`。
第二轮 9 个完整窗口的视频输出率中位数 1.7832 fps，不能作为修复成功。
第三轮首次部署（deploy-20260912-180317）备份完成、staged SELF 校验通过，
但 kill 返回 Killed 后 FTP 仍拒绝重命名/删除旧 SELF，实际尚未替换。
已发出设备 reboot 释放占用，后续需核对安装回读与实测结果。
