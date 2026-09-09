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
- 用户再次指出低帧，`build/hardware-logs/20260910-010029-current/host.log` 显示
  两个组均每帧缓存命中、无离屏切换，但 329 quads / 23 draws、submit 约 17～18ms，
  logic 约 6ms，约 40 FPS。此前 `005933-current` 还显示区域不透明扫描最高约 318ms。
  核对实际 flags.make：C/C++ 编译均无 -O 参数（Rust core 为 release，不受此影响）。
- CMake 现将未指定的单配置构建设为 Release，保留显式 Debug/RelWithDebInfo。
  实际重编译参数确认 -O3 -DNDEBUG，无 fast-math；所有宿主源重编译、链接和 VPK 完成。
  本轮也包含前述未部署的头像绘制范围缩小，后续收益不能全部单独归因于编译优化。
  区域扫描仍需实机复测；尚未用未经验证的近似 alpha 检查替换精确证明。
- Release/局部绘制版本部署为 `build/direct-deploy/deploy-20260910-010242/manifest.json`。
  `build/hardware-logs/20260910-010345-current/host.log` 确认完整缓存像素自测 PASS、333MHz。
  当前指定长文本页面和大图扫描性能仍待复测，不能以自测通过代替性能结论。
- Release 实机早期日志 `20260910-010436-current` 中 960×540 不透明扫描约 16～18ms，
  比部分旧日志低，但仍会占用一帧；具体图像内容未逐项对齐，不能作为严格扫描基准。
  本地继续将 tile 检查改为 NEON 批量 AND，只判断 alpha，仍精确验证每个像素。
  CPU 区域 oracle 10000 次通过，ARM 构建通过；新增启动自测覆盖实际 NEON 通道、
  尾部及跨度，失败会禁用区域证明与缓存。此 NEON 版本尚未部署，硬件自测和收益未验证。
- 用户确认回到低帧页后，Release 安装版的 `20260910-010737-current` 已出现
  329 quads / 23 draws、296 帧/约 5 秒。只读复取
  `build/hardware-logs/20260910-011040-current/host.log`，末八个约 5 秒窗口分别为
  300/300/299/298/300/298/299/300 帧，约 59.5～59.9 FPS。
  绘制规模持续为 329 quads / 23 draws，两组逐帧命中、builds=0；submit 8.0～8.4ms，
  logic 4.1～4.4ms，present（含等待）12.3～12.5ms。此前相同绘制计数的窗口约 40 FPS。
  仍有零星长帧（这些窗口最大约 77ms），因此结论是停句持续帧率恢复，非完全消除卡顿。
  期间未更新安装包、未操作按键；NEON 候选仍只在本地，不能计入本次改善。
- 后续只读日志 `20260910-011128-current` 抓到换图窗口最长帧 923742us；
  `:bg/zbg27k`（1920×1080）多次 decode 389～401ms，upload total 224～229ms。
  对应 host 上传中 opacity 69～72ms；这证明换图还有同步解码/上传瓶颈，
  不能把所有长帧归于渲染或透明度扫描。当前 inactive 缓存预算计 CPU+GPU 共 16MiB，
  单张该尺寸图已占约 15.82MiB；重复解码与缓存压力一致，但尚需逐次淘汰记录确认因果。
- NEON 候选 `4188ba3` 部署清单 `build/direct-deploy/deploy-20260910-011210/manifest.json`，
  实机日志 `build/hardware-logs/20260910-011300-current/host.log` 的 ARM 向量通道、
  尾部、跨行自测及全部保留层像素自测 PASS，333MHz。标准 latest 包已与安装文件同步；
  换图扫描耗时和游戏帧率待用户实际跑过对应场景，尚不声称本轮 NEON 性能收益已验证。
- `20260910-011600-current` 中相同 `:bg/zbg27k` 的 NEON opacity 约 60.5ms，
  较此前 69～72ms 有限下降；decode 仍约 393～395ms。切换窗口仍有明显长帧，
  因而不能宣称扫描优化已解决加载停顿，也不能将动态效果窗口直接与停句 60 FPS 比较。
- 本地 core `d3a2868` 将 `DynamicImage::to_rgba8` 改为消费图像的 `into_rgba8`，
  并以 `Cow::Owned` 将解码缓冲直接交给 CPU 纹理缓存，省去已有 RGBA 图像的两次复制。
  借用上传保留原来的缓存容量复用，render-only 不保留 CPU 像素，透明度检查不变。
  新测试验证同一 allocation 转交、新旧纹理身份和 alpha 读取、非法数据不覆盖原图。
  `owned-upload-tests.log`：GXM 23 tests PASS；Vita core 和 Host 构建完成。
  此变更已导出到根仓库补丁，尚未安装到实机；标准 latest 仍为上一轮 NEON 版本。
