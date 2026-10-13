# 恢复版 E-mote 宿主接入

本次把 `emote-psv-test` 的快照接到实际 Rust 场景和 Direct GXM 渲染器。
它是可关闭的候选后端，不替换默认后端，也不声称全部原版播放器功能已恢复。

## 已接的路径

1. `emote-recovery` feature 提供 backend 2。`DIRECT_RECOVERED_PACKET=ON` 的宿主显式选择它；选择失败直接报错，不能退回旧求值器冒充成功。
2. 每个实例使用恢复端独立 timeline/control/view。脚本命令、查询、帧推进和 capture 走 C facade；输出保存在 `Arc<RecoveredPacket>`，按原位置进入普通场景与转场 DrawList。
3. `model_view_texture_host` 将 BC3 linear/swizzled 或 RGBA8 上传源交给宿主。未上传源由恢复端独立持有，PSB/view 销毁不使待上传快照失效；ready 后释放 CPU 源副本。
4. BeginScene 前 prepare；scene 中新上传返回 deferred，不解压 BC3 作为替代，也不在上传重试时推进姿态。纹理使用独立、单调、不复用的 token，和 Rust texture ID 无关。
5. packet/view 的异线程 Drop 只标记退休，owner 在线程安全注册表中收集。最终纹理 release 进入 `direct::destroy`，由现有 GPU 等待/退休路径释放。游戏退出先销毁 core，再清理恢复注册表，最后才退出 GPU。
6. 原 GXP/材质/显式遮罩命令在宿主唯一 GXM context 上提交。为当前父色目标临时附加独立 depth/stencil；软遮罩切换私有目标后回到原父目标。退出恢复宿主 shader、纹理绑定、viewport、裁剪和缓存状态，保留组嵌套层数。
7. 父 affine 应用于完整顶点；精确 clip 使用私有 stencil 高位，硬遮罩占低七位。非 identity affine 的软遮罩暂用全目标 ROI，避免把原画布外移入的图元裁掉；未降低网格或分辨率。

失败的 capture 保留一个失败快照占位，prepare/replay/不支持的效果会标记该帧不完整；不通过静默省略角色来掩盖错误。含恢复快照的帧暂绕过普通 quad/retained group 缓存。

## 构建

先运行 `scripts/build-recovered-core.ps1`，再在 WSL 运行：

```sh
bash scripts/build-recovered-host.sh /mnt/f/WorkSpaceAI2/art3m1s-psv-gxm/build/recovered-core/libart3m1s_core.a ON
```

恢复源码默认来自相邻 `../emote-psv-test`，也可显式设置 `EMOTE_RECOVERY_ROOT`。
脚本固定 core archive 副本、核验确有恢复后端符号引用，并对编译前后的宿主/core/恢复源码做 hash 比对。
结果在 `build/recovered-host/manifest.json`；默认后端对照在 `build/recovered-host-off/manifest.json`。
这是本地候选构建，不发布 release/tag。

候选固定 mesh=100%，保持完整 8×8，不修改用户保存的网格设置。
恢复端的 demo 文件日志被禁用，采用宿主原有诊断系统，不另开 demo writer 或 GXM context。

## 限制

- 固定 960×544。模型公开投影目前支持 scale、x/y；非零 pivot、angle、z 明确拒绝。
- Step/Skip 未恢复；mesh ratio 只支持 1。未知材质 provenance 保持 profile0，不能因文件名相似自动套 PSV profile1。
- 父 affine、clip、0..1 opacity、继承 color filter、规则/alpha mask 和已注册 shader 已接到整角色离屏合成；blend 使用宿主编码。未知 shader、非空逐命令 stencil 和非法 opacity 仍拒绝。所有 shader/blend 组合尚未逐项实机验收。
- 非 identity affine 的软遮罩全目标 ROI、保守 GPU 等待和新快照复制仍需在完整游戏负载中测性能。旧 demo 三人 59.940 FPS 不属于本集成后端。
- 真实语音 CSV 时钟、完整 controller/join/其他材质版本仍在恢复项目待办中。

## 验证记录

- 恢复端：真实 sample01 linear/swizzled 与两个 PSV 样本的上传源保活/延迟/取消/永久失败测试；旧五组完整绘制字节对照、79 模型 30,031 姿态、多实例、C/C++ ABI 回归通过。
- 目标 CPU：真实资源桥 ASan/UBSan；真实 sample01 facade 两实例、变量/动作、view 销毁后 prepare、跨线程退休和下个 Game 重建；106 个 GPU API 故障注入点；Rust 快照/clip 测试及旧 renderer 63 项通过。
- 完整 ON/OFF ARM ELF 均成功链接；编译通过不能代替实机验收。
- 实机 444/222 的初次有效 13 阶段探针完整退出、26 次 scene 内资源退休完成。硬遮罩跨主屏/一层/两层父目标（含 affine/clip）全帧逐字节一致；不透明内部、clip 外和错误恢复 marker 全部通过。
- 该探针的透明软遮罩边界在 depth0 和 depth1 间仍有 RGB 最大 1/255 差异（identity 1329 像素、affine 607 像素），depth1 与 depth2 完全相同。原逐字节验收失败记录保留在 `build/recovered-packet-device/device-5b0a3449`，不能改写成通过。

