# Vita3K 退出阶段异常：本地对照与 dump 证据

2026-09-09，本记录只涉及 PC 上 Vita3K 进程退出，不是实机帧率验证，也不是已修复结论。

## 结果

| 记录 | 操作 / 检测位置 | 结果 |
| --- | --- | --- |
| optK 旧包本轮复测 | 开篇两行正文 → Backlog 打开/关闭/重开/向前翻页 → 返回正文 → MCP shutdown | exitCode=0 |
| optM，06:40 dump，PID 48724 | QtWidgetsActionPrivate / QObjectPrivate / QAction 的清理链 → QWidget 析构 → Vita3K main | 0xC0000374 |
| optO，07:15 dump，PID 74272 | QImage/QPixmap/QIcon/QToolButton/QToolBar 清理链 → QWidget 析构 → Vita3K main | 0xC0000374 |
| 更早的 03:16 dump，PID 59964 | QtWidgetsActionPrivate / QAction 清理链 → QWidget 析构 → Vita3K main | 同类堆异常 |

更早 dump 的内部时间是 2026-09-09 03:16:44；历史快照优化提交 `3755ddc` 为 06:20:20，消息比较优化提交 `2cd313c` 为 07:17:46。早期 dump 无法提取宿主版本 banner，因此不把它标成特定 opt 包。它证明同类 PC 退出异常早于这轮缓存改动；它不证明每次异常最初的内存破坏来源完全相同，也不能排除新代码对复现时序的影响。

optK 此次正常退出是一次对照结果，不能证明异常已经消失。当前保留 optM/optO 退出检查失败记录，未将其改判通过。

## 本轮对照

原始 optK 包 SHA256 `ab2247ecbc80f745c8b0294545459740700d7403478f0835a7fdeabdf3107630`，直接运行归档包，未重新构建。先确认 MCP 健康与此前会话终态，再启动 `bc851f2f-dfa3-4333-abaf-a285aad864f0`，只在模拟器安装 ART3DIR01。

沿 SHUF00002 开篇进入「如果云层之上真有神的国度……我就无法看见」两行正文，打开 Backlog、关闭、重开、向前翻一页，再关闭返回正文。截图和日志保留在 `build/exit-baseline-optK/`。没有点击存档槽或历史项返回剧情，没有改游戏资源、模拟器渲染设置或时钟。临时 trace-nextline.flag/off 的原状态已恢复，controls.json restored=true。

`session-exit.json` 确认本次进程已退出、exitCode=0。模拟器当前安装包因此为 optK，会话已结束；实机仍保留 optL，本轮没有向实机写入。

## 离线分析与限制

使用本机 Windows Kits 8.1 x64 CDB，读取已有 CrashDumps，没有 attach 活进程，没有修改注册表、page heap、模拟器或实机设置。Vita3K.pdb 成功载入私有符号，匹配本机 Vita3K.exe（时间戳 6A93CC3B）。Qt、ucrtbase、ntdll 缺少私有 PDB，只使用导出符号；不能把相邻导出符号加偏移误当成内部函数精确名称。旧 CDB 还提示若干未知 minidump stream，未将那些扩展流用于结论。

三个异常栈均在主窗口清理链，返回点为 `Vita3K!main+0x233f`。optO 的对应指令前是 `MainWindow` 析构调用，位于 QApplication 事件循环返回之后；随后还有 EmuEnvState 和 Config 清理。反汇编初始取点有未对齐字节，只有已确认的 main 返回点与析构调用用于定位。

堆异常在 free/析构时**检测到**，这不是最初越界写入或重复释放的确切位置。现有 minidump 只有部分内存；未取得首次破坏的分配/释放历史，因此不能宣称根因已找到或已修复。也不能把 PC Qt 调用栈直接归因于 PSV 字体或 GPU 缓存。

证据文件：

- `optO-dump-stack.log`、`optM-dump-stack.log`、`pre-cache-dump-stack.log`：异常线程栈。
- `optO-exit-location.log`：异常记录、main 返回点与附近清理指令。
- `dump-manifest.json`：三个原 dump 的绝对路径、大小和 SHA256；未上传 dump。
- `prior-windows-events.json`：Windows 对 optM/optO 的应用错误记录。
- `mcp-shutdown-readonly.txt`、`mcp-source.json`：已运行 MCP 关闭函数的只读摘录与文件哈希。

MCP shutdown 路径清空输入/触摸，再请求 emulator.shutdown，等待后才有兜底终止；该函数没有渲染配置写入。它不能证明整个模拟器或 MCP 从未改过其他设置，仅排除本次所检查关闭函数中的设置写入。此次没有改 MCP 源码或二进制。

## 对性能工作的影响

保留 optO 已取得的 CPU 分项对照与像素一致性证据，不把退出异常改记成成功。实机 optL 等待位置 A/B 仍需用户确认运行画面；随后才可单独测 optO 在 333MHz 的消息同步收益。PC 端退出异常若继续排查，应在 Vita3K/Qt 的释放路径取得首次堆破坏证据，而不是盲改已验证的 PSV shader、GPU 等待或正文质量。
