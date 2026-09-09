# 1.10 当前实机测试版本

用户于 2026-09-09 指定将当前 controls5 版本标记为 1.10。VPK APP_VER 为 `01.10`，应用 ID 仍为 ART3DIR01。此次编号更新只修改版本和日志标识，核心库与 controls5 相同。

- 包：`build/01.10/art3m1s-direct-01.10.vpk`
- core：独立分支 codex/optL-controls，提交 `2da40e8`；库 SHA256 `0ae0cd49c68f1bc99cf6471ad812996ba08df7cf043429d7a2dcd41c84cadf2a`
- VPK SHA256：`0ed1c5cae931ed7e1dd6b56ace5d53599dca867cd0b8f0400cf2f2725a4d0341`
- eboot SHA256：`1b506416556afe905e97259fe8063850fbc24e06c0778ab837ef0580f55bbaa2`
- shader SHA256：`f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6`（未改）

内容包括 text-epoch 性能基线、新按键、原生／后备菜单切换，以及原生游戏独立 platform.txt=VITA 配置和 JPEG 图片／预载支持。未包含正在研究的时钟变化与命中检测优化。

构建：host 的 DIRECT_SEMANTIC_CONTROLS、DIRECT_TEXT_EPOCH_CANDIDATE、DIRECT_DEFERRED_FINISH_CANDIDATE 开启；ART3_DIRECT_VERSION=01.10；显式指定上述 core 库。core 需 gxm-menu-key-alias、gxm-text-epoch、gxm-native-renderer、gl-backend。

详细测试范围与保留问题见 `tests/direct_gxm/CONTROLS_MENU_20260909.zh-CN.md`。350 项 core 测试通过；两个菜单分支已有 Vita3K 运行证据，但不能等同于实机帧率或全游戏兼容验证。此前 Config 的一次模拟器 DeviceLost 尚无根因结论，新进程两种入口均通过。

实机 192.168.1.50 已更新并启动。部署备份和逐文件回读校验记录：`build/direct-deploy/deploy-20260909-220452/manifest.json`。更新未修改游戏资源和存档。更早 text-epoch 回退程序及日志保存在 `build/direct-deploy/deploy-20260909-220125/`。
