# 数值缓动路径（optR）

原路径 `resolved_props → format_value → set_raw` 在每次动画采样时把已有 f32 转成字符串，再解析成 typed 属性。动画完成和强制完成也使用同样的往返转换。

`LayerProps::set_tween_value` 让这些入口共用数值赋值：

- 有限的坐标、尺寸、锚点、缩放和旋转直接赋值；zoom 同时设置两个缩放轴。
- alpha 沿用先 round、转 i64、再限制到 0～255 的结果。
- 布尔属性只接受取整后的 0 或 1；其他数值保持原属性，不能按非零为真处理。
- 自定义 uniform、文本属性、intermediate_render 和其他长尾属性仍走旧格式化规则。非有限输入也走旧路径，保留 NaN/无穷值的处理。

不改变缓动时钟、插值公式、延迟、循环、yoyo、完成回调、动画速度或渲染顺序。GPU 和 shader 无改动；optR 使用 optQ CPU 基础，加上先前的 reveal 遍历优化，保持 optK GPU/帧尾等待。

## 回归与测量

`numeric_tweens_match_legacy_text_conversion` 对 33 个属性、3 种初始布尔状态逐次比较旧文本路径。输入包含 21 个特殊/边界值和 512 个确定性随机 f32 位模式；浮点字段按位比较，包含负零和非有限值，其余字段完整比较。`numeric_tweens_preserve_frames_across_loop_boundaries` 在循环和 yoyo 边界比较完整 DrawList；已有 compositor 动画测试覆盖终值、删除和回调。

忽略的手动基准 `numeric_tween_frame_benchmark` 使用 128 个精灵、每个 4 条持续缓动、1000 次完整场景构建。桌面 release 改前为 89682/86380/85148 µs，改后为 33984/31639/29172 µs；最终构建复测 37941/31320/27651 µs。它是动画密集的合成场景，不是实际游戏耗时、实机帧率，也没有证明解决静止语音页掉帧。原始结果在 `build/numeric-tween/`。

test-all 964 项通过、25 项忽略，既有 pf8 Windows 路径分隔符测试失败；pfs-upk 另 6 项通过。all-features lib、Vita core 和宿主编译通过；完整 all-features 仍缺 emote_parity_probe.rs，全库 fmt 有未处理差异。

## 原生证据的范围

本轮先健康检查 13341，确认输入为 PCSG01297 的 eboot.bin.elf；只读导出 `0x8102F718` 和其 thunk `0x8102FFA4`，保存在 `build/native-transform-audit/`。绘制函数读取对象中的数值，更新 `a1+448` 指向的四顶点内存，使用传入矩阵进行乘加后提交。该函数不是缓动解释入口：这些证据不能证明原生全部动画都不解析字符串，也不能证明它有固定的多级场景缓存。本次是按当前 core 已发现的重复转换实施优化，并非声称逐行复刻原生引擎。

## 包身份与运行边界

`build/01.02-optR/manifest.json` 记录包/core/eboot 哈希。GPU 目标文件 SHA256 仍为 `01075f1feff40f257b61d4f00d323bfa5ebc72e25580f38ea9cbf3d518a19afd`，shaders.hpp 仍为 `f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6`，已与 optQ 归档核对。没有部署实机；真实 333MHz 收益未验证。

首次 Vita3K 会话 `1b8a3253-8089-45eb-b2a1-392460ff4af8` 菜单、标题和章节确认显示正常，但点击“否”后采样失败，会话终止为 `0xC0000409`。Windows 转储 `Vita3K.exe.10872.dmp` 用匹配的 Vita3K.pdb 离线解析，栈是 Vulkan `cmd_handle_sync_surface_data → VKContext::stop_recording → vk::Queue::submit → throwResultException → terminate/abort`。返回地址位于 `throwResultException+0x43f`；前面的反汇编明确构造并抛出 `vk::DeviceLostError`。这是 Vulkan 设备丢失，不能仅按 Windows 通用退出码描述为栈溢出，也不是此前的 Qt 退出堆异常。系统 DLL 只有导出符号，旧 CDB 的通用 WRONG_SYMBOLS 标签不替代具体 Vita3K 栈和反汇编证据。

保留 `emulator-crash-host.log`、`emulator-vita3k.log`、`emulator-exit.json`、`crash-stack.txt`、`crash-exception.txt`。旧 optQ 包会话 `ffc17c6a-f726-490d-bf7b-becafe6dd4a6` 执行同样点击“否”并采集 18 帧，顺利进入开篇天空，随后 shutdown 退出码 0。一轮旧包通过不能排除新代码改变时序后的影响；optR 首次失败仍计为失败，不得把 CPU 基准或 shader 哈希相同当成运行通过。

不改包、不改配置的 optR 第二次会话 `ea6bf5ca-0c7f-4fb8-995a-e20ee3b9d6ea` 通过同样的章节确认及 18 帧采集。开篇首句后按 Circle 五次到“如果云层之上真有神的国度……”两行正文，Backlog 正常打开并用 Cross 关闭，回到相同正文；shutdown 退出码为 0。三块 Backlog 文字区域与 optQ 截图逐像素相同（backlog-pixels.json）。正文框区域直接比较有 23937/39840 像素不同、最大通道差 19；背景云层处于不同动画时刻，不能把这份比较写成正文逐像素通过，也未用它断定字形退化。

第二次通过不消除首次 Vulkan 失败；目前为一次失败、一次通过，根因未确定。没有运行完整转场/grayscale 场景矩阵、OGV 或实机性能回归；这仍是候选包。模拟器最终安装 optR、进程已退出。没有修改临时诊断控制文件、模拟器时钟或渲染设置。
