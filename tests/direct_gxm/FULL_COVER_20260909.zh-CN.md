# optL-full-cover：消除已被覆盖的未提交批次

基于实机已验证的 optL-visible-clip，独立编译开关 `DIRECT_FULL_COVER_CANDIDATE` 默认关闭。开启后默认使用 full-cover；`ux0:data/art3m1s-gxm/full-cover.off` 可在同画面关闭本项。visible-clip 保持独立开启。

只有纹理已获不透明认证、四角 alpha=1、普通无混合快路径、没有 clip/rule、有限坐标/UV/颜色且轴对齐矩形覆盖完整 960×544 目标，才能消除前面尚未提交的 batch。此时后绘制的矩形完全替代旧颜色，当前渲染器没有深度/模板副作用。部分覆盖和旋转仅包围盒满屏都不接受。

只把未提交 batch 的 count 清零，不回退 vertexUsed，不覆盖已有顶点，不修改纹理寿命或安全等待。预先检查新 quad 容量，避免丢弃旧图后才发现新图无法写入。原 quads 统计仍是覆盖剔除前的数目；独立 `pending_quads_dropped_avg` 记录实际省去的 quad，不能只看原统计认定未命中。

## 验证

C++ 测试通过：普通/加法/未知透明度/rule/clip/部分覆盖/NaN 排除，四种轴翻转与超屏矩形，2,088,960 个屏幕采样中心的覆盖检查。CPU 混合验证只证明完全不透明源覆盖旧颜色这一性质，不代替实际 shader 视觉对照。

候选产物位于 `build/01.02-optL-full-cover/`。VPK CRC 与内含 eboot 对比通过。

- VPK SHA256 `e818f3e7748e164eac710e10285372d5f7787bdc845b09db21f3ada20d4fae64`
- eboot SHA256 `51523b604179d8532eea509c41826c0122a26837d992a7505dfd6aceb73d601f`
- core SHA256 `31184f196eaa6b284a6908ac012c8ad27db35daa854b339c930da63cd82a4059`，与 visible-clip 相同。
- shaders.hpp SHA256 `f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6`，未修改。

Vita3K 会话 `2a517b51-1014-434e-a99b-43e57ed56813` 已启动、显示菜单和标题、读档 2。模拟器证据保存在 `build/full-cover-validation/`。实机性能需固定页 ON/OFF/ON 后才能判断。

固定双人放射线页的 ON/OFF/ON 像素对比已通过：ON1 对 OFF 和 ON2 的差异均仅在 `(891,482)-(920,511)` 的动画等待图标，其他像素一致。`pixel-comparison.json`、三段日志快照及最终 `final.log` 已保存。稳定 ON 窗口 `pending_quads_dropped_avg=1`，OFF 为 0；切换混合窗口不作计量。测试开关已恢复开启。

18:32 实机部署完成：`build/direct-deploy/deploy-20260909-183051/manifest.json` 记录完整旧程序/日志备份以及新程序读回哈希。旧 eboot 为 visible-clip 的 `896ff8e...`，新 eboot 与本候选一致，SFO 未改变。启动证据 `build/hardware-logs/20260909-183256-current/host.log` 确认 full-cover 标识、333/222/111/111 MHz、full-cover 和 visible-clip 均 enabled=1，菜单帧已绘制。性能仍待用户返回固定页面后比较，不能把菜单启动记录当作人物页收益。

实机采样器已准备为 `build/heap-audit/cover-hardware-ab.py`，只改 full-cover 标记；分析器 `build/heap-audit/analyze-cover-ab.py`。尚未运行实机性能 A/B。
