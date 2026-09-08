# 01.02／Opt2 固定基线

用户于 2026-09-09 选定此版作为后续修复基线。Git 工作分支 `fix/01.02-baseline`，基线标签 `baseline/01.02`。01.05 + uvdb 与未部署计时实验保留在原 Git 快照/归档分支，不混入本次恢复。

## 包和恢复内容

原始安装包：`build/direct-01.02/art3m1s-direct-01.02-original.vpk`，APP_VER=01.02，TitleID=ART3DIR01。

SHA256：`9CCA434A7FFF542B50C8526C66B32694E01DD66EA2CFD68DD7D5926B0D10AC28`。

恢复的八个宿主源码/头文件逐字节对应 `build/direct-opt2/source/host-direct/src` 和效果合入前的独立快照；shader 源码及头文件也恢复为 Opt2。主路径不包含后来的 builtin effects 分组、GPU 快照、uvdb 启动钩子或新增计时实验。

01.02 延续相邻批次合并、四种专用片段程序、三缓冲、无 MSAA、安全 Finish，新增透明边界裁剪/不可见剔除和缓存内存中的顶点变换。它也包含已发现的 RGBA 更新重复扫描问题，尚未在本基线中修正。后续修复要另作提交。

## 核心库边界

找到了实际构建 01.02 时保留的 `build/direct-opt2/baseline/libart3m1s_core.a`，SHA256：

`A93E5A7EC538CFE1E41CDAF7B952B99F83D745E47D35F2923B492150A31C6DBB`。

CMake 默认链接这份库并检查哈希，不链接当前 build/rust-native 中较新的库。完整的历史 core 源码快照没有保留，当前 core 目录不能声称完整还原为 01.02。因此不能通过重编译当前 core 来冒充历史基线。

需要后续修改 core 时，应在单独提交中显式设置 `ART3_DIRECT_CORE_LIBRARY` 为新构建产物，并做同条件行为/帧率比较；`scripts/build-core.ps1 -NativeRenderer` 已恢复效果合入前的特性组合。默认基线构建不会执行这一步。

## 构建和已完成验证

在项目根目录执行 `scripts/build-direct-all.ps1`，或在 WSL 执行 `bash scripts/build-direct-host.sh`。输出独立放在 `build/direct-01.02-host`，不覆盖原始 VPK。归档核心及媒体静态库属于本地构建依赖，未放入 Git；它们的哈希见 manifest.json，迁移工作区时须一并恢复。

恢复后已实际编译、链接并打包成功，且与旧构建二进制比较：

- 14 个宿主目标文件中 13 个逐字节一致，包含 GPU、媒体桥接、H.264 视频、音频、文件和菜单模块。
- main.cpp.obj 仅编译日期/时间字符串变化，规范化这些字符串后逐字节一致。
- **最终 ELF 都是 27,496,964 字节，规范化编译日期/时间字符串后，整个 ELF 逐字节一致。** 这比只核对源码目录名更能确认实际恢复范围。
- 原始 VPK 的 CRC、版本号、TitleID 和 SHA256 均已核对。

证据在 `build/direct-01.02/object-comparison.json`、`elf-comparison.txt` 和 `rebuild.log`。本次恢复验证没有增加新的功能，也没有宣称达到 60 FPS；基线包保留其原有能力与缺陷，后续以实机测试确认改进。

uvdb 的已验证接入保留在 `snapshot/01.05-uvdb-before-rollback`。本分支旧 GPU 接口不能直接使用 01.05 的调试包构建步骤；脚本已加检查，避免生成没有调试能力却误称 uvdb 的包。
