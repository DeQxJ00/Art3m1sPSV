# 补丁用途与维护方式

当前核心在 [PSV core fork](https://github.com/DeQxJ00/art3m1s-core-psv/tree/codex/psv) 独立维护，主项目的 `core/` 子模块固定其提交。新 core 修复直接提交到 fork，不再用补丁堆叠构建。第三方依赖修复仍可在此保留补丁，方便更新依赖时核查。

这里的核心补丁是历史改动导出，基线不同、相互重叠，有的策略已被后续实现替代。**构建不读取这些补丁；不要批量应用，也不要向当前 core 重复应用。** 当前行为及额度以子模块源码为准，文件名中的旧额度不是现行配置。

下表逐项说明导出时解决的问题和影响范围。用途说明不等于整份补丁仍可反向应用；后续代码已演进，必须逐段比较。补丁、文档及提交说明按功能命名，不使用具体游戏名称或简称。

| 补丁 | 用途 | 范围／状态 |
| --- | --- | --- |
| [alpha-font-atlas-core.patch](alpha-font-atlas-core.patch) | 把正文、姓名和历史文字的字形／描边图集改用单通道 alpha，支持区域更新和失败重试；不支持的宿主回退 RGBA。 | GXM provider、glyph、draw |
| [artemis-bundled-and-external-shaders-core.patch](artemis-bundled-and-external-shaders-core.patch) | 内置效果按源码身份匹配，外置 HLSL 子集转换、Cg 编译、GXP 注册及参数绑定。 | GXM bundled/external effects、HLSL、runtime events |
| [cache-hud-core.patch](cache-hud-core.patch) | 右侧缓存浮窗的数据快照接口；默认关闭，采样有频率限制，读取端不做磁盘 I/O。 | cache_hud、GXM provider |
| [cache-hud-preload-counts-core.patch](cache-hud-preload-counts-core.patch) | 在缓存浮窗增加脚本计划／完成／解码就绪／压缩就绪的唯一资源数；完成数不等于常驻数。 | cache_hud、surface_loader、image_cache_budget |
| [circle-text-reveal-core.patch](circle-text-reveal-core.patch) | 保留 wait 的 input 参数，允许确认键先补全当前文字，再次确认才进入下一句。 | 解释器事件／标签、runtime script/text |
| [core-direct-builtin-groups.patch](core-direct-builtin-groups.patch) | 历史汇总：内置效果分组、预载提示、共享预算、PNG 附加信息与资源账本等；与多个单项补丁重叠。 | 解释器、GXM、图片／资源／运行时；不是可重放的完整基线 |
| [cpu-cache-shared-borrow-core.patch](cpu-cache-shared-borrow-core.patch) | 让已有 CPU 解码备份借用共享池余量，预载需要空间时回收；闲置 GPU 与压缩备份仍有独立上限。 | GXM provider；替代早期 CPU 固定额度策略 |
| [dialogue-image-cache-core.patch](dialogue-image-cache-core.patch) | 保留预载交付的解码像素，支持对白间复用；当前场景 GPU 资源保持引用，CPU 备份独立记账。 | GXM provider、surface_loader；额度以当前代码为准 |
| [effect-cache-final-priority-core.patch](effect-cache-final-priority-core.patch) | 最终特效结果优先保留，避免被输入缓存挤走；参数变化区分动画输入与最终结果失效。 | GXM native_effects/node_cache |
| [font-outline-cache-core.patch](font-outline-cache-core.patch) | 预生成连续覆盖率描边并缓存，透明边缘保留白色 RGB，减轻文字描边毛边／暗边。 | glyph、glyph/outline |
| [generic-effect-input-cache-core.patch](generic-effect-input-cache-core.patch) | 按图层输入快照缓存已有离屏节点的输入，复用子效果结果，并检查槽位修订与资源变化。 | GXM native_effects/input_snapshot、node_cache |
| [gray8-mask-cache-core.patch](gray8-mask-cache-core.patch) | 无损保留符合条件的单通道灰度遮罩为 L8，减少缓存和上传体积；CPU RGBA 消费者按需展开。 | GXM provider、图片解码／账本／surface_loader |
| [grayscale-neutral-rebuild-core.patch](grayscale-neutral-rebuild-core.patch) | 对源码已验证的灰度效果识别安全中性边界，重建时减少多余合成，仍保持预乘 alpha 语义。 | GXM external_effects、native_effects |
| [gxm-host-ownership-docs.patch](gxm-host-ownership-docs.patch) | 记录 GXM context、提交、同步由 Direct 宿主管理，Rust 管理纹理身份和元数据的边界。 | GXM 模块注释；仅文档 |
| [hlsl-only-shader-progress-core.patch](hlsl-only-shader-progress-core.patch) | 编译进度仅把 HLSL 请求计入处理总数；其他平台格式只统计，不算转换失败。 | runtime events |
| [idle96-decoded64-core.patch](idle96-decoded64-core.patch) | 早期把闲置保留上限改为 96 MiB、解码备份为 64 MiB 的实验记录。 | GXM provider；后续共享池借用策略已替代部分固定额度 |
| [image-upload-retry-core.patch](image-upload-retry-core.patch) | GPU 上传分配失败时保留已解码像素，后续帧重试，避免重复读盘／解码。 | GXM provider |
| [intermediate-ancestor-alpha-core.patch](intermediate-ancestor-alpha-core.patch) | 先合成人物身体、表情等子图，再对离屏结果应用祖先透明度，避免重叠部位透明度重复。 | compositor/build |
| [intermediate-coverage-core.patch](intermediate-coverage-core.patch) | intermediate_render 的模式 2 同样保留覆盖率，避免强制不透明导致背景被黑色覆盖。 | compositor/build |
| [legacy-font-roles.patch](legacy-font-roles.patch) | 早期依据消息层末级角色识别姓名／正文的字号覆盖；保留兼容回退。 | glyph/font_override；后续以脚本角色查询为主 |
| [logo-resource-alias-core.patch](logo-resource-alias-core.patch) | 不把游戏定义的黑／白背景别名预先替换为固定 2×2 纹理，尊重真实资源尺寸与透明度。 | GXM provider、runtime/project |
| [lua-cache-priority-core.patch](lua-cache-priority-core.patch) | 按 Lua 资源绑定的首次使用顺序优先预载，每批脚本请求后给补充提示留机会，避免饥饿。 | runtime/surface_loader |
| [masked-portrait-core.patch](masked-portrait-core.patch) | 将局部灰度遮罩按图层坐标合成，嵌套离屏层保留覆盖率，并在遮罩变化时使缓存失效。 | compositor build/scene、GXM native_effects |
| [mosaic-source-cache-core.patch](mosaic-source-cache-core.patch) | 对已验证的马赛克等采样效果复用输入画面，避免参数动画时每帧重绘相同内容。 | GXM mosaic_source、external_effects；后续另有通用输入缓存 |
| [native-commands-core.patch](native-commands-core.patch) | 新增／补齐原生文本指令与别名，包括缩进修改、范围计数和相关排版重放／缓存语义。 | 解释器 scenario/event、runtime text、glyph、backlog |
| [native-semantics-boundaries-core.patch](native-semantics-boundaries-core.patch) | 补齐自动播放的指定声音等待及清除、停止条件，并约束原生文字指令的作用边界。 | runtime control/events/media、文本排版 |
| [neutral-blur-routing-core.patch](neutral-blur-routing-core.patch) | 对尚以恒等程序回退的模糊节点省略无效隔离；有遮罩／裁剪时走保守路径。不是新增真正模糊实现。 | GXM native_effects；真实效果不能套用此恒等优化 |
| [progressive-prefetch-reservation-core.patch](progressive-prefetch-reservation-core.patch) | 预载按实际就绪量加有限增长窗口预留空间，避免少量待载任务过早清掉大量热解码缓存。 | image_cache_budget、GXM provider |
| [queued-wait-return-core.patch](queued-wait-return-core.patch) | return 解栈时清理被离开调用帧拥有的等待和延后标签，防止重用 UI 辅助脚本时复活旧停点。 | 解释器 interpreter；保留调用者的等待 |
| [remove-vita-gl-adapter-core.patch](remove-vita-gl-adapter-core.patch) | 移除已不用的 VitaGL 平台适配层；PSV 绘制由原生 GXM 路径承担。 | backend/gl/platform；不删除通用 GL 接口 |
| [save-thumbnail-false-mask-core.patch](save-thumbnail-false-mask-core.patch) | Lua 可选遮罩表达式返回 false 时按无遮罩处理，不能把 false 当作文件名导致缩略图消失。 | 解释器 lua_engine 的两类标签队列 |
| [script-message-font-roles-core.patch](script-message-font-roles-core.patch) | 通过游戏脚本查询当前消息层角色，区分姓名／正文；失败保留已有映射，缺少查询器时兼容回退。 | 解释器 message_roles、runtime text/save、glyph override |
| [shader-loading-progress-core.patch](shader-loading-progress-core.patch) | 发布内置命中、转换／缓存准备、编译等阶段，供宿主显示 Shader 加载进度。 | GXM external_effects、runtime events |
| [shader-shared-cache-core.patch](shader-shared-cache-core.patch) | 游戏自身缓存之外增加共享 Shader 缓存回退，并核对源码／Cg 身份，保留游戏缓存优先级。 | GXM external_effects；目录与开关由宿主实现 |
| [supplemental-builtins-core.patch](supplemental-builtins-core.patch) | 补充混合、放射等内置程序和源码身份表；现与已有内置库统一使用，不要求单独 supplemental 目录。 | GXM bundled、manifest、external_effects |
| [texture-upload-pressure-core.patch](texture-upload-pressure-core.patch) | 先固定完整场景资源引用，再按分配压力回收闲置 GPU 缓存并安排上传重试，避免遍历时误释放。 | GXM provider、runtime/render_gxm |
| [tremor-vita-arm.patch](tremor-vita-arm.patch) | 修复 Tremor ARM 内联汇编寄存器／约束写法，使 Vita 工具链可正确编译。 | vendor/tremor/asm_arm.h；已修补源码由 scripts/build-tremor.sh 编译 |
| [video-yuva-core.patch](video-yuva-core.patch) | 接收宿主已完成的 GPU 视频 RGBA 结果，避免每帧额外 CPU 镜像；保留必要的共享读取视图。 | GXM provider、FFI、runtime media；配套宿主 YUVA 转换 |

## 依赖更新与历史核查

`tremor-vita-arm.patch` 已合入 `vendor/tremor`，构建脚本直接编译已修补源码。只读核对可在主项目根目录运行：

```sh
git apply --reverse --check --directory=vendor/tremor patches/tremor-vita-arm.patch
```

此命令只检查，不撤销。核心补丁的路径通常带 `core/` 前缀，不能直接对独立 fork 根目录应用；即使使用路径剥离，也需核对基线和已有实现。`core-direct-builtin-groups.patch` 是累计汇总，不能作为从上游自动生成当前 PSV 核心的步骤。
