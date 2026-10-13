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


后续 OBS 实机核对发现第二处尺寸限制：sample01 的实际舞台为960×540，native_effects回放仍拒绝非960×544舞台。现把舞台缩放同时合成到packet affine与clip，保持原生960×544 GPU目标；坐标/裁剪2项单测及ARM构建通过，修复包已安装。OBS已实际看到人物，叠加显示444/222约42FPS；日志稳定窗口约40FPS，最后连续32个5秒窗口没有新回放错误。该次运行早段系统返回CPU500，不能把整轮当作受控444性能测试。

未解决：快速换模型阶段有8MiB纹理分配失败、碎片化CDRAM与主存不足，共906次延迟/回放警告；之后稳定场景恢复。没有Lua错误，但不能据此宣称完整生命周期验收或60FPS通过。详见 `tests/emote_recovery/stage-mapping-verification.json`，下一项是纹理上传在内存压力下的处理。


### 2026-10-13 sample01 色调分组性能

实际存档2的 kaz_6 蓝色服装场景每帧含 cmul 色调处理及两层中性透明父组。扩展源码哈希核对后的 bundled cmul 证明：无遮罩、alpha=1、RGB乘数有限且均在0～1内时，保留色调着色器和其自身离屏输入，只省去两层中性父组合成。未知着色器、遮罩、越界颜色和非普通混合继续走原路径。网格100%、纹理和动作不变。

同场景实机CPU444/GPU222，关闭重型追踪的六个稳定窗口：优化前 26.70 FPS，优化后 40.77 FPS（+52.7%）；恢复原追踪设置后先前采样约39～40FPS。另一较轻姿态达到59.94FPS，不把它冒充原场景的60FPS结果。用户确认颜色/透明边缘正常，OBS核对时人物可见；未做这组场景的自动逐像素对比。

66项native_effects回归通过，后加mask_range保护的定向回归通过；ARM核心/宿主/打包通过。新包已经FTP读回校验安装，原包留有远端备份，临时追踪设置已恢复。增加低频CPU场景构建及分组诊断，明确下一瓶颈：重场景人物快照约6.9ms、其余场景构建约5.9ms。还未达到重场景/三人60FPS，之前快速换模型的纹理内存压力问题也未在此变更解决。证据见 tests/emote_recovery/cmul-performance-verification.json。


### 2026-10-14 CPU 快照打包与场景缓冲复用

跳过未超预算时的纹理淘汰队列构造；回收已退休提交的 CPU DrawList 容量；快照提前分配几何缓冲、缓存固定三角形索引，并去掉一次顶点中间拷贝。仍逐帧求值和生成完整网格，不改变材质、遮罩或 cmul。

实机 444/222，OBS 确认存档2蓝色衣服及“不过，因为大人去哪都开车……”场景。关闭重型追踪的六个稳定5秒窗口平均45.78 FPS（45.46～46.14），此前同场景40.77 FPS，约提升12.3%。开追踪的快照打包由约1.66ms降至0.63ms；这些分段计时不能混作关闭追踪的绝对耗时。仍未达到60FPS，也不代表三人性能已验收。

74项provider回归、facade ASan/UBSan、packet O0/O2逐字节回放及分配失败回归通过。ARM构建打包和实机读回安装通过，诊断开关已恢复原状态。未新增自动实机逐像素比较。结果与二进制hash见 tests/emote_recovery/cpu-packet-performance-verification.json；后续重点仍为姿态/顶点计算和剩余CPU场景构建。


### 2026-10-14 第二轮姿态与线程调度实测

增加64项精确输入位模式的仿射结果复用、相同缓动曲线采样复用，以及确定隐藏子树的提前退出。该组同场景关闭重型诊断后45.41FPS，未显示相对45.78FPS的明确收益，不把它计作性能提升。

随后让主线程仅在并行顶点任务期间使用允许的CPU0，两个既有工作线程仍使用CPU1/2；全部任务join后恢复调用方原affinity，不能切换则沿用原调度。444/222、同一存档2台词、六个稳定窗口平均46.77FPS（46.55～46.89）。相邻试验顶点计算/等待从1.99ms降至1.35ms；姿态求值仍约3.11ms、快照打包0.61ms，CPU场景构建约3.1ms。没有接近60FPS，也未重验三人场景。

79模型、30031姿态、10165736个顶点及shape参考检查通过；33项宿主构建回归通过（2项既有跳过），packet O0/O2逐字节回放/资源失败清理通过。CPU参考测试不覆盖Vita affinity系统调用，后者由ARM构建及本轮实机验证覆盖。最终包已读回安装，原诊断开关恢复，OBS最终人物/文字/背景可见；本轮没有自动逐像素比较。记录见 tests/emote_recovery/cpu-affinity-performance-verification.json。


