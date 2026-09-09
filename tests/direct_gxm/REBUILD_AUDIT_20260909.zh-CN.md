# optL-rebuild-audit：独立诊断构建

core 工作树 `build/heap-audit/rebuild-source`，分支 `codex/optL-rebuild-audit`，提交 `ebd0b93`，父提交 `982a4a6`。它增加编译特性 `gxm-rebuild-audit`，默认关闭。宿主 `DIRECT_REBUILD_AUDIT_CANDIDATE` 默认关闭，开启后沿用 message-input 的其他配置。未部署到实机；实机仍是 optL-message-input。

诊断不参与脏标记或重建条件计算。每 300 次到达 GXM 重建判定的 present 汇总 `[rebuild-audit]`：

- `rebuilt/reused`：当前完整 DrawList 是否重建。
- `dirty/first/texture/transition/wait_icon`：重建判定及等待图标状态变化。
- `events/clock/emote/text`：本次 present 之前积累的来源位，每个来源在一帧最多计一次；可重叠，不能相加当作重建次数。
- `dirty_unattributed`：未记录上述来源、也非等待图标状态变化时的脏标记，包括输入或其他显式失效。它不是错误，也不能直接解释为某一具体模块。
- 等待转场 capture 的提前返回不进入决策统计；present 完成后清除来源位。

不逐帧格式化日志，不新增场景遍历、不克隆纹理、不修改 shader、GPU 提交或同步。日志仍有少量观测开销，不能把诊断版计时直接当作发行版性能。

计数累积/清除测试通过；Release core 全测试 345 passed、13 ignored、0 failed（`build/heap-audit/rebuild-all-tests.log`）；Vita 交叉编译和宿主构建通过。冻结产物 `build/01.02-optL-rebuild-audit/`，VPK SHA256 `8d84e81abe70081d53fad0740d83a9371d69928bc1e50eedd0a65c419f16e644`，eboot SHA256 `41c41550dffc1d8de4b970108cb137d977e5430a069e7a4348f0370ffc165a35`，core SHA256 `7e63c780ff919e0e555e4e6daa8c31771d3ddd8c1c137261fdbdf2d386baf77e`。VPK CRC 与包内 eboot 字节检查通过，shaders.hpp SHA256 仍为 `f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6`。

MCP 健康检查后启动 Vita3K 会话 `d4e2f956-c0f2-4d1b-ad3c-5a23d4ce3f33`；通过启动横幅和菜单截图核对实际运行包。完成读档、单人停句、双人/头像/放射线场景检查，截图与日志保存于 `build/rebuild-audit-validation/`。

单人稳定窗口连续三组均为 `decisions=300 rebuilt=300 reused=0 clock=300`，`texture/transition/wait_icon/events/emote/text/dirty_unattributed` 均为 0。推进至双人场景后日志新增九个诊断窗口，最后三组也是相同结果。日志前缀检查证明双人末尾窗口属于推进后的新增数据，不复用单人旧行。

这证明在这两个模拟器固定页面，合成器时钟推进报告的动画变化持续触发全场景 CPU 重建；不证明具体哪一个图层在变化，也不证明实机已经测到相同原因或优化收益。`clock` 合并 tween/anime，下一步要区分实际动画层以及可见性/父层依赖，不能先关掉动画或放宽失效条件。等待图标 `wait_icon=0` 只表示 show/hide/定位没有变化，不排除图标本身的旋转/序列动画属于 `clock`。
