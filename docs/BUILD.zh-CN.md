# 构建、版本和 VPK 交付

主程序完整构建入口（PowerShell）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

脚本先调用 `build-native-commands-core.ps1` 重编译当前核心，再通过 WSL 调用 `build-native-commands-host.sh`，产出并核验 VPK。所需 VitaSDK、Rust 工具链和媒体依赖须已安装；目前正式核心仍位于 `build/heap-audit/controls-source/core`。

每次成功构建都归档到：

```text
build/releases/
  art3m1s-direct-<完整版本>-<UTC时间>.vpk
  art3m1s-direct-<完整版本>-<UTC时间>.json
  latest.json
```

JSON 记录 Git tag、提交、是否有未提交改动、核心静态库 SHA256、VPK SHA256 和构建时间。VPK 内另含 `build-info.json`。同一个 tag 多次构建也不会覆盖先前的包。

## 版本规则

- 使用当前分支第一父链最近的 `vMAJOR.MINOR.PATCH` 标签；不取其他分支的最大版本，也不误用 `-core` 或旧两段式标签。
- 正好位于标签且工作区干净：例如 `v1.2.14`。
- 标签之后或有本地改动：例如 `v1.2.14-dev.3+g12345678.dirty`。正式 tag 不会因构建而自动增加。
- 找不到可用标签、构建过程中版本状态变化、包内 SFO 不匹配时构建失败，不以旧包冒充新包。

本机 VitaSDK 的 `vita_create_vpk` 要求 `APP_VER` 为 `##.##`，无法原样放入三段版本。采用无歧义映射：前两位为 `major × 10 + minor`，后两位为 `patch`，例如 `v1.2.14 → 12.14`、`v1.3.0 → 13.00`。major 和 minor 各限 0～9，patch 限 0～99；超出时明确报错，不截断。完整 tag 仍显示于应用名称，完整构建版本写入日志与包内信息。

开发包的系统版本沿用最近 tag 对应的值，开发状态由完整版本区分。验收后由用户指定新增 tag，再构建该标签的包。

只改宿主时可运行 `bash scripts/build-native-commands-host.sh`，它使用已有核心库并同样归档 VPK；修改核心后应使用完整入口。核心、shader、依赖库脚本是构建阶段工具，不应将它们单独成功报告成“已生成安装包”。

`build-direct-all.ps1` 与 `build-direct-host.sh` 属于旧基线重建入口，不作为当前版推荐入口。手动调用 CMake 时也会从 Git 生成版本并归档 VPK；若 tag 或工作区状态已变，应重新配置 CMake 后再构建。