- 用户按指定时机触发人物/背景平移：前置日志 `20260910-011719-current`，
  结果 `build/hardware-logs/20260910-011810-current/host.log`。安装版本仍为 NEON，
  不包含本地 owned-upload 变更。切换前 112 quads / 21 draws、约 60 FPS。
  约 at_us=300201875 的窗口载入 `:ev/ev_lth_01/zev_lth_01a`（1920×1080），
  decode 392334us、upload total 209684us、最大帧 665987us。
  下一约 5 秒窗口 204 帧（约 40.8 FPS），over20ms=79，requested group 每帧一个，
  缓存 hits=127 / builds=1，groups_avg=0.377、switch_avg_us=3068，present_avg=20639us。
  这是包含运动及切换的混合窗口，不能直接认定纯运动阶段恰好 40.8 FPS。
  后续 300/298/300 帧窗口中缓存逐帧命中、离屏切换为零，停句恢复约 60 FPS。
  可确认进入时同步资源加载与运动时离屏重绘两类开销并存；具体阻止 bypass/fuse 的
  组属性仍需更细诊断，不能直接断言原生引擎或缓存容量是唯一原因。
- 用户授权修改后，部署 owned-upload + moving-group 诊断（root `7eef17f`，core `29938ba`），
  清单 `build/direct-deploy/deploy-20260910-012203/manifest.json`；
  `build/hardware-logs/20260910-012343-current/host.log`：全部启动像素自测 PASS、333MHz。
  诊断在单组连续八帧变化时打印组参数和前八个子命令，不逐帧打印。
  GXM 定向测试 23 PASS，Vita Release 构建通过，原始 shader 哈希保持不变。
  当前已实际修改加载复制开销，平移组路径尚未修改；需再次触发该场景，不能宣称平移低帧已修复。
- 用户复测日志 `build/hardware-logs/20260910-012711-current/host.log`：相同
  `:ev/ev_lth_01/zev_lth_01a` decode 272943us / upload 107032us（此前 392334 / 209684us）。
  host_us=107004us，说明缓存端原先约 99ms 的复制开销已消失；切换仍有 440341us 长帧。
  moving-group 确认 stage 960×540、neutral forced-opaque group，range=1..3：
  2×2 TextureId(1) 普通子图 + 1920×1080 平移大图。两张都未通过 opaque_cover。
- 新 root `54a4396` / core `c37e47d`：融合路径允许忽略 host 像素边界证明全透明的
  普通 Alpha 子图，余下一张图继续使用已有 RGBA8 量化兼容的 builtin_single shader。
  不按纹理 ID 或尺寸猜测；未知来源、可见像素、特殊混合、独立 shader、mesh/stencil/emote
  不跳过；全空强制不透明组仍保留原路径。host AlphaBounds 随更新保守扩张，避免旧空图证明失效。
  GXM 24 tests PASS；CPU AlphaBounds 80000 次更新及 629326 双线性样本 oracle PASS；
  Vita Release 构建完成、原始 shader 哈希未变。平移路径实机收益和画面仍待验证。
- 透明占位图融合版本已部署：`build/direct-deploy/deploy-20260910-013006/manifest.json`，
  `build/hardware-logs/20260910-013335-current/host.log` 启动自测 PASS、333MHz。
  标准 latest 包与本次安装文件同步；这些启动自测不代替新融合路径的游戏画面验证，
  仍需用户重放指定平移以确认 single 路径、目标切换次数、帧率与视觉一致性。
- 用户复测 `build/hardware-logs/20260910-013558-current/host.log` **未命中新融合路径**：
  single_avg=0，指定平移仍出现 moving-group 两个子图、composite/offscreen；不可宣称修复。
  同图 decode 289603us / upload 104134us，加载复制优化仍有效；结束后约 60 FPS。
  进一步检查 `core/src/runtime/project.rs` 初始化：`:bg/black` 首先上传 2×2 RGBA(0,0,0,255)，
  随后上传 white。因此此前“2×2 可能透明占位”的推断不足，不能把黑色底图删除。
  后续应保留黑色底图覆盖区的合成语义，分析局部重叠与 forced-opaque 的处理，
  而非放宽像素证明或直接忽略该命令。透明空图优化可保留，但本场景收益为未证实/未触发。