### 2026-10-14 可重复的存档2完整宿主基准与性能候选

快速demo采用完整宿主自动读档模式，避免另建一个省略原游戏脚本/UI的轻场景。`scripts/test-recovered-save-device.py` 临时写入 `recovered-save-bench.txt`（game ID、save basename、唯一token），启动后等原游戏注册onLoad，再调用正常存档恢复。界面存档2实际文件为 `save0001.dat`。文件仅限当前游戏save目录的basename；加载至多一次，45秒没有恢复hook即明确失败。不开此文件时正常启动路径不读档。

运行：`python scripts/test-recovered-save-device.py --run <新的结果目录名> --seconds 30`。默认sample01、save0001.dat、20秒预热、CPU444/GPU222、recovered后端，自动进入游戏，无需每轮手动选择存档。需要安装本轮带benchmark入口的完整宿主包。它读取当前PSV的存档作为基准，未来换存档必须比较报告中的source_save_sha256，不可混作同场景A/B。`--seconds`支持30～600秒。

runner将日志和JSON保存至build/recovered-save-bench/<run>，丢弃跨预热边界的窗口；确认有角色capture、无加载/回放错误，检查源存档字节未改，finally恢复CPU配置、后端偏好、追踪开关及自动读档开关。保留游戏在测试场景供观察，不恢复成菜单。备份位于每次运行的preferences-backup.json，异常断线时需按该备份恢复后再测。VPK不打包ROM和用户存档。

本轮从两份待办与原eboot复查出的顺序：

1. **局部姿态的脏字段门控及其测量。** PCSG00341原81193E36..40检查强制参数/层+0x24，81193EC0..EE8比较并保存插值后跳过部分字段更新；当前flatten每帧广泛重算。先拆分局部pose、父mesh sync、组合patch、anchor/event时间；再恢复依赖与原ARM差分。不能凭相同关键帧或epsilon跳过父级/物理/事件。
2. **完整宿主回放的状态缓存。** 原811A91C2..DA按renderer+0x3D0的program指针判断绑定是否变化，但随后仍更新uniform。独立demo的gpu.c普通状态缓存已验，host-direct/src/recovered_packet.inl::mask仍逐mesh发相同depth/stencil/program设置；可先移植普通未绑定soft路径，在clip、hard/soft mask、父RT及失败路径严格失效。保留透明顺序、uniform、纹理描述符和所有同步。
3. **CPU场景构建依赖缓存。** 目前约3.1ms，先细分fragment候选检查、文本/角色命令和shader组构建，再决定复用范围。扩大shape memo和提前隐藏退出未取得明确收益，不重复计入。
4. **流/packet缓冲复用。** 原811A6DA4有直接绑定/记录临时流选择，811A6DF6有临时流准备、两纹理槽和特殊二次提交。下一步需继续追缓冲所有权/退休，不构成删除GPU fence或跨透明层合批的依据。

来源为用户fagdec的PCSG00341 ELF，SHA256 21fe2e8f69645897b708d5110a38583feba57a47d4185b60675868d6d7351f45，本轮已重新校验。复查的是此前IDA导出的原汇编/调用链，未宣称重新反编译另外两作；原文件不改动。对应恢复工程TODO_RECOVERY.md顶部新增当前优先级，历史清单仍保留。

实机自动基准已连续通过两轮：各预热20秒后采样约30秒，分别46.94、46.91FPS。首次OBS确认同一句台词、人物/背景/文字正常及CPU444/GPU222；两轮使用同一存档hash，源文件未变且临时配置恢复。9项save_io回归、Python语法检查、ARM核心/宿主/打包及部署读回通过。记录见 tests/emote_recovery/save-benchmark-verification.json。该结果验证快速基准的可重复性，不计作新的性能提升，也不代表60FPS或三人已验收；本轮未做自动逐像素比较。

### 2026-10-14 优先级实测：局部姿态试验与普通GXM状态复用

依据81193E04尝试按layer/current frame/next frame/原始blend位模式缓存纯局部坐标、缩放、角度、shear和curve结果，父变形、继承、anchors及事件仍正常计算。79模型30031姿态中7356149次局部值对照一致（5011114命中），10165736顶点及shape对照通过，ASan/UBSan通过。但存档2实机46.88FPS，eval约3.20ms，较自动基准46.91～46.94FPS、eval约3.06ms无收益；已撤回生产实现。候选和验证保留恢复工程reverse/local-pose，tools/test_vertex_cache.py --verify-local-pose --verify-shape仅验证该归档候选，不能当作当前生产开启此优化。

