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

重启后第三轮安装恢复完成：原 SELF、暂存 SELF 和最终 SELF 均校验；
最终 SELF `7f382cdabd50c4750bc8ec18c69a2acd75d5317300aa729ea57ef55d2f72593f`。
同一部署清单已补充 new_sha256；nosleep on 已重新启用，程序已启动。
候选第三轮仍保留第二轮的灰度解码优化和优先级调整。

第三轮实机结果：candidate3-startup.log 的 35 项像素自检 ok=1，无 ok=0；
candidate3-entry.log 在 at_us=65537821 确认 arm=333/gpu=111。
进入同一 otomeriron 后，自然播放 logo.mp4（约 30 fps）和循环樱花。
未改变片源或游戏 shader。候选运行中的 CPU 转换 oracle 仍通过。
candidate3-play.log 的前 5 个完整 sakura 窗口共 105 帧 / 26018 ms，
输出率约 4.04 fps（窗口中位数 3.98），明显好于第二轮约 1.78 fps，
但远低于片源 30 fps，仍未完成性能验收。约 39 ms 的 RGBA 转换本身
已经超过一帧预算；mask 和 decoder 指标仍含跳过帧的工作，不能直接
视作单个源帧成本。后续应继续针对 CPU 转换和双流解码开销，而非仅扩缓存。
统计文件 build/ogv-convert/candidate3-summary.json。已安装包保留在
build/direct-candidates/ogv-yuv444-3/art3m1s_direct.vpk，版本号未升级。

## 2026-09-12 第四轮：8-lane Q6 NEON 颜色转换

第三轮颜色转换仍约 39 ms/输出帧。原 Q16 内核将 8 像素拆成两组 32 位乘法；
新内核保持 8 路 16 位 Q6 中间结果，用 rounded high multiply 和末尾饱和
窄化。RGB 允许的差异仍为每通道最多 1/255；不缩小分辨率、不改变 shader、
帧时间戳或 alpha。旧 Q16 内核保留，运行时 CPU oracle 检查 Q6 不通过时
退回旧内核；旧内核也未通过时才退到既有 swscale。灰度转换维持原算法。

验证：
- 桌面 ASan/UBSan：合法 YUV 范围与 swscale 最大差异 1；全部 256^3
  三元组与 Q16 最大差异 1；1–65 像素宽度、不对齐和尾部写保护通过。
- 原始 sakura/sakura_m 解码后的 30 帧对照：RGB Q6 max=1，alpha 完全一致。
- 独立 MCP ARM/NEON 应用 ART3Q6T01 同样穷举通过，NEON=1；会话
  943d2da6-95ed-486b-84ec-fe71e121fc83，结果 build/ogv-convert/q6-mcp.log。
  首轮 freopen(stdout) 的探针没有有效结果文件，没有算作通过；第二轮使用
  独立 FILE 和显式断言日志，完整 PASS 后才部署。
- 后台视频的正常播放、停顿追帧、循环、退出以及延迟 mask 转换通过。
  记录 q6-recording-tests.log、q6-async-tests.log。
- 部署前恢复了临时 main.cpp/CMakeLists.txt，正式包不包含探针入口。

实机包 build/direct-candidates/ogv-yuv444-4/art3m1s_direct.vpk，
VPK SHA-256 `01ca606263292935b13bc7248a1c1b853dc58cc6a4154e216b1821241afab75c`，
SELF `c76f4a9ff4184b812af935a964c5ab023e678a2c6c1e9d02b6a01af3687b4d82`。
部署清单 build/direct-deploy/deploy-20260912-182138/manifest.json。
启动 35 项像素自检通过，性能结果待同画面 333 MHz 采样。

第四轮实测：candidate4-play.log 确认 arm=333/gpu=111；运行时 q6=1，
q6_max=1，alpha_max=0。candidate4-steady.log 的 5 个 sakura 窗口共
106 帧 / 26337 ms（约 4.02 fps），颜色转换中位数 26743 μs，较前一轮
约 39 ms 降低，但总播放率没有明显改善。存在一个音频工作超过预算的窗口，
output_errors=0；不得宣称不存在音频卡顿。

## 第五轮：遮罩解码与颜色生产并行

对支持 frame threads 的配对 Theora mask 使用 2 个内部解码线程，保留
GRAY 和时间戳匹配。提前解码下一帧，与颜色转换和另一路解码重叠，减少
输出线程逐帧同步等待。音频线程、主线程和 CapUnlocker 策略不变。
新增 mask 线程仍使用已验证的 Theora buffer callback 及优先级 159。

桌面和实际 Vita FFmpeg 库（MCP 会话 08011b5d-9b4f-4d77-9e60-d3edda6f594f）
均确认 300 帧亮度和 PTS 与单线程普通解码完全一致，gray_threads=2，
frame_threaded=1。日志 mask-pipeline-desktop.log、mask-pipeline-mcp.log。
后台播放、停顿追帧、循环、取消及 mask 延迟转换的 ASan/UBSan 测试通过，
记录 mask-pipeline-async-tests.log。临时探针入口已恢复，正式包继续使用
游戏入口。性能与音频影响仍需实机验证。

