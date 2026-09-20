# 原生指令与脚本兼容性审计

日期：2026-09-14。全程静态分析，没有启动两款游戏的 EXE，没有修改用户的原始 EXE、IDA 数据库或游戏资源。本轮仅提交分析结果，没有修改运行时、shader 或缓存策略。

后续补充：2026-09-15 的 [Stella Android Rev.3292 审计](ANDROID_SCRIPT_AUDIT.zh-CN.md) 核对了相同的 153 个命令 / 54 个 Lua 注册名，并新增记录 Lua 参数类型转换、多层 `$` 与 filter 顺序、异步加载预算分支。该补充是静态证据，不替代后续行为实验与已有修复验收。

## 结论

最明确、实际被游戏调用的缺口是 **Toshiue 的 `indentmodify`**。原生实现及脚本调用均已确认：清除姓名、正文、分页时撤销旧的缩进状态。当前解释器没有该指令，不能用“已有 indent 配置支持”代替。

两版低层指令整体一致：本轮提取的注册表分别有 **151 / 153 项**，Toshiue 新增 `indentmodify` 和 `appreview`。Lua CLua 注册项分别为 **53 / 54 项**，其中均包含 `__tostring` 元方法；新增普通方法是 `getWindowHandle`。

其他未注册项目大多没有在此次扫描的游戏脚本中发现直接调用，或受到平台条件限制。不能把“引擎有这个接口”推导成“游戏现在必须实现它”，也不能把 AST 高层脚本命令全部当作原生指令。

## 分析对象与定位

| 对象 | 版本/平台 | 指令注册函数 | CLua 注册函数 |
|---|---|---|---|
| 13342：otomeriron.exe | Artemis Rev.3144，Windows x64 | `0x1400BA120` | `0x1401293E0` |
| 13343：Toshiue_Kanojo2.exe 外层 | Windows x86 启动包装程序 | 不含游戏引擎注册表 | 不适用 |
| Toshiue 内嵌引擎，分析副本 13344 | Artemis Rev.3257，Windows x64 | `0x1400BE300` | `0x140130100` |

Toshiue 原文件在文件偏移 **479232 / 0x75000** 处含有完整、可直接解析的 x64 PE，长度 5037568 字节。仅复制该段到 `build/native-script-audit-20260914/Toshiue_Kanojo2-engine-analysis.exe`，用 IDA 打开分析；没有执行这段文件，也没有调用包装程序的解包/启动代码。13343 原会话保持原对象，未切换其路由。

输入路径、SHA-256、归档索引 SHA-256、对照 core 提交及源文件 SHA-256 见 [summary.json](evidence/native-script-20260914/summary.json)。完整名称/处理函数映射见 [commands.csv](evidence/native-script-20260914/commands.csv) 和 [lua-methods.csv](evidence/native-script-20260914/lua-methods.csv)。

这些是 Windows 引擎的接口与行为证据，不能据此断言 PSV 原生 eboot 的 GPU 内存布局完全相同。

## 已确认的缺失指令

| 指令 | otomeriron 处理函数 | Toshiue 处理函数 | 本轮解析结果与优先级 |
|---|---|---|---|
| `indentmodify` | 未注册 | `0x1400D87A0` | 实际使用，优先补齐；详细规则见下一节 |
| `animedel` | `0x1400D6650` | `0x1400E0E10` | 两版均与 `lydel` 注册到同一处理函数，应按已有删除入口的别名评估。入口还涉及保存/回放记录清理，不能只看这部分就解释成仅删除动画记录。未发现直接脚本调用 |
| `backloglayer` | `0x1400D21B0` | `0x1400DA770` | 原生 backlog 容器操作，包含 bind、clear、write、分页/滚动查询及 margin 设置。游戏主要用 Lua backlog 逻辑；未发现此低层指令的直接调用 |
| `linkreset` | `0x1400D1AD0` | `0x1400D9CF0` | 两版均与 `linkenable` 注册到同一处理函数；Toshiue 调用文本层虚表 +576，遍历链接对象并调用其 +48 接口。应按重新启用/复位交互评估，不解释成删除全部链接。未发现直接调用 |
| `lysave` | `0x1400DA290` | `0x1400E5320` | 获取指定图层表面并写图片；读取 id/file，无扩展名时补 `.png`，写入路径涉及 SaveDataPath。未发现直接调用 |
| `ime` | `0x1400D3A30` | `0x1400DCCB0` | 原生输入框，参数包括 type、varname、default、left/top/width/height。未发现直接调用 |
| `unison` | `0x1400CA760` | `0x1400D1A50` | 文件/存档网络同步：下层 `0x14014D240` 构造 info/down 请求及文件上传，涉及 GameDataPath/SaveDataPath；读取 file、server、appname、id、pass，错误结果写 s.result。不是音频命令；完整协议兼容性未验证，未发现直接调用 |
| `appreview` | 未注册 | 注册为空函数 | 此 Windows 构建无实际操作，低优先级 |
| `trophy` | 注册为空函数 | 注册为空函数 | 脚本含平台分支调用；Windows 构建不能证明 PSV 奖杯行为。当前 core 未注册，按平台兼容性单独评估 |