完整宿主RecoveredPlayback接入packet内普通GXM状态复用：只在无clip、无hard/soft mask、无soft构建且fragment program相同时跳过重复depth/stencil/program设置。texture、stream、uniform和draw照常提交；scene/clip/clear/reset/step/soft路径失效，缓存不跨packet。10万步差分覆盖独立program identity和双面状态，设置次数621737→556105；102项API失败清理通过。实机同存档CPU444/GPU222为46.90FPS，OBS人物/背景/文字正常；未测出明确帧率收益，未做自动逐像素比较。网格、动画更新和GPU同步均未削减。

记录见tests/emote_recovery/priority-performance-verification.json。下一步细分CPU场景pipeline、stencil materialize及稀疏cache replay/candidate/store计时，避免把高缓存命中率直接等同于帧率收益。

CPU场景细分实测后完成两项优化：单次scene build内按层identity复用子树静态资格，begin每帧清空结果；fragment重建时复用旧stamp的String/Vec及DrawList容量，但逐项更新所有revision、edited依赖、命令和group/mask偏移，删除节点会truncate。未缓存动态画面或改变动画更新频率。

最终实机连续两轮47.34、47.27FPS，原自动基准46.94、46.91FPS，小幅提升约0.8%。pipeline稳定窗口约3.0ms→2.5ms，stencil约0.012ms；稀疏累积样本candidate约0.93→0.63ms、store约0.38→0.19～0.21ms（包含启动样本，不等同于严格预热后的分段平均）。33项compositor build/cache回归通过，2项既有opt-in测试未执行；ARM构建、部署读回及两轮存档hash/临时设置恢复通过。OBS最终首轮确认同一台词和人物/背景/文字正常、CPU444/GPU222。最终SELF e56d69b9678127adf1e6bb3f0871a91d0a8662002fc60554af4c27473a796e67，VPK f39c139755a00f46706075f99651b5ee6fd440e5284881ba6aa39f6c754ab533。

尚未达到60FPS或完成新三人验收；未新增自动逐像素测试。人物求值约3.1ms、顶点/等待约2.1ms及GPU提交仍需继续拆分，原缓冲生命周期尚未恢复到允许删除同步或跨透明顺序合批的程度。数据、日志hash、源码hash及部署记录在priority-performance-verification.json。

### 2026-10-14 顶点坐标复用与上传缓冲位置

继续使用完整宿主自动读档存档2、CPU444/GPU222、20秒预热及至少30秒采样，完整网格/透明度/cmul/动画不变。本轮含稀疏诊断的基准47.14FPS；复用preflight坐标后48.84FPS；动态vertex/index arena改为uncached主内存后两轮49.77、50.00FPS。相比本轮基准约+5.8%，尚未到60FPS。

- 原路径对同一顶点做两次父affine变换（校验、mesh提交）。现把校验后的精确值保存在可复用CPU scratch中，每包覆盖；GPU仍只读原有fence保护的arena。保留浮点运算顺序、源packet只读、overflow在绘制前失败、原绘制顺序和遮罩。128组变化矩阵逐字节对照及回到identity通过。
- 8MiB动态arena优先USER_RW_UNCACHE，对齐4KiB；这与独立demo的`reserve(ARENA_SIZE,0)`一致。纹理和depth继续原分配策略。初始主内存分配失败才使用已有fallback；base/map失败释放并撤销live记账。未删任何GPU fence，未改GXM状态/着色器。allocator故障/对齐/记账测试、10万步状态差分、102个API故障注入均通过。
- 预热后同样4560顶点的稀疏样本中，copy/map中位数约1.45ms→0.86ms→0.67ms。仍有约1.5ms父坐标计算、约2.1ms scene准备（含等待），下一轮继续拆分这两项。姿态计时含系统时钟调用开销且有early-exit，不能把各项直接相加当成纯求值耗时。
- 新增诊断后的快速姿态回归是3模型777姿态612500顶点逐位一致及shape参考/ASan/UBSan；不冒充重跑旧79模型全集。OBS末轮确认同台词、人物/背景/文字正常，444/222可见；没有新做自动像素差分或三人验收。
- arena将8MiB从CDRAM移至主内存；末轮OBS为MEM357/365MiB、VMEM69/112MiB。资源账本uncached=8388608、CDRAM=56360960、faults=0。低内存切模型/长时间游玩仍须专门测试；不能把分配失败fallback当作后续内存压力已经解决。