- root `1c6e17b` / core `f97dffc` 改为保留局部不透明底图：只针对 neutral forced-opaque
  两子图组，第一张经 host 证明不透明且为小型轴对齐矩形，第二张覆盖完整 stage。
  先用已有 single shader 绘制大图，再在底图矩形内重放底图和大图的普通 source-over；
  correction 的几何和 UV 均裁到该矩形，避免补画整屏。组滤镜/透明度/遮罩/嵌套仍回退。
  GXM 25 tests PASS；新增实机 self-test 比较原离屏与新路径，涵盖 alpha 0/128/254/255、
  整数/小数边界和局部周边像素，容许 RGBA8 最大 1 级量化误差，失败禁用快路径。
  构建完成，部署清单 `build/direct-deploy/deploy-20260910-014318/manifest.json`。
- 首次局部补画 self-test 未通过：`20260910-014403-current` 中 alpha128 最大差3，
  回退到原安装包，清单 `deploy-20260910-014426`，回退日志 `20260910-014543-current`。
  新路径增加独立的硬件证明开关（默认关闭），失败不再影响其他已验证快路径。
  诊断部署 `deploy-20260910-014619` 的 `20260910-014746-current` 确认差异在矩形外侧
  x450/y288，reference196 / candidate199；其余 alpha0/254/255 对照一致。
  针对固定1:1组表面，修改 group color/mask 和 retained 输出为点采样，避免邻像素线性混合；
  原始图片与可独立缩放的 capture 纹理保持原采样设置，shader 字节码未变。
  root `6827482` 测试部署 `deploy-20260910-014915`，结果待读取。
- `build/hardware-logs/20260910-015128-current/host.log` 确认修正后八个 local-base
  对照全部 max_delta=0，其余 retained 自测通过，333MHz。当前进程通过启动测试后
  启用了局部补画；候选仍默认关闭，重启不带自测标志不会启用。后续正式默认启用前
  还需指定平移的实际绘制路径、帧率和用户画面验证，不能仅凭合成自测宣称目标完成。
- 新低帧场景 `build/hardware-logs/20260910-015622-current/host.log` 持续约38.5 FPS，
  多个窗口均193帧/约5秒、94quads/24draws、single_avg=1、groups=0、hits/builds=0。
  submit约1.25ms、logic约2.65ms、present约23.25ms。这次已走融合路径，但停句仍重复执行它。
  根因之一是 retainable_group 显式排除 fused/local 路径，即使组内容完全静止也不能缓存。
- root `2373896` / core `639ea39` 取消此排除，保留 neutral passthrough 直接绘制。
  变化帧仍使用现有直接路径，连续稳定后烘焙一次，后续使用保留层；内容变化重新失效。
  GXM26项测试通过（包括运动/静止/再运动生命周期），Vita Release构建完成。
  当前具体场景实机帧率仍待复测，不能以消除组切换或测试通过代替性能结果。
- 部署 `build/direct-deploy/deploy-20260910-015819/manifest.json` 完成；
  `build/hardware-logs/20260910-020003-current/host.log` 全部26条启动自测通过、333MHz。
  标准latest包与此部署同步。已请用户回到刚才低帧场景，性能验收仍待实际同场景日志。
- 用户确认停句问题修复。`build/hardware-logs/20260910-020619-current/host.log`
  停句多个300帧/5秒窗口；指定平移窗口202帧/5秒，single_avg=0.495、groups_avg=0.015，
  present约21.7ms，进入时decode281549us/upload103812us/最大帧440231us。
  用户进一步明确“还是帧率低”，非报告画面错误。运动时单图合成开销仍需优化。
- root `3043be3` 新增单独 `builtin_single_neutral_f.cg`，仅在 group强制不透明、
  无滤镜/遮罩、父子tint全1、无实际屏内裁剪时使用；保留采样后RGBA8中间量化和零alpha处理。
  旧sprite及五组builtin字节数组逐项确认未变。省去无效uniform上传并新增neutral_single计数。
  新路径默认关闭，启动自测临时启用并在对照通过后留开；失败回原single。
  离线shader及Vita Host构建通过，实机像素和运动帧率仍待验证。
- 新程序部署 `build/direct-deploy/deploy-20260910-021020/manifest.json`，
  `build/hardware-logs/20260910-021229-current/host.log`：八组local-base像素对照
  使用neutral程序全部max_delta=0，其余自测通过，333MHz。当前进程开启候选，
  后续正式启用仍需真实平移帧率和画面验证；GXP体积变小本身不构成性能改善证据。
- 当前又低帧日志 `build/hardware-logs/20260910-021654-current/host.log`：静态计数
  349quads/24draws，多窗口约45～48FPS，neutral_single_avg=1，cache hits/builds=0，
  groups=0。发现路径选择不一致：retainable 排除 passthrough，但 render_range 先做
  local/fused 再做 passthrough，满足两者时仍每帧运行合成shader。
