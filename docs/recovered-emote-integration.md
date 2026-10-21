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

新引擎的旧版 mesh 比例固定为 100%，渲染细分独立按游戏选择 8×8、6×6、4×4（默认）、2×2 或 1×1；不修改旧引擎保存的网格比例。
恢复端的 demo 文件日志被禁用，采用宿主原有诊断系统，不另开 demo writer 或 GXM context。

## 限制

- 固定 960×544。模型公开投影目前支持 scale、x/y；非零 pivot、angle、z 明确拒绝。
- Step/Skip 已接入，原生宿主语义及验证范围见本文末尾；mesh ratio 只支持 1。未知材质 provenance 保持 profile0，不能因文件名相似自动套 PSV profile1。
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

早期还原后端固定 Mesh 1.0；自 2026-10-19 起，“E-mote 精度”行可选择独立的 8×8、6×6、4×4、2×2、1×1 细分档位，仍保留原后端的已存 Mesh 值。仅含原后端的构建不会应用保存的 backend2，也不会覆盖该偏好。

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


### 2026-10-14 对话切换PSB卡顿：消除重复整张纹理哈希

用户反馈各场景切PSB均停顿。原日志kaz_6/kaz_34/kaz_35的source cache命中仅5～10us，附近仍出现400～650ms长帧；现有Source/ParsedModel缓存不等于Recovered模型实例、编译场景或GPU纹理已预备。新建路径仍同步拷贝PSB、创建播放上下文、遍历并编译场景和准备纹理。新增[recovered-load]分段日志区分collect/copy/contexts/bind/view/total；[recovered-resources]仅记录超过20ms的资源准备。view包含纹理获取和scene编译，不包含后续GPU上传。

定位并移除纹理共享筛选的全量FNV64扫描：以前每次逐字节处理数MiB压缩纹理，再做完整memcmp；现在像素最多采样256字节、短源全读、palette仍全部参与筛选。hash只作快速排除，格式、尺寸、长度、宿主身份及完整像素/调色板memcmp仍是共享的必要条件，identity仍为单调token。未改纹理内容、布局、压缩率、网格、效果、动画、GPU同步或缓存容量。恢复工程native/model_view.c为生产改动，目标项目增加诊断与对话推进基准。

444/222完整宿主三模型各创建3次（初次及2轮销毁重建）：kaz_10总创建中位437.14→185.90ms，ior_9 240.74→115.46ms，kog_12 447.15→198.18ms。两包普通/灰度透明/反色透明稳定窗口约46.6/40.9/40.8FPS，未计作持续帧率优化；无加载/回放失败。新包三人资源准备仍约80ms/次，创建耗时不是完整切换帧总耗时。

原游戏存档3以--advance-ms 1500自动推进，源save0002.dat hash相同且未修改：两轮都创建kaz_6五次、kaz_34两次。kaz_6中位300.192→156.093ms（新包范围155.722～210.594），kaz_34 306.4765→166.2715ms（159.557～172.986），下降约46%～48%。本轮按相同模型的create调用比较，按键间隔受FTP/调度影响，不冒充完全帧同步的剧情回放；推进过程平均FPS不能当静止场景成绩。

O0/O2 ASan/UBSan真实纹理12场景通过；新增两份4096字节BC3源在未采样byte9不同、fingerprint相同的碰撞用例，必须生成不同resource，第三份完全相同的源必须共享；0..300长度边界检查通过。Facade真实PSB独立状态、延迟上传、销毁后快照、跨线程释放及退出重绑通过；ARM构建、脚本语法和diff检查通过。本轮未做新的实机自动像素oracle，不把动画截图当相同姿态像素验收。

新包已读回校验安装：SELF `49c3f782a92ec08c67363d580bd34f8e876785c3146ea80784d1584bd4024dff`，VPK `7d5c1fe6a7d9d7d2097cd1c8165fba9f65fab6e943b69bb560a0dd5780930d62`。详见tests/emote_recovery/model-switch-performance-verification.json。仍不是无卡顿：后续优先分离并复用Recovered不可变编译结果、梳理PSB源所有权避免重复拷贝、核对原后端预解析与Recovered需求，并在既有预算/退休规则下处理冷纹理上传。不可直接无限保留整个可变实例或忽略物理、事件与播放状态重置。

安装后另做存档3静止回归：10秒预热、约30秒采样59.0383FPS，源存档未改、临时设置恢复。OBS头像/背景/文字正常，444/222可见，最终留在该场景；本次预热条件不同于此前20秒，不用此轮推导持续帧率优化收益。

### 2026-10-14 sample01_sw 主游戏对话切换对照

使用实机已有完整宿主 SELF `feb4e66e091a3d7e1974ec0da63a5e475dce8cae9a6f2536a5462b2b9b7860bc`，CPU444/GPU222。本轮不修改渲染代码、网格、动画、效果或缓存配额。先测试原始 sample01_sw PFS，日志确认人物 BC3 `preswizzled=1`；存档2/3推进均完成，OBS可见立绘/头像、背景与文字。随后将已逐块验证等价的 kaz_6/kaz_34 线性与SW PSB分别通过相同的临时散装覆盖路径读取，其余SW游戏资源及存档不变，以排除背景格式和PFS/散装读取方式的差异。

