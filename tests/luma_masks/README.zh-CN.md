# Gray8 遮罩缓存验证（2026-09-20）

## 实现范围

- 原生 Gray8 PNG 的预载、已解码缓存、闲置 GPU 缓存保持每像素 1 字节。
- GXM 纹理使用 `U8_1RRR`，采样仍得到 `(灰度,灰度,灰度,255)`，不改 shader。
- 根据解码器报告的像素格式选择，不能靠目录名或 PNG 的 8 位深度判断。带 `tRNS` 的灰度 PNG、灰度加 Alpha、调色板图片继续 RGBA，保留透明度。
- 原 PNG 压缩备份仍保留。需要 CPU RGBA 的读像素、组合遮罩接口按需展开；`intermediate_render_mask` 生成的合成结果仍为 RGBA。
- 单通道 CPU 数据和 GPU 数据目前各保留一份，并非共享同一个物理分配。每份像素都缩小至 RGBA 的四分之一。GPU 分配仍按 256 KiB 对齐。
- 总缓存 192 MiB、预载遮罩上限 16 MiB、闲置 GPU 32 MiB、堆请求 320 MiB 均未提高。
- 每次启动在右下角比较 U8/RGBA；若像素自检失败，本次运行自动改用 RGBA。

## 验证

1. `python tests/luma_masks/prepare.py` 生成全合成 fixture 到 `build/mask-format-audit/fixture`，不包含商业资源。
2. 用独立 `TEST_LUMA_MASK` 游戏目录运行 `probe.iet`，先异步预载全屏 Gray8/RGBA，再分别测试普通绘制与 `intermediate_render_mask` 合成。
3. 获取 `plain-rgba.png`、`plain-gray.png`、`mask-rgba.png`、`mask-gray.png`，按 `candidate-` 前缀保存。
4. 安装 Pillow 后运行 `python tests/luma_masks/compare.py <截图目录>`。本轮右上角缓存浮窗数值会变化，仅排除该区域，其余逐像素比较。
5. 测试结束恢复 `last-game.txt` 及其备份，将独立测试目录移至 `validation`，返回游戏选择界面。

实机证据保存在忽略目录 `build/mask-format-audit/device/`；部署前备份在
`build/direct-deploy/deploy-20260920-124640/`。

- 529 项核心测试通过，18 项原有测试忽略；Vita 核心与 Direct 宿主编译成功。
- 启动 6 组像素自检全部通过，最大通道差值 0：奇数行宽、普通绘制、3 个转场进度、内置 rule 与 alpha-mask 采样。
- 实机脚本确认 `payload=gray8` 和 `GXM prefetch-hit`；两组截图除变化中的缓存 HUD 数字外完全一致。
- 960×540 像素数据：RGBA 2,073,600 字节，Gray8 518,400 字节；对应 GPU 分配粒度约为 2 MiB 和 0.5 MiB。
- 本轮合成素材的一次预载耗时：Gray8 116,196 µs，RGBA 197,074 µs；Gray8 上传 3,149 µs。预载总保留量分别为 578,014 和 2,204,059 字节（含各自不同大小的 PNG 压缩备份及 RGBA 附加信息）。这些是单次合成探针数据，不是游戏帧率基准。

## 游戏资源适用性

本机资源审计表明，多份原生 Vita 的 rule 遮罩、Toshiue 大部分 rule 为 Gray8，可以受益。
当前压缩版 otomeriron / SHUF00002 的 rule 多为调色板图，且实际包含半透明像素，本次不会强制丢弃 Alpha。资源不转换时，它们不享有同样的像素容量缩减。

回归测试还覆盖缓存回收再上传、上传失败重试、设备不支持时 RGBA 回退、CPU 合成覆盖率、灰度透明键和调色板透明度。
