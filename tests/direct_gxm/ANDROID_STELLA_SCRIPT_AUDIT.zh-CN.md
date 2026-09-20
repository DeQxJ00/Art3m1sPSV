# Stella Android libartemis：命令、Lua 参数和加载逻辑补充审计

日期：2026-09-15。对象由用户提供，IDA MCP 为 `127.0.0.1:13345/mcp`。本轮只做静态分析、读取归档和记录，没有运行 Android 库、修改引擎或部署测试包。每次 IDA 调用前均检查 `server_health`。

## 范围和结论

输入是 ARM32 `libartemis.so`，保留 C++ 符号。`0x311BC8` 的版本串为 `**** Artemis Engine Rev.3292 ****`。输入 SHA-256、归档索引指纹和对照提交见 [summary.json](evidence/android-stella-rev3292/summary.json)。

| 项目 | otomeriron Windows | Toshiue Windows | 本次 Android |
|---|---:|---:|---:|
| 引擎 revision | 3144 | 3257 | 3292 |
| 低层指令注册数 | 151 | 153 | 153 |
| CLua 注册数，含 `__tostring` | 53 | 54 | 54 |

Android 的 **153 个指令名、54 个 Lua 名称均与 Toshiue 一致**。名称集合相同不代表各平台实现相同，也不代表当前移植版已完全兼容。

本轮新增的重点记录是：

1. **Lua table 转低层参数的类型规则**，当前实现存在可定位差异。
2. **连续 `$` 的多轮求值，以及求值在 tag filter 之前**。
3. **异步加载模式 1 / 2 的差别、超预算丢弃预载项、解码后的提交检查**。
4. `intermediate_render` 分离“开启离屏”与“选择格式”，补齐带符号的调用链证据。
5. Android 平台命令的真实实现和空实现，以及 Stella 资源内的条件调用。

以上没有证明此前某一张错误截图的唯一成因；需要用受控实验建立因果关系。

## 1. Lua TableToTag：比单个游戏特例更值得优先验证

入口：`artemis::CLua::TableToTag`，**`0x62F9C8`**。`Tag`（`0x6262C8`）和 `EnqueueTag`（`0x62643C`）共用该转换。

对 table 形式，反编译中的 Lua 类型判断明确区分 number=3、string=4、table=5：

| 输入 | Android 本构建的处理 | 当前移植版 |
|---|---|---|
| 数字键 `1`，字符串值 | 指令名 | 指令名 |
| 字符串键，字符串值 | 保留字符串 | 保留字符串 |
| 字符串键，普通有限数值 | 转 `float`，`floorf`，再转整数文本 | 数字转字符串，保留小数 |
| 字符串键，布尔值 | 不写入参数表 | 大部分转为 `"true"` / `"false"` |
| 其他数字键 | 不作为普通参数写入 | 当前可转成数字文本键 |
| table / function 等其他值 | 未进入 number/string 写入分支 | 大部分跳过 |

例如，**这个 Android 构建的直接调用**中，`left=1.9` 与 `left="1.9"` 不等价；`left=-1.2` 的向下取整也不能换成向零截断。极大数、NaN、Infinity 的转换边界未验证。

当前代码位置：`build/heap-audit/controls-source/core/crates/asb-interpreter/src/lua_engine.rs` 的 `tag`、`enqueueTag` 等 table 转换入口。现有 `lyc mask=false` 特例仅跳过这一组合，其余布尔字段仍可能成为字符串。这里存在一个更通用的兼容性问题，**不能据此立即删除特例并全局改规则**：应先核对 Windows/PSV 原生行为及现有宿主生成的调用。

Stella 的 `system/adv/func.lua` 中 `tag(p)` 只按 `p.eq` 选择 `e:tag` / `e:enqueueTag`，并没有统一把所有参数 `tostring`。因此不能假设 Lua 包装层已经消除了类型差异。

建议实验：用自定义 tag filter 收集 `true/false`、`1.9/-1.2`、对应字符串、数字键 `2`、字符串键 `"0"`；分别走直接和排队入口。核对参数是否存在及实际字符串，再测试正常图片 mask 和 shader 小数常量。此组尚未做 Android 动态验收。

## 2. 参数求值和过滤器顺序

入口：`artemis::CArtemis::Command`，**`0x5C52D0`**。

- 扫描每个参数值开头连续出现的 `$` 数量。
- 去掉这些前缀，调用 `CCompute::Computing`，把结果转为文本；前缀有几层就进行几轮计算。
- **参数求值先完成，之后才调用 `CLua::FilterTag`**。恢复/回放标记及脚本来源还有绕过 filter 的条件。
- filter 正常返回非零时消费该指令；否则查低层指令表，未命中再交给 `CommandMacro`，不是直接认定未知。

当前 `expression.rs::resolve_param*` 去掉首个 `$` 后处理内部美元符号，再做一次求值；`interpreter.rs::execute_instruction_impl` 把原始 `instruction.params` 传给 tag filter，后续各 handler 自行求值。这与上述原生路径并非完全一致。

