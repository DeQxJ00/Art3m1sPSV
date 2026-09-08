# GXM 图片矩形与相邻合批验证

普通直通 RGBA 图片通过 nvgImageQuad 提供变换后的顶点和 UV。新的片段 shader 保持旧 image fill 的透明度、颜色和 scissor 计算。GXM 仅合并紧邻且纹理、完整 uniform、混合因子一致的三角形；不改前后覆盖顺序。规则转场、AA、预乘图片或 NVG_IMAGE_FLIPY 图片及分配/链接失败保留旧路径。

每个矩形由原来的 4 顶点 fan 变为 6 顶点 triangle list；换取减少路径构造、paint 变换和相邻重复提交。不增加 GPU 等待，不缩减描边/阴影，也不改变字形图集内容或尺寸。实际实机收益需要同存档测试。

1. Windows 执行 scripts/build-image-quad-shader.ps1，使用用户提供的本机编译器；检查 build/image-quad-shader/manifest.json 和参数表。生成的自有 GXP 头随源码提供，普通构建不需要 SDK 编译器；vitaShaRK 源码路径仍保留。
2. WSL 执行 python3 tests/image_quad/test_queue.py。直接编译生产 NanoVG 几何和 GXM 入队函数，ASan/UBSan 覆盖变换、翻转、UV、透明度/颜色/裁剪/混合隔离、提交顺序、U16 大批次拆分和分配失败回退。
3. 配置宿主 ART3M1S_BUILD_IMAGE_QUAD_PROBE=ON，构建 art3_image_quad_probe.vpk（ART3GQP01）。独立 GPU 探针对照 12 组相同图像：左半旧路径、右半新路径，覆盖半透明、染色、正反 UV、镜像、旋转/斜切、裁剪、叠加、重叠描边和插入普通 fill 的批次屏障。
4. 等待入场动画完成，截取 960×544 PNG，用 python3 tests/image_quad/check_capture.py <capture.png> --output <result.json> 对照全部 RGB 像素，容差每通道 3。该截图检查不证明 framebuffer alpha，也不代表实体机性能。
5. 安装带新路径的主程序，确认 host.log 的 [gxm-image-quad]。读取 T01 存档 2，前进到 build/save2-baseline/after13.png 同一台词与画面，检查文字/UI、立绘透明边缘、效果，并对比 [gxm-submit-perf]。原档必须保持不变；实体机录像及日志决定流畅度是否改善。

实际文字的严格对照使用 ART3M1S_IMAGE_QUAD_AB=ON 单独构建主程序。此诊断版本每 600 个游戏帧交替旧/新路径，左上 6×6 红/绿块标识捕获时的路径。将同一运行停在静止台词上，执行 tests/image_quad/capture_ab.ps1，保持相同图集内容、位置、字体状态和材质，避免用两次读档时不同的字形排布判断采样回归。比较时单独排除诊断块和活动的等待图标。该选项默认关闭，scripts/build-gxm-host.sh 明确设为 OFF；诊断包不作为实机日常测试包交付。
