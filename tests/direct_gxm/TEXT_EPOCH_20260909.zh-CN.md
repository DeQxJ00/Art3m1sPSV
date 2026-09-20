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

产物 `build/01.02-optL-text-epoch/`，VPK SHA256 `ba5dbec609bf4ad221c10205f5fd331480518c3f002069cd4658580a1e35681f`，core SHA256 `d548fcbb75bb05b71e790b9da4a34b556da3f5c1c5ea3c4878b49e65fc09e0db`。

## 模拟器复测

第一次启动沿用了进程 62436，会话 `30972a33-c740-4e79-92c7-007128c6d932`。截图接口先有一次 NO_ACTIVE_SESSION（重查仍运行、重试成功），随后 Vulkan `vk::Queue::waitIdle: ErrorDeviceLost`，状态确认 crashed、exitCode=3221226505。错误/状态/游戏日志保存在 `build/text-epoch-validation/first-run-device-lost/`。不能由 GPU 源码未改推导出这次异常与候选无关，也没有把该失败算作通过。

确认旧进程终止后新启进程 54392，会话 `21020eaa-1f4f-46ce-8869-c440046c8be5`，使用同一已安装 eboot。完成菜单、标题、读档、换句及双人/头像/放射线场景检查；ON/OFF/ON 截图差异仅在动态等待图标 `(891,482)-(920,511)`，证据 `build/text-epoch-validation/`。历史记录经上方向键打开，最近三句正确更新；ON/OFF/ON 三张截图逐像素完全一致，证据 `build/text-epoch-backlog-confirmed/`。先前 `text-epoch-backlog-validation/` 的触摸未命中按钮，实际仍是普通场景，不能用于历史界面验收。

最后状态核对进程持续运行 667344ms，未再次报告 crash。首次 DeviceLost 原因尚未确认，此复测只证明新进程中的上述功能正常，不是长期稳定性或完整 shader 回归结论。实机性能仍待测。

## 实机部署

已备份上一版 optL-message-input 的 eboot/SFO/host.log，再上传回读校验并启动。记录 `build/direct-deploy/deploy-20260909-200253/`；启动日志 `build/hardware-logs/20260909-200415-current/` 确认 optL-text-epoch、333/222/111/111MHz、三缓冲无 MSAA。已请求用户进入同类型固定停句页面；本候选实机 ON/OFF/ON 尚待执行。开关脚本 `build/heap-audit/text-epoch-hardware-ab.py`，分析脚本 `build/heap-audit/analyze-text-epoch-ab.py`。

## 实机同页结果

首轮 `build/hardware-text-epoch-ab/20260909-200545/` 因用户误按改变页面，不用于同画面对比。用户重新准备双人停句页面后，重测证据为 `build/hardware-text-epoch-ab/20260909-201006/`，含各段原始日志、SHA256、analysis.json 和 core-per-render.json。ON/OFF/ON 每段约 30 秒，取切换稳定后的末三个完整窗口；三段均为 96 quads、30 draws、0 uniforms，333/222/111/111MHz。开关已恢复开启。

| 状态 | FPS（由帧耗时计算） | 帧耗时 | GPU begin 等待 |
| --- | ---: | ---: | ---: |
| ON1 | 54.03 | 18.507ms | 6.704ms |
| OFF | 48.94 | 20.434ms | 6.746ms |
| ON2 | 53.90 | 18.552ms | 6.853ms |

开启收益为约 10.1–10.4% FPS，每帧减少约 1.88–1.93ms。按 sample_count/rendered_frames 将 core 平均值归一到每次 render，backlog/metrics 从 OFF 的 1.808–1.816ms 降到 ON 的约 0.005ms；整个 frame_build 从约 6.98ms 降到约 5.06–5.11ms。scene 仍约 3.8ms，text commands 约 0.47ms，retain 约 0.68–0.70ms。GPU 等待没有相应下降，符合本次仅省 CPU 文字快照工作的范围。

该结果确认固定双人页面的收益，不是全部游戏场景已稳定 60 FPS，也不证明换句瞬间尖峰已全部解决。下一步应检查动画导致的静态场景重复构建与资源保留遍历，保持现有 shader 和显示同步不变。

用户补充确认本页包含放射线条漫画效果，认为此页 5x FPS 可接受。后续优先验收普通双人/头像场景与换句尖峰，不将此特效页强制达到 60 FPS 作为当前重点。
