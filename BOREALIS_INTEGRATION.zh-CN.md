# Borealis PSV/GXM 在 art3m1s 移植中的复用方案

2026-09-07 源码核查；以下是接入设计，尚未编译或运行集成包。

## 结论

**推荐采用 xfangfang/borealis 的 PSV GXM 分支作为 C++ 宿主 UI 与显示平台层，并与 Artemis GXM 剧情渲染器共享 context/shader patcher。** 资料库、设置、暂停菜单、弹窗、输入法、平台状态优先复用；游戏脚本、文字语义、场景合成、音视频解码仍由 core/MediaService 负责。

核查提交为 `5f08b286f3df737f3321d2247a6fe633fcead03c`，当前 `wiliwili` 分支 HEAD 与本次固定 Wiliwili 提交的 `library/borealis` gitlink 完全一致。建议先固定这一配套版本，不混用 Switchfin 的另一份 Borealis fork。

## 1. 已确认是原生 GXM 路径

- `PsvVideoContext` 使用 `gxmCreateWindow()` 和 `nvgCreateGXM()`，创建 GXM/NanoVG 环境；`beginFrame()` 清屏，`endFrame()` 完成场景、系统对话框更新及显示交换。
- `nanovg_gxm_utils.h` 默认 960×544、stride 960、三缓冲，包含 GXM 内存、render target、shader patcher 和显示队列工具。
- `PsvVideoContext::getWindow()` 暴露 `NVGXMwindow`，其中包含 `context`、`shader_patcher` 和 framebuffer。Wiliwili 正是从这里取得对象，传给 mpv GXM 后端。
- 构建使用 `-DPLATFORM_PSV=ON -DUSE_GXM=ON -DUSE_VITA_SHARK=OFF`。GXM 不是该项目唯一的 PSV 路径，需显式选择；此模式不要求为显示接入 OpenGL/EGL。NanoVG 已有内置 shader，Artemis 自定义效果的 GXP 生成仍需单独解决。