- root `a98da6f` / core `bd79621` 把已证明正确的 passthrough 提到融合之前。
  GXM27项测试通过，新增测试确认同时满足两种路径时连续三帧均发普通kind0绘制，
  不发kind4合成；Vita Release构建完成。当前场景实际恢复情况需安装后同场景验证。
- 安装清单 `build/direct-deploy/deploy-20260910-021914/manifest.json`；
  `build/hardware-logs/20260910-022059-current/host.log` 26条启动自测全部通过、333MHz。
  标准latest包已同步，待用户返回同一低帧页面验收实际路线和帧率。
- 用户复测日志 `build/hardware-logs/20260910-022726-current/host.log`：当前停句127quads/
  27draws，300/300/298/300帧窗口，单图合成和离屏切换均为零，缓存逐帧命中。
  当前页面约59.5～59.9 FPS；其绘制计数与旧349quads页面不同，不能当作该页面严格A/B。
  同日志指定ev_lth平移附近窗口258/283帧（约51.6/56.5 FPS），neutral_single平均0.178/0.385，
  最大帧449929us，decode285080us/upload109298us。相对前述约40FPS混合窗口有改善，
  但窗口混有载入/运动/停句，不足以证明纯运动全程60FPS；加载长帧仍未解决。
- 新core `6da43ff` 在同一16MiB inactive估算预算内保留CPU解码层：预算压力下先回收
  idle GPU副本，再按访问次序回收CPU像素；命中后转交像素重新上传，不重新读取/解码。
  显式上传、capture覆盖、prefix清理同步作废解码副本，动态文本/视频生命周期不进入此缓存。
  GXM29项测试通过，覆盖像素所有权、GPU身份换代、预算清零、前缀清理和覆盖后不回旧图。
  Vita Release构建完成；此策略会增加部分GPU重新上传，实际收益需日志验证，不能解决首次解码停顿。
- 2026-09-10重新健康检查13341，实际IDB为PCSG01297；只读反编译复核
  `0x8101F6CC`、`0x812375A4`、`0x81000D18`，原始结果在
  `build/native-five-audit/20260910-resource-recheck.json`。确认后台队列处理/锁外loader、
  预算超限逐项回收和变换变化标记。未确认原生采用本次CPU/GPU两层策略或严格LRU。
  原生后台加载与当前同步resolve的差距仍在，后续应解决首次使用时的主线程阻塞。


## 2026-09-10：脚本 surface 后台读取与解码（core 637c9ad）

原来的 `bindSurfaceAsync` 直接调用同步 `bindSurface`，`isLoadingSurface` 恒 false。SHUF00002 的缓存脚本实际调用该 API，因此接入单个 CPU 工作线程，不修改游戏脚本或猜测后续资源。借鉴原生 PCSG01297 0x8101F6CC 的取队列、锁外加载、锁内核对请求再发布；不声称复刻原生全部调度。

- 仅 Vita GXM 接入，桌面维持旧逻辑。读取和 RGBA 解码在工作线程；GPU 上传仍在 render resolve，shader、采样、GXM 同步均未改。线程优先级180，主线程此前日志160，返回值写日志；不改时钟或固定 CPU。
- 按路径引用计数，排队最多64项，完成结果总预算16 MiB（额外于既有16 MiB闲置纹理预算；不含单次读取、解码临时内存）。超额任务/结果允许后续同步源回退；解码尺寸和分配限额先检查。RGBA消费时转移所有权，进入现有两层缓存。
- 提供全局与按路径加载状态。需求到达时优先其排队任务并等完成，避免用临时 None 发布缺图；没有提前绑定、任务取消、失败或结果淘汰时同步兜底。因此不保证首次载入彻底无停顿，也没有消除 GPU 上传开销。
- clearQueue取消尚未发布任务；解绑后重新绑定有不同ticket，旧结果不得覆盖。运行时销毁取消并join工作线程，防止切换游戏后继续访问旧文件源。
- 日志 `[surface-prefetch] ready`/`demand-wait` 和 `GXM prefetch-hit` 区分提前就绪、需求等待及纹理命中。
- 完整核心测试379通过、13忽略：新增异步引用计数、取消/重绑、缺失/超额、队列上限、总预算、预取像素进入两层缓存与编码回退测试；Vita release核心及VPK编译完成。证据 `build/direct-builtin-shader/async-loader-full-tests.log`、`async-loader-core-build.log`、`async-loader-host-build.log`。
- 两层缓存的上一包6495e0a已部署并启动自检通过，日志 `build/hardware-logs/20260910-023947-current/host.log`；首次FTP替换被文件占用拒绝，第二次关闭应用后安装成功，清单 `build/direct-deploy/deploy-20260910-023859/manifest.json`。本次后台加载包的实机效果另行记录，不能混用。

