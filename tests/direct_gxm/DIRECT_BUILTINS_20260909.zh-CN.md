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
- 不做 4×MSAA，不改变三显示缓冲和时钟。用户随后要求推送实机，已于 22:42 部署并启动本次更新；日志确认 333MHz。
  备份及上传回读校验见 build/direct-deploy/deploy-20260909-224205/manifest.json；
  启动日志为 build/hardware-logs/20260909-224254-current/host.log，实际游戏效果与性能等用户测试。
- 新增屏外同步可能降低含图层组效果的帧率。正确性先验证，再针对真实组结构减少不必要的合成和等待；
  不用 Vita3K 的 60 FPS 证明实体机性能。不得直接删 Finish 来获取帧数。
- PCSG01297 日志仍有原生音频 prepared open failed（-1128613112）；本次没有修改音频解码。
- 其他四款与 SHUF 的完整效果/存档回归尚未覆盖。

## 重建

### 2026-09-09 实机性能回归排查（进行中）

- builtin-v1 实机 SHUF00002 在 333/222/111/111 MHz 下约 14–16 FPS，
  direct_present 平均 59–62 ms；证据为 build/hardware-logs/20260909-224511-current/host.log。
- builtin-v2 将复制、图层组合成、普通颜色处理分成小 shader，仍未解决问题：
  每帧两组离屏绘制，切换累计约 33.6 ms，direct_present 约 57 ms。
  证据为 build/hardware-logs/20260909-225321-current/host.log。
- builtin-v3 增加有严格前提的中性组直绘、单图组融合及独立 single shader；
  不改变原普通 sprite shader 字节，不删除生产者/消费者 Finish 同步。
  单图融合保留子图与父组 tint、强制不透明、灰度、反色和中间 RGBA8 量化。
  复杂组仍走完整离屏路径。
- core 本机测试 360 passed、0 failed、13 ignored；融合像素检查 31 项通过，
  但像素截图取自将融合公式拆成独立 single shader 之前的同公式版本。
  最终拆分版本已经重新编译，不能据此宣称实机效果或性能验证完成。
- v3 于 23:15 推送实机并启动，上传回读校验记录：
  build/direct-deploy/deploy-20260909-231546/manifest.json。
  build/hardware-logs/20260909-231918-current/host.log 确认版本和 333 MHz；
  该段用户持续切句，仍每帧一组离屏且没有命中直绘，direct_present 约 45 ms，
  不可当作静止同场景性能结果，当前回归尚未修复完成。
- scripts/profile-builtin-hardware.py 在用户确认停稳后执行新/旧/新各 30 秒采样；
  不发送游戏输入、不改时钟，临时 builtin-generic.on 强制旧通用 shader 和离屏组路径，
  finally 恢复快速路径。原先存在该文件时停止，避免覆盖用户配置。
- 用户确认停稳后，build/hardware-builtin-ab/20260909-232239 的三轮实机结果：
  新 26.63 / 旧 15.74 / 新 26.53 FPS（host 各阶段 wall 耗时估算），
  各取三个完整窗口、均 50 quads / 25 draws，每帧一组且 flattened=0。
  最后确认移除旧路径覆盖文件。用户确认加效果前同画面约 60 FPS，故仍属明显回归。
- 继续核对发现宿主大于 1024 像素的纹理被保守标记为 opacity 未知，阻止
  neutral opaque group 的直绘证明。最新宿主在初次上传从源 RGBA 检查完整 alpha，
  动态更新仍保留原有保守规则；不扫描 GPU 截图或推测图片名称。
  现有 opacity oracle 的 156224 项检查通过，ARM 实际耗时与收益仍需硬件验证。
  最新部署为 build/direct-deploy/deploy-20260909-232712/manifest.json。
- 本轮先 server_health 确认 13341 的 PCSG01297，再只读核对 0x810134B0、0x81031EF4。
  仍可确认上层状态分派与切换时的同步，未宣称已复刻原生全部缓存失效条件。
  原始响应 build/direct-builtin-shader/native-current-health.json、native-current-groups.json。
- core 源提交 a90f2f1（父 2da40e8），补丁存于 scripts/patches/core-direct-builtin-groups.patch，
  避免仅将修改留在被忽略的 build/ 源目录中。