依据：[PSV 视频实现](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/library/lib/platforms/psv/psv_video.cpp)、[GXM 工具层](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/library/include/borealis/extern/nanovg/nanovg_gxm_utils.h)、[构建选项](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/library/cmake/commonOption.cmake)、[Wiliwili 接入](https://github.com/xfangfang/wiliwili/blob/88e5876bea9502d06f46a8656e3530684d3aaf7d/wiliwili/source/view/mpv_core.cpp)。

## 2. 哪些部分直接用

| 功能 | 采用方式 | 仍需编写的业务逻辑 |
|---|---|---|
| 游戏选择菜单（首版必需） | Image/Label/Recycler、滚动、焦点导航、XML 布局 | 启动进入多游戏列表；扫描 ini/PFS、项目 ID、封面缓存、最近游玩、重新扫描、确认启动及退出返回列表 |
| 设置与独立菜单字体 | TabFrame、CellBool/CellSelector/CellSlider、Dialog | 全局/每游戏配置、独立 `ui.font_path` / `ui.font_size`、游戏字体、音量、按键映射及配置持久化 |
| 暂停和宿主菜单 | 半透明 Activity、按钮、确认弹窗 | 暂停/恢复 runtime、屏蔽剧情输入、返回资料库的资源释放 |
| 手柄/前触摸 | PsvInputManager、焦点和触摸事件 | 剧情虚拟鼠标、拖动、长按、键码映射、坐标逆变换 |
| 输入法/系统信息 | PsvImeManager、系统语言、确认键习惯、电池、网络状态、焦点变化事件 | 保存名称等操作、向 runtime/媒体转发暂停恢复状态 |
| 显示与 GXM 基础设施 | PsvVideoContext + NanoVG GXM 工具层 | 自有材质、游戏纹理/目标池、外部视频帧与 GPU 同步 |
| 调试与反馈 | Label/弹窗/进度控件、logger、timer | 显示解码器、帧时间、显存、日志导出等游戏诊断内容 |

Borealis 是可复用控件和平台框架，游戏目录浏览器、存档列表业务和封面加载策略需要宿主实现。UI 用 C++17，保留其需要的 RTTI/异常设置；Rust 核心继续通过 C ABI 连接，C++ 异常不得跨过 Rust/C 边界。[项目说明与集成要求](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/README.md)、[控件目录](https://github.com/xfangfang/borealis/tree/5f08b286f3df737f3321d2247a6fe633fcead03c/library/include/borealis/views)。

### 游戏选择菜单与字体要求（用户明确要求）

启动页必须能选择游戏，支持手柄/前触摸；展示标题、封面或占位图、资源路径，找不到标题时使用目录名/Title ID。空列表提供扫描路径提示，坏资源显示错误并留在菜单；退出游戏后回到列表且恢复焦点。测试默认优先 SHUF00002，不能将程序做成只启动该游戏的固定入口。

菜单字体由 Borealis 的 UI 字体注册/缓存层管理，使用独立配置项 `ui.font_path` 和 `ui.font_size`。建议内置 `app0:/resources/fonts/menu.ttf`，设置页扫描 `ux0:/data/art3m1s-gxm/fonts/` 的 TTF/OTF 供选择，提供预览和恢复默认。默认字体覆盖中文/日文常用字符，缺字回退按 UI 字体链处理；自定义文件缺失或加载失败回退内置字体。

菜单字体用于资料库、设置、宿主暂停菜单/弹窗；游戏正文、ruby 和游戏自带菜单继续使用 core 的字体配置与资源。切换 UI 字体需刷新布局和文本测量缓存，保存后重启恢复；不修改游戏配置或存档。实现时核对 Borealis 字体内存保活规则，字体切换与反复进出游戏都不得释放仍被 UI 引用的数据。手柄图标等专用字体保持单独注册，不随普通菜单字体替换而丢失。

## 3. 继续由 Artemis/媒体模块负责的部分

- ASB/Lua、等待/跳转/选择肢、游戏状态、存档语义及 PFS 文件系统。
- 游戏自己的标题菜单、正文框、ruby/逐字/描边、历史记录和图片按钮：保持 core 语义及原资源布局，使用 Artemis GXM renderer 绘制。Borealis 文字用于宿主 UI。
- 图层蒙版、特殊混合、转场、shader group、网格/E-Mote：实现专用 GXM 材质，不把每个游戏图层转成 Borealis 控件。
- FFmpeg H.264/NV12、Vorbis/AAC/MP3、BGM/SE/voice 混音和 A/V 同步。PSV 平台代码创建的是 `NullAudioPlayer`，不会因为接上 Borealis 就获得游戏音频或硬解。

依据：[PSV 平台实现](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/library/lib/platforms/psv/psv_platform.cpp)。

## 4. 推荐的第一版绘制衔接

```text
单一主/渲染线程
  1. 处理输入、媒体完成事件和 runtime tick
  2. Borealis 显示帧开始前：Artemis GXM 渲染到持久 stage 纹理
     视频 NV12 在这里作为纹理参与游戏合成；安排明确的写后读依赖
  3. Borealis 显示帧：GameSurfaceView 显示 stage 纹理
     然后叠加暂停菜单/设置/提示，完成一次显示交换
```

由 Borealis 的 PSV video context 统一拥有 GXM 初始化、显示缓冲和最终交换。core 借用同一 context/shader patcher，持有自己的游戏资源；不再额外创建第二套 GXM window/显示环，也不由 core 调用全局 GXM terminate。

`nvgxmCreateImageFromHandle()` 可以把 `SceGxmTexture` 注册为 NanoVG 图像，而且设置 `NVG_IMAGE_NODELETE`；适合让 `GameSurfaceView` 直接显示 stage 纹理，避免整屏 RGBA 回读后重新上传。该函数复制纹理描述符，并不自动追踪原描述符后续变化；stage resize/重新分配需要刷新包装，并延迟回收旧 GPU 资源。

显示包装只借用底层显存；NODELETE 只说明 NanoVG 不释放它，不证明 GPU 已使用完。切游戏/销毁时先移除视图及图像包装，等待关联 GPU 使用完成，再释放 stage/纹理/renderer，最后关闭 Borealis context。自有渲染器借用的 patcher 不能先销毁。

源码：[外部纹理包装](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/library/include/borealis/extern/nanovg/nanovg_gxm.h)。

### 最重要的时序限制

`Application::frame()` 内已经调用 BeginScene，不能在普通 `View::draw()` 中再执行包含 BeginScene/EndScene 的游戏离屏渲染。Wiliwili 的 GXM 代码明确针对这一点，将 mpv 离屏绘制用 `brls::sync` 排到 `Application::frame()` 外。

我们增加明确的帧前游戏渲染阶段，或使用同类主线程调度，保证每轮最多一次所需更新，并处理退出取消。显示纹理放在 GameSurfaceView 的正常绘制位置；不要把高频解码任务不断塞进 UI 队列。UI 事件和媒体命令需要 generation/取消机制，避免退出后旧任务访问已释放 runtime。

`PsvVideoContext::resetState()` 当前为空，NanoVG flush 会设置自己的部分 GXM 状态，但不能当作完整的外部状态恢复协议。自有 renderer 每个 pass 显式设置依赖的 program、blend、stencil、clip、viewport、纹理及顶点数据。输入也由统一路由分发：菜单打开时只让菜单处理该次输入，关闭菜单的同一个按键不能再推进一句剧情。

依据：[Borealis 帧循环](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/library/lib/core/application.cpp)、[Wiliwili GXM 离屏调度](https://github.com/xfangfang/wiliwili/blob/88e5876bea9502d06f46a8656e3530684d3aaf7d/wiliwili/source/view/mpv_core.cpp)。

## 5. 应提前验证的成本与限制

1. **默认 4×MSAA。** 当前 PSV context 固定为 `SCE_GXM_MULTISAMPLE_4X`，GXM 工具创建的目标沿用该配置。需要暴露配置并比较默认模式与低成本模式的字体/矢量边缘、帧时间、depth/stencil 占用；不能只改一处枚举，shader patcher 与 target 采样模式必须匹配。
2. **增加一次 stage 呈现。** 第一版将游戏合成到纹理，再由 UI 采样一遍，换取清晰的所有权与绘制顺序。实际测量该 pass；只有证据显示它成为瓶颈，才扩展原生绘制 hook，使无菜单时的游戏最终 pass 直接写显示目标。该优化仍须遵守三缓冲和转场捕获规则。
3. **静止页面的开销。** core 静止时复用 stage；Borealis 默认仍运行自己的 UI 帧流程。分别统计宿主 UI 与剧情成本，菜单关闭时不保留不必要的动画和封面纹理。不要把常刷 UI 当成剧情重绘。
4. **输入补充。** 当前只有前触摸；虚拟鼠标/背触摸等需求另行实现。摇杆当前归一化表达式为 `pad.lx / 255.0f - 1.0f`，中心值不会归零，阈值导航却使用原始读数；游戏若使用连续摇杆必须先校准范围/死区。肩键映射也要核对实体按键。
5. **两套坐标。** Borealis 触摸映射到 `Application::contentWidth/contentHeight`，游戏使用自己的逻辑舞台。GameSurfaceView 明确 viewport，先从 UI 内容坐标转换，再映射至游戏舞台，处理黑边、缩放和拖动。
6. **两套文字缓存。** 宿主字体与游戏字体分别按用途保留；进入游戏可释放封面和闲置 UI 资源。显存预算新增 NanoVG 字体/几何/USSE/目标占用，显示缓冲共享后只记一次；实际 stride 为 Borealis 的 960，主计划中 stride 1024 的数值是 IDA 样本估算。
7. **生命周期。** 复用平台焦点/电源事件，将事件排到主线程后协调音频、解码和 runtime；并非接入 UI 后即可自动完成游戏睡眠恢复。

依据：[输入实现](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/library/lib/platforms/psv/psv_input.cpp)、[PSV context 配置](https://github.com/xfangfang/borealis/blob/5f08b286f3df737f3321d2247a6fe633fcead03c/library/lib/platforms/psv/psv_video.cpp)。

## 6. 对实施计划的调整

| 阶段 | 加入 Borealis 后的工作 |
|---|---|
| P0 | 固定与 Wiliwili 一致的 Borealis commit；编译 GXM demo，测 UI/字体/显存基线；验证自有 shader 编译链 |
| P1 | 建立 C++17 Borealis 宿主，接 Rust staticlib；确定 context、外部纹理和退出的借用契约；runtime 解耦继续进行 |
| P2 | 复用 Borealis 三缓冲与 GXM 初始化；实现 GameSurfaceView + 自有 RGBA stage 纹理，再接 H264/NV12 探针；验证无嵌套场景、GPU 同步与菜单覆盖 |
| P3 | 用现成控件制作可选游戏的启动列表、独立菜单字体选择/预览/回退/持久化、设置/暂停和错误弹窗；优先把 T01 用户移植版接入新运行时 |
| P4–P5 | 完成游戏特效和媒体同步；按实测调整 MSAA、UI 缓存与最终呈现 pass |
| P6 | 保持既定顺序 T01 → T02 → T03 → T04 → T05 → T06，增加反复打开/关闭菜单、切游戏、输入法和睡眠恢复回归 |

首个集成验收演示：启动进入游戏选择菜单 → 设置独立菜单字体并预览 → 从列表选择 T01 → 剧情纹理持续显示且游戏字体保持原配置 → 打开暂停菜单 → 修改音量 → 关闭菜单且不误推进 → 返回资料库 → 再次选择启动；重启后验证菜单字体保持，删去自定义字体后验证默认回退。T01 按计划完成后再验证其他样本的选择启动。同时运行有声视频覆盖菜单，检查视图/纹理/解码资源回收。此探针通过后再据实调整工期；Borealis 可以减少宿主 UI 和显示基础设施的重复工作，不能抵消 runtime 解耦及游戏 shader 的兼容工作。
