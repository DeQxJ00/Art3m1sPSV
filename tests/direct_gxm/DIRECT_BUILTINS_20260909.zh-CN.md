# host-direct 内置效果补齐（1.10 主线更新）

## 范围和基线

用户要求仅基于 host-direct 补齐效果，不使用 host-direct-effects。
按用户要求沿当前主线提交，不另建候选分支。既有 v1.10 / 2275a3a 标签不改写。
当前主线安装包输出到 build/art3m1s-direct-latest.vpk。
core 使用 controls-source 的 2da40e8，新增编译特性 gxm-builtin-effects；
保留 gxm-text-epoch、gxm-menu-key-alias 等原来的特性。core 源码未改。

host-direct/src/shaders.hpp 的 SHA256 仍为
f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6。
不改这些已验证的 shader；新增独立 builtin_f.cg / builtin_shader.hpp。
未读取、复用或修改 host-direct-effects 的实现。

## 实现

- 旧 draw_texture ABI 不再丢弃 grayscale / negative。
- 接入 core 已有的 EffectDraw ABI，传递各命令和图层组的效果、颜色、透明度、遮罩、几何。
- 增加灰度、反色、alpha-mask、group-composite，以及规则转场与颜色过滤的组合。
- 分别实现 Alpha/Add/Multiply/Screen、预乘透明/加法及原生加法、反向相减、乘法、Screen 的混合状态。
- 普通 Alpha 图片/文字、无其他效果的规则转场继续使用原 shader、裁剪、批处理、透明边界与不透明优化。
- 扩展三角形网格、E-mote 四角色、局部裁剪和 wipe 参数的绘制通路。
- 按需分配并复用屏外颜色/遮罩目标，最多 8 层嵌套。进入时 GPU 清透明，离开时按正确顺序合成。
- 转场截图是 GPU 复制的独立纹理，不别名引用循环使用的显示缓冲；物理 960×544，逻辑尺寸仍由 core 管理。
- 顶点区只追加，不在屏外切换时重置。切换目标时结束 scene 并 Finish，保留安全边界。
- 普通帧不会因为新增程序而强制进入 FBO；额外 shader 首次需要时才创建。

## 验证

1. 追加 shader 用本机 psp2cgc 编译通过；原 shaders.hpp 哈希不变。
2. core 对应特性组合测试：356 passed / 0 failed / 13 ignored。
3. 独立 ART3BUI01 测试程序直接使用生产 host-direct/src/gpu.cpp。
   Vita3K session 37be6ffa-2b6f-4259-b172-1bbf4269e787：
   - 30 个独立 CPU 颜色计算检查通过，允许 2/255 误差；
   - 包括灰度、反色及其顺序、半透明、图层组反预乘、嵌套、透明 mask、池复用、九种混合、clip、三角网格；
   - 检查效果绘制后普通绘制，以及复制的截图在多次显示缓冲循环后仍保持原像素。
   - 证据 build/direct-builtin-shader/probe.png、pixels.txt。
4. 主线更新在 Vita3K session 879c29a5-91fa-4609-a8ca-01f7bd55f17f 运行。
   PCSG01297 已进入开篇，演讲/校报画面的旋转及灰度正确显示，随后恢复彩色立绘；
   证据 pcsg-opening-0-0.png、1-0.png、2-0.png（同上目录）。
   用户随后确认“可以了，效果正确了”。
5. 此后仅补了 uniform 申请失败时的绑定缓存失效，以及诊断按效果种类只记录一次；
   最终包重新编译。以上截图来自这两项防护/日志调整之前，shader 与正常渲染数学相同。

## 限制及后续性能工作

- 这是内置效果补齐，不是任意 HLSL 的通用编译器；自定义 HLSL 仍由 core 报未转换。
- E-mote stencil 尚未实现，四角色/wipe 尚无实际 E-mote 游戏回归，不能称为完整 E-mote 支持。
- 不做 4×MSAA，不改变三显示缓冲和时钟。实机保留原 1.10，未部署本次更新。
- 新增屏外同步可能降低含图层组效果的帧率。正确性先验证，再针对真实组结构减少不必要的合成和等待；
  不用 Vita3K 的 60 FPS 证明实体机性能。不得直接删 Finish 来获取帧数。
- PCSG01297 日志仍有原生音频 prepared open failed（-1128613112）；本次没有修改音频解码。
- 其他四款与 SHUF 的完整效果/存档回归尚未覆盖。

## 重建

先运行 scripts/build-direct-builtin-shader.ps1 和 scripts/build-direct-builtin-core.ps1。
host-direct CMake 指定 ART3_DIRECT_CORE_LIBRARY 为新 archive，启用 DIRECT_BUILTIN_EFFECTS、
DIRECT_TEXT_EPOCH_CANDIDATE、DIRECT_DEFERRED_FINISH_CANDIDATE、DIRECT_SEMANTIC_CONTROLS、DIRECT_HEAP_DIAGNOSTICS。
独立像素测试由 DIRECT_BUILTIN_PROBE=ON 构建，检查脚本为 check_builtin_pixels.py。
不要运行旧 build-direct-shaders.ps1 来重写基线 shader。
