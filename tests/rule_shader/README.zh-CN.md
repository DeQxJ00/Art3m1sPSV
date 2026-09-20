# 独立规则转场编译探针

旧 UI 框架的渲染与混合缓存探针已删除。本目录保留独立 vitaShaRK 编译器对照实验及其许可证，不依赖 UI 框架。

离线编译：`scripts/build-rule-shader.ps1` 将 GXP、参数表、清单和生成头全部写到 `build/rule-shader-offline/`，不写入第三方库源码目录。

用途：使用设备已有的 `ur0:/data/libshacccg.suprx` 和 vitaShaRK 将独立 Cg 测试样本编译为 GXP。该探针不创建 GXM 渲染场景，不读取商业游戏或存档，不代表转场显示验收通过。

构建（WSL，仓库根目录）：

```sh
bash scripts/build-rule-compiler.sh
```

可设置 VITASDK 覆盖脚本默认 SDK。生成 `build/rule-compiler/art3_rule_compile.vpk`，独立 title ID 为 ART3GRC01。不替换游戏运行器。

启动探针后会编译并自动结束；没有游戏画面。输出目录是 `ux0:/data/art3m1s-rule-compiler/`：

- `compile.log`：立即刷新编译器诊断、耗时和 uniform/sampler resource index，结尾 PASS/FAIL。
- `rule_transition_f.gxp`：仅在编译成功、frag/oldFrame/ruleMap 参数存在，且采样器为 TEXUNIT0/1 后生成。

探针启动会删除该目录中上次生成的同名 GXP，并覆盖 compile.log，防止将陈旧输出误判为本次成功。失败时只保留日志。设备缺少 compiler module 会记录 shark_init 错误，不自动安装模块。

当前源码打包为 app0:/rule_transition_f.cg。使用 SHARK_OPT_SAFE，禁用 fastmath、fastprecision、fastint，以免先引入额外精度差异。返回 GXP 仍须进一步验证与 fill vertex shader 的链接、双纹理采样、图像输出及实际帧时间。

截至本次工作：C/Vita ELF/SELF/VPK 构建成功；尚未运行探针，尚无编译成功的 GXP。编译日志为 `build/rule-compiler-build.log`。构建只有已有环境时钟偏差提示，没有 C 编译错误。

链接的 vitaShaRK 使用 LGPL-3.0-or-later，许可证随 VPK 附带。探针源代码和构建定义保存在本目录，可使用自己的兼容 VitaSDK/vitaShaRK 重新链接。
