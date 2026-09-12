# otomeriron 标题前停顿：录像与独立阶段诊断

2026-09-12。用户录像 `Z:/Record/OBS/2026-09-12 22-07-48.mp4`，47.783 秒、1280×720、60 fps。
分析截图保存在 `build/startup-hang-video/contact.png`（每2秒）和 `ending.png`（29秒起每0.5秒）。

## 已观察到的顺序

- 前半段品牌 MP4、广告、注意事项仍推进。
- 约30.5秒注意事项淡出时，剧情存读档按钮和右侧按钮显露。
- Artemis 标志随后仍正常淡入、停留、淡出；此时不是“按钮一出现就卡住”。
- 约38秒以后停在黑底剧情按钮画面，录像末尾仍未出现樱花标题；性能浮窗数字也不再变化。
  浮窗残留的60 FPS不能用于证明游戏此刻仍在绘制60帧。

`build/video-queue-loan/overlap-final.log` 的旧停顿现场最后主循环记录在
171752545 us，最近脚本快照为 `system/script.asb` 指令109、stack_depth=5、
`Stop(reason=trans)`。没有 Sakura playing 标记。快照在停顿前取得，不能当作卡住时的调用栈。

22:14另一次只读备份位于 `build/hardware-logs/20260912-221402-companion/`。
该次 host.log 已进入有语音和其他动画的剧情，是之后成功运行的会话，不能混入上述停顿采样。
取样时应用仍在继续写入，日志文件哈希仅标识下载得到的快照。

## 剧情按钮重新出现的脚本线索

只向 `build/startup-hang-video/assets/` 提取相关四个文件，未修改游戏目录。
当前归档对应文件与9月11日分析快照一致。

1. `system/image/boot.lua:brand_logo()` 调用品牌脚本后删除 `init.mwid`（1.80）并 flip。
2. 广告结束的 `system/extend/user/cm.lua:cm_exit2()` 调用 `init_adv_btn()`，然后
   `gotoScript{file="brandlogo",label="cm_exit"}`。
3. `init_adv_btn()` 通过 `csvbtn3("adv", game.mwid, ...)` 重建1.80下的按钮。
   `init_advmw()` 中的父层隐藏操作不在这个函数内。
4. 当前 core 的 Scene 删除整棵子树，重建缺失父层时使用新节点默认属性。

以上可解释应优先追踪“删除消息层后重新建按钮”的路径，但尚未证明原生引擎的
父层属性保留规则，也尚未证明它导致停顿。原始 `work/otomeriron.pfs` 与当前
`root.pfs` 的 cm.lua 完全一致（SHA-256
`ac94bb4448f588009252922d4d110515abf0a70bdc5006a89f3c7833dd855b9c`）。
不能仅根据这段调用，把问题认定为移植资源改坏或直接给引擎增加游戏专用隐藏规则。

## 独立诊断候选

`python tests/startup_watch/prepare.py` 从当前 build mirror 创建
`build/startup-watch/source/`，继承当前全部 DIRECT_* BOOL 选项和同一个 core archive。
随后运行 `wsl bash build/startup-watch/build.sh`。

仅在独立副本中增加主线程阶段标记和低优先级内核观察线程。观察线程不用正常
日志队列，直接记录 `startup-watch.log`：主线程状态、wait type/id、运行时钟、
阶段与停滞时长。覆盖逻辑推进、纹理准备、视频打开/解码器初始化、GXM begin/end、
日志统计/flush。等待对象只有通过内核 mutex 查询后才追踪 owner；不能仅凭
wait_type 数值认定是 mutex。视频启动后的同一阶段超过10秒时，最多尝试4次
有地址范围校验的原始栈保存。原始栈不是已还原的调用栈，需匹配 ELF 和装载地址
分析，不能把其中残留的返回地址当成当前执行位置。
只观察，不跳过等待、不更改同步边界、不自动重启。
核对 core 和 shader 保持相同；详见 `build/startup-watch/source-manifest.json`。

编译通过，包为 `build/startup-watch/host/art3m1s_direct.vpk`：

- VPK SHA-256：`7df0a24a3aa2b8633dfc83cecf45725e91114e05ebea0b6aaebabd5b25702f1c`
- SELF SHA-256：`4e80947748f5febae90bd62b9b6384b6d0a7efa52e45f85ac3f5c5a43ab704c9`

23:08已部署最新诊断版，备份和上传读回校验见
`build/direct-deploy/deploy-20260912-230800/manifest.json`。
性能改进候选和 `ogv软解初步版` 标签均未回退，shader与core archive未改。

