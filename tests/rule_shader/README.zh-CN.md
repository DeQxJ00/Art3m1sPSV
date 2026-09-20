# 独立规则转场编译探针

> 2026-09-20：旧 Borealis/NanoVG 渲染探针及以下像素验证描述属于历史记录，原 `build-rule-render-probe.sh` 入口已随旧宿主移除。独立 vitaShaRK 编译探针仍保留，Cg 源码已迁入本目录；使用下文 `scripts/build-rule-compiler.sh` 构建。离线编译脚本也已更新源码路径。

## 新增：独立 GPU 像素检查

`render.cpp` 使用生产 Borealis/NanoVG GXM rule shader，独立 title ID ART3GRP01。通过 `bash scripts/build-rule-render-probe.sh` 构建 `build/gxm-host/art3_rule_render.vpk`，不需要商业游戏或 vitaShaRK 模块，不替换 ART3GXM01 游戏运行器。

程序绘制十二个区域：前六个覆盖 progress=0/1、黑/白规则、线性灰度及水平反向 UV，后六个覆盖 SOURCE_OVER、LIGHTER、SOURCE_IN、SOURCE_OUT、XOR、COPY。旧画面为半透明暖色，新画面为不透明蓝色，检查预乘 alpha 与混合公式。不同参数同帧排队，也能检出全局参数泄漏。运行 120 帧后，场景结束阶段等待 GXM 完成并读取显示缓冲，比较 36 个采样点，RGB 容差 3、alpha 容差 1。

CPU 侧缓存检查：`python3 tests/rule_shader/test_blend_cache.py`（WSL）直接编译生产 blendProgram/sameBlend 函数配合模拟 patcher，ASan/UBSan 下验证缓存命中、shader/alpha 因子隔离、深度绕过和失败重试。该测试已通过，不能替代 GPU 输出验证。

输出 `ux0:/data/art3m1s-rule-render/result.log` 与 `output.ppm`，随后自动结束。PASS 必须来自设备实际运行日志；当前仅构建成功，**尚无 GPU 像素通过结果**。该检查不覆盖所有 vague 值、垂直翻转、游戏旧帧捕获和性能；T01 实机录像对照仍须完成。每次运行覆盖该独立目录下的同名输出，不读写游戏及存档。

以下部分仍是用户要求保留的 vitaShaRK 编译器对照探针。

用途：使用设备已有的 `ur0:/data/libshacccg.suprx` 和 vitaShaRK 将生产待接入的 Cg 源码编译为 GXP。该探针不创建 GXM 渲染场景，不读取商业游戏或存档，不代表转场显示验收通过。

构建（WSL，仓库根目录）：

```sh
bash scripts/build-rule-compiler.sh
```

可设置 VITASDK 覆盖脚本默认 SDK。生成 `build/rule-compiler/art3_rule_compile.vpk`，独立 title ID 为 ART3GRC01。运行器 ART3GXM01 不受影响。

启动探针后会编译并自动结束；没有游戏画面。输出目录是 `ux0:/data/art3m1s-rule-compiler/`：

- `compile.log`：立即刷新编译器诊断、耗时和 uniform/sampler resource index，结尾 PASS/FAIL。
- `rule_transition_f.gxp`：仅在编译成功、frag/oldFrame/ruleMap 参数存在，且采样器为 TEXUNIT0/1 后生成。

探针启动会删除该目录中上次生成的同名 GXP，并覆盖 compile.log，防止将陈旧输出误判为本次成功。失败时只保留日志。设备缺少 compiler module 会记录 shark_init 错误，不自动安装模块。

当前源码打包为 app0:/rule_transition_f.cg。使用 SHARK_OPT_SAFE，禁用 fastmath、fastprecision、fastint，以免先引入额外精度差异。返回 GXP 仍须进一步验证与 fill vertex shader 的链接、双纹理采样、图像输出及实际帧时间。

截至本次工作：C/Vita ELF/SELF/VPK 构建成功；尚未运行探针，尚无编译成功的 GXP。编译日志为 `build/rule-compiler-build.log`。构建只有已有环境时钟偏差提示，没有 C 编译错误。

链接的 vitaShaRK 使用 LGPL-3.0-or-later，许可证随 VPK 附带。探针源代码和构建定义保存在本目录，可使用自己的兼容 VitaSDK/vitaShaRK 重新链接。
