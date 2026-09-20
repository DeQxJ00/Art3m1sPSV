# 补丁用途

补丁按功能或问题命名，不使用具体游戏名称、简称或游戏 ID；对应文档和证据目录也遵循这一规则。

本目录统一保存第三方依赖修补记录和核心改动导出。补丁基线各不相同，部分改动已经合入源码；**不要按目录顺序批量应用，也不要向当前源码重复应用**。

| 补丁 | 用途与当前状态 |
| --- | --- |
| `tremor-vita-arm.patch` | Tremor 的 Vita ARM 汇编兼容修复。已合入 `vendor/tremor`；当前宿主链接对应音频库，`scripts/build-tremor.sh` 直接编译已修补的源码，不再次应用补丁。保留此文件用于依赖更新时核对差异。 |
| `core-direct-builtin-groups.patch` | 核心历史汇总补丁，包含内置效果、预载、缓存、图片和字体等多项改动。旧报告记录的是早期导出，该文件之后还曾累计更新，不能把它当作只对应报告中某个旧提交的单项补丁。当前核心构建不读取它；保留作迁移与历史核查资料。 |

以上两个文件原位于 `scripts/patches/`，现统一迁入本目录，内容未改变。

其他核心补丁同样需要先核对导出基线、上下文和现有源码。实际构建入口为 `scripts/build-native-commands-core.ps1`，目前读取 `build/heap-audit/controls-source/core`，不是自动重放本目录补丁。源码布局迁移完成前，请特别注意这一点。

核对第三方修复是否已合入，可在仓库根目录执行只读反向检查：

```sh
git apply --reverse --check --directory=vendor/tremor patches/tremor-vita-arm.patch
```

该命令只检查匹配情况，不撤销修复。
