# optL-message-input 独立 CPU 候选

基于当前实机 core 的独立 Git 副本 `build/heap-audit/message-source`，分支 `codex/optL-message-input`，父提交 `122d675`，候选 core 提交 `982a4a6`。仅复用此前 `2cd313c` 中的 MessageInput 模块、精确比较测试和控制入口，不整体 cherry-pick 旧 OptO，也不使用主工作树后续 core。

当前消息层每帧比较 page_font 与嵌套 Font 标签的 HashMap，原实现为每个字段重复查哈希。候选保存键值的迭代序列，每次仍精确比较长度、键、值、标签内容。HashMap 重新排列会保守失效重建，不假定迭代顺序有语义，也不依赖指针、generation 或有碰撞风险的内容摘要。

宿主开关 `DIRECT_MESSAGE_INPUT_CANDIDATE` 默认关闭，显式开启后沿用 full-cover 的 GPU 配置。`message-cache.off` 只控制新旧消息输入比较；不切换不可变历史页缓存、字体图集、文本布局缓存、shader 或 GPU 等待。开关时清除输入缓存，下一帧刷新，跨切换窗口不纳入计时。

## 本地验证

- Release core 全部库测试：344 passed、13 ignored、0 failed。日志 `build/heap-audit/message-core-tests.log`。
- 新旧输出对照覆盖 320 轮直接字符串修改、Font 标签变化、rehash、删除重建消息层、清空输出快照、切换旧/新比较；另验证稳定输入继续复用输出字符串。
- 桌面微基准（每次同步）：1 层 0.690→0.392 µs，16 层 12.915→6.833 µs，48 层 42.037→23.293 µs。仅证明该微基准函数耗时降低，不是 PSV 帧率或整帧收益。日志 `build/heap-audit/message-core-benchmark.log`。
- Vita core 与宿主交叉编译成功；构建过程有既有 unused 警告以及 WSL/Windows 毫秒级时间戳 skew 提示。VPK CRC、包内 eboot 与生成 SELF 字节一致性校验通过。

## 构建身份与状态

产物 `build/01.02-optL-message-input/`：

- VPK SHA256 `c6536b8250d18a98d8dd1be861a3f43121283c255bfd20929d46972d475054dc`
- eboot SHA256 `01112bd62fed823a088dd51d9114d2311a90160b0a9ec5bc95fc373b5bc3c2f1`
- core SHA256 `6c4af2d0e985142ac19eac2c9bd4794a71013a51582d85231a5a62aec88a6cc5`
- shaders.hpp 保持 `f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6`。

MCP 健康检查后模拟器会话 `114f9620-e67e-46df-ba07-c482e45a1c2c` 完成菜单、标题、读档及推进到双人/头像/放射线页面的检查。同一页面 ON/OFF/ON 截图仅等待图标变化，差异范围合并为 `(891,482)-(920,511)`，其余像素一致；日志确认 76 个消息层、1,440 个字体字段，开关生效。证据 `build/message-input-validation/`。打开历史记录后的 ON/OFF/ON 三张图逐像素完全一致，证据 `build/message-input-backlog-validation/`。这些是选定静态页面的功能检查，不代表全面 shader 回归或实机帧率结论。

实机已部署并回读验证，备份和安装记录 `build/direct-deploy/deploy-20260909-190608/`。启动日志 `build/hardware-logs/20260909-190747-current/` 确认 optL-message-input、333/222/111/111MHz、三缓冲、无 MSAA，选游戏菜单稳定窗口为 300 帧/5 秒。此前 optL-full-cover 的 eboot、SFO、日志已备份。

## 实机同画面开关结果

用户于 19:32 确认准备好后执行 `build/heap-audit/message-hardware-ab.py`，全程不发送游戏输入、不改变频率。证据 `build/hardware-message-ab/20260909-193237/`；原始日志逐份 SHA256 记录于 manifest。每阶段等待 30 秒后读取，只取最后三个完整约 5 秒窗口，排除开关后的至少 6 秒跨界数据。

| 阶段 | 日志推算平均 FPS | 平均帧时 | begin 等待 | quads / draws / uniforms |
| --- | ---: | ---: | ---: | --- |
| ON1 | 50.239 | 19.905 ms | 4.406 ms | 405 / 29 / 0 |
| OFF | 46.601 | 21.459 ms | 4.512 ms | 405 / 29 / 0 |
| ON2 | 50.504 | 19.801 ms | 4.576 ms | 405 / 29 / 0 |

开关状态日志均确认 333/222/111/111MHz；采样时语音已结束，未见纹理上传。分析文件 `analysis.json` 保留选中窗口。ON 两次结果回到同一区间：相对 OFF，帧时减少约 1.55–1.66 ms，FPS 提升约 7.8–8.4%。这是该实机固定页面的结果，不能外推所有游戏/转场，也未达到原生近 60FPS 的目标。

辅助 core 计时经 sample_count/rendered_frames 归一化为每次 render：backlog 同步约 ON 2.12–2.16 ms，OFF 3.71–3.73 ms；场景构建约 3.98–4.06 ms，文字命令约 1.64–1.67 ms。原始及归一化结果见 `core-per-render.json`，不能直接把包含逻辑与 present 两类样本的 profiler average 当作一帧成本。

脚本 finally 已恢复 message cache 开启，manifest `restored_enabled=true`，运行中的包仍是 optL-message-input。保留此优化；后续针对动画牵连的整场景构建继续优化，诊断包尚未覆盖实机。