建议实验：令变量 A 的内容为另一条表达式，对比 `$A` / `$$A`；让 filter 记录输入并在返回前修改变量，检查 handler 使用的是 filter 前还是 filter 后的值。需同时覆盖普通指令、宏、排队指令与读档回放，避免重复求值带来的副作用。

Stella 静态脚本中有 `setTagFilter`；本轮找到的 `$$` 文本出现在 Siglus 转换说明的注释中，**不能把注释认作实际调用**。这是通用兼容缺口，不是已证明的 Stella 当前阻塞点。

## 3. 解析器：位置参数和特殊入口不要误报为新命令

`CScriptParser::ParseCommand` **`0x559CEC`** 会把无 `=` 的位置参数按 `"0"`、`"1"`……写入 map；重复命名参数对已有值赋值。读取双引号值与普通 token 走不同分支。当前 `script.rs::parse_instruction` 已有位置参数处理，本轮不把它列为新增缺失项。

`CScriptParser::Parse` **`0x55979C`** 有行命令、标记命令、正文的分流；`ForwardToWhiteOr` **`0x55B728`** 还处理转义字符和不同字符集的多字节边界。不能只按空格切分整份 IET，也不能从普通字符串扫描未命中推断命令不存在。

注册表中的 `\vgoto` 是实际首字节 `0x0B` 的内部名称，CSV 用可读转义表示。`lua` 的特殊解析与空的普通 handler 应分开判断。

## 4. Surface 缓存和异步提交的新证据

| 函数 | 地址 |
|---|---|
| `CSurfaceManager` 构造，接收 `unsigned long long` 预算 | `0x51A5B8` |
| `Load(path, int mode)` | `0x51AD00` |
| `LoadThread` | `0x51C888` |
| `Correction` | `0x51DBF0` |
| `ClearAsyncLoadQueue` | `0x51D944` |
| `IsAsyncLoading()` / `(path)` | `0x51D97C` / `0x51D9B0` |
| Lua `BindSurfaceAsync` | `0x62A574` |

### 已确认的控制流

- `Load(path)` 无模式重载传入 **0**；Lua `bindSurfaceAsync(path)` 明确传入 **1**。
- `Load(path, mode)` 入队时另保存 **`mode == 2`** 标记。worker 对该标记检查 `使用计数 >= 预算`：到达上限便移除该队列项并通知等待者，跳过解码；模式 1 不走这条预算丢弃分支。
- 此处预算是构造传入的 64 位字段（对象 `+48`），计数为 `+56`，加载成功会加入 surface 的字节大小。**不是 Android 统一固定多少 MiB，也不是可直接照抄的 PSV 堆大小**。本轮未完整追溯模式 2 的所有发起者。
- worker 持锁取任务和保留 surface 引用，然后**解锁进行加载**，再上锁提交。
- 提交前重读队列，比较当前队首路径与刚加载的路径；队列清空/改变后，不会无条件把刚完成的结果塞回活动表。
- `ClearAsyncLoadQueue` 持锁清队列，本身没有等待 worker 结束；不能当成取消正在执行的解码并 join。
- 同步请求遇到队首正在加载的同文件，会等待条件变量，再调用加载入口重新检查；遇到后面的待加载项，有移除该项并同步加载的路径。
- 可复用表命中时把**同一 `shared_ptr<ISurface>`** 移回活动表，并设置绑定计数；不是命中后再次解码。
- `Correction` 从历史队列选择可复用项删除，按表面大小扣账，直到满足预算或无可释放项；不是清空正在显示的活动表。
- `IsAsyncLoading` 查待加载队列，不是“图片是否已经驻留”的接口。

### 移植时的限制

这补充了“必需加载”和“可放弃预载”的不同策略，适合用于设计优先级及取消/完成状态。但 Android 的队首文件名检查不能自动证明 PSV 同名重排队、跨章节、快进时的生命周期安全；移植版仍应验证任务身份和 GPU 引用存活。

本轮没有改动曾发生过卡死的 `bind_surface_async` 实现，也没有把其验收状态改成“全部完成”。

## 5. PNG 信息读取不是自动附着在解码缓存上的

Lua `LoadPngComments` **`0x62AB88`**：创建 `CArtemisPackFile`、按传入路径打开文件，再调用 `CPng::LoadComments`；这条入口没有先查询 `CSurfaceManager` 的已解码表。

因此不能声称 Android 原生一定把 PNG 附加信息和解码 surface 自动绑定成一次缓存。我们已有的 PNG 附加信息缓存仍有价值；本轮证据只覆盖这个 Lua 入口，不能否定包文件层或其他调用点还有缓存。

## 6. intermediate_render：带符号的格式选择证据

`CommandLyprop` **`0x5971F8`**：

- 读取 `intermediate_render` 后先调用 `SetIntermediateRender(bool)`。
- 字符串 `"0"` 关闭，`"1"` / `"2"` 都开启。
- `"1"` 传给 `SetIntermediateRenderFormat` 的枚举值为 **2**；`"2"` 传入枚举值 **1**。
- 设置 mask 是另一个参数入口 `intermediate_render_mask`。