实机部署完成：`build/direct-deploy/deploy-20260910-024634/manifest.json`，eboot SHA256 `b82743ad0d5f48ccaefed7970502a8b1eaf41e2051287450c005038737c8e762`。启动日志 `build/hardware-logs/20260910-024740-current/host.log` 的26项渲染检查全部通过，ARM333/bus222/GPU111/XBAR111。`art3m1s-direct-latest.vpk/json` 已同步。此时停在启动菜单，尚未证明后台工作线程在游戏中的预取命中或帧率收益；已请用户进入原人物平移/头像段落。


## 2026-09-10：预取压缩源保留、需求结果保护（core b9672c7）

实机前一版637c9ad确实启动worker（priority=180 result=0），并有RGBA预取命中。然而 `20260910-024914-current/host.log` 显示 zbg04a 先预取8294400字节成功，随后渲染仍读盘335646us、解码263880us、上传104033us。后续大批预取挤掉较早结果，是可从代码与日志确认的重复加载路径。`20260910-025208-current/host.log` 继续记录多次长帧，不可宣称预取已修完平移/头像性能。

修正：解码结果附带原始压缩数据；总预算仍16MiB，超额先回收RGBA保留压缩源，再按预算淘汰编码数据。消费RGBA时转移像素所有权并释放附带编码源；消费编码源走原解码路径，并记录 `GXM prefetch-encoded-hit`。前台等待结果不被后续预取淘汰；超大且不能保留的结果不再连带清空其他有用缓存。已知长度的读取省去一次重复size查询，严格检查短读。没有调整shader、GPU同步、字体、OGV。

完整核心测试382通过、13忽略；新增共享预算下压缩源保留、超大结果不冲掉旧缓存、前台结果抗预取压力的测试。构建证据 `async-compressed-full-tests.log`、`async-compressed-core-build.log`、`async-compressed-host-build.log`。不能保证所有预取数据常驻，仍可能解码/读盘；持续绘制负载需另测。

实机测试建议：333MHz；从开篇或相同存档走10句，语音句和无语音句播完各停10秒；仅背景、有头像、双人各停10秒；走原人物平移段，并从同一存档在同次运行中重放，区分首次与重复载入；观察灰阶/转场正常、有无缺块。保持存档2不被覆盖。测试后记录场景和大致时间并抓日志，用预取命中、demand-wait、slow-read/decode/upload和稳定5秒帧窗口评估。

实机安装及readback校验完成：`build/direct-deploy/deploy-20260910-025317/manifest.json`，eboot SHA256 `b9409d57808467ad82abf874f85ed3c05c1d5600a348cbb1a4de25a6e8dbf594`；启动日志 `build/hardware-logs/20260910-025453-current/host.log` 自检PASS、ARM333/bus222/GPU111/XBAR111。latest包及元数据已同步。实际场景对比尚待用户操作。


## 2026-09-10 03:08：用户标记约50FPS平移，只读采样

实机日志复制：`build/hardware-logs/20260910-030840-current/host.log` 和 `20260910-030859-current/host.log`，未重启、未操作按键、未换包。约667.58秒窗口250帧/5秒：231quads、38draws，logic5569us、direct_present14352us，其中submit8234us、begin wait6016us。随后672.59至687.61秒窗口236/235/231/228帧（约47.1/47.0/46.0/45.6FPS），294quads、44draws，submit8922–9416us、begin wait5699–6226us、logic5858–6555us。

对应纹理窗口decoded=0/uploads=0，少量失败资源查询合计每5秒18–24ms，不是图片首次读取/解码造成的持续低帧；缺失路径之前记录为pc/ui/ja/mw/dummy。builtin groups=0、retained hits/builds=0、neutral_single=1/帧。没有反复离屏合成，但每帧仍绘制且无组结果复用。submit是host测得的准备/提交墙钟区间，不等于纯GPU耗时；不能仅凭计数把新增quad全部归因文字或断言GPU饱和。后续优先定位移动过程中可复用的静态子层、每帧场景构建、绘制批次开销；保留已验证shader和同步安全边界。


## 2026-09-10：平移时文字框/按钮透明图层缓存候选

用户对照：按×隐藏文字框后恢复60FPS，因此优先处理静态文字/按钮的反复提交；普通平移矩阵快速路径未改入本次包。

Core选择所有shader组之后、连续普通Alpha且没有shader/mesh/stencil/emote/灰阶负片的命令尾段（至少32条）。精确比较命令、纹理内容revision、画布尺寸和保守mask依赖；连续8个后续帧相同才烘焙，避免逐字动画频繁重建。复用槽3，显式清除与普通组槽3交替使用时的旧记录。隐藏/换句/纹理改变/几何变化均失效；背景组变化不在此依赖内。GPU只在烘焙时进入透明target，完成Fence后把整个Offscreen对象转移到保留槽；使用既有预乘copy shader合成，不新增shader、不移除同步。缓存图像按命令覆盖矩形加1像素guard绘制。