表中的“未发现调用”表示本轮有效归档中的静态文本扫描结果，不排除运行时拼接命令、外部资源或动态宏。

`lua` 和内部 `\vgoto` 在 core 中走专门解析/控制流分支，并非缺失指令。提取注册表时还校对了编译器直接写入的短字符串：Toshiue 的 `tag` 为立即数 `0x676174`；不能因为普通字符串搜索没命中就认为被删除。

## indentmodify 的完整已确认行为

原生入口 `0x1400D87A0` 检查参数是否包含 `unindent`，转换为整数，调用当前文本层虚表 **+440**。`CTextLayer` 与 `CLinkableTextLayer` 在本构建的该槽均指向 **`0x1401537C0`**。

- `unindent > 0`：循环弹出成对保存的缩进状态，最多指定次数；栈为空则提前结束。
- `unindent = 0`：不弹出。
- `unindent < 0`：清空两组缩进状态。游戏使用 `-1`。
- 未传 `unindent`：不执行上述栈修改。
- 普通执行路径还记录该脚本块；回放/恢复状态时的记录语义也需要保留。

它改变的是当前文本层的**运行中缩进状态**，不是字号、坐标或括号配对配置。当前 core 的 `IndentHandler` 只发出 `IndentConfig {pair, range, nest}`；排版代码也有局部缩进栈，但没有 `indentmodify` 的指令处理和状态操作接口。

Toshiue 的 6 个真实调用位置（相对本轮提取目录 `build/native-script-audit-20260914/toshiue`）：

- `system/image/image.lua:928`
- `system/msg/message.lua:229`
- `system/msg/message.lua:591`
- `system/msg/message.lua:621`
- `system/msg/message.lua:631`
- `system/msg/message.lua:638`

这些调用位于清除姓名、正文、副语言文字和分页相关流程，形式均为 `e:tag{"indentmodify", unindent="-1"}`，通常紧随 `rp`。补齐时应作用于 `chgmsg` 选中的文本层，而不是无条件清空全局状态。

**还不能下的结论**：仅凭缺少此指令，不能认定过去 otomeriron 的文字闪现、字体大小或帧率问题都由它造成。Rev.3144 没有该指令；当前排版每次重建局部缩进栈也可能掩盖部分表现，需要定向排版用例验证。

## Lua 方法与平台分支

当前 CLua 对照中未注册的普通方法包括 `saveToMemory`、`loadFromMemory`、`setHttpResponse`、`setUseGlobalCapture`、`initNis`、`dumpMemoryStatus`、`callOeAPI`，新版另有 `getWindowHandle`。

| 方法 | 已确认用途/调用条件 | 处理建议 |
|---|---|---|
| `saveToMemory` / `loadFromMemory` | 原生保存状态序列化到内存字符串、从字符串恢复；不同于写存档文件 | 本轮未发现直接调用；后续补时验证完整状态/二进制长度，不能直接别名到磁盘 save/load |
| `setHttpResponse` | 内嵌 HTTP 响应接口 | 未发现直接调用，低优先级 |
| `callOeAPI` | 游戏 `system/extend/trophy.lua` 中用于 Switch 截图/录像/版权水印，受 `game.sw` 限制 | 不应因字符串存在就在 PSV 实现 Switch 系统 API |
| `initNis` | `shader_getnis()` 要求 `game.trueos == "windows"` 且 feature level 匹配；用于 NIS 设置 | PSV 应正确报告平台/能力，避免进入不支持分支 |
| `getWindowHandle` | Toshiue 新增注册；本轮发现的调用位于已注释的代码中 | Windows 窗口句柄，不是本轮 PSV 阻塞项 |
| `getEmoteVersion` | Toshiue 脚本只在 `get_artemis("is_emote") == 1` 时调用；本次 EXE CLua 注册表也没有 | 可选引擎变体接口，不能误报为此 EXE 新增必需接口 |

另发现 Toshiue `system/adv/func.lua:967` 在 **Switch 对话框分支**写了 `e:tag{"val", ...}`。两版原生注册表都没有 `val`，当前 core 也没有；它更像脚本拼写问题，需要结合实际 Switch 行为确认，不能擅自作为新的 Artemis 指令实现。

## bindSurfaceAsync / 缓存：两版共有的实际写法

| 入口 | otomeriron | Toshiue |
|---|---|---|
| Lua `bindSurfaceAsync` | `0x140132F50` | `0x14013A440` |
| CSurfaceManager 加载入口 | `0x140011A10` | `0x14000FC00` |
| `clearSurfaceLoadQueue` 下层 | `0x1400111F0` | `0x14000F340` |
| `isLoadingSurface(nil)` 下层 | `0x140011250` | `0x14000F3B0` |
| `unbindSurface(path)` 下层 | `0x140010D60` | `0x14000EEE0` |

