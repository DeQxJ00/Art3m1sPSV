# 本地 Git 与版本回退

2026-09-09 开始在本工作区建立 Git。旧版本之前没有提交历史，不能把现在建立的仓库描述为拥有完整的历史版本。

用户已选定 01.02 为基线。恢复分支为 `fix/01.02-baseline`，恢复内容和验证见 [01.02 基线说明](baselines/01.02/README.zh-CN.md)，标签为 `baseline/01.02`。当前优化分支为 `perf/01.02-texture-latency`；本轮仍链接原始 Opt2 core，shader 源码、生成头、三缓冲和 GPU 完成等待保持基线实现。

## 保存边界

- 管理 core、宿主、shader 源码及生成头、测试、脚本、构建所需 vendor 源码和许可证。
- build、工具链、下载的 refs、商业游戏和原生 eboot/IDA 输入、SDK 压缩包、日志、录像、编译产物不进 Git；本机原文件保留。
- 仓库仅在本地，没有创建远程仓库或执行推送。

`snapshot/01.05-uvdb-before-rollback` 保存开始回退前的 01.05 + uvdb 源码。它是问题版本的恢复点，不代表性能合格。uvdb 已完成实机断点/单步测试，主渲染低帧仍未解决。

`archive/01.05-pass-timing` 单独保存未部署、未编译验证的分组计时实验。它不应混入后续选定的旧版基线。

## 旧版本资料

| 包版本 | 本机 VPK | 说明 |
|---|---|---|
| 01.01 / Opt1 | build/direct-opt1/art3m1s-direct-opt1.vpk | 合批和专用程序；有旧实机日志 |
| 01.02 / Opt2 | build/direct-opt2/cmake/art3m1s_direct.vpk | 透明裁剪/扫描；保留部分宿主源码快照 |
| 01.03 | build/effects-merged/art3m1s-direct-01.03.vpk | 效果合入后；转场输入仍存在问题 |
| 01.04 | build/direct-01.04/art3m1s-direct-01.04.vpk | GPU 快照和排序缓存；实机低帧 |
| 01.05 | build/direct-01.05/art3m1s-direct-01.05-rollback.vpk | 仅撤回排序缓存；实机低帧仍在 |

旧包身份、哈希和源码比较见 build/perf-regression-01.04/older-comparison。旧包可以恢复到实机，但“能还原旧包”和“有该版本全部源码”不同；根据快照重建旧版时应注明缺失部分，验证后再标为修复基线。

后续每次修复在明确基线上建立分支，以小提交记录单项修改，保留效果验证与实机同条件数据。可用版本通过验收后再打标签；未验证实验放独立分支。
