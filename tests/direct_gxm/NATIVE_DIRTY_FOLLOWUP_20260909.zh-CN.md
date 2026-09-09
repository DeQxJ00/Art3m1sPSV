# 原生变化标记与局部复用边界补查

2026-09-09，13341 先调用 server_health，确认实际样本仍为 PCSG01297、Hex-Rays ready，再作只读反编译及 xref/数据读取。没有修改 IDB 或调试状态。

## 本次新增证据

`0x81000D18` 比较传入的六个矩阵分量与对象 `+124..+144`，按变化设置 bit 0 / bit 2。bit 0 成立时调用虚表 `+192`，再调用 `+196` 或 `+200` 分派绘制。反编译中浮点调用原型不完整，不能照抄其 C 伪代码作为比较公式。

函数指针交叉引用显示多种派生表；其中相邻更新入口 `0x8121F0F4` 是调整 this 指针 -4 后跳转 `0x8121E044` 的 thunk。后者先复制并更新矩阵，计算行列式、非零时求逆，最终将六个结果写入 `+368..+388`，并置 `+428` 标志。基础更新入口 `0x8120BBA4` 则复制六项并调用下一虚方法。由此可确认这里的变化处理不只是保存一个本地矩阵，还保存派生矩阵数据；尚未在本轮逐一追踪全部消费者，不能宣称已重建原生完整输入/绘制架构。

原始响应保存在 `build/heap-audit/native-dirty-current.json`、`native-dirty-vtables.json`、`native-dirty-updates.json`、`native-layer-matrix-update.json`，不将大段商业反编译文本纳入 Git。

当前 core 的 `compositor/reduce/hit_test.rs` 每次输入遍历计算 world，并对可交互图层调用 `world.inverse()`。这与 scene 构建的重复本地变换共同构成检查方向；尚不能仅凭代码推导某个逆矩阵缓存能带来多少实机 FPS。

## 为什么先测合成组归属

已有动画诊断发现等待图标 rotate 每帧变化。若只更新该图标命令、复用其它命令，需要证明它的父变换、排序、纹理身份/内容依赖不变；若命令位于 shader 合成组、stencil 或动态 mesh 内，还需正确更新组范围、裁剪与关联数据。不能直接凭“只看到一个图标转动”跳过整帧构建。

独立诊断 core `build/heap-audit/rebuild-source` 提交 `6048553`，父 `25bdaaa`，现加入每 300 次判定窗口的一帧 command-key 采样。输出各图层命令数及 shader-group、独立 shader、stencil、mesh/emote 归属，最多 64 层并明确记录截断。采样使用既有 command-key 元数据，未修改命令、组、shader 或同步；采样时的 CPU 耗时不可作为正常性能基线。

Release 测试 345 passed、13 ignored、0 failed；Vita core 编译通过。日志 `build/heap-audit/structure-audit-tests.log`、`structure-core-build.log`。该诊断基于 message-input 路线，不含已部署 text-epoch，因此只用于结构检查，不与当前实机包作 FPS 横向比较。实机继续使用已验证的 text-epoch。

## 游戏内结构采样

宿主构建与 VPK CRC/eboot 一致性校验完成。`build/01.02-optL-structure-audit/` 的 eboot SHA256 为 `430ce0438d2d30c2986c79d5bfce3badf2b78ab7bd4939e0edc346b7bc11659f`，与 Vita3K 实际安装文件一致。MCP 会话 `d699a45e-a5bc-46a9-817a-8c98d999ec9a`，进程 54392；经过菜单、标题、读存档2并推进一句。

证据 `build/structure-audit-validation/single.png` / `single.log`、`double.png` / `double.log`。单人页稳定采样 86 commands / 0 masks / 1 group / 42 keyed layers；双人头像放射线页 112 commands / 0 masks / 2 groups / 49 keyed layers。二者等待图标 `1.80.mw.glyph.0` 均只有一个命令，in_group/shader/stencil/mesh_or_emote 均为 0。

双人页的 `1.0.8.bg.8.p.m.a.a.b.0`（此前已确认有 3 帧/300ms anime）属于 group；人物命令和头像 `1.80.mw.bb.2.0.0/.1` 也报告 in_group=1。因此不能把“全部动态图层直接改一个 quad”作为通用方案。可以先验证仅等待图标 rotate 变化时的命令更新，维持已有组不变；anime 帧、事件、纹理/文字变化、父层变化及其它未支持依赖仍完整重建。需新增失效与像素对照测试，当前尚未实现此优化。