配对测试各10秒预热、至少35秒采样、固定12次对话按键，两版各场景采样中的模型序列相同。切换帧为模型创建日志之后首个记录的长帧，包含其他CPU逻辑/效果，不是模型创建函数的纯耗时；定时输入不是帧同步回放，每组只有一轮配对，不能把小幅波动当稳定帧率提升。

| 对比项 | 线性 PSB | SW PSB |
| --- | ---: | ---: |
| 存档2：模型切换长帧中位（4次） | 149.179 ms | 140.326 ms |
| 存档3：模型切换长帧中位（4次） | 195.523 ms | 186.527 ms |
| 存档2：实际纹理上传中位（3次） | 70.185 ms | 34.169 ms |
| 存档3：实际纹理上传中位（3次） | 73.518 ms | 33.672 ms |
| 存档2：推进期间平均 FPS | 54.389 | 54.305 |
| 存档3：推进期间平均 FPS | 58.485 | 58.571 |

SW只减少部分卡顿：需要上传时节省约36～40ms，但新建实例/纹理源复制更慢；无需重新上传的kaz_6共享纹理路径，create约70→113ms，存档2对应长帧111→153ms。现有完整memcmp和8MiB源复制是后续候选，尚未用独立计时证明各自占比，也没有放宽精确内容校验。两模型源文件总量约10.25→17.59MB，GPU各自占用仍为8MiB。存档2约260～262ms的另一次效果提交长帧仍在；本轮没有解决所有卡顿，也没有持续FPS提升。

6轮无加载/回放错误；每轮源测试存档hash未变，最终原存档2/3再次校验未变。所有临时PSB、测试存档和临时设置已清理/恢复，保留原sample01_sw游戏包并重新启动宿主。没有新的真实动画逐像素oracle。脚本新增可选`--advance-count`以固定配对按键次数，Python语法和diff检查通过。证据见 `tests/emote_recovery/sw-layout-device-verification.json`，原始日志/截图位于本地忽略的 `build/model-switch-study/sw-compare` 和 `build/recovered-save-bench`。

### 2026-10-14 CPU 创建与纹理共享：省去自比较和中间源复制

保持原sample01_sw PFS、444/222、完整网格/动画/透明/效果及缓存额度。纹理候选仍核对宿主、格式、布局、尺寸和源长度；仅当像素或palette指针相同时省去对应memcmp，不同地址仍逐字节比较，采样fingerprint碰撞仍不能共享。已有同一不可变PSB的多实例不再重复扫描8MiB纹理。

新增可选EmTextureSourceLease。明确提供不可变PSB生命周期的BC3加载，延迟上传直接借用原压缩数据，不再先分配/复制8MiB临时源；普通调用方和CI8转RGBA仍走原有独立拷贝。宿主SourceLifetime同时持有CpuModel与Rust源/预算Arc，只有create成功才接管，待上传纹理可独立保活。模型缓存驱逐或view销毁不会使packet悬空；上传就绪或最后一个pending资源取消时释放借用。GPU分配/上传拷贝、fence及退休规则完全不变；未引入无预算的纹理常驻缓存。

实机以旧包feb4e66e与候选05edef4f各测原存档2/3，10秒预热、至少35秒采样、固定12次按键；每处前后采样的四次模型创建序列一致。按键是定时推进而非逐帧同步，以下切换帧包含其他逻辑/场景工作，不是纯创建耗时：

| 对比项 | 优化前 | 优化后 |
| --- | ---: | ---: |
| 两处CPU创建范围 | 52.174～112.767 ms | 11.250～15.788 ms |
| 存档2切换长帧中位 / 最大 | 140.857 / 153.899 ms | 89.318 / 95.530 ms |
| 存档3切换长帧中位 / 最大 | 185.922 / 202.655 ms | 139.071 / 153.707 ms |
| 存档2推进期间平均FPS | 54.457 | 54.238 |
| 存档3推进期间平均FPS | 58.598 | 58.856 |

存档2共享已有纹理的那次切换153.899→55.872ms。普通新上传仍约33～39ms；create剩余主要是约10～13ms上下文分配/初始化，view绑定约1～4ms。整体平均帧率无明确提升；存档2另一个效果提交长帧259.701→270.190ms仍未解决，不宣称无卡顿或稳定60FPS。本轮仅两处短期对比，不外推完整游戏长时间内存压力。

O0/O2 ASan/UBSan真实纹理12场景及新保活/失败/取消用例通过；同指针零字节比较计数、异地址fingerprint碰撞拒绝均通过。Facade测试实际释放PSB backing，覆盖先驱逐model、异线程销毁view、packet后续延迟上传/失败/取消，引用恰好释放一次，上传源hash不变；12帧顶点/UV/颜色逐字节一致。ARM构建、打包及diff检查通过。实机四轮无加载/回放错误、资源账本faults=0；OBS确认立绘/头像/背景/文字和444/222，没有新做真实动画像素oracle。

最终SELF `05edef4fd11f713743bb54998fcce78f5ed6539d7e1629d3c72fb68c1a95f55e`，VPK `2d532fa371ec5742b299a411a8cf66df47417c26f9eb5700602ab175de446fa5`，已FTP读回校验安装并保留。原存档hash未变，临时存档已删除、设置已恢复；实机留在存档3推进后的头像对话。详细记录：`tests/emote_recovery/cpu-source-lease-device-verification.json`。


### 2026-10-14 对话框隐式规则预读：消除约 270 ms 的效果提交长帧