先运行 scripts/build-direct-builtin-shader.ps1 和 scripts/build-direct-builtin-core.ps1。
host-direct CMake 指定 ART3_DIRECT_CORE_LIBRARY 为新 archive，启用 DIRECT_BUILTIN_EFFECTS、
DIRECT_TEXT_EPOCH_CANDIDATE、DIRECT_DEFERRED_FINISH_CANDIDATE、DIRECT_SEMANTIC_CONTROLS、DIRECT_HEAP_DIAGNOSTICS。
独立像素测试由 DIRECT_BUILTIN_PROBE=ON 构建，检查脚本为 check_builtin_pixels.py。
不要运行旧 build-direct-shaders.ps1 来重写基线 shader。

### 2026-09-10：稳定组缓存与运动场景限制

- 全纹理 alpha 扫描未使实际大图通过不透明证明，没有改善上述回归；已恢复原先有限扫描策略。
- 实机当前安装的是单组合成缓存版，部署记录为
  `build/direct-deploy/deploy-20260909-234503/manifest.json`。
  VPK SHA256 为 `a3e0a65c30053bf6f663c7cc5be2dd968d8f2222d53d8acae82431d32d364338`。
- `build/hardware-logs/20260909-235632-current/host.log` 的静止正文窗口为
  300 帧/约 5 秒、300 次缓存命中，约 60 FPS；变化窗口仅约 24.5 FPS。
  标题的两个组未覆盖，仍约 17 FPS。用户进一步报告背景平移、头像场景低帧，目标未达成。
- 首次缓存自测读回黑色失败；增加诊断读回后的四帧灰度、负片自测通过
  （`build/hardware-logs/20260909-234712-current/host.log`）。首次失败原因仍未证实。
- 本地 core `d0078dd` 增加四个独立缓存槽、透明组缓存，并且只有连续两帧相同才构建缓存。
  持续变化时使用原有组绘制，避免每帧多一次缓存写入；这不等于原有离屏成本已消除。
  宿主补充透明预乘结果复用和多槽像素自测；自测失败会禁用缓存，继续原路径。
- core 363 项测试通过（13 ignored），ARM core 与 host VPK 构建通过。
  原始 shaders.hpp SHA256 仍为
  `f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6`。
  多槽、透明像素及预热自测尚未在实机验证，新包未部署，用户继续测试现有版本。
- 本次只读收集头像日志时 FTP 返回 `550 Could not allocate memory`；没有重启或操作游戏。
  不能据此确定头像帧率下降原因。下一步需取得对应日志，并验证运动路径与缓存失效范围。
- 后续 core `fd915c1` 将组参数依赖缩小到命令范围重叠的组；不相关头像组的
  alpha 等参数变化不再使背景缓存失效。嵌套组、重叠组仍参与比较，遮罩列表和
  纹理版本仍保守地全局比较。新增回归覆盖不相关参数、嵌套参数和遮罩变化，
  364 项测试通过（13 ignored），ARM core 编译通过。硬件收益尚未验证。
- core `3827c0c` 与宿主配套增加每张纹理的内容版本：只有全局上传版本变化时，
  才核对缓存所引用图片及效果纹理的版本。上传、局部更新、视频更新、截图替换和
  删除/重建均使相关依赖失效，无关纹理上传可继续命中。遮罩命令仍保守全量收集。
  回归包含无关上传保留缓存、相关纹理同 ID 内容变化拒绝缓存；364 项测试通过。
  这消除了一个确定的过度失效条件，但尚无当前头像场景的实机收益数据。
- 实机最终部署 `build/direct-deploy/deploy-20260910-003253/manifest.json`。
  `build/hardware-logs/20260910-003349-current/host.log` 确认缓存自测 PASS、333MHz：
  灰度 124、负片 131、透明合成 (78,94,110)，各连续四帧正确；旧缓存槽在新槽构建后仍正确。
  之前左上角 (50,50) 的显示读回全黑，但中间纹理正确、其他显示位置正确。
  将测试图块/采样移至中央 (450,290)，且用户关闭性能浮窗后通过；这符合浮窗遮挡，
  不再把左上角失败解释为组着色器错误。两个条件同时改变，未单独隔离变量。
  自测仅在一次性标记存在时运行；中间像素日志仅在自测启用，正常游戏无读回开销。
  部署工具逐个记录成功替换的文件，并支持安装后请求一次自测，避免中途失败掩盖部分替换。
  多缓存与失效范围修正现已安装，游戏头像/平移的帧率收益待后续测量。
- 后续真实场景测量：`build/hardware-logs/20260910-003622-current/host.log`
  平移窗口约 25 FPS，单组逐帧离屏，hits=0/builds=0，present 约 36ms。
  `build/hardware-logs/20260910-004050-current/host.log` 用户确认头像停句场景，
  两组中一组缓存命中，另一组逐帧离屏；正常窗口约 31 FPS，present 约 28ms。
  排除断网/恢复等跨窗口长停顿，不把 12 秒异常窗口当稳定帧率。
