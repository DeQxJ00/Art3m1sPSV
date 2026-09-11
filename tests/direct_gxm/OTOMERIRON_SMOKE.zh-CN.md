# otomeriron 首次启动测试

2026-09-11。用户说明资源移植尚未完成，要求接着检查。测试当前目录 `E:/EmuGame/vita3k_data/ux0/data/art3m1s-gxm/games/otomeriron`，使用 native-compat-6，Vita3K 独立会话 `fe1c318d-b182-4c6a-805d-72935323e6d2`。未操作实机，也未修改游戏资源或正在制作的表格。

## 已复现的阻塞

从选择器进入后黑屏，无法到标题。首个重复错误为：

```
include 读取 system/table/list_vita_ja.tbl 失败
system/image/boot.lua:505 font_init
system/init.lua:329 system_initialize
system/first.iet instruction 6
```

`font_init()` 根据 `game.os` 和 `get_language("ui")` 拼接 `system/table/list_<平台>_<语言>.tbl`，本次实际选择为 `vita`、`ja`。入口尚未进入后续音频初始化、shader 初始化及标题流程，不能据此判断这些功能是否正常。

本轮启动时所有归档合并索引中没有 `list_vita*.tbl`；松散文件中有 `list_vita.tbl`、`list_vita_cn.tbl`，缺少 `list_vita_ja.tbl`、`list_vita_en.tbl`、`list_vita_tw.tbl`。同时存在 `list_windows_{ja,cn,en,tw}.tbl`，但 VITA 路径不会自动改读它们。`list_vita.tbl` 是公共表，不能替代语言表。

## 下一步

先决定此阶段支持的界面语言：若只做中文，启动默认语言与语言选项需要和 `cn` 一致；若保留四种语言，需要准备对应 VITA 语言表，并核对表内坐标、字号、资源引用。不要仅因 Windows 表存在就视为 Vita 布局已完成。

此阻塞解除后再测试：标题方向键／○操作、开篇正文和姓名、BGM／语音／音效、立绘和背景坐标、转场／shader、视频、菜单与存读档。本轮这些项目均未通过验收。

## 证据

- 截图和运行日志：`build/native-five-v125/otomeriron-boot.png`、`otomeriron-boot.log`。
- 独立分析目录：`build/otomeriron-test/`，含启动脚本、有效归档索引、测试前文件信息和界面表 SHA-256。
- 分析提取物只写到 build 目录。结束校验时发现 VITA 两张表及备份表已被外部移除，现存 Windows 五张表哈希未变；本任务没有删除／改写它们。变动记录为 `ui-hashes-after.json`，本报告描述的是首次启动时的快照，后续资源调整需重新测试。
- 已正常关闭本次模拟器会话，避免重复异常持续写日志。

## 2026-09-11：按原始 PFS 核对并修复表格

用户随后授权修改 Windows 表，并指出 `work/otomeriron.pfs*` 是原始资源。对全部九个原始归档建立覆盖索引后，五张有效 Windows 表均来自 `otomeriron.pfs.020`。只把所需参考文件提取到 `build/otomeriron-test/`，未解包到游戏目录，未改写任何 PFS。

核对结果：

- 原始物理画布 1920×1080，当前 960×540。标题背景 PNG 也从 1920×1080 变成 960×540，消息框从 1899×372 变成 949×186。
- 当前 AST 的坐标同样减半，例如 `ax=640,ay=360` → `ax=320,ay=180`。因此保留当前 `game_scale={640,360}`；不能直接套用旧转换脚本中保留 1280×720 的方案。
- 当前语言表的正文/姓名字号已分别缩到 24/22，保留，不再次缩放。
- 当前公共表误将四处旋转角度 `r=±180` 缩为 `±90`，恢复原始角度。
- 四张语言表各检查 859 个 UI 行，各修正 44 行：遗漏的滑条 `area`、滑块 `p2` 以及负数像素取整。`obj2/obj3` 百分比与原始表一致，保持不变；命令、按键、ID、选项值不缩放。
- `game_lang/game_cm` 也是 UI 表，不能只识别 `ui_` 前缀；最终转换对所有带 `com` 的 UI 行执行相同规则。

新增 `scripts/repair-otomeriron-tables.py`，读取“当前已缩放表”和“原始参考表”，向独立目录生成修正后的 Windows 五张表及同内容的 VITA 五张表。重复执行不会继续缩小：五张表幂等验证、Windows/Vita 内容一致性验证均通过。该脚本针对当前已经缩放过 AST 的资源包，不是任意原始游戏的通用转换器。

生成命令：

```powershell
python scripts/repair-otomeriron-tables.py --adapted build/otomeriron-test/pre-repair-backup --original build/otomeriron-test/original-tables --output build/otomeriron-test/repaired-tables
```

当前十张修正表已复制到 Vita3K 的 `games/otomeriron/system/table/`。修改前五张松散表保存在 `build/otomeriron-test/pre-repair-backup/`；原始 PFS 未改变。来源/输出 SHA-256 在 `repaired-tables/manifest.json`，逐项差异在 `ui-field-corrections.json`。

运行验证使用 native-compat-6，最终会话 `9612b5d5-5b23-4304-b865-dccb448f680b`：

- 缺少 `list_vita_ja.tbl` 的启动错误消失，进入品牌视频、标题/设置及开篇。
- 截图确认设置页和开篇背景、立绘、消息框已显示；本轮后半段由用户操作，不能把页面跳转误认成脚本自动推进。没有完成逐个滑条、语言和存读档的交互验收。
- 音频日志有非零输出，抽样 `output_errors=0`；不等同于主观听感验收。
- 日志仍有启动阶段请求缺失默认字体 `image/font/VL-Gothic-Regular.ttf` 的警告，后续实际加载 Source Han 字体。此轮未改变引擎字体逻辑，正文/姓名显示仍需独立验收。
- 发现归档中部分 Lua 也经过改写（例如 `image.lua` 的图像中心 `/2` 被改为 `/1`），留作后续场景坐标排查依据；此轮未擅自替换这些 Lua、AST 或 shader。

证据：`build/native-five-v125/otome-layout2-user-scene.png/.log`、`otome-layout2-scene.png`（设置页）、`otome-layout2-afterlogo.png`。未向实机部署这轮资源表；用户正在操作最终模拟器会话，因此保留运行状态。
