# art3m1s-core

Artemis 视觉小说引擎的 Rust 兼容运行时：解释 ASB/IET 脚本、维护场景与图层树、
渲染文本与特效，经 C FFI 与宿主连接。本仓库的生产宿主是 `host-direct/`，
由 core 的原生 GXM 后端组织绘制，宿主执行 GPU 提交并提供文件与媒体回调。
根目录 `core/` 是当前生产核心；旧独立工作区仅作历史回溯。
core 本身不创建窗口，不做音视频解码。

## 功能

- ASB/AST/IET 脚本解释与 Lua 桥接（桌面/Android 用 Lua 5.1，iOS 用 Luau）
- 图层树、变换、混合、转场、动画与命中测试；Artemis HLSL shader 子集
- E-Mote PSB 立绘（内置后端；`crates/eluna` 为实验后端）
- 场景文本、Ruby、逐字显示、backlog、宿主文本翻译注入与覆盖字体
- PFS 归档（含分卷、pf8 加密）与目录资源、编号/系统存档
- 鼠标、键盘、触摸、拖动的脚本事件派发

## 仓库结构

```text
src/                  运行时、合成器、GXM/GL 后端、文本、FFI
crates/
  asb-interpreter/    ASB/AST/IET 解释器与 Lua 桥
  art3m1s-emote/      内置 E-Mote 后端
  eluna/              实验性 E-Mote 后端（基于 xmoezzz/eluna 适配，MPL-2.0）
  pf8/                PFS 归档库（vendored 自 sakarie9/pfs-rs，MIT；
                      本地扩展见 crates/pf8/VENDORED.md）
  pfs-upk-rust/       PFS 的 C ABI 封装（产物即 libpfs_upk）
tools/game-probes/    兼容性探针
tests/                集成与兼容性测试
doc/                  宿主接入指南与 FFI 参考
```

## 构建与测试

PSV 完整构建从仓库根目录运行 `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1`，
先将本目录编译为 `libart3m1s_core.a`，再链接宿主并输出 VPK 到 `build/releases/`。
PSV 使用 Lua 5.1；启用 `gl-backend,gxm-native-renderer,gxm-text-epoch,gxm-menu-key-alias,gxm-builtin-effects`，
关闭默认 features。其中 `gl-backend` 保留共享资源与运行时接口，实际绘制走原生 GXM。

Windows 逻辑回归入口为根目录 `scripts/check-direct-effects-core.ps1`，包含生产 GXM feature 组合。
以下为 core 的通用桌面开发入口：

```bash
cargo fmt --check
./scripts/test-all.sh
cargo build --release
```

默认 features 包含 GL 渲染器与实验 Eluna 后端；只用无 GPU 的核心模块时
`cargo build --no-default-features`。需要商业游戏资源的兼容性测试默认不执行，
见 [tests/README.md](tests/README.md)。

## 宿主接入

线程、生命周期、帧循环与回调约定见 [doc/HOST_INTEGRATION.md](doc/HOST_INTEGRATION.md)；
完整 C ABI 声明与协议见 [doc/FFI_REFERENCE.md](doc/FFI_REFERENCE.md)。
E-Mote 后端选择：默认内置；宿主在加载项目前调用
`art3m1s_runtime_set_emote_backend(..., 1)` 才切到实验性 Eluna。

## 状态与限制

- HLSL 支持面向已测试游戏实际使用的 shader 形态，不是通用 DirectX shader 编译器。
- E-Mote 针对已测试游戏的 PSB 变体；部分私有 easing 与外部纹理格式未覆盖。
- HTTP、native call、浏览器、振动等宿主服务需宿主实现回调后方可用。

版本详情见 [CHANGELOG.md](CHANGELOG.md)。

## 许可证

[MPL-2.0](LICENSE)：文件级 copyleft——修改本仓库已覆盖的文件需以 MPL-2.0
提供对应源码；由其他许可文件组成的 Larger Work（宿主应用、闭源分发版）
可保持各自许可。例外：`crates/pf8` 保留上游
[MIT 许可](crates/pf8/LICENSE)。
