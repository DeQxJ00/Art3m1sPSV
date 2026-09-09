# 原生游戏总菜单调查（2026-09-09）

本次只读检查已安装游戏的资源表与 Lua，不代表五款菜单均已运行验证。单文件提取位于 `build/native-menu-audit/`，未向游戏运行目录解包。

| 游戏 | 资源证据 | 结论 |
| --- | --- | --- |
| PCSG01297 | root.pfs: system/table/list_vita.tbl 的 ui_menu；system/ui/menu.lua | 右侧总菜单，有设置、自动、跳过、历史、存档、读档、快速存读档等按钮 |
| PCSG01201 | root.pfs.002: system/csvPSV.lua 的 ui_menu；system/ui/menu.lua | 有存档、读档、历史、设置等总菜单；PSV 表的快速存读档按钮使用 none 且放在屏外，不能把脚本存在等同于可见按钮 |
| PCSG01107 | root.pfs.000: system/csv_vita.tbl 的 ui_menu；system/ui/menu.lua | 有右侧总菜单，包含快速存读档、存档、读档等 |
| PCSG01084 | root.pfs: setting/ui/menu.csv、menu2.csv；system/ui/menu.lua | 有网格总菜单，存读档、快速存读档、设置、说明、返回标题及关闭 |
| PCSG01127 | root.pfs: system/csv_vita.tbl 的 ui_menu；system/ui/menu.lua | 有右侧总菜单，含快速存读档、存档、读档等；脚本对不存在快速存档的情况禁用快速读档 |
| SHUF00002 用户移植版 | 有效 system/table/list_vita.tbl 的 ui_menu={} | 菜单脚本残留，但表为空；直接触发 MENU 会在 menu_refresh → setBtnStat 中因 idx=nil 报错。Vita3K controls2 已复现，截图与日志在 build/controls2-validation/square-settled.* |

不能只用 menu.lua 是否存在判断原生菜单可用。需检查当前游戏实际加载的菜单表、入口及按钮定义；空表使用用户授权的自绘后备菜单。

同时发现数值键码不能跨游戏直接当成功能：SHUF00002 的 113 是 AUTO，而 PCSG01297 的 list_vita.tbl / list_windows.tbl 中 113 是 MENU、114 是 AUTO。正式映射需要依据游戏注册的动作识别，并保留输入过滤、UI 导航和低键码上限兼容，不应让 Select 在某款游戏变成打开菜单。

状态：此文记录调查结果；自绘后备菜单及跨游戏动作解析尚未完成。controls2 不能作为最终完成版交付。
