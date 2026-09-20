# 按游戏脚本映射识别正文／姓名字号（2026-09-15）

## 依据

旧 `legacy-font-roles.patch` 增加 `.mw.adv_name/adv_adv/adv_sub` 后缀匹配，
仅覆盖已知命名。任意前缀、任意 ID、语言或消息框切换都不应靠继续追加字符串处理。

IDA：otomeriron `chgmsg` 注册指向 `0x1400D1B10`，结束标签指向 `0x1400D1FD0`。
处理器读取 `stack/id/layered`，按完整 ID 查找／创建文本对象、更新
`s.current_message_layer`；没有看到根据 `adv_name` 等后缀划分字体角色的逻辑。
本结论来自该处理器，不把“全引擎不存在任何角色状态”作为未经证实的结论。

实际脚本提供了语义入口 `mw_getmsgid(role)`：

- otomeriron、Toshiue：`game.mwid .. '.mw.adv_' .. role`。
- SHUF00002、PCSG01297：`game.mwid .. '.mw.' .. role`。
- PCSG01127 的旧脚本没有该函数，直接写 `1.80.mw.name/adv`。

`getMWID('name')` 是消息框 UI 元件定位，在部分游戏会指向姓名背景图片，
不能用它代替 `mw_getmsgid('name')` 的文字层。

## 新实现

解释器通过受保护的 Lua 调用读取 `mw_getmsgid('name'/'adv'/'sub')` 返回的完整 ID，
渲染器保存本次游戏的精确角色映射。没有修改 PFS、Lua 文件或原函数定义。
字号仍是宿主显示覆盖，不写回脚本 font 参数和 backlog 标签。

- name 使用姓名倍率，adv 和 sub 使用正文倍率。
- 即使 ID 完全不含 name/adv/mw，也能生效。
- 有有效 resolver 时，未返回的 ID、子层、菜单、存档及 backlog 不靠后缀猜测。
- resolver 返回 nil/false/空字符串表示该角色不存在；三个都空不回退后缀。
- 返回错误、错误类型、过长 ID、NUL 时保留上次有效映射，后续边界重试。
- 姓名与正文／副语言共用一个 ID 时取消该冲突 ID 的分类，不随意决定倍率。
- resolver 不存在时才保留历史命名兼容，避免旧原生游戏字号设置失效。
  因此这不是对所有未知脚本的自动语义理解；未知布局应提供明确映射。
- 引擎默认正文层仍应用正文倍率。

查询在消息层切换／出栈、宿主字号设置和读档恢复时触发，不在逐帧绘制或逐字
光栅化循环里执行。每次查询至多调用三个角色；函数由游戏实现，并非静态 AST 猜测。
只有映射或倍率真的变化才重新光栅化受影响的现存页面、清除排版与绘制缓存。
缺少原字体时不提交半更新；保留文本、ruby、逐字进度和脚本字号。

只会更新最近 resolver 返回的角色，不无限累计曾经出现过的图层，避免旧 UI ID
复用时仍被当作正文。关闭覆盖后恢复逻辑字号。

## 验证

- 核心常规测试 490 通过，17 项默认忽略。
- 两项字体依赖回归单独运行通过，包含旧字号开关、动态角色互换、ruby、恢复和失败回滚。
- 解释器测试 235 通过、1 忽略；覆盖任意 ID、运行中改映射、缺失、nil、异常、非法返回和冲突。
- Direct release 与 VPK 构建通过。
- Vita3K 四页可视验证：自定义 ID、姓名正文互换／副语言移除、显式空映射、无 resolver 的历史回退。
  故意加入名为 `100.mw.adv_name` 的非消息层：有 resolver 时保持 24，移除 resolver 后才走旧回退到 30。
- 本轮没有实机验收，没有重新遍历所有游戏剧情。IDA 证据和截图存于
  `evidence/font-role-mapping-20260915/`；四页脚本在 `fixtures/font_role_mapping/`。

测试只使用独立 TEST_FONT_ROLES 目录、独立字号配置（姓名125%／正文150%）和测试字体。
结束后归档临时入口、存档和字号配置并恢复 last-game.txt，用户各游戏设置不变。

新补丁 `patches/script-message-font-roles-core.patch` 应用基线为 core `ceb8e38`。
旧 `legacy-font-roles.patch` 保留作历史重放，并注明后继方案；不是再给旧补丁加游戏特例。
测试包 `build/font-role-mapping/art3m1s-font-role-mapping.vpk` 已安装 MCP Vita3K，
校验值在 deployment.json。本轮不增加版本号或 tag。

核心提交：`ffe6094`。