原 `cpu-after-slot2` 的 270.190 ms 帧并非 GPU 效果计算本身：`msgoff.png` 在提交中同步读文件 146.930 ms，随后解码，`effect-frame-spike` 的 submit 达 245.538 ms。目标切换仅 228 us，无新离屏目标分配。游戏 `system/msg/mw.lua` 的 `msg_getrule` 从 `init.rule_msgon/rule_msgoff`（或编号窗口配置）选别名，`list_windows.tbl` 配置为 `msgon/msgoff`；旧 AST 预读只覆盖显式 `rule`，漏了该依赖。

- `core/crates/asb-interpreter/src/preload_hints.rs` 在章节 mask hints 中加入已配置的显示/消失规则及 01..99 编号配置，复用原规则别名、路径/扩展名、去重和语言筛选逻辑；仅 raw 读表，不调用 Lua helper/metatable，不写死游戏资源名。
- 继续使用现有 16 MiB mask 预算、章节取消和后台解码；GPU 仍由渲染线程按原 gray8 路径上传。无 shader、网格、动作、混合、效果或纹理内容改动。未配置/动态/语言规则仍走原正确性回退。
- 9 项预读测试通过，1 项外部章节夹具测试未提供而跳过；ARM Rust/native 构建与打包通过。

sample01_sw 实机固定 CPU 444 / GPU 222，原包和新包分别冷启动加载存档 2、3：预热 10 秒、有效采样至少 35 秒、各 12 次同间隔推进。输入和模型切换序列相同，但不是逐帧同步回放，以下是单组配对结果。

| 指标 | 原包 | 新包 |
| --- | ---: | ---: |
| 存档 2 msgoff 对应完整帧 | 255.221 ms | 40.373 ms |
| 该帧 direct_present | 230.4 ms 左右 | 11.030 ms |
| 存档 2 整段采样最长帧 | 255.221 ms | 102.393 ms |
| 存档 3 整段采样最长帧 | 163.204 ms | 164.485 ms |
| 存档 2 推进期间平均 FPS | 54.713 | 54.296 |
| 存档 3 推进期间平均 FPS | 58.855 | 58.884 |

新包两张规则图均有后台 `payload=gray8` 和使用时 `GXM prefetch-hit` 证据，没有对应 `texture-slow-read`。存档 2 msgoff 上传 2.708 ms；存档 3 msgon 上传 2.413 ms。存档 3 原包的 msgon 同步读 327.257 ms 出现在正式采样前，不能算成采样 FPS 的收益。平均帧率没有实质提高；收益是去掉存档 2 这次规则读取/解码的明显卡顿。剩余模型切换峰值（约 102/164 ms）尚未消除。

OBS 已检查存档 2 切换后的立绘、眨眼、透明对话框和存档 3 头像，画面正常；非逐像素 oracle。日志无渲染错误，末尾 ledger faults=0。原始存档 hash 验证未变、临时测试存档删除、每轮设置恢复。当前实机保留新包。

- SELF：`078a53141c064d39a3d455630046b876d1a805127fdfda9bd93e08f709931f81`
- VPK：`715c654da46ff774982055be1b94ea441a06ff7228e98f6d00cd7e60576259d0`
- 验证报告：`tests/emote_recovery/message-rule-device-verification.json`
- 原始日志：`build/recovered-save-bench/rule-{before,after}-slot{2,3}/`
- 安装/备份记录、OBS 截图：`build/effect-rule-study/`


### 2026-10-14 引擎名称与默认选项

游戏设置中 recovered/backend=2 显示为“新emote引擎（默认）”，builtin/backend=0 显示为“旧emote引擎”。包含 DIRECT_RECOVERED_PACKET 的安装包默认使用新引擎；未包含该实现的构建保留旧引擎回退。没有有效配置时使用构建默认值，已有有效配置仍按原编号读取，不覆盖用户明确保存的旧引擎选择。新引擎默认使用 4×4 网格，也可独立选择渲染细分；旧引擎继续使用已保存的 Mesh 值。菜单预载文本、说明和英文翻译已同步。

现有 emote_backend_settings_test 在启用/禁用新引擎两种构建下均通过 ASan/UBSan，覆盖默认值、持久化、备份读取、损坏配置回退、跨游戏隔离、Mesh 保留和菜单输入。


### 2026-10-16 批量 CPU 求值接口

宿主的art3m1s_recovery_capture_batch同步接收至多64个不同实例ID；每轮至多三个模型并发求值，返回前join，再在原资源所有者线程生成与注册packet。Rust按实例ID取回packet，不依赖HashMap遍历顺序。顶点工作池与纹理/GPU资源生命周期保持串行批次边界；销毁或命令修改不能穿过求值期间的宿主锁。

第一版444/222实测：存档6为47.04 FPS（基线45.30），存档5为59.61 FPS（基线53.32）。最终减少TLS查询包恢复前台后实测：存档6为47.62 / 47.53 FPS，存档5为59.81 FPS；三轮实际444/222、源存档不变、设置恢复。详细验收与未完成项见recovered-native-gaps.md的“跨模型 CPU 求值隔离”及tests/emote_recovery/model-parallel-performance-verification.json。


### 双模型CPU任务与宿主构建重叠（2026-10-16）

