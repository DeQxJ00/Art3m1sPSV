# 新版原生指令移植与验收

2026-09-15；主参考 Toshiue Rev.3257，辅助参考 otomeriron Rev.3144。仅静态读取 EXE、提取少量资源，没有启动 Toshiue EXE。宿主仍为 `host-direct`，没有改动 shader 源码或二进制。

## 已实现

| 指令／行为 | PSV 的处理 |
|---|---|
| `indentmodify` | 对当前消息层：正数弹出 N 层缩进，负数清空，0 或缺省不修改。按指令出现的位置生效，保留之前已排文字的位置。 |
| `indent range=0` | 不限识别范围，与原生一致。 |
| `indent logicalrange` | 有限 range 开启逻辑行计数时跨自动折行累计；显式换行重新计数。关闭时按显示行计数。 |
| `rp` 与缩进 | 分页保留尚未解除的缩进；不同消息层独立；加入本引擎存档／历史的回放状态。 |
| `animedel` | 复用 `lydel`，按原生注册别名删除目标图层与动画。 |
| `linkreset` | 复用 `linkenable`，恢复当前消息层的链接。 |
| `appreview` | 与所参考 Windows 原生一样为空操作；不伪造平台商店功能。 |

内部 `__art3_indent_state` 仅承载本引擎回放状态：使用十六进制编码 JSON，绕开 IET 属性对反斜线引号的解析差异。不是新增原生游戏指令，也不表示能读取原版 Windows 存档。

配置在正文打印前设置的场景已覆盖；中途更换整套 indent 配置、跨页后改变字号对继承缩进像素的缩放仍需单独补充验收。未将 Windows 句柄、网络同步、IME、NIS 或整套 backloglayer 接口冒充为已移植；其评估仍见 SCRIPT_PORTABILITY.zh-CN.md。

## 测试包

输出：`build/native-command-port/host/art3m1s_direct.vpk`，资源：`build/native-command-port/native-command-test-resources.zip`。资源目录为 `ux0/data/art3m1s-gxm/games/TEST_TOSHIUE_CMD` 和 `TEST_OTOMERIRON_CMD`，不覆盖原游戏或原游戏存档。

每款只提取原版 Log 按钮 PNG 和字体；字体裁成验收文本的字符子集。每目录的 provenance.json 记录原档路径、偏移、哈希及相关指令的少量行号证据。完整字体和分析副本只放构建目录，不部署、不提交 Git。

每套六页，前四页用 ○／触摸翻页；第 5 页必须触摸文字链接，第 6 页结束。□ 可呼出宿主菜单退出测试。

1. 双层缩进 → 弹一层 → 清空，文字左边界应逐级左移。
2. `rp` 后仍缩进；姓名层独立；正文清空缩进后回左边界。
3. 两列在「之前自动折行；左列第三行缩进，右列第三行回到原左边界。
4. 原版 Log 图移动且持续可见。展示的是原 PNG 图集，不是重制按钮。
5. 按钮应被删除，`appreview` 后代码继续。链接先禁用再恢复；触摸链接进入第 6 页。普通翻页只重进第 5 页，避免误判链接通过。
6. 完成页应保持显示，不再依赖动画刷新。

测试脚本会在每页等待 300 ms 后通过 takess/savess 保存一张截图，位置在对应测试游戏的保存目录 `savedata/cmd-case-N.png`。这是验收辅助操作，不用于性能测量。

## 检查证据

- 核心：464 通过、15 忽略；解释器：230 通过、1 忽略。新增测试覆盖 range=0、逻辑范围、pop/clear、分页与层独立、缓存失效、序列化后的真实 IET 解析与回放。
- 用原字体运行已有字号覆盖回归：1 通过。
- 实机 333 MHz 启动：`retained=1 local_base=1 overlay=1`，两套自动截图已取回。并非以模拟器帧率代表实机表现。
- Vita3K 静态页闪黑已定位：离屏自检返回空图，两张空图的相对比较误判通过。关闭 overlay 后显示恢复；正式修正要求相对比较同时通过绝对像素／缓存重放检查。无临时标志时，模拟器自动使用直接重绘，实机仍保留缓存。
- 原 fixture 第 3 页宽度 112 没有让原字体在括号前换行，修正为 80 后可观察左右第三行缩进差异。

原始日志／截图均在 `build/native-command-port/`：`mcp-static-fix.log`、`fixed-*.png`、`psv-TEST_*-*.png`、`psv-static-fix-startup.log`。资源重新生成后已通过 FTP 逐文件校验。

## 源码与复现

core 基线 `319c696` → `2cf4ab8`；根仓库保存 `patches/native-commands-core.patch`。对已有新 core 不要重复应用；对基线先 `git apply --check` 再应用。

本机编译入口：`scripts/build-native-commands-core.ps1`、`scripts/build-native-commands-host.sh`。后者由 `wsl -d Ubuntu-24.04 -- bash scripts/build-native-commands-host.sh` 执行。路径对应本机已验证工具链及 core 工作树，不代表独立上游仓库可以直接编译。

资源生成：`python scripts/prepare-native-command-tests.py`；仅测试目录部署：`python scripts/deploy-native-command-resources.py`。

VPK SHA256：`79d80f99e9c4802884184f60e59ead5b2040d332402b5c220900a47bb7ff4679`。

SELF SHA256：`662be67333ece75f499165a270f1a7eabc852b732f37523f9ad0c636d37d29f2`。