从参数校验、虚表分派和下层实现确认：

1. Lua `bindSurfaceAsync` 接收路径字符串，异步模式传值 1；不是“路径加 Lua 完成回调”的接口。
2. 管理器先查已绑定表（对象偏移 +248），命中时增加绑定计数（+344）；再查可复用表（+392），命中时把同一 surface 引用移回绑定集合。
3. 未命中再查加载队列（+480），重复的异步请求不会再排一个任务。同步请求遇到待加载路径则有条件等待/重新查询的分支。
4. `clearSurfaceLoadQueue` 在锁内清队列；入口中没有停止线程、join 或清空全部 surface 表的动作。
5. `unbindSurface(path)` 先尝试删除该路径的排队请求，否则对已绑定对象走释放绑定入口。它与清队列是不同操作。
6. `isLoadingSurface(path)` 与 `isLoadingSurface(nil)` 分派到不同虚函数。无路径版本读取队列计数；不能用“有无已解码缓存”代替。

这里能确认的是绑定集合、可复用集合、加载队列及共享 surface 引用；不能从这几个函数推断整个进程是否保存额外压缩数据，也不能把这些集合叫做三个 GPU 帧缓冲。

两款游戏的 `system/image/cache.lua` 都有 Lua 层路径去重，`delImageStack` 先清队列，再逆序 `unbindSurface`，最后清脚本缓存表。`system/table/list_windows.tbl` 均配置 `autocache="middle"`、`cachemax=400`。**400 为脚本文件数额度**，不是像素内存字节额度；还会受用户缓存等级和模式调整。

Toshiue 对 UI 缓存增加了更明确的展开与去重：`<auto>` 可以展开成多个图片路径；`cacheExec` 分 async/bind/del 三种操作；system UI 用异步，title UI 用同步 bind。

标题缓存等待链为 `title_cachewait()` → `setScriptStatus(4)` → 每帧 `isLoadingSurface(nil)` → 完成后 `setScriptStatus(0)`。原脚本中通用 `waitImageCache()` 整段是注释，不能把它当作现在每次背景切换都在运行的等待逻辑。

这些证据支持后续检查取消请求、重复绑定计数与完成状态的一致性；本轮没有重新引入此前已回退的异步队列改造。

## 游戏脚本、AST 与 ASB 的解析范围

按归档文件名排序、后层同路径覆盖，只在工作区提取脚本类资源：

| 项目 | otomeriron | Toshiue |
|---|---:|---:|
| PFS 归档 | 9 | 2 |
| 有效脚本/配置文件 | 269 | 111 |
| 文本中不同的 `e:` 调用名 | 38 | 39 |
| 文本中不同的字面量低层标签候选 | 60 | 83 |
| AST 高层命令种类 | 37 | 36 |
| ASB 文件 | 3 | 3 |
| save.asb 记录数（含标签） | 12 | 12 |
| script.asb 记录数（含标签） | 185 | 181 |
| ui.asb 记录数（含标签） | 165 | 165 |

六个 ASB 均为 `ASB\0` / 版本字节 0，全部记录及结尾边界验证成功。保留指令名、参数字典、原行号和标签，不通过执行游戏获得结果。

AST 中大量 `rt2`、`fg`、`vo`、`extrans` 等是游戏 Lua 脚本层命令；ASB 通过 `calllua` 等调用脚本函数。不能把它们不在原生注册表中，直接理解为移植版漏实现。`script.asb` 含有选项、delay、poptag/popfunc、estag、movie_play 等调度标签。

原始文本、ASB JSON、函数反编译、逐项调用位置均保存在 `build/native-script-audit-20260914/`。只读扫描不是完整控制流覆盖，条件分支、动态生成参数和运行时注册仍需单独验证。

## 建议后续顺序

后续以 **Toshiue Rev.3257 为行为主参考，otomeriron Rev.3144 为辅助回归参考**。新增参数缺口、别名核对与 PSV 可移植性见 [Toshiue 移植评估](SCRIPT_PORTABILITY.zh-CN.md)。

1. 优先补 `indentmodify`，同时核对 `indent logicalrange` 和 `range=0`：选中消息层、清空/弹出缩进状态、普通执行与回放一致；测试姓名/正文/副语言分别清除、嵌套括号跨行、分页和读档恢复。
2. 对 Toshiue 做“启动初始化—标题缓存—正文—分页—backlog 返回”的离线脚本兼容用例。按用户要求，不启动其 Windows EXE。
3. 比较当前宿主与原生对重复 bind、cancel→unbind、路径查询/全局查询、退出游戏的语义；以现有稳定异步基线为准，先测后改。
4. `animedel`/`linkreset` 可另组做已有入口的别名兼容；未实际调用的 `lysave`、`backloglayer` 等保留地址与参数线索，出现真实使用场景后再实现；桌面/其他主机平台 API 单独按能力处理。
