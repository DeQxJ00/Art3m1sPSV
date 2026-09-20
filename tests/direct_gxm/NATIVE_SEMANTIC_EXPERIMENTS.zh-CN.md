# 原生语义疑点实验（2026-09-15）

此清单跟踪用户要求的定向实验，不把旧审计的“当时缺失”直接当作当前状态。
`intermediate_render=2` 透明覆盖已修正，用户确认验收通过；不再作为待验证项。

## 实验范围

| 项目 | 当前证据/疑点 | 实验 |
|---|---|---|
| `indent` 动态换配置 | 基本缩进已实现并验收，中途换 pair/range/nest 的既有文字及栈语义未验收 | 原版自动截图，比较已排文字/后续文字、显式与自动换行 |
| `rp` 后改字号 | 已有跨页缩进继承，但继承值按像素或随字号缩放未验收 | 先缩进、分页、切字号、打印；原版与核心布局位置对比 |
| `voice` / `/voice` | 当前结束标签显式空操作；不能等同于游戏 Lua backlog 的语音按钮 | 核对原生进入/退出范围、历史记录内容；独立脚本重放 |
| `automode syncse` | 已实现：control→automode_sync_ready 等待指定声音；events.rs 留有过时注释 | 短文本+固定长度 SE，统计停句至自动推进的时间；对照缺省/空列表/指定 ID |
| `video skip=2` | 当前按可跳过处理，与 1 的输入来源区别未保留 | IDA 分支+受控视频，分别普通确认/取消/菜单输入 |
| surface 加载接口 | 主流程已知；重复绑定、取消、按路径查询与全局查询边界需核对 | 重复 bind→unbind、队列清理、缺失文件、重新入队；先测稳定基线 |
| `lysave` | 当前未注册；已知图层导出入口，范围、坐标、透明、子树与落盘细节未全验收 | 原版导出单图/分组/半透明图层，检查 PNG 像素、尺寸、扩展名 |
| `backloglayer` | 当前未注册；解析入口已找到，分页/绑定/滚动查询需闭合 | 原版最小容器写入、清除、查询、边界页实验 |
| `saveToMemory/loadFromMemory` | 当前未注册；原版内存保存/恢复，载体/状态边界待验证 | 字符串长度、变量/图层/脚本位置与恢复时序；不写用户存档 |
| `setUseGlobalCapture` | 已闭合：Windows 输入焦点检查开关 | 初始化、setter 与消费者已交叉核对，非截图选项 |
| `ime` | 当前未注册；参数已知，type/取消/变量写回边界待验证 | IDA 参数与返回路径；需要交互的验收另注明，不能当空操作成功 |
| `unison/setHttpResponse` | 网络相关，完整协议未闭合 | 静态追踪；如需请求，只用自建本地回环服务与虚构数据 |
| `trophy` 等平台接口 | Windows 空实现不能证明 PSV 行为 | 对照原生 PSV eboot 的注册与消费链，标明平台差异 |

## 判定规则

- IDA、当前实现、可观察输出分别记录；单凭标签不报错不算验收。
- 原版主参考是 Toshiue Rev.3257 的静态分析；动态辅助使用已允许运行的
  otomeriron Rev.3144。不得启动 Toshiue EXE。
- 构造资源和输出只放 `build/native-semantic-experiments/`。
- 不使用 Computer Use；优先原版自运行脚本导出结果、核心测试及可用的 Vita3K MCP。
- 没有对应硬件或真实调用的项目标明验证边界，不擅自声称已完全兼容。

## 已排除的旧缺口

`indentmodify`、`indent range=0/logicalrange` 基本路径、`animedel`、`linkreset`
和 Windows `appreview` 兼容已见 `NATIVE_COMMAND_ACCEPTANCE.zh-CN.md`。
Windows 窗口句柄/NIS、Switch 系统调用不自动扩充为 PSV 功能。

## 进行中的结果

### setUseGlobalCapture：输入焦点门控，已静态闭合

otomeriron `0x140134720` 检查 bool 并写对象 `+14476 / 0x388C`。
扫描该字段的指令读写点，只有初始化、该 setter 和 `0x1400B5870` 查询。
初始化 `0x1400B5370` 设为 0，写入 `CArtemis::CArtemisInput` 虚表。
查询在值为 0 时调用 `GetForegroundWindow`，与两个所属窗口句柄比较；
值为 1 时直接返回 true。因而含义是绕过输入的前台窗口检查。
这不是全屏截图、纹理缓存或视频颜色转换设置；PSV 无需模拟 Windows 窗口句柄。

### 自运行探针：已可生成原版结果

独立 otomeriron EXE 通过 IET 的 takess/savess、lysave、exit 自动产出并退出。
所有输出在 `build/native-semantic-experiments/`，没有写用户存档。
Lua io 可写入该实验目录；saveToMemory 返回 string（最小场景 1231 字节），
开头含 0 字节，不能按普通以 NUL 结尾的文本复制。状态往返验证尚未完成。
setHttpResponse 的字符串 setter 可调用，但这只验证参数入口，并未发网络请求，
不能据此宣布 HTTP 响应生命周期已兼容。