最终包已安装并读回校验，SELF `c574c069e4b6b309f7dcad9c0733732d3f0904c54622ce0b7467b1b5a8663a48`，VPK `2e539c2b20817c47494ccb1cc4b4daa5f984e63216e9fdaac89db5bf431c6d4f`。四轮源存档hash均未变，临时配置均恢复。证据：`tests/emote_recovery/vertex-upload-performance-verification.json`。


### 2026-10-14 模型切换压力与父目标预附加

完整宿主CPU444/GPU222，以kaz_10/ior_9/kog_12三模型、300ms转场，先跑3轮、再跑12轮销毁重建。12轮共39次模型创建，无回放/纹理分配失败、资源账本faults=0。预热后系统user_free稳定6MiB、CDRAM剩余48234496字节；最终newlib heap used=65963152、free=27528048字节，未发现模型大小级别的持续增长。仅覆盖这三个模型和本轮循环，不等于长时间完整游戏或所有低内存场景已验收。

三人普通/半透明灰度/半透明反色，本轮初始包分别46.67/37.72/37.69FPS；12轮压力包46.46/37.70/37.68FPS。之前完整宿主42.27/34.89/34.87FPS与此可作阶段对比，独立demo的59.94FPS不能替代完整宿主结果。普通阶段三次快照生成仍约15.7ms，另有宿主逻辑、场景命令和提交开销；瓶颈并非只有遮罩。

坐标候选将逐顶点40字节临时对象复制改为一次bulk copy后原位变换，保持浮点顺序和所有字段。128组变化affine逐字节对照通过，但实机单场景49.03FPS，较前轮49.77～50.00没有收益，不能作为独立性能提升。

父目标候选只允许明确的人物隔离组、或最内层仅含一个无额外隔离人物packet的shader组，在首次打开960×544目标时附加已就绪的私有depth/stencil。共享DS一旦在途即拒绝再次借用，直到既有完成屏障；没有删除GPU fence。实际目标打开函数纳入状态fixture，预附加/拒绝借用/Begin失败清理、10万步状态差分、102故障注入通过。66项非recovery renderer回归及ARM recovery构建通过；桌面recovery feature测试仍因已有facade符号缺失无法链接，不冒充通过。

实机合成GPU探针12组old/prebound同阶段全帧RGBA逐字节一致，包含affine、hard/soft、0/1/2层父目标；GPU排空及资源退出成功。先前PSVshell浮窗干扰的一次失败单独保留，关闭浮窗复测后上述12组差异均为0，随后恢复CPU/FPS/MEM/VMEM/时钟浮窗。跨不同父目标深度的旧oracle仍在soft边缘有1329/607像素差异，原失败记录保留；优化前后相同不代表该历史色差已修好，也不代表真实PSB全材质逐像素验收。使用scripts/verify-recovered-prebound.py独立验收同阶段对照，不放宽原oracle。

最新shader入口在存档2已命中prebound=1，抽样scene准备从约2400us降至12～14us；随后设备画面/日志中断，尚未完成有效FPS窗口，性能验收待继续。不要用局部分段下降推算整场景FPS。证据：tests/emote_recovery/model-pressure-parent-performance-verification.json。

采样最终因No completed scene sample超时失败，临时配置已恢复。实机eboot及build-info已读回恢复到测试前e6f865e6…版本，收到Launched；最新cee5266…候选只保留本地，不作为已通过性能验收的发布包。


### 2026-10-14 存档3头像：避免重复打开主目标

用户反馈存档3左下角头像低帧。完整宿主自动加载sample01 save0002.dat，OBS确认蓝天背景、和叶头像及“总之，去学校的路本身不是太复杂。”对话。基准36.56FPS；仅有单人物shader父组优化的包36.71FPS，prebound=0，没有改善。头像模型kaz_6的逻辑画布1440×1536，实际恢复packet有clip且画在主目标，不符合原来的独占人物父组入口。

分段诊断显示CPU逻辑/场景约9.36ms，提交/呈现约17.93ms；抽样scene准备中位数约15.64ms，明显超过此场景的坐标处理。现只在当前DrawList确有recovered packet时设置帧内hint；背景分组完成并resume主目标时，若已有私有DS就绪、且尚未被在途GPU工作使用，直接附加到新scene。随后普通合成保持中性depth/stencil，人物回放复用同一scene；每帧重置hint，已借出的DS必须等原完成屏障才可再借用。没有新增DS、没有删GPU fence、没有降低网格或动画/效果更新。