双模型通过capture_begin返回待完成的快照身份；求值与分段顶点任务在两个工作线程上执行，所有者同时构建文字和场景，在retain/render前由不可跨线程移动的RAII守卫完成join及packet。提前使用快照、修改实例、收集或shutdown会先join；退休标记不会提前释放任务输入。非双模型和工作池失败保留同步路径，纹理/GPU所有权不转移。

存档槽6、444/222实机：上一版47.53FPS，单独顶点调度47.56FPS，加入宿主重叠后58.24FPS，复测58.42FPS；CPU主逻辑16.29→12.47ms。无网格/效果/动画降级。重叠后的frame_emote计所有者准备与剩余等待，不再代表完整人物计算时间；原生capture与scene计时不能相加。详见tests/emote_recovery/model-overlap-performance-verification.json。


### 快进切模型时的线程状态耗尽（2026-10-16）

实机转储定位到顶点线程 `pte_osTlsGetValue` 的非法地址读取：`R0=0xfffffff8`，上层为 `vertex_update_range → __emutls_get_address`。核对同一安装包 ELF，`vitasdk_get_tls_data` 会将 `__getreent_for_thread` 返回值减 8；该 SDK 的 256 条线程记录全部占用后，回收分支返回初始零指针。旧顶点池随模型释放反复销毁，只执行内核线程 join/delete，未通过 pthread 清理编译器 TLS 与 newlib 线程记录，触发上述路径。

顶点池和模型求值池改为 joinable pthread，退出执行 TLS 析构，join 后退休运行库记录；CPU 亲和性、优先级、栈大小和任务划分保持原设置。工作线程先发布设置结果，失败时完整 join/清理后回退，模型资源仍在 join 后释放。独立 demo 构建显式链接 pthread，避免弱符号导致 TLS 路径缺失。

本地 ASan/UBSan：320 次附加线程池重建、640 次 TLS 析构、8 处失败注入以及 1,080 次串行/并行快照对比通过。实机 444/222 两类线程池各重建 512 次，总计 2,048 条线程、4,096 次合成 TLS 任务通过；完整修复包已安装并读回校验；存档 6、444/222 采样约 30 秒为 59.94 FPS，源存档未变、设置恢复。连续快进触发额外 131 次模型创建，未新增崩溃转储，释放按键后帧日志持续更新。FTP 中途超时，重连验证运行正常；观察窗口约 170 秒包含传输/重连开销，不视作精确快进时长。压力探针堆使用合计增加约 32 KiB，尚不宣称零泄漏；该少量增长需继续排查。原崩溃会话另有 195 次、本轮快进新增 799 次 Step/Skip 未实现的脚本错误，本次未补齐该 API，也不将它当作本次原生访问异常的根因。详情见 `tests/emote_recovery/worker-tls-crash-verification.json`。

## Step / Skip 宿主原生语义接入（2026-10-16）

Step 处理尚未完成的循环前奏，按原生顺序到达循环末端、回到循环入口并推进差分过渡；重复调用不会再次跳前奏。Skip 保留循环的当前位置。两者都会收尾非循环动作和变量过渡；Step 会结束正在淡出的循环，Skip 先保留它并结束权重过渡，由后续 Tick 退休。

宿主命令 10/11 在修改模型前等待并行求值结束。物理状态恢复为元数据初始值，清空反馈和待处理物理时间；自动眨眼、说话、风和模型时钟保持各自状态。跳转后的循环使用实时命令队列续播，防止下一帧的解析求值把表情拉回；动作槽复用时仍按原生播放顺序提交。Win 过渡取出队列即消耗本次 delta，与 PSV 过渡内核的延后一步行为分开。

原 DLL 指令差分执行 192 组，共 2,688 个状态比较通过，包含实际 seek/forward/SetVariable/差分更新/收尾和随后 Tick；仅替代 CRT、分配器及安全 cookie 服务。本地 ASan/UBSan 通过混合动作、重复调用、变量队尾收尾、物理重置与自主控制器保留测试，以及 1,086 次串行/并行快照比较，覆盖求值未完成时发出 Step/Skip。

边界：这是脚本使用的原生宿主 ABI 恢复；并未证明三个 PSV eboot 的同名 vtable 身份，也不等于完整原生播放器所有专用控制器、变换 API 的重建。原生证据、公共 API 测试及实现范围见恢复仓库 `reverse/host-step-skip-audit.md`。

实机最终包已安装并读回校验，SELF SHA-256：`5f00566b6a0ffb2dae985c440d17408fa13bba80b9b8a99a9db9119c73cf23b7`。存档 6 双人场景在 CPU 444 / GPU 222 下，预热 10 秒、采样约 30 秒为 59.81 FPS，源存档未变、临时设置已恢复。连续快进由用户按住 R 键实测并反馈“测试了下没问题”；本轮远程按键未成功触发持续快进，因此不把该自动采样作为快进通过证据，也不报告未经测量的快进时长或 Step/Skip 次数。验证摘要见宿主 `tests/emote_recovery/step-skip-hardware-verification.json`。

### 多人物自动眨眼去同步（2026-10-16）

宿主两个创建入口在模型绑定完成后，使用不同实例 ID 初始化各自的随机状态和首个眨眼等待时间。原先所有实例采用相同固定种子，相同参数与起始时刻会导致持续同步。保留模型的眨眼间隔范围、闭眼/睁眼时长、脚本表情以及 Step/Skip 保留自主控制器的行为；只在创建时初始化，不在求值线程或每帧重新播种。种子扩展是适配层策略，未声明恢复了原生随机种子的来源。

