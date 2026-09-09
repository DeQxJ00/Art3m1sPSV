# optL-text-epoch：未变化文字快照/度量复用

core 独立工作树 `build/heap-audit/text-epoch-source`，分支 `codex/optL-text-epoch`，提交 `18ee1d9`，父提交为已验证的 `982a4a6`。不整合 rebuild-audit 的诊断计数，也不使用主工作树后续 core。宿主开关 `DIRECT_TEXT_EPOCH_CANDIDATE` 默认 OFF，core 特性 `gxm-text-epoch` 默认不启用，运行时 `text-epoch.off` 控制同包 A/B。

## 范围与失效

当前阶段只复用 `BACKLOG_SNAPSHOT` 和 `TEXT_METRICS`，不缓存整份 DrawList、不停止动画，也未加入文字命令跨帧缓存。此前实机 ON/OFF/ON 已证明 MessageInput 可省约 1.6ms，但开启后 backlog/metrics 每次 render 仍约 2.1ms。

`TextRenderer::snapshot_revision()` 默认返回 None，第三方实现继续精确比较。GlyphTextRenderer 在启用特性时提供渲染器身份与修改计数：身份由单调序号分配，不依赖指针地址；计数或身份耗尽后返回 None，永久回退保守路径。

所有公开文字/字体修改入口保守失效，包括 `font_state_mut()` 返回任意可变借用之前；因此调用方直接改 String、HashMap 或恢复整个 FontState 也会失效。逐字显示仅在仍有非空 pending 层时失效；链接悬停只在 hover 实际变化时失效。字体选择、字体度量、消息层切换、页边界、ruby/link、隐藏显示、缓存调试开关等均计入。

复用身份存于进程级 BacklogInputs，多个 runtime 交替发布时不会凭各自的旧计数误命中。原有全局快照清除同时重置 BacklogInputs。普通精确 update 和 message cache 开关变化也清除身份。未命中时继续调用已验证的 MessageInput / 历史页缓存并刷新文字度量；不改再现标签格式。

## 验证及身份

- Release 新特性开启：346 passed、13 ignored、0 failed。
- Release 新特性未启用：344 passed、13 ignored、0 failed。
- 400 轮对照覆盖直接内容/字体 Map 修改、rehash、消息层切换/弹出、换页、清空、逐字揭示/隐藏恢复、多个 renderer 交替、运行时开关；每轮快照与不缓存查询及度量相等。
- 单独检查不同 renderer 身份和计数溢出不会误命中。
- Vita core/host 编译、VPK CRC 与包内 eboot 字节验证通过。shader 哈希维持既有值。

产物 `build/01.02-optL-text-epoch/`，VPK SHA256 `ba5dbec609bf4ad221c10205f5fd331480518c3f002069cd4658580a1e35681f`，core SHA256 `d548fcbb75bb05b71e790b9da4a34b556da3f5c1c5ea3c4878b49e65fc09e0db`。启动模拟器会话 `30972a33-c740-4e79-92c7-007128c6d932` 做功能验证；游戏内画面对比尚待完成，未部署实机，尚无实机收益结论。