当前限制：只识别连续普通尾段；尾段存在持续动画会阻止命中，效果组内文字仍走原路径。背景不能因此跳过更新。初次烘焙有一次目标切换成本，并非全部场景或每个字都更快。

384核心测试通过、13忽略；包含重复命中、文字改变、纹理改变、隐藏失效及组边界保护。证据 `overlay-full-tests.log`、`overlay-core-build.log`、`overlay-host-build.log`。实机新增透明面板、模拟字形/阴影重叠，在三种背景比较。初次全屏检查maxdelta255，后续坐标定位为(117/131,48)，位于下方测试图层之外的性能浮窗区域，见 `build/hardware-logs/20260910-032205-current/host.log`。自检改为全宽y360..539，包含测试面板、字形和四周未覆盖边缘，避开无关浮窗刷新；仍要求每通道差异≤1。自检不过overlayAllowed保持false，既有渲染自检不被混同为新缓存通过。候选仅在当前进程自检通过后启用，可用overlay-cache.off同场景禁用作对照。

最终包部署 `build/direct-deploy/deploy-20260910-032324/manifest.json`，core f5b187e/root20b4a85，eboot SHA256 `b8006ef6a21ff2ae661151097a6fa583c863bb2a6082cba8975ec7a75b7d873b`。实机自检 `build/hardware-logs/20260910-032442-current/host.log` 三个背景overlay maxdelta=1/1/0、均ok=1，旧渲染自检PASS，时钟333/222/111/111。当前进程overlayAllowed启用，已请用户回文字框平移场景进行命中和帧率验证；未声称已恢复60FPS。latest包及元数据同步。


## 2026-09-10：静止场景周期长停顿，日志 I/O 定位

实机证据 `build/hardware-logs/20260910-034136-current/host.log`：进程186.27/317.87/386.62/435.77秒分别出现8.18/9.70/9.66/10.06秒长帧。多次主逻辑与音频decode墙钟时间同时增加，386秒一次主要落在Direct present，不能归因固定图片或单一解码CPU开销。间隔不固定；最新现场另备份到034431-current。

IDA 13341健康检查后，重新读取PCSG01297的SurfaceManager worker 0x8101F6CC与Vorbis读取0x8104E7B4，证据 `build/native-audio-audit/20260910-periodic-recheck.json`。资源worker在loader虚调用之前释放队列锁，再加锁验证并发布；音频例程循环填充调用者的目标缓冲。尚未证明原生不存在其他共享I/O锁，亦未证明本次阻塞源头。

Direct诊断版只给既有同步日志路径增加锁等待、写入（包含格式化和stdio自动刷新）、显式刷新独立墙钟计时及峰值线程/时间。汇总使用trylock，计数在日志锁内更新，输出在解锁之后，无递归日志；本身不改变同步写盘行为。`[log-io]`在下一个5秒窗口报告此前已完成操作。主线程、音频线程仍可能因原有日志锁而阻塞；该候选用于确认，不能声称修复。

构建日志 `build/direct-builtin-shader/log-io-host-build.log`，VPK目标完成。核心库保持已部署f5b187e，未带入surface_loader未提交改动。原sprite shader SHA256仍f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6。后续同一画面静置至少跨越此前停顿间隔，比较log-io峰值与frame/audio峰值；若计时不足以解释卡顿，再测资源锁持有和实际读写，禁止先行将问题定性为日志。


日志计时版root2aedd9c已部署：`build/direct-deploy/deploy-20260910-034436/manifest.json`，eboot SHA256 2f4cff7e1b37510550548ca5339bd14b75ac3e4108a82d48c7ee4ec297073475。034553-current实机自检26项PASS，透明overlay三个背景delta1/1/0，333MHz；log-io首次输出生效。启动阶段最大日志写入99us，尚未覆盖游戏中的周期停顿，不是排除日志问题的证据。latest安装包已同步，core仍f5b187e。

## 2026-09-10：bind_surface_async生命周期补全（尚未部署）

Core 1bffe55：队列满的绑定以deferred状态保留并自动补入有界64项工作队列；当前需求可优先，不丢掉被挤出的预取。取消/失败/已消费后的再次绑定重试使用新ticket，旧任务不能发布到新绑定。全局Loader改为Arc句柄，等待/取消/退出均不再持有全局注册锁；取消检查加入读盘前后及解码reader的read/seek，取消使用终止错误而不是可被自动重试的Interrupted。退出清空记录并唤醒消费者，队列满时跳过无用扫描。16MiB预算、RGBA降级保留压缩源和GXM主线程上传约束不变。

