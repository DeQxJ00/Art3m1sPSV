# 内置演示去重验收（2026-09-15）

51 个来源文件按 SHA256 分组，并逐字节确认相同后才合并。生成结果包含 31 份原始 HLSL、31 次注册和 31 个演示页；20 个共用项标注 game1/game2，另外 11 项来自 game2。manifest.json 的 origins 保留全部 51 个来源路径，不暴露实际游戏简称。GPU 内置表原本已有 31 个唯一程序，本轮没有更改程序或效果算法。

源码统一放入 `sources/shared/system/shader/pc/`。为兼容已保存的启动器选择，保留 `TEST_SHADERS_51` 目录 ID 和“内置 Shader 演示 demo”标题。外置演示保持六个测试项。

验证：生成时核对全部 51 个来源字节与 31 个输出对应；VPK CRC 正常，包含 31 个内置 HLSL 和 6 个外置 HLSL。Vita3K 在转换/编译双关下取得 31 次内置命中、31/31 进度、失败 0、零编译；按键遍历 1–31 页后回到第 1 页。已查看共用项、第 31 项及循环截图，退出回启动器并恢复原偏好。未修改真实游戏资源，未推送实机。

证据：`build/shader-dedup/emulator.log`、`page-01.png`、`page-21.png`、`page-31.png`、`page-32.png`。

安装包：`build/shader-dedup/art3m1s-shader-dedup.vpk`，SHA256 `d568d9d489427a475a33d63b3a536f2c74d5fe718a94241febf4a502d2258985`。