首轮优化59.940FPS，抽样scene准备中位数32us；OBS头像/背景/文字及裁剪外区域正常，CPU444/GPU222。后续复测和立绘回归详见portrait-performance-verification.json最终记录；该结果不代表完整三人已到60FPS。

验证：实际resume_target提取进入ASan/UBSan fixture，新增主目标有/无hint及在途拒绝借用，128affine逐字节、10万步状态差分和102故障注入通过；66项native_effects普通配置回归通过，ARM recovery核心及宿主构建通过。实机探针两条路径都先画背景分组，再比较old与主目标预附加；12组合成像素对照全帧0字节差异，覆盖clip/hard/soft/嵌套与资源退出。原跨父深度soft边缘1329/607像素差异仍在，未放宽原oracle，也未宣称真实动画PSB自动像素一致。探针前隐藏PSVshell、结束后恢复原浮窗，再执行性能对比。

最终头像两轮分别59.94005、59.94017FPS（各20秒预热后约30秒有效采样），源存档hash相同且未改；存档2立绘回归57.11252FPS，OBS同台词及人物/背景/文字正常。临时配置已恢复，最终留在存档3头像场景。SELF abf8e93defc809a3f2aa4369d2c245ab82f00a9b6ef286851d5636b6f3d8265b，VPK 4e5e1d2ffd0363f15500523cc7fff393129dffb9c55d012adf8ed245a36b882f，已FTP读回校验安装。记录见tests/emote_recovery/portrait-performance-verification.json。


### 2026-10-14 最终包完整三人效果复测与相邻人物父目标复用

重新使用上一轮最终包 abf8e93d，在完整宿主、CPU444/GPU222、kaz_10/ior_9/kog_12完整网格及动态动作/说话下测试。普通46.33～46.36FPS、灰度透明37.71～37.76、反色透明37.67～37.72；另完成3轮销毁重建及300ms转场，无脚本/回放失败。新增cmul三人阶段44.24FPS：读取用户sample01原pc/cmul.hlsl，使用既有builtin，red=1/green=0.94509804/blue=0.8901961。原shader及PSB仅存于本地忽略的fixture；这是完整宿主的可重复三人脚本，不冒充存档2原场景。

父目标预附加入口从最内层单packet扩展到同一效果组内连续的非隔离人物packet。全部command必须为recovered、opacity=1、默认color、无单独shader、Alpha混合，且范围内任何位置都不能有嵌套组。每个人物仍重置自己的遮罩，整组三人效果仍只在原边界合成一次；沿用既有私有DS就绪/在途保护，没有加DS、删fence或降低网格/动画/效果。

新包两轮三人FPS：普通46.65/46.62、灰度透明40.86/40.81、反色透明40.84/40.62、cmul49.66/49.57。相对同轮旧包，透明效果约+8%，cmul约+12%；普通场景无明确收益。效果回放CPU墙钟灰度/反色约4.51ms→2.67ms，cmul4.44ms→2.16ms。三次capture仍约14～16ms/帧，透明效果还存在GPU/场景等待；复测反色窗口最大94.196ms，不隐藏长帧，也不宣称稳定60FPS。

验证：67项native_effects普通配置测试、ARM recovery构建、脚本语法及diff检查通过。实机探针每个old/prebound变体连续绘制3个相同synthetic packet，12组全帧RGBA逐字节一致；不冒充真实PSB自动像素一致。原跨父深度soft边缘oracle仍失败，本次重复三包为769/704像素，保留失败结果及独立同阶段验收。首次探针残留FPS数字导致stage2差异357字节（bbox11,14..33,29），记录归档；彻底关闭HUD后重跑通过，性能测试前恢复原浮窗。

三人两轮均完成且恢复临时配置，无脚本/回放错误；OBS确认人物及反色/cmul效果可见、444/222。最终SELF `5499651abb5b802c26619543425b9efd42fd4ada294d4bcb85d57ab542af19dd`、VPK `039eb0f01660b1efc726e081f75797e193e08f365b031bdc1d19ac403ca2b6ee` 已读回校验安装。完整证据见 `tests/emote_recovery/three-effects-performance-verification.json`。后续优先继续P1快照/父变形依赖与P3动态场景构建；不能把旧独立demo的59.94FPS作为完整宿主结果。

安装后原游戏存档2回归：20秒预热后约35秒有效采样，57.8975FPS；源存档hash与上一轮一致且未修改，临时设置已恢复。OBS确认同一句蓝衣人物台词、背景/文字/cmul正常，最终留在该场景。此前最终包该存档57.1125FPS，本轮仅视为未回退，不把这小幅波动计作独立优化收益。