独立探针前三次未进 main 的原因已通过 crash PC/LR 定位到遗漏 `-Wl,-q`，构造器表未正确重定位；目标 CMake 原本包含该参数。SELF/数学库/强弱导入的早期猜测不作为根因结论。

### 21 阶段补充实机验收

完整证据见 `tests/emote_recovery/host-device-verification.json`。实机 CPU 444 / GPU 222 下，真实宿主回调完成 40 次延迟上传、40 次就绪和 40 次场景内退休；最终引用归零，正常退出。测试后已读回校验并恢复原 eboot，重新启动原程序。

BC3 linear 与预先 swizzled 源的全帧 RGBA 完全相同；linear 源由宿主重新排列压缩块后上传，并非声称使用 linear GXM 描述符。六组不透明父目标上的软遮罩比较也全部逐字节一致。

透明父分组的上述边缘差异仍保留为 `not_exact`，总结果为 `scoped_pass_transparent_groups_not_exact`；没有放宽逐像素容差。首次严格失败报告另存为 `tests/emote_recovery/initial-parent-pixel-verification.json`。

完整候选包已由 `scripts/package-recovered-host.sh` 生成：`build/recovered-host/art3m1s_direct.vpk`，SHA-256 为 `93497c292f67e57f51aa3a8e62f8de3dc8e94ae9b845f7c455343a6962d09fea`。构建与打包记录保存在 `tests/emote_recovery/host-*-verification.json`。该 21 阶段记录仅覆盖独立探针；后续完整宿主测试见下节。
## 2026-10-13 透明度、继承效果和完整宿主三人测试

- 整角色先按原材质/网格/遮罩完成，再应用 opacity、颜色和父效果，避免重叠网格被重复淡化。规则遮罩新增 premultiplied 合成模式；alpha-mask 继承灰度/反色。
- 中性分组仅在所有 recovered 材质为 source-over 时省去离屏往返，减少额外 RGBA8 量化；含透明度、滤镜、特殊混合的分组仍保留。这不代表强制离屏与直绘逐像素等价，旧 1 LSB 差异报告仍有效。
- 修复完整宿主首次加载时 prepare 实际晚于 BeginScene 的问题：Game 在 BeginScene 前准备实例和转场保留 packet 的纹理，不推进动画。
- 连续角色共享当前私有 depth/stencil scene，目标切换及错误恢复仍走原保守路径；不降低网格精度、纹理或效果。102 个 GXM 失败注入点通过，最新连续角色像素探针仍待设备验收。
- 完整 Lua → runtime → capture → GXM 的三人实机负载使用 kaz_10、ior_9、kog_12，待机/动作/嘴型持续更新。444/222、完整 8×8、详细 profiler 关闭，每项 60 秒，排除跨阶段首个日志窗口后：普通 35.397 FPS，半透明灰度 21.750 FPS，半透明反色 21.718 FPS；最大窗口记录帧时分别 31.352/48.942/49.245 ms。尚未达到近 60 FPS。
- 早一轮详细 profiler 开启时为 26.168/17.988/17.989 FPS。两轮同时改变了 scene 复用和 profiler，不将差值全部归因于复用优化。
- 三张中性分组截图仍有姿态差异，不能用于色差验收；保留原差异统计，不放宽容差。后续必须冻结同一个 packet/姿态再比较 direct/group/nested。
- 65 项 Rust effects 测试、真实 facade/上传源/桥 ASan 与资源退休测试通过。27 阶段旧探针验证了 alpha 0/0.5/1 的等价合成；最新 29 阶段尚无有效运行结果。2026-10-13 尝试改用 ART3DIR01 启动时 FTP 连接超时，尚未改写设备。

本轮完整宿主 eboot SHA-256：`f9e2f8d6571c6452dd2f8131a37b4977348ffa54a4bb396f6d1f75ba3d999382`。测试后原 eboot 和用户设置均已读回校验并恢复。
详见 `tests/emote_recovery/effects-full-device-verification.json`；原始日志与截图位于 `build/recovered-full-device/run3/`。