合成模型 ASan/UBSan 回归覆盖 1,800 帧：不同种子眨眼轨迹分离、同种子重放逐状态一致、两个实例均持续眨眼。PSV 编译、打包和安装读回校验通过。存档 6 在 CPU 444 / GPU 222 下，预热 10 秒、采样约 40 秒为 59.66 FPS，另一次 30 秒复测为 59.60 FPS（上一版 59.81 FPS），源存档未改写、临时设置已恢复。存档 11 是用户指定的眨眼测试场景：OBS 已观察到左侧闭眼时右侧仍睁眼，之后左侧重新睁眼；用户确认通过。眨眼检查为定性画面验证，不是逐帧原引擎对照。实机 SELF：`a0a5024fd87a7cbcb400119459c3a003faa9d57a1b282225b5fd95bbdb82bc44`；记录见宿主 `tests/emote_recovery/blink-independent-hardware-verification.json`。

### 2026-10-19 Three-character parent/patch reuse experiment

Original ARM `811A00B0` writes position before the optional angle/scale branches
(`811A01BE..811A01EA`). A candidate therefore shared position-only parent samples
between children with identical inputs, and reused world patch construction only
when the affine, rectangle and combined control points were bit-identical.
Original position/guard checks, 79-model full-recompute comparisons and synthetic
cache invalidation cases passed. This did not justify keeping the optimization:
two consecutive 30-second device windows at CPU444/GPU222 measured 27.73/27.70 FPS
against the preceding 27.84 FPS candidate (earlier baseline 28.04 FPS). Logic
remained about26.5ms and presentation about9.4ms. All three characters were visible
in the same saved dialogue. No mesh/effect reduction was applied.

The parent/patch reuse candidate was removed from production; the private
candidate and logs remain available for investigation. Existing triple capture
overlap changes are preserved. See
`tests/emote_recovery/parent-chain-reuse-verification.json`. Next work should split
base geometry invalidation by affine, translation, chain identity and rectangle,
rather than treating a cache correctness pass as a performance result.

Additional ARM evidence: `811A0544..811A056C` derives the force argument from
model+277, layer+39, parent+596 and layer+640, calls `81193E04`, then branches to
`811A0B50` when the return value is zero. This is a wider downstream update gate
than caching only the local pose value. A future implementation must preserve
parent/physics/event dependencies and retained per-instance state; it is not
justified to skip a child because its authored keyframe is unchanged.

### 2026-10-19 Parent update gate device experiment

The ARM force boundary above passed 48 flag combinations and three return-branch
cases. A per-reference-instance candidate retained completed nodes and optional
deformation records while keeping clocks/events/anchors live. Frame-local stencil
IDs were never replayed. The second revision propagated effective output changes,
not merely whether a parent executed work, and avoided preparing keys when the
parent already required recomputation.

The second revision passed 518,187 cache-hit versus full-recompute checks,
1,086 serial/batch comparisons under ASan/UBSan, and the 79-model catalog with
10,165,736 bit-exact vertex comparisons. This validates the tested adapter reuse
conditions, not every original native update flag.

The same three-character save at CPU444/GPU222 measured 26.73 FPS over30.08s and
26.70 FPS over25.09s versus the preceding27.84 FPS. CPU logic rose from26.45ms to
about27.9ms; presentation remained about9.4ms. About94 percent of layers were
parent/chain-forced, about4 percent reused, and no deformation patches replayed.
Tracking costs exceeded the savings. A missing telemetry interval was excluded,
not counted as slow frames. All three characters were visible; effects, mesh
density and animation frequency were unchanged.

Both candidates were rejected and removed from production; private experiment
storage retains their source and binaries. Existing capture overlap and mesh
work are preserved. See `tests/emote_recovery/layer-update-gate-verification.json`.
Next work should reduce genuinely changing geometry costs and distinguish the
deformation components a child consumes, rather than add whole-parent equality
checks to every layer.

### 2026-10-19 Deformation component and sampling refinement

Parent position synchronization now keys only the angle/scale components that
the parent sync bits and child inheritance bits actually consume. Cache hits
preserve the current values of ignored components. Raw parent patch, position,
rectangle and offset remain exact dependencies; ancestor/world deformation is
still evaluated independently.

Single-point cubic sampling evaluates its four independent columns with NEON,
retaining each column's multiplication/accumulation and final reduction order.
Four normalized finite-difference samples also share the point-parallel kernel.
The original ARM comparisons passed: 10,000 scalar world samples, 8,100 batch
points, 324 tail points, 32,400 chained transformations and 8,000 child-frame
comparisons. Integration checks include 256 unconsumed-component mutations and
1,086 serial/batch packets under ASan/UBSan. The tested fixtures are finite;
these checks are not a proof for all floating-point exception modes.

The new package was installed with full readback verification. The unchanged
three-character save at CPU444/GPU222 measured 27.74 and27.70 FPS in consecutive
30-second windows, versus a fresh27.83 FPS baseline. CPU logic remained about
26.6ms and presentation about9.4ms. No meaningful speedup was established.
The requested new package remains installed, with all three characters visible;
no mesh, effect or animation-frequency reduction was applied. This refinement
does not complete the scene performance target. Evidence and executable hashes:
`tests/emote_recovery/deform-components-verification.json`.

