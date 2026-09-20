# 每个游戏的运行平台与兼容表尺寸

入口：启动器中选中游戏，按 **□ → 当前游戏设置 → 运行平台**。○ 或 ←→ 在 Vita / Windows 间切换并保存，仅影响当前游戏，下次启动生效。字号设置移到同一菜单第一项；游戏内 L+□ 字号快捷键不变。START 仍打开全局启动器设置。

`platform.txt` 是可选配置。缺失、空值或未知值默认 Vita，读取不会创建文件；既有 VITA/vita/psvita 与 WINDOWS/windows 均支持。菜单切换才写 `VITA` 或 `WINDOWS`。普通游戏写到自身目录；VPK 内置演示写到 `ux0:data/art3m1s-gxm/game-settings/<演示ID>/platform.txt`，不写只读 app0 资产。临时文件发布失败会回退原配置，跨游戏相互独立。

按本轮用户要求，自动生成的目标平台主表在末尾同时设置：

```lua
init.game_scale={960,540}
init.game_width=960
init.game_height=540
```

数值读取该游戏 system.ini 的 `[VITA]` WIDTH/HEIGHT，例如内置演示是 960×544；没有 Vita 配置时默认 960×540。补出的 Vita 配置段使用同样的默认尺寸，其他源配置字节保留。语言表不追加这几个字段。

上一版自动生成的主表会升级：先对比 PFS 来源、完整生成内容及已有尺寸后缀，完全匹配才替换。手动表、编辑过的生成表及 PFS 原表不覆盖。不会修改资源图片、PFS 或源 system.ini；自动表里的这些尺寸假定资源已经按用户说明缩放到目标尺寸。

验证：`tests/direct_gxm/run_game_platform.sh`、`tests/host_stream/run.sh` 的 ASan/UBSan 测试通过，包含可选配置、平台切换持久化、跨游戏隔离、失败写入回退、备份读取、Vita 尺寸读取、旧生成表升级及手改保护。VPK 编译通过。

Vita3K MCP 菜单验收：在 otomeriron 的 □ 菜单中将 Vita 切为 Windows，读取游戏目录 `platform.txt` 确认 WINDOWS；关闭菜单重新打开仍为 Windows。字号子菜单进入及 × 返回通过，随后切回 Vita 并确认文件为 VITA。截图为 `build/game-platform-menu/menu-windows.png`、`reopen.png`、`font-submenu.png`、`font-return.png`、`menu-restored.png`。

本轮读取原资源 PNG 头确认 otomeriron 的 `pc/cn/title/bg.png` 和 Toshiue 的 `pc/ja/title/bg.png` 均为 960×540。otomeriron、Stella 的旧自动生成 Vita 主表已通过字节匹配升级，末尾三个尺寸字段为 960×540，原表备份保存在 `build/game-platform-menu/before/`。早期共享操作阶段 MCP 模拟器曾以访问异常退出（3221225477）；单独接管后上述菜单测试未复现，未将该次模拟器异常判定为已修复。

独立接管后，otomeriron 按 VITA 入口进入标题，背景铺满 960×540、人物和按钮布局可见且无整体缩角。截图 `build/game-platform-menu/otomeriron-after-confirm.png`。当前普通版与 MCP 版 Vita3K 共用数据目录，因此图像验收以 MCP 会话截图为依据；共享 host.log 中其他游戏的记录不用于本轮结论。未进行本轮实机验收。
