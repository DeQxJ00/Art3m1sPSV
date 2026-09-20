# 构建与交付约定

- 完成一次主程序构建时，必须生成并核验可安装的 VPK，交付路径应指向 `build/releases/` 的本次产物。只生成静态库、ELF 或 SELF 不算构建完成。
- 推荐入口：`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1`，先重编译当前核心，再构建宿主与 VPK。
- 包版本跟随当前分支最近的正式 `vMAJOR.MINOR.PATCH` tag。不可写死旧版本，也不可未经用户要求自动递增、创建或移动 tag。
- 标签之后或工作区有修改时必须标为开发包；完整版本保留在包名、程序日志和包内 `build-info.json`。PSV `APP_VER` 的受限映射见 `docs/BUILD.zh-CN.md`。
- 每次构建保留独立 VPK 与 SHA256 清单，不覆盖上次交付。默认不安装到设备，除非当前任务已有部署授权。
- 使用本仓库已配置的 Git 作者信息；保留用户未提交的改动，不混入无关提交。
