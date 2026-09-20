# optL-reuse-history 单项实机候选

针对当前约 43 FPS 页中 frame_backlog 约 3.8 ms/帧，先单独启用已有的不可变历史页缓存，不把后续文字、场景和 tween 优化一次混入。主分支已有这项实现；为保留崩溃排查边界，实际核心在 `build/heap-audit/history-source` 的独立 Git 分支 `codex/optL-reuse-history`：基底 d8cf0f6，7e2730f 携带已检查的 DrawList 复用，122d675 只移植 3755ddc 的历史快照缓存。

历史未变化时比较唯一版本标记，不再逐页深比较；达到历史上限并滚动时，存活页沿用序列化结果，淘汰页释放引用。活动消息层仍作原有精确比较，文字度量刷新也保持原样。因此不能预先承诺 frame_backlog 的全部 3.8 ms 都会消失。

包及准确 ELF 位于 `build/01.02-optL-reuse-history/`。VPK SHA256 `055ce29990ff306f348532b1278c42dd5342d701a3aae40e5dada33b963e0065`；eboot SHA256 `7b5e7df87c5192f87e309d405b213ef46df1514253e05b55fb9d6aea1a01ac7c`。`core.patch` 保存相对 optL 基底的完整核心差异，`manifest.json` 记录产物身份。

GPU object 与 optL-reuse 逐字节相同，SHA256 `0e59ea67933c205ce0d5f4eafeff0e67c1af8951871971ced9afc1b2a88938b7`。shader、转场捕获、显示三缓冲和 GPU 安全等待没有修改。宿主仅增加候选选项和准确启动标识。

候选源码 `cargo test --all-targets` 346 项通过、20 项忽略（含外部游戏 fixture 和基准），Vita release 交叉编译通过，VPK CRC 和内含 eboot 校验通过。历史回归覆盖同长度替换、100 页轮换、清空、输出缓存清空、旧页面释放；继续保留 DrawList 转场一致性回归。

Vita3K MCP 先执行 session_status 健康检查，再安装候选。会话 `fa2bf1a1-b000-42f9-b501-b6caf73399e2` 可启动菜单、进入 SHUF00002 标题与正文、取消章节跳过对话框、Circle 换句；截图 0005、0006 显示不同正文，背景和文字正常。运行约 260 秒后主动 shutdown，随后报告 0xC0000374；运行中检查通过不等于整个会话通过，本轮没有重新解析其 Windows dump，不能仅凭同退出码断言与之前 Qt 异常完全同因。MCP 输出保存在 `build/heap-audit/history-mcp-*.json`。

实机 VitaCompanion 备份与逐文件回读验证完成，记录 `build/direct-deploy/deploy-20260909-170350/manifest.json`，新 eboot 哈希匹配上文，launch 返回 Launched。旧 optL-reuse VPK 保留，旧安装程序和安装前完整日志也保存在该备份目录。未修改游戏资源、存档、时钟或运行标志。

同页实机 A/B 尚待用户回到先前 137 quads / 26 draws 页；保持 333 MHz，语音结束后采集至少一分钟，比较 frame_backlog、frame_build、总帧时间及 heap。安装重启会改变历史内容，不能只凭相同屏幕就假设历史页数相同，需在结论中保留该限制。当前不宣称已提速或达到 60 FPS。

## 17:11 实机复测：未见明显提速

用户确认回到测试页后，17:09 首次日志仍混有读档与语音，未用其全窗口平均值作性能结论。随后等待停稳，17:11 只读回收 `build/hardware-logs/20260909-171135-current/host.log`，SHA256 `1d2931402fecb379e2136cf41a557553817f223b57ae1b8182b82cc6447d6860`，新包标识正确，启动时钟 333/222/111/111 MHz。末尾绘制量 137 quads / 26 draws，纹理解码和上传为零，active_voice_hint=0。

末尾 12 个 host 窗口共 2598 帧，按帧数加权为 23.1044 ms/帧、43.2817 FPS；旧版停稳页为 23.2217 ms/帧、43.0631 FPS。约 0.22 FPS 的差异不足以证明有效提速。新版本 logic 4.1118 ms、present 18.9288 ms，其中 begin 等待约 9.8486 ms。最后六段 core profile 按实际 rendered_frames 归一化：frame_backlog 3.59–3.71 ms，frame_build 7.91–8.12 ms。旧值分别约 3.81–3.89 ms、8.03–8.20 ms，绝大多数常驻开销仍存在。

本次未改代码、包、运行标志或时钟，只采集日志。没有以微小帧率差异宣称优化成功；重启/读档后的历史内容也不一定与旧进程完全一致。当前测量只能说明：单独跳过不变历史页深比较，没有解决该页低帧率。

下一步需把 sync_backlog_snapshot 内的历史页、活动消息层、文字度量分开计时，再决定是否接入消息层共享缓存或度量缓存。不能将 frame_backlog 整项误称为历史序列化成本，更不能在未测量前保证另一项缓存能恢复 60 FPS。