### 2026-10-19 GPU affine submission

The host now applies the packet affine through the existing vertex shader's
modelViewProj matrix. It no longer maps every character vertex on the CPU and
copies an additional transformed stream. Packet capture records a finite input
bound; unusual input or transform ranges keep the previous CPU validation path.
The packet and GPU arena remain immutable until their existing retirement points.

Private clip/clear quads use identity, while character and mask geometry use the
packet matrix. Scene changes reset the matrix state. Affine soft-mask ROI keeps
the full target even on the CPU fallback, avoiding reuse of a previously clamped
untransformed ROI. This does not reduce mesh density, animation rate or effects.

An intermediate interleaved NEON copy increased capture packing from about4.6ms
to7.2ms per frame and limited the scene to29.2 FPS. Restoring the platform bulk
copy and checking only the position lanes brought packing back to about4.9ms.
The finite bound is checked during capture, not rescanned at submission.

Verification covers128 changing affines within0.001 stage pixel using a CPU
matrix oracle, private identity quads, hard/soft transitions, delayed GPU reads,
sparse/capacity fallbacks and108 injected API failures. The test compiles the
vendored packet implementation. The ARM bounds kernel passed200 boundary and
arbitrary-bit cases; its oracle substitutes a byte-copy implementation for libc,
so it does not benchmark the platform memcpy. Serial/parallel packet equality
passed1,086 comparisons under ASan/UBSan. These are not raster equivalence proofs.

Device performance windows, executable/source hashes and visual scope are in
`tests/emote_recovery/gpu-affine-verification.json`. Remaining CPU scene/evaluation
work is still the dominant cost; this change does not complete the60 FPS target.

The final package was installed and read back successfully. At CPU444/GPU222,
the same three-character save measured31.17 and31.16 FPS over consecutive30-second
windows, versus27.63 and27.69 FPS in the baseline capture. Presentation wall time
fell from about9.4ms to5.3ms; CPU logic remained about26.7ms. All three characters
and their masks appeared in the same layout. These device observations cover
this hard-mask scene; soft-mask pixel equivalence remains a separate check.
The final package remains installed; no save was overwritten.

### 2026-10-19 Fixed grid topology sharing

Each captured packet now owns at most one index span for each fixed 1x1 and
8x8 grid topology. Repeated sprites reference those spans without repeating
index validation or copying. Arbitrary caller meshes retain validation and
private copies; their source pointers are never used as cache identities.
Vertex streams, mesh density, effects and animation frequency are unchanged.

Production replay tests compare expanded and shared grids, interleaved arbitrary
meshes and mutated caller indices, retained GPU data after packet destruction,
allocation failures and sealed mutation rejection. The mixed fixture reduces
indices from24,960 to3,906 while preserving the recorded triangle stream.
Serial/batch packet comparisons also passed1,086 checks under ASan/UBSan.

At CPU444/GPU222, two consecutive30-second windows in the unchanged three-character
save improved from31.18/31.17 FPS to33.87/33.91 FPS, approximately8.7 percent.
CPU logic fell from about26.8ms to25.1ms, packet construction from5.0ms to3.7ms,
and presentation from5.2ms to4.3ms. All three characters were visible in the same
layout. This does not meet the60 FPS target or prove pixel equivalence.
See `tests/emote_recovery/grid-topology-verification.json` for measured windows
and package hashes.

Sparse profiling samples one packet per121 captures. Allocation/destruction are
small in this scene; copy and validation costs dominate packet construction.
A follow-up counter splits deformation from vertex packing once per121 model
updates. Its times sum CPU work across workers and must not be interpreted as
frame wall time. The diagnostic package was installed and read back. Scene measurements put
roughly three quarters of sampled vertex CPU time in deformation, with the
remainder in packing. These sums are not the entire CPU logic time.

### 2026-10-19 Bounded render-grid sampling

Render-grid sampling now factors the two Bernstein axes, reducing repeated
multiplication of each control coordinate by both axes. Only the renderer calls
this path. Original scalar, batch and child-frame APIs keep native operation
order. Controls outside +/-8192, normalized samples outside approximately
[-0.25,1.25], nonfinite inputs and short tails use the original kernel.
The integer bit-range checks avoid ARM compiler scalarization of floating-point
comparisons; explicit column accumulation avoids large temporary stack arrays.

This is mathematically equivalent reassociation, not bit-exact output. Compiled
ARM tests found a maximum0.00390625 model-coordinate difference across4,067
single-stage points and0.0008544921875 after six-stage smooth chains across19,440
point transformations. Guards, tails,1,303 fallback points and12 nonfinite cases
were checked; the latter preserve exact-kernel output bits. These tested maxima
are not universal error bounds. The original ARM suite still passes10,000 scalar,
8,100 batch,324 tail and32,400 chained point comparisons bit-for-bit.

The same three-character scene at CPU444/GPU222 improved only modestly: a fresh
instrumented baseline measured33.29/33.32 FPS, while final candidate windows
measured33.82/33.90 FPS (earlier steady windows34.04/33.98). CPU logic fell from
25.6ms to about25.0ms. Three characters remained present with no visible
geometric distortion; masks, topology and animation frequency were retained.
The package was installed with full readback verification. This does not meet
60 FPS. Evidence: `tests/emote_recovery/grid-factor-verification.json`.