待办：同姿态透明分组严格像素验收；29 阶段连续 packet 遮罩/clip 隔离验收；完整宿主三人向 60 FPS 优化；实际游戏自定义 shader、转场组合覆盖。当前仍为默认关闭的候选后端。


### 网络恢复后的 29 阶段验收

2026-10-13 使用 ART3DIR01 标题运行同一个最新探针，29 阶段完整结束。56 次真实 deferred→ready→scene 内退休，资源引用归零。连续两个 packet（前一个带 clip、后一个不带）与隔离参考全帧 RGBA 完全相同；整角色 alpha 0/0.5 与单次离屏合成参考完全相同，alpha 1 与直绘相同；BC3 linear/swizzled、不透明父目标和硬遮罩原有门槛也通过。

强制透明父目标的软边缘仍有 1 LSB 差异，报告继续标记 `scoped_pass_transparent_groups_not_exact`，未放宽容差。设备原 eboot 已读回验证恢复。证据：`tests/emote_recovery/effects-29-stage-device-verification.json`。

冻结姿态诊断独立构建在 `build/recovered-host-frozen`，`DIRECT_RECOVERED_FREEZE_PROBE` 默认为 OFF。启用时只停止动画/眨眼/物理时钟推进，保留原 capture、网格、材质、效果和宿主场景路径。测试脚本禁止用冻结包测性能，不覆盖普通候选构建。


### 同姿态中性分组色差验收通过

三人完整宿主冻结诊断包（eboot `f68f21f794e0a4f1d6782cc42ca151fedd2cb774c23dd3eecdae3eebc8f531e7`）在 444/222 下完成直绘、一层中性父分组、父分组加三个子分组比较。960×544 全帧 RGBA 两组比较均为 **0 差异像素、0 最大通道差**。日志 `requested_avg=4 / flattened_avg=4` 确认走了实际中性分组消除路径；截图含全部三个真实角色，非空屏比较。

此项验证省去不必要透明分组后的色差修复，不将强制离屏的 1 LSB 量化差异改为通过。测试后原程序和全部临时设置已恢复。证据：`tests/emote_recovery/effects-frozen-groups-device-verification.json`，截图/日志在 `build/recovered-full-device/frozen1/`。

复现：

```sh
bash scripts/build-recovered-host.sh /absolute/libart3m1s_core.a ON frozen
bash scripts/package-recovered-host.sh /absolute/art3m1s-psv-gxm/build/recovered-host-frozen
```

Windows 部署：`python scripts/test-recovered-full-device.py --run frozen-next --reuse-models --pixels-only --build build/recovered-host-frozen`。已有本地授权模型 fixture 和设备测试目录后使用 `--reuse-models`；正常动态性能仍使用默认 live 构建。分析：`wsl -e python3 scripts/analyze-recovered-full-device.py build/recovered-full-device/frozen-next`（需要 Pillow）。

当前待办收敛为：完整宿主三人向近60 FPS优化，以及更多实际游戏 shader/转场组合覆盖。29阶段连续角色隔离、同姿态中性分组两项实机门槛现已通过；此前待验收记录保留为历史。


## 2026-10-13 可选接入版：CPU 快照与 GPU 重叠

已在目标工程的 backend 2 路径中接入普通场景的提前构建：在宿主 BeginScene 等待上一帧 GPU 之前计算新的 DrawList/恢复快照。准备结果独立保存，真正提交时才替换上一帧；下一次逻辑推进丢弃未提交准备结果，场景时钟、尺寸、纹理 revision 或脏状态不匹配时回到原路径。首帧加载和转场保留原流程；未降低网格、纹理或材质效果。

加入窗口累计的 capture/replay/prepare/advance CPU 墙钟计时。实机确认每帧仍为三次 capture 和三次 replay；普通快照生成约17ms/帧，回放约4.6ms/帧。当前主要瓶颈仍在快照计算，不能宣称已经接近60FPS。

| CPU444/GPU222，完整三人 | 调整前（perf4） | 调整后（acceptance8） |
|---|---:|---:|
| 普通 | 35.84 FPS | 42.27 FPS |
| 半透明＋灰度 | 21.89 FPS | 34.89 FPS |
| 半透明＋反色 | 21.85 FPS | 34.87 FPS |

调整前每阶段30秒，最终接入回归每阶段15秒，统计排除跨阶段首个窗口。另有调整后每阶段60秒的有效性能记录（acceptance6），结果41.85/34.98/34.96FPS；该轮后续生命周期测试脚本写错，性能阶段以外不能算通过。完整原始窗口、二进制和源码hash见 `tests/emote_recovery/host-overlap-acceptance.json`。

验收范围：