### 内存保存的变量范围：已做原版往返

先把 `t.probe/f.probe/g.probe/s.probe` 均设为 17，saveToMemory 保存，再均改为 99，
loadFromMemory 后观察：t=0、f=17、g=99、s=99。该最小场景保存载体 1248 字节。
临时变量清空、存档变量恢复、全局/系统变量保持，不能直接恢复全部变量。
原始脚本、snapshot.bin 和结果在 `memory-vars/`。图层、文本和脚本位置的完整
往返仍要分开验证；本结论仅对应已运行的变量实验。

### 缩进：原版边界输出已取得

`indent/`：28 号字创建缩进，rp 后改成 56 号字，缩进保留旧像素距离，不翻倍；
后续变更 pair/nest 并不自动清除继承的缩进。
`indent-midpage/`：同一页中更换括号配对配置，已经排好的第二行没有向左跳动，
后续第三行仍保持现有缩进。旧 core 重解释了旧文字，第二、三行向左跳；
修复后 Vita3K 第二、三行左缘约 x=90，与原版 x=60 按 1.5 倍显示一致。
对照图片保存在 `evidence/native-semantics-20260915/indent-{before,fixed}.png`。

修正将配置变化记录在当前字形位置，保留旧字形的配置及已有缩进栈；
翻页继承最后一次配置；布局与绘制缓存均把这些配置事件纳入键和占用统计。
回归测试同时覆盖旧位置不变、新括号生效、翻页后改字号仍继承原像素缩进。

### 自动播放声音等待：第二处已修正差异

原版独立 `auto-click-*` 探针用 1 秒合成 WAV、`s.automodewait=100` 和 `[@]`。
`[stop]` 不会被自动播放推进，先前使用 stop 的探针超时不算声音门控失败。

| 原版设置 | 观察到的推进等待 |
|---|---:|
| 普通 SE，`syncse=""` | 130 ms |
| 普通 SE，列出实际 ID | 2023 ms |
| 普通 SE，列出不在播放的 ID | 124 ms |
| voice，`syncse=""` | 122 ms |
| voice，列出实际 ID | 2020 ms |

这些是此次 Windows 原版探针的墙钟记录，不能直接当作 PSV 音频耗时。
同一 WAV 的匹配 ID / 非匹配 ID 差异确认：空列表不自动等待所有语音。
Toshiue `0x1400DD820` 同样只在参数存在时清空并重建 `+13720` 的 ID 列表。
当前 core 过去把空列表扩充为“等待任意语音”，现已撤销这个推断；
显式 ID 的等待、缺省参数保留原列表仍保留。音频状态回归覆盖空列表、
不存在 ID、多 ID、单独完成、全部完成。

### 同页追加文本：第三处已修正差异

把三种声音等待串在同一页，辅助原版依次经过 166 / 2035 / 132 ms 自动完成。
旧移植版能显示前两行，但追加第二行后 `reveal_pending` 没有重新置位，
完成索引小于缓冲长度且不会再推进，第三个 `[@]` 永久等待。
本轮使追加文本重新更新揭示完成状态，保留已经显示的文字和原动画时钟；
已完成的纯换行同步完成索引，不重播旧文字。
字体资源回归测试已先复现失败，再修正通过；测试需显式 ART3M1S_TEST_FONT。
最终 Vita3K 串联复测三行均显示，并抵达 DONE；控制台记录分别为 64 / 1300 / 133 ms。
这些数据只用来核对声音门控与流程可达性，不作为实机性能结论。

### surface：查询的是在途加载，不是缓存驻留

原版重复 enqueue 同一路径后，按路径 / 显式 nil 查询都为 true；等待后均为 false。
未入队的不存在路径查询 false。unbind、清队列、重新 enqueue、再等待均能完成。
`e:isLoadingSurface()` **省略参数**会报重载不匹配；`e:isLoadingSurface(nil)` 才是全局查询。
Toshiue `0x14013A740` 与辅助原版 `0x140133160` 都区分字符串与非字符串分支。
当前移植层容许省略参数可视为宽容扩展；不能据此声称它与原版加载队列完全等价。
本次没有修改异步加载主流程，也没有重引入此前回退的双向队列方案。

### backloglayer：清除和绑定有状态约束

写入两页后 size=2；未绑定时可见页 min/max=-1、scroll_max=0。
绑定后 min=0、max=1、scroll_max=288（本探针的尺寸）。
绑定期间 `clear=0` 不清除。解绑后 `clear=0` 清为 0；绑定前同样可以清为 0。
因此 clear 是参数存在即触发，且要求未绑定。该命令目前仍未移植，不能用通用
backlog 的“清空数组”代替其显示层、滚动位置和查询语义。

### voice 范围：确认缺少历史重放记录

原版 `get_message_tags` 和翻页后的 `get_backlog_tags` 均保留
`voice,file,tone.wav,id,probe`、范围内 print、`/voice`，前后正文不在范围内。
Toshiue `0x1400F2D60/0x1400F2DE0` 与辅助原版进入/退出处理一致：
进入先调用播放路径，再记标签；退出只记标签，不停止声音。
当前 core 的结束标签显式空操作，范围重放尚未实现。
另观察到原版这两个查询返回逗号分隔的标签字符串，当前核心输出 IET 字符串；
转义和消费者兼容也需单独补实验，不能只补一个结束标签就宣布完整兼容。