A separate local structural comparison used the same evaluator on three current
models and four corresponding models from each of the sample01/sample02
swizzled resource sets, sampling each action at frame30. The four paired PSBs
were byte-identical between those two reference sets. Current models produced
79-94 grids,6,399-7,614 vertices and14,661-20,007 point/patch transformations per
sample; both reference sets produced21-51 grids,84-3,240 vertices and0-6,480
transformations. Maximum chains were5 versus2-3. All had six defined mask groups;
this is not the number of executed mask passes.

This diagnoses structural workload differences, not relative FPS: textures were
mocked, script states were not matched, and action scans carried control state
forward. It does not mean every reference action has zero deformation or every
model in either resource set has been tested. Original assets and raw fixture
reports remain external. See `tests/emote_recovery/model-cost-comparison.json`.

## Per-game render subdivision (2026-10-19)

The E-mote precision row selects 8x8, 6x6, 4x4 (default), 2x2 or 1x1 for the new
engine, and retains the existing Mesh ratios for the old engine. Values are
stored independently per game and applied on the next game startup. Reduced
subdivision changes render tessellation only; animation and child deformation
semantics remain unchanged. Curved outlines may become less smooth, especially at 1x1 or 2x2. A 1x1
grid still evaluates deformation at four corners; it does not disable animation.

Vertex generation, packet topology, soft-mask bounds and parallel work weights
use the selected subdivision. Changing a model context invalidates mesh and
vertex caches; the host startup setter refuses changes while instances or
packets are alive. Host tests cover persistence with both backend build modes,
mixed 1/2/4/6/8 packet topology, and 1,810 serial/parallel comparisons including
quality changes and reloads. These checks do not establish device FPS gains
for the reduced quality modes.

## Idle phase separation (2026-10-19)

The host seeds both blink scheduling and idle phase from the instance identity.
Previously only blink scheduling differed, while simultaneous idle loops began
at frame zero. Newly faded-in Win additive loops now receive a deterministic
per-instance phase when their loop begins at zero and they have no enabled
instant-expression tracks. Playback speed remains authored. This is an adapter
presentation policy, not a recovered native random-start ABI.

Repeated fade-in keeps the current phase. Explicit Play and Seek, one-shot
motions, loops with an intro, enabled instant-expression tracks, and unseeded
or Vita contexts retain their prior start semantics. Step treats the seeded
loop as already entered; Skip preserves its current position. Instance
recreation can select a new phase; persisted idle-phase restoration is not
implemented. Synthetic ASan/UBSan regression evidence is recorded in
`tests/emote_recovery/idle-phase-verification.json`.

## Four-model evaluation queue (2026-10-21)

The facade now schedules up to four distinct model contexts in one batch on
its existing two workers and the owner CPU. Workers claim additional model
jobs after finishing their first evaluation; the Rust adapter also permits
four-model deferred capture. Previously four visible models used a synchronous
three-plus-one sequence and lost overlap with host scene construction. Packet
creation and texture ownership still remain on the owner after worker joins.

ASan/UBSan tests compare 2,490 serial/parallel packets across all five grid
settings, including four-model deferred capture, shutdown, retirement and
worker startup failure. These checks preserve exact vertices and commands.
At CPU444/GPU222 the same four-character save improved from 36.68 FPS to
45.64 FPS in the first stable 30-second sample and 45.01 FPS in a later sample.
CPU logic fell from 24.68 ms to 19.51/19.81 ms. Both runs used the existing
1x1 grid preference; quality settings were unchanged.
This is still below 60 FPS, with CPU evaluation remaining the main cost.
Device timings and their limits are recorded in
`tests/emote_recovery/four-model-device.json`; no live pixel-equivalence claim
is made by this performance measurement.

Explicit grid comparison on the same save and binary at CPU444/GPU222:
- 4x4: 37.66 FPS, CPU logic 23.20 ms, presentation 3.27 ms.
- 8x8: 24.28 FPS, CPU logic 34.59 ms, presentation 6.51 ms.

Each run used a 15-second warmup and 30-second sample, with four captures per
frame. Persisted preferences were restored after each run.
Evidence: `tests/emote_recovery/four-model-grid-device.json`.

## Control-patch CPU batching (2026-10-21)

Hot pose helpers now receive their owning model context explicitly, reducing
repeated ARM emulated TLS access. Fixed control-patch blending, parent patch
composition and affine mapping use NEON batches with the scalar operation
order retained. This does not change render subdivision or animation cadence.

The compiled ARM helper test matches the retained scalar reference byte for
byte in 1,536 finite-input cases, including extrapolated blend weights and
signed zeros, with output guard bytes checked. This is bounded test evidence,
not proof for every floating-point input. Serial/parallel packet comparisons
also pass all five grid settings, including worker startup failure.

At CPU444/GPU222 on the same four-model save, explicit context passing alone
measured 38.10 FPS at 4x4. Adding the patch batches measured 39.24 FPS versus
37.66 FPS before these two changes; CPU logic fell from 23.20 to 22.09 ms.
A second 4x4 run after the 8x8 comparison measured 39.08 FPS.
At 8x8 the combined changes measured 25.26 FPS versus 24.28 FPS, with CPU
logic falling from 34.59 to 33.19 ms. Both modes remain CPU limited and below
60 FPS. The device runs check clocks and four captures per frame; they do not
assert pixel equivalence. Evidence:
`tests/emote_recovery/patch-ops-verification.json` and
`tests/emote_recovery/four-model-patch-device.json`.

