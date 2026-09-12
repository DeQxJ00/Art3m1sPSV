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
日志统计/flush。只观察，不跳过等待、不更改同步边界、不自动重启。
核对 core 和 shader 保持相同；详见 `build/startup-watch/source-manifest.json`。

编译通过，包为 `build/startup-watch/host/art3m1s_direct.vpk`：

- VPK SHA-256：`6e1b4da4dd15e92ee40586a2133fd43714505b535bd2a269207d438ba8e65f76`
- SELF SHA-256：`438d268895fda38e9c1ed88ab77f38bb05265a6a82e366b9dbe85a82f99c2fb6`

尚未部署，未完成此诊断包的模拟器/实机运行验证。当前实机已回到正常剧情，未打断。
性能改进候选和 `ogv软解初步版` 标签均未回退。

下一次验证：保持333MHz，完整经过广告→注意事项→Artemis→标题，记录是否操作过按键。
若重现，先取 host.log 和 startup-watch.log，再据实际等待阶段增加对应栈/事件诊断。
按钮提前显示需另行对照原版，不能把成功进入一次或隐藏按钮当成停顿修复验收。