- 优化前后同一冻结三人姿态全帧RGBA完全一致；优化后直绘、一层和两层中性分组仍零差异。冻结包不用于性能统计。
- 最终动态用例完成三轮运行时删除/重建和300ms转场，无Lua错误；最终截图保留三个人物，CDRAM live回到60293120字节，retired为0。仅为本次短循环观察，不外推为无限期无泄漏。
- 宿主facade ASan/UBSan通过；相关Rust运行时3项和事件11项通过，3项外部fixture测试按原声明跳过。GPU回放未改，沿用已通过的29阶段实机遮罩/clip/opacity/BC3门槛。
- 曾尝试在Trans之前提前挂接角色，但复测和脚本实现核对发现测试的 `[lua]` 块会在加载时统一执行，根因是测试没有按顺序重建。测试现用 `calllua` 运行时调用；未经证实的Trans宿主改动已撤回。acceptance6/7失败记录保留，未改写为通过。
- 各次设备测试后原 eboot 与设置已恢复。新包位于 `build/recovered-host/art3m1s_direct.vpk`，其精确hash记录在上述验收报告。

接入决策：可继续作为完整宿主的**可选候选后端**接入，`DIRECT_RECOVERED_PACKET=ON`显式选择backend2；默认构建仍保留原后端。构建需要相邻恢复源码目录及带emote-recovery的core archive，命令沿用上文。尚不提升为默认后端，不发布release。下一优先级为快照计算进一步优化，以及实际游戏的shader、控制器和脚本兼容性覆盖；Step/Skip等既有边界保持明确拒绝。


### 2026-10-13 按游戏选择后端

支持 recovered 的测试包现在同时提供原后端和还原后端；`DIRECT_RECOVERED_PACKET=ON` 表示编入后端，**不再强制所有游戏使用 backend2**。未配置的游戏默认使用原后端（backend0）。

操作：游戏选择页选中游戏 → □ 游戏设置 → E-mote 后端 → ○ 或 ←→ 在“原后端（默认）”与“还原后端（测试）”之间切换 → × 返回 → ○ 启动游戏。设置按游戏保存，下次启动游戏生效。游戏已运行时先退出到游戏选择页。

还原后端显示 Mesh 1.0（固定），禁用 Mesh 调整并保留原后端的已存 Mesh 值；切回原后端后恢复使用该值。仅含原后端的构建不会应用保存的 backend2，也不会覆盖该偏好，菜单说明需要安装测试包。

文件位于 `ux0:/data/art3m1s-gxm/font-settings/<游戏ID的UTF-8十六进制>.emote-backend`，格式 `1 0`（原）或 `1 2`（还原）；保存采用临时文件和备份替换。自动三人测试现在显式设置 backend2 并在结束时恢复该偏好。

本次主机测试覆盖后端 ON/OFF 两种编译配置下的菜单按键、触控、设置隔离、读写失败/备份恢复、Mesh 值保留，以及相邻 CPU/OGV 设置行回归；ASan/UBSan 通过。ARM 宿主编译和 VPK 打包通过。实机菜单切换结果另见本次设备记录；此前性能数字不作为这一新菜单包的重新性能验收。

实机部署尝试：192.168.1.50 控制端可达，但两次在修改前读取旧 eboot 备份时 FTP 数据传输超时，第二次返回426。未上传新程序、未改设置；已向 vitacompanion 发送启动旧 ART3DIR01 并收到 Launched。新菜单包的实机切换验证**未完成**。用户可安装当前 `build/recovered-host/art3m1s_direct.vpk` 手动测试。新包hash及本地验收范围见 `tests/emote_recovery/backend-settings-verification.json`。


### 2026-10-13 实际 sample01 人物不显示与重试卡顿

新菜单包装入后，实际 sample01 的 `image/fg/kog_40.psb` 创建失败。日志显示每次读入5124512字节约2.2秒，创建失败后脚本继续重试，E-mote层数为0；此前预先解密的独立fixture未覆盖此入口。

修复：复用宿主已有PSB头解密/校验算法，把校验后的明文PSB头交给恢复解析器，纹理和动作正文保持原字节。加密标志不受支持、校验损坏等情况仍明确拒绝。原生适配器支持游戏脚本的独立逻辑画布尺寸，投影中心与裁剪跟随该尺寸；GPU目标及packet坐标编码仍为960×544，并由宿主场景变换映射，未降低网格密度或纹理精度。

回归：PSB头5项单测通过（包括解密正文不变、明文借用、损坏拒绝）；真实sample01 native facade ASan/UBSan通过，额外比较960×544与1920×1088画布的全部顶点，只改变预期投影中心，UV与顶点数量不变。ARM核心/宿主重建、打包通过。修复包已读回校验并装入192.168.1.50，旧包留存远端备份；实际游戏场景反馈与运行日志仍在核对。