## 本轮运行结果与限制

- 独立 Vita3K 探针 ART3SW001：观察人为12秒延迟及恢复；探针不运行游戏或GXM。
  模拟器返回的栈所属内存范围不能通过完整校验，记录 valid=0 并跳过读取。
  这验证了拒绝不完整范围，**没有验证成功取得栈内容**。
  探针启动先删除自己的旧日志，避免模拟器 O_TRUNC 行为导致旧 PASS 混入。
- 实机共4轮启动：第1、2、4轮自然播放，第3轮分别在 logo.mp4 和 Artemis 标志
  出现后按一次确认。4轮均进入樱花标题，未复现持续卡住。
- `build/startup-watch/title4-startup-watch.log` 第4轮在标题入口记录约2秒
  runtime-advance 停留，随后恢复。不能将它视为录像中持续卡住的原因。
- `title4-host.log` 进入标题后实际完成的视频上传约16.6帧/秒，CPU333MHz。
  这是沿用已验证的 OGV 优化效果，不是本次诊断带来的新提升。
- 超过10秒的实际故障栈尚未捕获，故障尚未确认修复。

原版 Windows 最小图层实验位于 `build/native-layer-delete-probe/`：子层自身
visible=0 可隐藏，但无内容父层的隐藏与当前 core 继承可见性的行为有差异。
原版游戏完整启动的截图只覆盖广告、Artemis不透明阶段和标题，未捕获按钮显露
对应的短暂淡出时刻，不能宣称已完成原版转场逐帧对照。未据此修改通用图层规则。

下一次验证：保持333MHz，完整经过广告→注意事项→Artemis→标题，记录是否操作过按键。
若重现，先取 host.log 和 startup-watch.log，再据实际等待阶段增加对应栈/事件诊断。
按钮提前显示需另行对照原版，不能把成功进入一次或隐藏按钮当成停顿修复验收。

## 后续脚本与原版对照

9月12日晚继续从当前归档和原版归档提取 `system/image/cache.lua`、
`system/ui/title.lua`，仍只写入 build。忽略换行后，前者与原版仅有屏外字体预热
坐标尺寸差异，`title_cache()` 相同；后者完全相同。

实际调用顺序为：品牌复位 → 跳入 `system/first.iet:title` → `title_cache()` →
`title_init()` 中 ClearMemory、csvbtn3和按钮初始化 → `video sakura.ogv` → 标题动画。
`title_cache()` 使用同步 `e:bindSurface`；Vita 回调进入 `surface_loader::bind(...,false)`，
调用者会等相应任务完成。因此标题前停止绘制也可能发生在图片缓存/UI准备阶段，
不能仅凭紧邻OGV就认定解码器死锁；这个调用链本身不是根因证明。

补拍一次原版自然启动：观察到广告、黑底暗色Artemis过渡帧、紧接着的标题动画；
这些采样帧中未出现剧情工具栏。并非完整逐帧录制，不能排除未采到的单帧显露。
原版此次顺利进标题后关闭。没有修改原版或移植游戏数据。

只读检查实机 `ux0:data` 和 `ux0:data/art3m1s-gxm`，未发现9月12日新系统转储或
startup-stack文件；最后列出的系统转储是9月9日。记录 `build/startup-watch/dump-list.json`。
没有转储不能排除死锁、暂停或其他原因。

实机host/watch日志连续读取仍停在同一会话时间约545秒，投影图像也不更新。
FTP与VitaCompanion仍可连接，但这两点不能确认游戏进程状态。用户关于是否退出/
切换应用的答复仍待返回；未据此强制重启、替换程序或宣称新候选已在实机通过。

## 9月12日23时后续

设备状态后来恢复，日志增长且投影为樱花标题，已部署映射队列候选并完成开/关
自然启动两轮；均能进标题，约16.7FPS，无录像中的持续卡住。详细数据见
`../video_queue/MAPPED.zh-CN.md`。这排除了“去掉复制必然改善帧率”的推测，
没有排除间歇性启动故障。

部署下一重叠实验时kill返回失败，写入临时文件550失败，旧SELF未替换。
随后旧包运行时间重新从零开始，投影显示重新载入；先停止操作并询问用户来源。
`build/startup-watch/overlap-await-front-startup-watch.log` 中在logo.mp4开始前
runtime-advance有约20秒和10秒停留，主线程CPU计时仍增加，后续正常播放logo。
这说明至少这些长停留包含主线程工作，不能直接称为GPU等待或互斥死锁；
发生位置也不同于用户录像约38秒后的标题前持续停止，未将两者混为一件故障。
