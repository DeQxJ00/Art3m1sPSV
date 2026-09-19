# 闲置缓存 96 MiB／解码备份 64 MiB

2026-09-20，用户确认按最新实机诊断提高两个子额度。

`build/hardware-logs/20260920-060414-companion/host.log` 显示：渐进预载申请已生效，
但闲置图片依然受 64 MiB 限制，已有 CPU 备份的保留门槛还是 32 MiB。
共享缓存只占约 96–103 MiB / 192 MiB 时，bg01fs_s、bg03as 已被回收，
返回 bg01fs_s 时读盘 502 ms、解码 480 ms。之后的灰度重建约 99 ms 是另一项成本。

## 修改范围

- `IDLE_TEXTURE_BUDGET`：64 → 96 MiB。
- 新具名常量 `DECODED_BACKUP_BUDGET`：原来的 32 → 64 MiB。
- 总缓存仍为 192 MiB，闲置 GPU 仍为 32 MiB，堆请求仍为 320 MiB。
- 遮罩／动画预载类别限额、压缩备份限额、渐进预载申请、回收顺序不变。
- 只保留已经存在的解码像素，不从 CDRAM 回读制造备份。
- 保留备份仍需共享账本实际余量；真实预载压力仍会压缩闲置缓存。

基于 core 3f4b39c，补丁 `patches/idle96-decoded64-core.patch`。
没有修改 shader、字体、视频解码、时钟设置或脚本行为。

## 验证

核心测试 500 通过、17 忽略。加强已有备份边界测试，使模拟宿主明确暴露
GPU surface，避免只有空 surface 时未实际走到保留分支：
连续 67 张 1 MiB 图片最多保留 64 MiB CPU 备份；ready 增至 120 MiB 并
申请 16 MiB 余量后，CPU 备份回收到 56 MiB，正在使用的 GPU ID 全部保留。

独立实机脚本 `fixtures/decoded_backup_pressure.iet`：先预载并显示 first，
逐个预载、显示 27 张 960×540 填充图片，再返回 first。first 使用游戏背景，
fill0..26 为程序生成的测试图片；资源仅保存在 build 和实机临时验证目录。
BOOT 指向该脚本，运行分辨率 960×544。截图写盘帧不纳入性能比较。

候选 eboot SHA256：`4136f8c99fb982c077a92538290982f1fb5c440a8dc2a1821d191d0d4fee3633`。
实际剧情是否仍有冷载入／首次合成停顿，需要用户按同一路线复测。

### 实机结果

新旧两轮进入 fixture 后日志均为 CPU 333 / GPU 111 MHz；启动器阶段 ES4
曾显示 41 MHz，不拿启动阶段时钟作为游戏测试条件。

- 旧额度：first GPU 淘汰后 CPU 备份也被回收，返回时压缩备份命中，
  解码 91.391 ms，发布 0.189 ms；提交场景 112.212 ms，整帧 114.472 ms。
- 新额度：first GPU 淘汰后 CPU 备份仍在，返回命中 decoded-cache，上传
  10.322 ms，提交场景 30.746 ms。该窗口 reads=0、decoded=0。
- 新额度返回后的闲置占用约 87.4 MiB，确实超过旧 64 MiB 限额；GPU
  闲置限制仍为 32 MiB，没有把增加的 CPU 额度挪给显存。
- first、return 两张截图新旧全图逐像素零差异；启动像素自检通过。

证据：`build/dialogue-stall-20260920/idle96-before/`、`idle96-device/`、
`idle96-pixel-comparison.json`。部署记录
`build/direct-deploy/deploy-20260920-061236/manifest.json`。
测试游戏从 games 移至 `ux0:data/art3m1s-gxm/validation/idle96-cache-20260920`，
恢复原来的 otomeriron 选择记录，回到启动器，未动游戏资源和原存档。