不能中止已经阻塞在系统内部的单次文件读取；不能因此认定解决静止场景8至10秒长停顿。新增队列溢出后排空、需求优先、失败/消费重试、取消后原路径重新绑定、等待消费者唤醒和解码取消测试。优先队列测试先消费已提前的q99再发其他需求，避免测试自身q0请求合法改变优先级造成时序误报。完整388通过/13忽略，`async-state-final-tests.log`；Vita核心编译完成`async-state-final-core-build.log`，patch反向校验通过。此轮只更新源代码和核心库，未重新链接安装包，实机仍为f5b187e核心日志计时版，保留单变量诊断。


## 2026-09-10：确认同步日志造成秒级卡顿，后台有界日志候选

`build/hardware-logs/20260910-034900-current/host.log` 给出直接计时：显式flush最长1877715us；另一次flush1233180us、日志锁等待1012949us。日志I/O确实能阻塞主/音频线程。进程134.48秒另有7036156us逻辑长帧，不能被上述flush时间完整解释，仍需后续检查文件流或其他阻塞。常态flush约5–15ms也会扰动帧节奏。此前frame-perf未计入heartbeat末尾flush，所以必须结合窗口帧数和log-io，不能仅看max_us。

`log_queue.hpp`：32×16KiB固定记录队列，单后台worker（请求priority180）执行stdio写入/刷新；生产者只格式化和入队，不等存储或空位。队列满丢诊断日志并统计，过长单行截断并统计，最大有效单行16383字节含换行；无游戏数据经过此队列。5秒heartbeat请求异步刷新，worker写盘期间不持队列锁；正常退出排空并join后关闭文件。初始化失败明确终止诊断版，不悄悄回退同步日志。`[log-async]`记录队列、丢弃、截断和后台写入/flush生命周期峰值。

阻塞sink测试 `tests/log_queue/test.cpp` 以条件变量保持存储不返回，同时生产者完成满队列+5次溢出，断言丢弃计数、有界容量、长行截断、FIFO和退出排空/重复stop；G++ ASan+UBSan通过。Vita构建 `async-log-host-build.log`。为单变量比较，临时读取已提交f5b187e的surface_loader重建核心后恢复1bffe55源文件，编译日志 `async-log-pinned-core-build.log`；核心源工作区干净。补全async的库另留`libart3m1s_core-async-state.a`，此包没有带入该变化。此候选尚未证明消除全部长停顿。

原生Vorbis读/seek回调复核见 `build/native-audio-audit/20260910-stream-buffer-recheck.json`：0x8104E96C在对象+28有缓冲时按+36位置/+40大小内存复制，否则调用源对象虚表+24；0x8104E9F8缓冲时更新游标，否则转发源seek。注意IDA把0x8104E66C并入0x8104D018输出，不可将整段反编译误称独立预载函数或直接推断全部音频常驻。


## 2026-09-10：测试开关查询耗时探针，待联网实测

复核134.48秒7.04秒长帧：纹理reads63/missing63合计read_us5840、decoded/uploads均0，GXM等待平均14us；同期音频单次decode墙钟6.65秒，不能用日志flush1.88秒完整解释。发现main逻辑内及gpu begin内都有每秒sceIoGetstat读取诊断开关。因此加入`diagnostic_stat`，保持查询和返回值不变，仅在单次≥20ms时经异步日志报告`[gate-io-slow]`路径/耗时/完成时间/返回值。启动trace-nextline.flag查询也计时。未提前改变测试开关语义、shader或安全等待；是否为剩余7秒卡顿原因仍未证实。

VPK构建完成`async-log-gate-host-build.log`，使用上一轮保留的f5b187e核心。`build/hardware-logs/connection-20260910-035521.json`确认设备命令1338和FTP1337均超时。未执行kill、重启或部署，latest仍为已安装的同步日志计时版；独立async-log-candidate包为待测试版本。


2026-09-10 03:59 实机恢复连接，已备份035704-current日志并部署9275d8f候选。manifest `build/direct-deploy/deploy-20260910-035713/manifest.json`完整上传/安装读回校验，eboot SHA256 31d8fce813d2ae91418d64fffd778bc90dc3de89e23b417eb2680141b4225d14。035902-current日志：26项渲染自检PASS，overlay三背景1/1/0，333/222/111/111MHz，log-async dropped=0/truncated=0。菜单阶段gate-io-slow已记录overlay-cache.off 32111us、full-cover.off 80384/84070us；说明诊断开关轮询确实引入短卡顿，但还未解释原7秒长帧。已请用户进入原静止场景继续测；latest包和元数据同步，不能把启动菜单300帧/5秒等同完整游戏性能达标。


