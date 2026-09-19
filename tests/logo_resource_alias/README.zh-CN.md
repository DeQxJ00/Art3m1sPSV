# Logo 间隙泄漏对话按钮：资源别名回归

## 原因与修复

`CoreRuntime::load_open_project` 原先预先上传名为 `:bg/black`、`:bg/white` 的 2×2 纹理。
这些名字实际是游戏可配置的资源路径，不是引擎保留的纯色命令。
纹理提供器先查名称缓存，命中小图后不再读取游戏 PNG；无显式尺寸的 `lyc` 因而只覆盖 2×2。

otomeriron 的广告结束流程 `cm_exit2()` 会重建对话按钮；logo 流程的第 10 层应该通过
`init.black` 遮住底层按钮。旧版读取到小图，让按钮在 Artemis 淡出期间露出来。
`lydel` 和相关脚本无需新增游戏专用补丁。

修复取消两个资源别名的预上传，并取消其永久保留的缓存特例。
真实 PNG 的尺寸、透明度保持原样；引擎 `__solid_` 纯色路径不受影响。

## 独立案例

运行 `python tests/logo_resource_alias/prepare.py <games目录>/TEST_LOGO_ALIAS`。
脚本只写独立测试目录，生成无版权素材的 RGBA PNG，不解包或修改真实游戏。
四页各持续 3 秒，顺序为黑图别名、黑图完整路径、白图别名、白图完整路径。
底层有红色矩形，正确结果应全部被图片覆盖；别名与完整路径的尺寸和透明边缘应完全一致。
PNG 故意小于舞台，确认没有强制拉伸或丢弃 alpha。

## 2026-09-20 验证

- 核心测试：492 passed，17 ignored，0 failed。新增测试验证别名读取源图的尺寸、alpha，以及活跃保留／闲置回收。
- Vita3K 修复前，使用当前 otomeriron 黑白底的独立案例中，别名页漏出 16,800 个红色像素，完整路径页为 0。
- 同一案例修复后：黑图和白图各自的别名／完整路径截图均为 0 像素差异。
- Vita3K 完整开场从 logo、广告、注意画面、Artemis 到标题，连续截图中不再出现提前泄漏的对话按钮。
- PSV 实机独立案例：黑图与白图各自的两种路径截图均为 0 像素差异，日志确认加载 960×540 资源，启动自检通过。
- 实机已安装修正版并恢复原有启动器选择记录。完整游戏的实机开场尚未录像验收，不能用模拟器结果代替。
- IDA 13342 此轮仍未连接；结论来自实际脚本、代码和修复前后实验，未声称新增原生反汇编结论。

本地证据保存在 `build/otomeriron-logo-ui/`：`comparison.json`、`alias-before/real-*.png`、
`alias-after/`、`mcp-after/`、`mcp-after-contact.png`、`device/`。
Vita3K 此次 `takess/savess` 返回空黑读回，因此模拟器比较使用 MCP 截图；实机使用 `takess/savess`。

已验证 eboot SHA256：`bac6d5bb3b5be16f8a24961559e78e2fa3adf2f52028d06941619c1038f47275`。
