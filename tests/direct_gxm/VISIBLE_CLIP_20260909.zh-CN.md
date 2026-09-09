# optL-visible-clip 单项候选（2026-09-09）

状态（18:07 更新）：已完成固定双人页的模拟器视觉 A/B，并安装到实机，启动日志正常。实机固定场景性能 A/B 尚待进行，不能宣称提高实机帧率。

## 依据与改动

实机背景页约 60 FPS，而人物/头像页约 41–43 FPS；后者帧首等待约 11.6 ms，背景页约 1.2 ms。相应稳态窗口没有图片解码、上传或活动语音。它提示优先调查额外图层的 GPU 工作，不能仅凭等待时间断言唯一瓶颈。

原实现用四个原始顶点判断是否需要片元裁剪。图片超出屏幕时，即使裁剪矩形就是整个 960×544 屏幕，仍会选裁剪程序。本候选只在裁剪框包住图形与屏幕相交后的整个包围盒时省去片元裁剪；坐标、UV、透明度及 rule 参数保持原样。非有限输入、空裁剪、部分裁剪保持保守路径。没有改变 shader 字节码、GPU 等待、显示缓冲或 core。

编译开关 `DIRECT_VISIBLE_CLIP_CANDIDATE` 默认关闭。候选运行时每秒检查 `ux0:data/art3m1s-gxm/visible-clip.off`：文件存在则退回原判断，不存在则启用优化。切换所在的五秒统计窗口必须丢弃。诊断每五秒最多输出三条图层详情。

## 验证及限制

- 独立 C++ 测试通过：4000 个随机仿射四边形中 1428 个满足优化条件，对 3,141,168 个被覆盖像素中心核对裁剪等价性；另有边界、部分裁剪、空值及非有限输入测试。
- Vita3K 会话 `b85d6acf-d869-4c1b-8f1f-3cd57124127d` 正常启动并读取存档 2。
- 人物特写页两张越界图各省去一次整屏裁剪：1920×1080 图的屏幕坐标为 (-480,-272) 到 (1440,816)；1020×1008 图为约 (-11,-176) 到 (1009,839)。稳定窗口 requested=2、removed=2、remaining=0。
- 后续多人页 requested=5，关闭时 remaining=5，恢复时 removed=5。开关功能已验证；但前后截图发生了剧情推进，不能作为严格同画面像素一致性证据。截图及日志位于 `build/visible-clip-validation/`。测试创建的关闭标记已移除。
- 尚需固定同一页面的视觉 A/B，以及实机 333 MHz 的 ON/OFF/ON 帧耗时对照。Vita3K 的 60 FPS 不作为实机性能结论。没有覆盖所有转场与灰度效果。

## 原生对照

只读复查 PCSG01297 的 `0x8102F718`：持久四顶点存储仍会写入坐标/UV，按图层状态选择预建程序。不能据此声称原生已经采取本候选的裁剪判断，也没有证明固定“三级缓存”。完整已知机制及证据边界见 `NATIVE_CACHE_OFFSCREEN.zh-CN.md`。

用户指定的旧目录 `E:/EmuGame/vita3k/_data/ux0/app/SHUF00002` 当前不存在；MCP 当前安装配置中的对应目录为 `E:/EmuGame/vita3k_data/ux0/app/SHUF00002`，eboot 大小 1,892,950 字节。直接通过已安装 Title ID 启动成功，会话 `5d25d810-0b58-40eb-acf7-b4e761252a7d`。原生读档第一页为空，用户选择自行推进；停止远程输入并保留会话，不修改存档或资源。

## 构建身份

产物目录：`build/01.02-optL-visible-clip/`，构建脚本 `build/heap-audit/build-visible-clip-host.sh`。

- core：独立 history-source 的 `122d675`，归档 SHA256 `31184f196eaa6b284a6908ac012c8ad27db35daa854b339c930da63cd82a4059`。
- shaders.hpp SHA256：`f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6`，与前版相同。
- VPK SHA256：`80a07ec2fea7bfe53b56fd435826d1e52b8a3ffc0995591bb73f18e210f71916`。
- eboot SHA256：`896ff8e80f128b9459c2e6ee649542faa099ca7b38b340c2f4f895ae66193b18`。

不要用主工作树中后续 core 改动替换这个归档，否则不再是本次单项对比。

## 固定双人页补测（18:05）

原生参考包含双人大立绘、左下头像、放射线、文字，台词为神秘女仆的「喝——！！」。截图与 MCP 状态保存在 `build/native-reference-20260909/two-characters-rays*`。原生读数 61 FPS 仅记录为模拟器状态。

重新启动同一 Direct 候选，读取存档 2 后推进一句到对应场景；随后不输入按键，执行 ON/OFF/ON。会话 `f765e0c7-d8ee-4043-9743-433e36ca6ff5`。证据在 `build/visible-clip-validation/fixed-two-person/`，含三张截图、三个日志快照和像素差异范围 JSON。

- ON1 与 OFF 的像素差异仅位于 `(892,482)-(920,511)`。
- ON1 与 ON2 的差异仅位于 `(891,482)-(920,511)`。
- 上述区域为右下角动画等待图标；其余像素完全一致，包括人物、头像、文字与放射线。
- 最终日志包含稳定 OFF 窗口：requested=5、removed=0、remaining=5；稳定 ON 窗口：requested=5、removed=5、remaining=0。跨开关窗口丢弃。
- 开关标记已移除。此结果补齐本页的视觉 A/B，不代表已覆盖所有效果或已获得实机性能收益。

另外，18:00 读取的实机旧版日志保存在 `build/hardware-logs/20260909-180037-current/`，仍是 optL-reuse-history、333 MHz。其末尾是 369 quads/51 draws 且文字构建约 27 ms 的不同场景，不能当作此前 73 quads/27 draws 人物页的同场景基线。

## 实机部署（18:07）

通过 VitaCompanion 先健康检查、备份旧 eboot/SFO/日志，再上传及读回校验，随后启动。

- 备份及部署清单：`build/direct-deploy/deploy-20260909-180550/`。
- 旧 eboot SHA256：`7b5e7df87c5192f87e309d405b213ef46df1514253e05b55fb9d6aea1a01ac7c`。
- 新 eboot 与上文产物哈希一致；SFO 没有变化。
- 启动日志：`build/hardware-logs/20260909-180717-current/host.log`，明确显示 optL-visible-clip、开关 enabled=1、333/222/111/111 MHz，菜单绘制已经提交。
- 已请求用户回到人物与头像页面，待其固定页面后进行实机 A/B。当前启动日志不能用来评价人物页性能。