## Additive loop continuity (2026-10-21)

Win differential-only lanes now import their live controllers at the first
loop wrap and continue through the existing boundary-command path. Previously
analytic sampling restarted from the Play-time initial value at each wrap,
causing idle channels with a nonzero final target to snap back to zero. The
next cycle now approaches its first target from the previous cycle's current
value with the authored duration and easing. No extra crossfade is added.

This change is scoped to lanes without enabled normal/instant tracks. Shared
normal-controller arbitration, explicit seek/play, Vita playback and seeded
per-instance phase remain separate. The synthetic idle regression reproduces
the old seam failure and checks multiple continuous cycles after the fix;
Step/Skip lifecycle tests also pass. Device visual acceptance is separate from
these CPU checks. The user confirmed the installed fix resolves the visible
loop jump on the four-model scene on 2026-10-21.

## Empty-frame local pose reuse (2026-10-21)

Empty frames now use the existing per-layer local-pose memo instead of
unconditionally recalculating their immutable local values. Frame, next-key
and seek changes still invalidate the memo; parent, child, visibility and
world evaluation continue normally. Mesh subdivision and update cadence are
unchanged. A proposed vertex-loop TLS change was discarded after ARM
assembly confirmed the compiler already hoisted the lookup.

Before/after packet streams match across 2,490 comparisons (digest
`ccbd3e1e6c0b4a4c`); serial/parallel and worker-startup-failure suites pass
under ASan and UBSan. These are bounded CPU tests, not device pixel checks.

At 4x4 and CPU444/GPU222, the same four-model save measured 38.70 FPS before
and 39.08 / 38.95 FPS in two runs after. The first comparison reduced average
CPU logic from 22.48 to 22.24 ms; present time remained about 3.3 ms. This is
only a small observed gain, not a 60 FPS result. Sparse profiling still points
to deformation, parent/world affine evaluation and packet construction as
remaining CPU work; sparse per-model sums are not frame wall times. Evidence:
`tests/emote_recovery/four-model-empty-pose-device.json`.

## Four-model packet overlap and anchor demand (2026-10-21)

The owner can now build a completed model's immutable render packet while
other models continue CPU evaluation and vertex work. Per-model completion
counters publish all vertex ranges and statistics with release/acquire
ordering. GPU callbacks, packet leases and registry access stay on the owner.
Every worker is still joined before batch storage or model inputs can retire;
returned packet handles keep their original model order.

Each view also memoizes whether a layer name is needed by the physics anchor
lookup. Scene reload and controller metadata rebinding invalidate the demand
memo. Coordinates, first-match traversal and physical integration remain live.
The tests clear and restore physics metadata without destroying the view,
exercise 1/2/4/6/8 grids, deferred capture, pending shutdown and startup failure.
ASan/UBSan and TSan comparisons pass. TSan required disabling ASLR for the test
process; the default-ASLR attempt exited before producing test output.

Two matrix experiments were rejected: layer-indexed affine memo measured
38.93 FPS, and scalar-order NEON composition measured 38.76 FPS. Both were
reverted. Anchor demand alone measured 39.18 FPS. Final packet overlap measured
40.42 and 40.45 FPS, versus the preceding 39.08 and 38.95 FPS baseline runs,
with the same four-model save at CPU444/GPU222 and 4x4. This is about 3.6%
improvement, still well below 60 FPS. No geometry density, effect or animation
cadence was reduced. Deformation and the remainder of CPU scene processing
remain substantial costs; this change overlaps existing work rather than
removing those calculations. Evidence:
`tests/emote_recovery/four-model-packet-overlap-device.json`.

## Four-model deformation kernel (2026-10-21)

The bounded render-grid kernel loads each control point's X/Y pair together
and uses NEON lane multiplies. Per-coordinate reduction order, mesh density,
animation cadence and exact fallback stay unchanged. A before/after ARM
comparison covered 5,664 points with byte-identical output and intact guards.
The paired-load build measured 41.00 FPS and 21.07 ms CPU logic versus the
preceding 40.42/40.45 FPS and 21.40/21.38 ms runs, using the same fifth
four-model save at 4x4 and CPU444/GPU222. This is a small gain of about 1.4%,
not a result near 60 FPS.

The next experiment prepares the control-point range verdict once per world
patch and reuses it across sprites. Per-vertex inverse-map checks remain live.
The prepared route matches the direct route in 180 randomized cases, 12
nonfinite cases and 240 stages of six-stage deformation chains. The original
exact-kernel comparisons and 2,490 serial/parallel packet comparisons pass,
including worker startup failure under ASan/UBSan. The final build measured
41.22 and 41.34 FPS, with CPU logic at 20.90 and 20.84 ms. This is about 2.1%
above the preceding baseline. A network-interrupted attempt was excluded;
original preferences were restored before resampling and after each completed
run. The final package remains installed. The 4x4 grid, effects and animation
cadence were not reduced. ARM before/after comparison against the preceding
bounded kernel remains byte-identical for all 5,664 tested points. These
measurements do not establish device pixel equivalence or a universal error
bound; the existing render-only reassociation tests still apply.

Evidence: `tests/emote_recovery/four-model-grid-kernel-device.json` and
`tests/emote_recovery/grid-factor-verification.json`.