### 内存存档：图层和文字往返通过

`memory-scene` 先保存蓝底+文字，改为橙底并追加文字后恢复。
恢复前后两张 640×360 原版截图逐像素一致，快照为 1562 字节。
这补充了前面的变量范围实验；不等于已验证所有动画、Lua 局部状态或外部句柄。
该 Lua 接口当前仍未移植，后续要复用现有保存协议而非直接复制整个运行时。

### lysave：叶图确定，离屏导出仍有疑点

100×100 橙色子层移动至 (40,40)、alpha=128，再置于灰度父层。
原版 lysave 子层导出始终为原尺寸、RGB 原色，不包含位置、父灰度或显示 alpha。
未写扩展名会生成 .png。没有离屏面的 mode=0 分组没有导出文件。
mode=1/2 分组分别导出 140×140 RGBA/RGB，但像素含不稳定数据，不能拿来作为
正确参考实现。屏幕上的半透明 mode=1/2 像素也不同，此组合需进一步独立验收；
不改动已验收的 `intermediate_render=2` 不透明角色回忆效果。

### 视频跳过：确认 1/2 不是同一个布尔值，保留未闭合部分

原版主循环 `0x1400C0470`：skip=1 检查输入组 0 或跳过态；
skip=2 检查输入组 1。两者命中时都写 `s.clickskip=1`。
自运行 3 秒合成 MP4、显式 keyconfig 组0=Enter、组1=Escape：
skip=1+Enter 与 skip=2+Escape 均在注入后约一帧返回且 clickskip=1；
skip=1+Escape 播放到结束，clickskip=0。
skip=2+Enter 出现提前返回但 clickskip=0，需继续区分通用输入推进与视频分支；
不以这个结果直接定义“2 只接受取消键”。当前 core 仍把 skip 非零简化为 true，
本次未贸然改输入策略。所有时间与标记保留在原始结果中。

### 平台接口与网络边界

- `ime type="field"` 的原版探针进入等待，5 秒内无后续标记；已终止该独立子进程。
  Toshiue `0x1400DCCB0` 确认 field 分支与普通 IME 对象分支不同。
  默认值写入、确认、取消的最终行为仍需平台输入适配验收，不伪装空操作成功。
- `setHttpResponse("MANUAL_RESPONSE")` 后对本地临时 HTTP 服务发 GET，
  返回 200 与 `LOCAL_RESPONSE_ONLY`，并未被 setter 覆盖。只证明它不是普通出站
  httpget 响应替换接口，完整的服务器回调用途还未闭合。没有访问原游戏服务器。
- `unison` 使用虚构凭据和 `127.0.0.1:1`，返回“与服务器通信时发生问题”的错误。
  只验证失败路径；完整交换协议仍不明确，不能移植成普通 GET 的别名。
- 五份 PSV eboot 均注册 trophy 且指向非空处理函数，见下表。只作静态检查，
  没有调用奖杯解锁，未把 Windows 的空函数作为 PSV 规范。

| IDA 输入 | trophy handler |
|---|---|
| PCSG01084 | `0x811B79B8` |
| PCSG01127 | `0x811B7FD0` |
| PCSG01201 | `0x811B8CD4` |
| PCSG01235 | `0x811BF838` |
| PCSG01297 | `0x811C0E54` |

注：当前 13340 中实际打开的是 PCSG01235，以 IDA health 的 input_path 为准，
不把它误记成用户较早列出的 PCSG01107。

## 复现与代码保存

25 份独立 IET 位于 `fixtures/native_semantics/`。准备器
`scripts/prepare-native-semantic-probes.py` 只在项目 build 新目录生成文件，
不启动 EXE，不部署，不修改游戏目录。需显式提供已有 CJK 字体；视频实验需传入
合成 MP4；可选复制 otomeriron.exe，禁止启动 Toshiue EXE。

```powershell
python scripts/prepare-native-semantic-probes.py --output build/my-semantic-probes --font <字体路径> --case indent-midpage
```

默认扫描全部实验，所以包含视频用例时需要 `--video <合成视频路径>`。
原版执行应使用隔离目录、Hidden 窗口和有上限的等待；每次使用新目录避免保存状态影响。
IME 等等待用例的超时不是通过。实验截图读取成功也不等于所有像素正确。

三项 core 修正保存在 `patches/native-semantics-boundaries-core.patch`，
应用基线是 `202257d`（此前灰度覆盖修复），core 提交为 `4e15ea2`。
487 项常规核心测试通过，默认忽略 16 项；
其中新增的 1 项字体依赖回归已提供本地字体单独运行并通过，其余 15 项未运行。
原始结果、像素统计和缩进对照在 `evidence/native-semantics-20260915/`。
完整 IDA 响应、原版生成图和 VPK 在 `build/native-semantic-experiments/`。
