# 按键与后备菜单候选（2026-09-09）

候选 `build/01.02-optL-controls3/`，独立 core `a077953`，基于 text-epoch `18ee1d9`。Host 需启用 `DIRECT_SEMANTIC_CONTROLS`，core 需启用 `gxm-menu-key-alias`；未部署实机。此候选尚未通过全部验证，不替换实机基线。

## 行为

- △／↑ 历史记录；← 快速存档；→ 快速读档；↓／○ 下一句；× 返回；Select 自动播放。
- Select 按当前游戏注册的 AUTO 动作解析低键码，不再固定使用 113；按下时保存键码，松开释放同一键码。
- □ 在正文中检查有效菜单按钮及 MENU 入口。原生菜单可用时调用原入口；无菜单时显示自绘总菜单。
- 后备菜单提供存档、读档、快速存读档、设置、历史、自动播放、返回。功能仍通过游戏注册按键及事件过滤器执行；找不到动作时显示为灰色。
- 后备菜单期间停止 core 时间推进，退出时重设 host 时间并释放菜单字体／图集。未改变游戏 shader、GXM 等待策略或游戏绘制路径。
- 当前正文检测依赖 getGameMode() 返回 adv；旧脚本的不同接口仍需适配验证。其余实体按键保留方向导航键码，尚不能宣称五款游戏均兼容。

## 已验证

- core 开启 feature：349 passed，13 ignored；关闭 feature：346 passed，13 ignored；Vita core 与 host 构建通过。
- shader SHA256：f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6。
- Vita3K SHUF00002：后备菜单显示正常，进入原游戏 SAVE 页面，× 返回原句。
- ← 确认快速存档，再 ↓ 从“小时候——”推进到下一句，→ 确认快速读档回到“小时候——”。测试前备份位于 build/controls3-validation/saves-before。
- Select 开始自动推进，再次 Select 停止；△、↑ 均打开 backlog，× 返回。
- 日志确认关闭后备菜单释放 1 页、34 字符、8364840 字节字体。
- 截图与日志：build/controls3-validation；VPK/eboot/core SHA 在候选 manifest。

## 未解决与下一步

选择后备菜单 Config 后，Vita3K 的 capture_screen 报 vk::Queue::waitIdle: ErrorDeviceLost；随后 session_status 确认 crashed，exitCode=3221226505。不能将这次崩溃当成通过，也不能仅凭此前出现过 DeviceLost 就认定与候选无关。

故障 session bdbdd9e4-9e68-474b-b1db-cb842202a0e0；core 日志已复制到 build/controls3-validation/config-device-lost.log，最后记录 action=7 key=121。模拟器完整日志仍在相应 MCP run 目录。需对比同一 Config 通过游戏原有入口与后备菜单入口，并复核菜单纹理释放／捕获生命周期。原生菜单分支需用 PCSG01297 运行验证；后备菜单其他选项亦需继续测试。

本次是功能兼容工作，没有新的实机 FPS 提升证据。性能目标继续保留，实机仍为此前 optL-text-epoch，待候选功能问题解决后继续 DrawList/时钟变更优化。

## 后续实际对照与原生菜单验证

- controls3 新进程 `991f54f6-b46f-40a6-9b9f-fafc07f91317`：标题页原有 Config、正文原有 Config 按钮、自绘菜单 Config 均成功进入设置。截图与日志在 `build/controls3-config-ab/`。之前的 DeviceLost 未再次复现，但仍不能据此认定已修复或与新入口无关。
- PCSG01297 初次运行发现宿主固定使用 WINDOWS，脚本读取 psw 图片，但原包实际提供 psv 图片。增加每游戏 `platform.txt`（内容 VITA）选择；未配置仍用 WINDOWS，保留 SHUF 的已验证模式。只在 PCSG01297 的测试副本创建了该文件，未向运行目录解包。
- 切换 VITA 后还发现原包 logo02、logo03、title/bg00 为 JPEG，旧 texture source 只查 PNG/原路径且 image 只启用 PNG。core `2da40e8` 增加 JPEG 解码、延迟扩展名回退、bindSurface 预载及预取命中支持；保持 PNG/原路径优先，避免成功 PNG 额外分配 JPEG 路径。合成 JPEG → RGBA 测试及全部 core 测试通过：350 passed，13 ignored。
- `build/01.02-optL-controls5/` 已在模拟器运行，eboot SHA256 `148da5723e7d76cf79aff99c0712142bec564170b6fefba64af49c1c62a11128`。core `2da40e8`；shader SHA 不变。
- controls5 session `73201d9a-8ca8-4e26-8814-fce0096ec446`：PCSG01297 标题、开篇背景、正文正常。□ 打开原游戏右侧 IxSHE Tell Menu，× 关闭；Select 显示 AUTO 并自动推进，再按停止。未触发后备菜单。证据 `build/controls5-native-menu/native-menu.png`、`auto.png` 与对应日志。
- 尚未实机部署 controls5；SHUF 在 controls5/JPEG 新构建上的回归与 Config 故障复现调查继续保留。原生其他四款尚未完成菜单运行验证。
