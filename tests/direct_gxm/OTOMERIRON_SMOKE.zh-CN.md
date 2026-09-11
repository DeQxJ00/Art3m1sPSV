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
