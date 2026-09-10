# 1.20 用户确认基线

2026-09-11用户确认当前版本已接近预期，要求打1.20标签。沿仓库既有命名使用注释标签`v1.20`；主目录与core是同一Git仓库的不同worktree、共享标签；主版本使用`v1.20`，对应core提交另用`v1.20-core`固定。此为当前实机测试版本的源码/原包固定点，没有重新编译、更改SFO或重新部署。包内APP_VER仍为01.10，Git版本号为1.20，避免将改版本号后未实测的新二进制冒充原包。

- Core：`5975e778bb9b49f145989aa22012cacab6abe039`，worktree`build/heap-audit/controls-source`，标签`v1.20-core`。
- 主仓库标签含当前host、可重放core补丁和截至本次的测试记录；主机源码与原包构建提交`ae2bcf0`一致。
- 固定原包：`build/01.20/art3m1s-direct-1.20-baseline.vpk`。
- 原候选完整产物：`build/direct-candidates/cache96-5975e77`。
- VPK SHA256：`b05471b0202c136fa83c69d7f1b7cc779666542b95981025cb50a80286040b83`。
- eboot SHA256：`e9085bb26c2dfcbb63ea4e894c9f026de1e20b46afb8060f8a167da2f70160f7`。
- core archive SHA256：`bc4ddc09ca986aaf2da9641d8f109db8d072b4a801bce24aa0e1660e9be86221`。

配置：host-direct；共享图片保留额96MiB；闲置纹理最大32MiB；程序堆192MiB；加载队列64；单次解码工作额度16MiB；已有压缩备份/alpha证书/视频硬解恢复保留。双向窗口已撤销；没有加入刚提出的40MiB闲置纹理实验。shader及GPU生命周期沿当前实测实现。

验证：443项核心测试通过、14忽略；Vita/host编译与原包哈希核验通过；实机deploy-20260911-045602安装读回一致，045722-current启动自检通过，333MHz。96MiB实测人物和line21/22/23命中Pixels；静止双人+放射场景连续窗口约60帧。首次切入仍可有363ms、重走时约182ms长帧，另有重复预载/GPU上传及I/O竞争未解决。标签表示用户指定基线，不代表所有游戏或过场达到稳定60。

详见`tests/direct_gxm/TWO_SIDED_CACHE_WINDOW.zh-CN.md`。用户未提交的TODO.zh-CN.md不纳入此标签。未推送远端、未修改实机。
