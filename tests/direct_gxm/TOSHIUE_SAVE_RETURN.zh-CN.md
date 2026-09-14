# Toshiue 存档／读档后的旧画面残留

2026-09-15。修正位于 `host-direct/src/gpu.cpp`，不修改游戏资源、Lua、core 或 shader。

## 定位证据

在 Vita3K MCP 的 ART3DIR01 中，从标题 CONTINUE 读取现有存档后，正文已经恢复，背景却仍显示标题。诊断确认当前场景仅有 `:bg/black` 与 `:ev/ev_com_c01/ev_com_c01_11`，转场已结束；提交的背景也是新纹理 `TextureId(26)`。因此这次残留不是 SAVE 图层漏删或场景没有重建。

显存地址日志进一步确认：转场截图 id=25 使用 `0x64800000`，释放后，新解码背景 id=26 复用同一地址。实际显示仍是旧截图。修正后相同路径恢复正确背景：转场始终复用自己的 `0x63400000`，背景单独位于 `0x64800000`。17 次转场捕获、8 次图片上传没有交叉复用。

这组地址与错误截图来自本次模拟器运行，不能作为所有模拟器或实体机内部实现的证明。没有修改 Vita3K 配置或二进制；也没有启动 Toshiue Windows EXE。

## 生命周期修正

- 两个惰性创建的专用转场 Offscreen 槽，内存、RenderTarget 和 SyncObject 保留在同一 GXM context 中，普通解码图片不能占用这些地址。
- core 的纹理表仍按原来的方式持有／销毁截图；宿主将销毁转换为归还槽位。
- 已提交 GPU 的截图先进入原来的 retirement 队列；现有 GPU 完成栅栏后才归还，不能在正在读取时覆盖。
- 新截图可以在旧截图仍被持有时使用第二槽；两个槽均忙时返回未就绪，沿用 core 的等待／重试路径，不无限申请内存。
- 每槽 `960 × 544 × 4`，按 CDRAM 粒度为 2 MiB，总上限 4 MiB；仍归入账本的 offscreen 项。归还时撤销 retired 计数，保留实际 live 占用。context 生命周期结束由既有进程退出路径交给系统回收。

## 验证

- 编译 core 与 host-direct/VPK 成功。core 仅用于临时诊断的代码已完全撤回；正式包无自动读档与额外场景日志。
- 旧包：标题读档复现错误背景；新包：相同操作显示正确背景。
- 新包：SAVE 写入测试槽位、确认框、按 × 返回，均恢复原场景，不需要推进到下一次背景切换。
- 移除诊断后的正式包再次完成标题读档、覆盖测试槽 3、点击 RETURN；返回立即显示原 EV 与正文。VPK 内 eboot、构建 SELF 与模拟器安装文件 SHA256 一致。
- 转场池没有交叉复用图片地址；账本观测 `faults=0`、停句后 `retired=0`。
- 本轮为 Vita3K 功能回归，尚未对该修正做实机验收或实机性能比较。
- 结束测试发送 MCP shutdown 后，模拟器日志先记录 `Running -> Stopping -> Idle`，随后进程退出码为 `0xC0000374`（MCP 标记 crashed）。这是关闭阶段另行保留的问题；上述保存／返回截图均在关闭前取得，未将退出稳定性计作通过。

用户原有第 1、2 格未覆盖。本轮第 3、4 格用于测试。

本机原始证据保留在 `build/toshiue-save-return/`：`alias-reproduced.log`、`alias-fixed.log`、`alias-comparison.json`、`normal-loaded.png`、`fix-loaded.png`、`fix-confirm.png`、`fix-cross-return.png`。

正式包复测：`final-loaded.png`、`final-confirm.png`、`final-saved.png`、`final-return.png`、`final.log` 与 `package-verification.json`。

正式安装包：`build/toshiue-save-return/art3m1s-save-return-fix.vpk`。

VPK SHA256：`eceee5d2ef09b1ebed4b84ef8ec35891697209c689172292ff59c46ae738a6f3`。

SELF SHA256：`ebc5c295d09b4e7a2f1d026a0071ae32e8ac2609a56ceecc0a0416f520447520`。
