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

MCP 健康检查后已请求启动模拟器候选，会话 `114f9620-e67e-46df-ba07-c482e45a1c2c`。游戏内同画面及历史记录开关验证尚待完成，尚未安装实机。实机仍是 optL-full-cover，不能把本地基准写成实机性能收益。