`CLayerSet` 的 vtable 符号位于 `0x6B97B8`，实际 address point 为 **`0x6B9850`**（不能按符号地址简单加 8）。其 +328 / +332 分别指向：

- `SetIntermediateRender`：`0x50B344`。
- `SetIntermediateRenderFormat`：`0x50B72C`。

开启时已有中间对象则复用；关闭时释放中间对象引用。格式相同时直接跳过重置；格式改变时清理已绑定表面并通知重绘。

这进一步证明 mode 2 是离屏格式选择的一部分，**不是“强制当前整层 alpha=1”**，也不是双缓冲数量。这里不凭枚举数字推断具体 GL/GXM 像素格式；此前 Windows 像素实验和用户对 `intermediate_render=2` 的验收继续有效，无需因这次审计回退。

## 7. Android 平台差异

| 项目 | Android 本构建证据 | PSV 处理含义 |
|---|---|---|
| `appreview` `0x5B7A70` | 调用 `ReviewManager_requestReviewFlow`，记录失败码 | Android 评价入口；Windows 空 handler 不代表全平台空操作 |
| `purchase` `0x5B7144` | JNI `AndroidApplication.InAppBilling`；读取商品/结果变量/验证及消费相关参数 | 商店计费与回调，不是可简单成功返回的剧情开关 |
| `statusbar` `0x581AC4` | 调用 `CPlatform::SetStatusBar`，写 `s.status.statusbar` | 平台 UI 状态；不是游戏图层或消息框 |
| `ime` `0x591E2C` | 此入口为空 | 不能据此删除 Windows 输入框语义 |
| `trophy` `0x5B7B14` | 此入口为空 | 不能据此推断 PSV 原生奖杯为空 |
| `initNis`、`callShellExecute`、`writeClipboard`、`getWindowHandle` | 本构建主要校验参数、日志/空返回；窗口句柄返回空 Lua object | 存在注册名不代表具备 Windows 功能 |

`setHttpResponse` 仍只是 CLua 内部字符串设置入口；没有找到足够证据把它解释成普通 HTTP 请求的替代结果。本轮没有关闭此前对此接口完整用途的疑问。

## 8. Stella 资源中的实际使用与优先级

从当前模拟器目录 `Stella_of_the_End/root.pfs` 只读提取 **147** 个脚本/表文件到 `build/android-stella-audit/stella/`；没有把解包内容放进运行目录，没有改 PFS。扫描得到 38 种 `e:` API 调用、83 种字面量低层调用。

限制：`system/save.asb`、`system/script.asb`、`system/ui.asb` 三个二进制文件不在本轮文本扫描内；运行时生成指令也可能漏检。扫描结果见 [stella-usage.json](evidence/android-stella-rev3292/stella-usage.json)。

- 未注册调用仍主要是已记录的 `callOeAPI`（Switch 分支）、`initNis`（能力检查分支）、`trophy`。`val` 仍出现在 Switch 对话框分支，Android/Toshiue 的低层注册表均没有该名称，不能当成新指令照单实现。
- `backloglayer`、`lysave`、`ime`、`unison` 的直接低层调用未在本次文本扫描中发现；其旧验收缺口没有因此消失。
- 包内 `system/table/list_android.tbl` 设有 **`autocache="none"`**。这只是包内默认，不能代替当前平台覆盖表及用户配置后的有效值。
- `system/image/cache.lua` 分开管理剧情 `setImageStack` 和 `system_cache` 的 UI 预载。剧情预载有文件数上限；清理时先清队列、再反向 unbind。
- 该脚本的 `gotoScriptCacheWait` 在 Vita 分支插入配置时间（缺省 1）的 wait，**不是显式逐个等待 isLoadingSurface 为 false**。

建议后续顺序：

1. **Lua 参数类型矩阵**：跨 Android 静态证据、Windows 原版小实验、当前解释器比对；优先解决普遍规则与现有单项特例的关系。
2. **多层 `$` 与 filter 时序**：先做无画面副作用的小用例，避免全局改动造成二次求值。
3. **预算不足/清队列/同步抢占异步的组合测试**：用失败图片、同名重排队和快进场景验证；通过后再讨论调整加载实现。
4. 商店/桌面平台接口保持低优先级；不为了“注册数一致”伪造成功结果。

## 证据与检查

- [commands.csv](evidence/android-stella-rev3292/commands.csv)：153 个名称、注册调用位置、handler 地址和写入 handler 的汇编位置；含短字符串立即数还原，未用函数名猜注册名。
- [lua-methods.csv](evidence/android-stella-rev3292/lua-methods.csv)：54 个 Lua 注册项。
- [decompile-index.json](evidence/android-stella-rev3292/decompile-index.json)：本轮关键反编译输出的大小、哈希和原型；原始输出留在本地 build 审计目录。
- 完整性检查通过：名称唯一性 153 / 54、两套名称集合与 Rev.3257 相同、153 个 handler 写入位置均已解析。常规命令别名关系与 Windows 一致，平台空 handler 的合并情况单独处理。
- 本轮没有 Android 动态实验、实机性能测试或 VPK 更新；没有把静态规则标成已通过实机验收。
