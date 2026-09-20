# 实机关闭开篇说明后黑屏

2026-09-15。发生于标题 START → MANUAL 说明页 → ○ 关闭说明。与 SAVE 返回时的旧截图复用问题分开处理。

## 证据与修正

实体机诊断记录：MANUAL 显示时停在 `system/script.asb:108`，调用栈返回地址为同文件第 13 条指令。关闭说明的 `user_gameclick` 通过 `ResetStack` 排入两次 return，退出宿主事件帧和说明辅助函数，随后 `uitrans` 再次调用同一个 estag 辅助函数。淡出完成后仍停在第 108 条指令，说明层已删除，画面只剩黑背景。

对解码后的 ASB 按可执行指令计数，第 108 条是 return。停住来自上次 `estag("stop")` 留下的 queued-wait checkpoint：旧记录以脚本、PC 和调用栈匹配，新调用再次经过相同位置时错误恢复了旧 Stop。

在解释器真正弹出调用帧时，丢弃深度大于剩余调用栈的等待记录及其未执行续接标签；保留调用者的等待。同步重映射活动等待索引，仅在被删除的记录确为当前等待时清理对应等待状态。没有修改游戏资源、Lua、shader 或渲染缓存策略。

回归覆盖：只退出一层普通事件回调仍停留原说明页；ResetStack 退出说明后再次调用同一辅助函数可正常返回主脚本；旧等待后排队的标签不会被误执行。原有菜单嵌套等待测试一并通过。

## 验证与复现

- 修复前新增测试失败：期望返回 main，实际再次停在 helper。
- 修复后解释器 231 项单元测试通过、1 项忽略；9 项集成、6 项脚本和 1 项文档测试通过。
- PSV core、host-direct 和 VPK 编译成功。临时 `manual-*` 渲染／控制流诊断均撤回。
- 首次安装遇到 FTP／命令端口超时；恢复后在 `deploy-20260915-023925` 成功部署，像素自检通过。随后实机已进入正文并保存；自动流程多次因用户 Esc 停止，因此未将完整 START／MANUAL 操作链计作自动验收通过。后续 LOAD 与 SAVE 返回实测见 `PORTRAIT_MASK.zh-CN.md`。

core：`2cf4ab8` → `d3698e6a6ce60027dcd04c0edfe83833a65ea798`。根仓库保存 `patches/queued-wait-return-core.patch`，在已应用 `native-commands-core.patch` 的 core 上检查后应用；已有新提交不得重复应用。

编译入口仍为 `scripts/build-native-commands-core.ps1`、`scripts/build-native-commands-host.sh`。本机 core 工作树为 `build/heap-audit/controls-source/core`。

包：`build/toshiue-save-return/art3m1s-manual-return-fix.vpk`，包含上一轮转场截图池修正。

VPK SHA256：`598e201dc337e3c74d0d0300b4dfed75c2fc13f56584994746b01ac361117c50`。

本机证据：`build/native-command-port/psv-manual-diag3-stop.log`、`psv-manual-diag3-exit.log`；`build/toshiue-save-return/hardware-black/test-before.log`、`test-after.log`、`fix-core.log`、`fix-host.log`。