第五轮实机：读盘正常的窗口约 7–9 个视频帧/秒，但主线程 media/upload
长帧也明显增加；另有数秒级 av_read_frame 等待，不能全部归为解码。
第六轮将内部 codec worker 从 159 调到 161（低于主线程 160），生产线程
仍是原来的 159。实际四个 codec worker 的日志均为 161；主渲染恢复约
60 fps 时，视频却可能因一直追帧而不再输出，故不能以 UI FPS 验收。
第六轮证据 candidate6-steady.log；第五轮 candidate5-steady.log。
用户是否有其他后台传输尚未确认；不要把读盘异常直接归因于用户下载。

## 第七轮：限制追帧耗时与当前循环文件缓存

- 只允许在距上次入队不足一帧时继续丢弃过期图片。解码持续落后时也会
  定期转换、提交已解码的图片，防止一直追赶而饿死显示；时间戳仍保持原样。
  最后帧判断增加半帧余量，覆盖帧间隔取整误差。
- 新测试人为让每次颜色帧解码多耗 80 ms，明显慢于 30 fps 片源。
  同样条件下，旧条件的独立副本显示 6 帧、丢弃 39 帧，触发 uploads>=30
  断言；新条件显示 37 帧，保持循环、PTS 单调及正常退出。
  记录 bounded-catchup-tests.log 和 unbounded-catchup-regression.log。
  旧副本只在 build/ogv-convert/unbounded-regression 内，未替换工作树。
- 小型、静音、循环 Theora 视频在打开时预载压缩数据，颜色与配对 mask
  共享 4 MiB 上限。sakura 两文件合计 1,863,915 字节，全部缓存后关闭其
  磁盘描述符；停止视频时释放并从 media 账本扣除。不改变全局资产缓存额度，
  不预载整个游戏。大文件、分配失败或读取失败保留原来的磁盘流式路径。
- 预载不改变 AVIO 位置或缓冲；通过预算不足、读失败回退、重复 seek、EOF、
  包内容/PTS/DTS/flags 对照及退出释放测试。实际 ARM/MCP 在
  7674e0c5-0bc4-4c3a-9344-e6ed0af5f949 中对两文件各循环三遍：各 900 包
  完全一致，no_disk_after_preload=1。记录 loop-cache-mcp.log。
- 第七轮正式包不含探针入口，SELF/VPK 都经过安装回读验证：
  VPK `468e7838730b0c9e84493aee621009e82a1c3def29893ebcdad98e34f626e368`；
  SELF `d85d62239be198c7527df1d69a22466f05e4a65952b8e786af893f2947c0bfc9`。
  部署清单 build/direct-deploy/deploy-20260912-184808/manifest.json。
  启动 35 项像素自检通过；shader、片源、分辨率、音频设置、版本号不变。
  实机播放率仍待最终采样确认。


第七轮最终实机采样：candidate7-full.log 确认 arm=333/gpu=111，Q6 oracle
通过（RGB 最大差异 1，alpha 精确一致）。筛选樱花开始后 4.9–6 秒的完整
采样窗口，共 54 个，累计输出率 10.257 fps，窗口中位数 10.342 fps；解码
等待/提交累计耗时中位数 38871 μs/输出帧，读包约 432 μs/输出帧。
记录 candidate7-summary.json。颜色转换仍约 30–32 ms/输出帧，mask 约
15 ms/输出帧；这些是有线程争用时的墙钟耗时，不能作为纯 CPU 指令成本。
动画仍慢于源文件 30 fps，PTS 落后继续增长，不能宣称性能问题已解决。

第八轮仅增加 [video-loop-cache] 的五秒累计计数，颜色/遮罩分别报告容量、
缓存读取次数和磁盘读取次数。原 [media-cache] INFO 被 Direct 的 FFmpeg
日志过滤器丢弃，因此第七轮没出现 ready 日志本身不能证明缓存未生效。
计数由解码生产线程读取；不新增线程、不改变解码、shader、画面或缓存预算。
ASan/UBSan 后台播放/取消/追帧/缓存与 seek 对照再次通过，记录
cache-report-tests.log；正式包编译记录 cache-report-build.log。

第八轮已安装并完成回读，清单 deploy-20260912-190052/manifest.json：
VPK 148ca63375f1682c5ed9572d142ec1bd1e41574b51cda21e299df03547d68fac；
SELF 60c2cad5b5adc8ecdb165f22082df0ab84e6f678f8ed57665940eaf4cc9f9497。
candidate8-startup.log 未出现 ok=0；candidate8-entry.log 确认 333 MHz。
logo.mp4 的三段采样分别 151/5006、150/5004、149/4984（帧/毫秒）。
随后日志停留在运行时间约 120 秒的 Artemis 图像阶段，多次 FTP 取样未更新。
已询问设备是否前台，尚不能确认暂停原因；第八轮实际缓存命中仍待实机取证。
不要将独立 MCP 的缓存测试结果冒充此轮实机游戏中的命中结果。
