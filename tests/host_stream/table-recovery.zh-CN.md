# 缺失平台表自动生成

Direct 宿主在正常虚拟文件查找失败后，仅对 `system/table/list_<平台>[_语言].tbl` 尝试补表。保存目录、游戏散文件、PFS 中已存在的目标文件仍按原优先级读取，不覆盖用户文件。支持的名称为 windows、vita、ps4、switch、android、ios、wasm，语言后缀按实际请求保留，不固定为三种语言。

来源只从已打开的 PFS 读取，补丁包优先级与普通读取一致；候选平台按上述顺序查找，并要求该平台主表存在。生成文件保留源表的布局、图片和视频路径，写到当前游戏的 `system/table/`，不会写入存档目录或 PFS。主表遇到 `.glsl` 配置，且 PFS 包含 `system/shader/pc/reset.hlsl` 时，追加改用 `pc/`、`.hlsl` 的配置。生成内容带来源注释。

按需生成意味着进入日文时只需生成主表和 `_ja`，以后请求中文或英文时再生成对应表。源文件限制 2 MiB、拒绝内含 NUL 的数据；完整读取后先写临时文件，关闭成功才发布。写入失败保持原有查找失败，不留下半截目标表。已有表即使是之前自动生成的也不自动覆盖；更换资源包后如需重建，应先备份并移走相应生成表。

这只是资源表补齐，不等于完整的平台移植：平台专属 API、初始化分支及认证机制均未修改。

## 验证（2026-09-15）

`tests/host_stream/run.sh` 使用 ASan/UBSan，通过 PFS 主表/语言表生成、图片路径保留、HLSL 配套选择、已有散文件与 PFS 目标优先、目录不覆盖、失败写入重试、未知平台/路径拒绝测试；原流式读取与并发音频回归同时通过。

Vita3K 使用用户压缩后的 Stella 包，自动生成 `list_windows.tbl` 和 `list_windows_ja.tbl`，内容包含对应 Android 源表原字节。原缺表异常消失，进入 UI 资源及 shader 初始化。当前游戏仍在 Windows 专属认证初始化中发出 `Exit`，未达到标题画面；没有删除认证脚本或伪造认证结果。

本轮操作前后 PFS SHA256 均为 `2177f853ab275bd1693e352cb267968c929a6a785a1afc2d280ecd1001d9ba70`。更早诊断时记录的资源大小不同，用户确认期间压缩了 MP4；该变化不归因于补表程序。证据保存在 `build/table-recovery/first-boot.log`、`generated-tables/` 和 `validation.json`，未向游戏目录解包其他分析文件。

安装包 `build/table-recovery/art3m1s-table-recovery.vpk`，SHA256 `bde50b068dcc3bbe108cb7efb9a5251dfa865a32d84f5942c3802070b933bd76`。已装入 MCP 模拟器，未部署实机。

## 2026-09-15：所有游戏默认 Vita 入口

不再限定 otomeriron，也不再要求先找到 Vita 主表才能确定平台。启动游戏时先读取 `platform.txt`：

- 缺失：本次按 VITA 启动，并将 `VITA\n` 写回游戏目录；写入失败会记录错误，下次重试。
- 已有 `VITA`、`vita`、`psvita`：均按 VITA 启动。
- 已有明确的 `WINDOWS`（大小写不限）：保留用户显式指定的 Windows 入口。
- 空文件或未知标记：文件不覆盖，运行时使用 VITA 默认值。
- VPK 内置演示同样默认 VITA，但不会尝试写只读的应用目录。

如果 `system.ini` 没有 `[VITA]` 段，启动缓冲区从已有 WINDOWS、ANDROID、IOS、SWITCH、PS4、WASM 段依次选取一段补出 `[VITA]`，保留分辨率、字符编码、BOOT 和其他配置原字节。原来的 `[VITA]` 段优先，不改写磁盘 system.ini 或 PFS。没有可用源段时仍报告加载失败，不编造启动脚本。

确定平台后，脚本请求 `list_vita.tbl` 和语言表，走本文所述通用缺表恢复。平台标记的补齐不依赖补表是否成功。

验证命令：`bash tests/direct_gxm/run_game_platform.sh` 和 `bash tests/host_stream/run.sh`。两组 ASan/UBSan 测试覆盖默认入口、已有标记、psvita 别名、只读资产、不覆盖已有 Vita 段、CR/LF 配置、传统编码原字节保留、写失败重试、Android 到 Vita 主表及语言表恢复，以及原有流式读取回归。

实际验证：模拟器目录中 AIME00005、AIME00006、SHUF00002 原先缺少标记，已补齐；其他目录已有标记原样保留。SHUF00002 另做运行时缺失测试，日志确认 `platform=VITA inferred=1 saved=1 error=0`，自动生成后进入剧情并显示文字。Toshiue 也按 VITA 进入标题。证据保存在 `build/platform-default/markers.json`、`shuf.log`、`shuf-title.png`、`toshiue.log`、`toshiue.png`。验证使用 Vita3K MCP，尚未进行本轮实机验收。
