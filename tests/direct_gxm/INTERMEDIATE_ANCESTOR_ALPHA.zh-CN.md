# 半透明立绘补片合成修复（2026-09-15）

## 原因与修正

用户两张截图对应 otomeriron `script/c20_01a.ast` 的 `0003_00035/36`
和 `0003_00055/56`。两处 `fg` 均指定 `alpha=128`，不是应当完全不透明的人物。

`system/image/image_fg.lua` 在 `init.game_fgsynthesis == "on"` 时给 base 子组
设置 `intermediate_render=1`；人物透明度由 `image_postween` 写到上级。
旧 compositor 只把合成组自身 alpha 延后，仍将祖先 alpha 分发给身体和脸部。
同一位置的身体与表情因此各自半透明叠加，出现明显亮块。

修正：进入 intermediate 组时将内部继承 alpha 重置为 1；该组最终合成 alpha
设为 `parent_opacity * props.opacity()`。内部图层仍保留自身 alpha，普通兄弟层
仍照常继承上级透明度；嵌套组逐级应用一次。shader 源码和字节码、资源、缓存额度不变。
这是所有 intermediate 组的通用修正，没有按游戏名或图片路径特殊处理。

## 原版对照

只提取相关 Lua、两个人物身体及表情 PNG 到项目 build；未改 PFS 或游戏资源。
原版 otomeriron.exe 只在隔离目录运行、自行截图并退出；未启动 Toshiue EXE。
原版直接使用与 Vita3K 相同的压缩 PNG，脸部正常，排除本次资源压缩损坏的猜测。

6 页探针包括模式 1 / 2 的父层半透明重叠、两张实际立绘、三层半透明嵌套、
灰度模式 2 的透明覆盖。原版截图 960×540，Vita3K 输出 960×544；
未把不同高度截图声称为逐像素完全相等。

| 平坦内部采样 | 原版 RGB | 旧 Vita3K | 修复 Vita3K |
|---|---|---|---|
| 模式 1，红色身体上的蓝色补片，父 alpha128 | 0,0,128 | 混入红色，视觉为紫色 | 0,0,128 |
| 模式 2，同上 | 0,0,128 | 未单独保存旧图 | 0,0,128 |
| 嵌套三个 alpha128 | 0,0,32 | 未单独保存旧图 | 0,0,32 |

样点原版(120,120)、Vita3K(120,121)，均远离边界。两个人物修复前后图和原版图
位于 `evidence/group-alpha-20260915/`。脸部亮块已消失；灰度页仍透出蓝背景。
当前证据来自原版与 Vita3K 独立场景；完整游戏流程和 PSV 实机本轮未验收。

## 测试、构建与复现

- 新增祖先 alpha / 嵌套边界两项测试：修改前失败，修改后通过。
- 489 项常规核心测试通过；16 项默认忽略，本轮未运行这些忽略项。
- Direct release 核心及 VPK 构建通过，MCP 安装后六页对照通过。
- core 提交 `ceb8e38`，基线 `4e15ea2`；可重放补丁
  `patches/intermediate-ancestor-alpha-core.patch`。
- VPK：`build/otomeriron-group-alpha/art3m1s-group-alpha-fix.vpk`。
  包及已安装 SELF 的 SHA256 在证据目录 `deployment.json`。

`fixtures/group_alpha/native.iet` 自动截图退出；`vita.iet` 各页按 ○ 推进。
将提供的 system.ini 与探针放到独立目录，补齐以下用户本地资源即可复现，
不要向正式游戏目录解包：

| 源资源（otomeriron PFS） | 探针文件名 |
|---|---|
| image/fg/res/z1/res_z1a0000.png | res-body.png |
| image/fg/res/z1/a0003.png | res-face.png |
| image/fg/lun/z2/lun_z2a0100.png | lun-body.png |
| image/fg/lun/z2/a0100.png | lun-face.png |

原版执行只在该隔离目录复制获准的 otomeriron.exe，并隐藏启动、设置超时。
Vita3K 使用 MCP 截图作为验收证据，不使用探针内部 savess 结果作为 GPU oracle。
临时 `TEST_GROUP_ALPHA` 游戏及存档在结束后移出运行目录、归档到 build，
恢复原 last-game.txt；原用户游戏存档未修改。无版本号或 tag 变更。