## 2026-09-10：放射动画三帧反复上传，跨层LRU修复

实机044131-current：ani/line21/22/23都是960×540，反复decoded-cache-hit并上传约12–15ms；5秒50次上传103680000字节（约98.9MiB），窗口93帧约18.5FPS；groups/composite每帧1，retained hits0/builds43，group switch平均约22.8ms、present48.9ms。是图片序列缓存抖动，不应把历史OGV日志当成本场景解码成本；upload与present区间有重叠，不能直接相加为GPU时间。

Core6f9eb78：原回收先降级所有闲置GPU纹理、最后清CPU解码缓存，导致老场景CPU数据挤掉更热的动画GPU副本。现在把两层候选按last_used统一排序；最旧GPU项先降级，仍超预算则释放它剩余CPU数据，再处理更新项。旧CPU项先于热GPU项回收。16MiB共享闲置预算、活动场景保留规则、动态目标生命周期不变；无硬编码文件名、无新增常驻预算。

新增旧解码场景占预算下三帧90次循环测试，验证仅3次上传、纹理ID稳定、闲置预算不超标；旧淘汰测试按跨层LRU更新期望，活动/动态/零预算释放仍验证。完整389通过、13忽略，hot-animation-tests.log。此包包含此前1bffe55的异步队列/取消补全；启动标识同步更新。shader字节不变，未移除GXM资源安全等待；离屏合成仍可能是独立瓶颈，不能用上传减少直接宣称60FPS。


实机部署完成 `build/direct-deploy/deploy-20260910-045003/manifest.json`，root69f84e6/core6f9eb78，eboot SHA256 77eb172e2e1a7895f0a568376efdae02830c90c0f506b791f61f0bcecd5746c3。045145-current日志确认26项PASS、overlay背景1/1/0、333MHz，latest包和元数据同步。尚待同一放射场景15秒采样，验证上传消失和帧率变化；不将自检通过等同性能目标完成。


## 2026-09-10：跨层LRU实机验证与循环合成结果候选

045343-current实机同放射场景，line21/22/23预取命中；后续约25秒decoded/uploads均0（此前5秒50次上传）。帧窗口130/118/125/126，即约24–26FPS；present32–36ms、group switch平均18–20ms，仍每5秒build约50次。重复上传问题已消除，但组缓存/显示开销仍明显，不能认为已接近原生60FPS。

原核心按当前组序号只保存上一种合成状态，循环图换回旧纹理仍要重建。候选把已有4个物理槽位作为有界LRU池：先查找严格相同的旧状态，否则选空位/最久未使用位；当前帧已提交的槽位禁止覆盖。保留命令、重叠效果组、全部mask、画布尺寸、纹理内容revision的精确验证，不按纹理ID单独判定。首次只观察，后续相同状态才烘焙。overlay占用槽3时显式失效pool元数据，并限制pool为0至2；overlay释放时也失效槽3旧记录。

不新增GPU目标或增加预算，保留全部GXM等待。新增三种循环状态预热后30次纯命中、纹理内容变更失效、同帧槽位保护和overlay保留槽限制测试；完整390通过/13忽略，group-versions-tests.log。此候选尚需实机画面/帧率验证；超过槽位容量的效果仍可能重建，连续变化效果也不能保证命中。


合成多状态候选已部署：manifest `build/direct-deploy/deploy-20260910-045710/manifest.json`，root44eba71/core67b044b，eboot d7f3c9a6dcb62b2a19096a98548fd4a95a1ed6524078d12cfb6ada34a917b0d3。045804-current自检26项PASS、overlay1/1/0、333MHz；latest同步。启动自检只验证host像素路径，不证明多状态选择在实际场景已正确命中，已请用户回原放射场景确认画面并采样。


## 2026-09-10 04:59：放射场景多状态缓存实机结果

用户回复场景准备好后只读取日志045936-current，未再操作按键/换包。进程89.27/94.28/99.28秒三个连续约5秒窗口均300帧（约59.94FPS），纹理decoded/uploads=0；retained600hits/300frames、builds0、groups/composite0、switch0。每帧5quads/3draws，logic6.06–6.14ms、present10.46–10.54ms、beginwait5us。相比前候选同放射场景24–26FPS/present32–36ms，重复离屏重建已消除；相比最初18.5FPS阶段，也不再循环上传三张图。

各窗口仍5–6个>20ms帧，最大25.1–26.7ms；不能称完全无抖动。尚有其他场景、转场首次上传、测试开关轮询和延后TODO需要继续验证/优化，不代表总体达到原生eboot目标。latest元数据只更新该场景的实测结果，安装包不变。