- core `da34b34` 增加可见 UV 区域不透明证明；宿主首次上传大图时按 64 像素块
  完整检查 alpha，查询包含双线性过滤边缘。局部更新立即撤销证明，未知视频/截图
  不根据画面名称推测。只有原有中性组条件和不透明覆盖条件全部成立才绕过离屏。
  365 项 core 测试通过（13 ignored）；独立 C++ 检查 10000 个区域，7485 个通过证明，
  均逐像素核对，含更新失效、越界和 NaN 拒绝。ARM core、host VPK 编译通过。
  这版尚未部署，平移收益未验证；头像组裁剪/遮罩诊断已加入，仍需实机定位。
- 待办保留：用户反馈正文和名字字号偏小；方块备用菜单每次重读 8364840 字节字体
  并重新生成字形，拟改固定文字图集。两项尚未改入此包。
- 可见区域证明版已部署：`build/direct-deploy/deploy-20260910-004438/manifest.json`，
  `build/hardware-logs/20260910-004540-current/host.log` 确认自测 PASS、333MHz。
  `20260910-004818-current` 显示已进入 SHUF00002，某一窗口 289 次缓存命中、无重建，
  但尚未确认是用户指定头像场景，不能当作该问题已修复。
- 本地 core `4cb120f` 允许有效矩形裁剪组缓存，烘焙时保留裁剪并写出透明边界，
  只有裁剪覆盖全屏且输出不透明时才使用不透明绘制。裁剪坐标变化会使缓存失效。
  新增宿主自测覆盖裁剪内、裁剪外和重复帧；365 项核心测试、ARM core/host 构建通过。
  该裁剪修正尚未部署，新增像素自测尚待实机执行。
- `20260910-004941-current` 日志出现背景组逐帧绕过离屏的窗口，约 48～54 FPS；
  用户随后确认平移改善。不同场景尚未严格对齐，不能据此宣称达到原生水平。
- `build/hardware-logs/20260910-005242-current/host.log` 用户确认头像停句画面：
  143 帧/约 5 秒、present 30.8ms，两个组中背景已绕过离屏。
  头像剩余组明确为 group-composite、opaque=0、mask=202、clip=(0,262,331,278)。
  因此前排除 texture mask，仍每帧重绘。core `f8e2d65` 允许纹理遮罩缓存，
  缓存依赖包含遮罩内容版本；GPU 烘焙和失败回退均保留遮罩，透明输出不走不透明混合。
  自测第四槽覆盖裁剪和半透明遮罩。365 项核心测试及 ARM core/host 构建通过。
- 普通精灵绘制同时复用可见区域不透明证明，只有轴对齐且采样区域可证明不透明时
  才允许既有混合关闭优化；仍检查顶点 alpha、rule 和 clip。区域 oracle 10000 次通过，
  补充反射、部分屏外与剪切拒绝用例。尚待与头像缓存一起作实机像素/性能验证。
- 上述修正已部署：`build/direct-deploy/deploy-20260910-005346/manifest.json`。
  `build/hardware-logs/20260910-005508-current/host.log` 确认四种情况各四帧及多槽独立性
  全部 PASS，包括新增裁剪＋遮罩，仍为 333MHz。相同头像页帧率对比仍待用户回到该场景。
- 本地进一步缩小已缓存裁剪组的显示矩形：使用裁剪范围外扩一像素的保守区域，
  保留完整 UV 映射，避免头像缓存每帧仍混合整屏透明像素。全屏组维持原范围。
  Host 构建通过；已有裁剪像素自测可覆盖此路径，但本次尚未部署/执行，不能宣称边缘和收益已验证。
- 头像缓存实机结果（安装版仍为 `9b6551d`，不是本地 `4c1bee5`）：
  用户确认头像页面后，`build/hardware-logs/20260910-005746-current/host.log`
  显示两个组、一个绕过、另一个缓存逐帧命中，300/293/300 帧窗口、builds=0。
  `build/hardware-logs/20260910-005820-current/host.log` 补充同样两组的 298/300 帧窗口，
  present 约 14.3ms、logic 约 2.2～2.4ms；相比此前确认头像页 143 帧/约 5 秒、present 30.8ms，
  当前停句接近 60 FPS。部分窗口仍有少量长帧，不能宣称所有帧都在 16.7ms 内。
  后续组数变化的窗口不混入静止头像对比；头像更换、平移、透明边缘及转场仍待用户继续验证。
