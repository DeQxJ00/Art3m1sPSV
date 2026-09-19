# Stella 已验证 Shader 转内置（2026-09-20）

将三款资源测试中通过的 `blend`、`blend2`、`radial` 加入内置源码哈希表。Stella PC／Android 的这三份源码及实机缓存完全相同，共用三个程序，不按游戏目录重复嵌入。原 31 项保持不变，共 34 个源码版本；原 51 来源 demo 仍是 31 页。

- `blend`：RGB 平均亮度作为透明度。
- `blend2`：保留原文件将 float3 隐式截为红通道、再作 0.78 阈值判断的行为。
- `radial_stella`：径向模糊，算法与原 radial 同类，但源码不同，保留其已验收的 libshacccg 二进制，避免切换编译产物带来精度变化。

程序仍按完整源码哈希匹配；游戏中的 HLSL 不应删除或改名。三项命中时不读取 shader-cache、不执行转换或编译，不需要开启两个自动开关。原有外置缓存不删除，便于旧版本复用。

使用 `scripts/import-verified-stella-shaders.py` 从历史实机证据生成 AGX1 包，转换元数据 ABI2 与二进制容器 ABI1 分别校验，再运行 `scripts/bundle-artemis-shaders.py` 更新核心和宿主诊断表。源码／GXP 摘要见 `shaders/artemis-pc/supplemental/provenance.json`。核心提交 `f0e8cba`，对应可重放补丁 `patches/stella-builtin-core.patch`。

验证：491 项核心测试通过；PSV 两份 Stella 各 9 页，转换／编译均关闭，3 项明确命中内置，编译及 GXP 缓存命中均为 0，旧缓存四文件保持不变。18 张完整截图与更新前外置版逐像素一致。实机脚本使用保留的原始验收资源及独立 TEST 目录，不修改真实游戏或 PFS。

最终安装包另执行完整内置像素自检：`passed=34 total=34 ok=1`，包含新增三项的普通透明度、遮罩叠加透明度及空间采样。新增 blend 的诊断期望值按平均亮度计算，统计总数改为数组长度。该改动仅涉及自检，运行时程序与上述 18 页对比相同。

```powershell
python -X utf8 scripts/test-three-game-shaders-device.py start --game 1 --round builtin --evidence-root build/stella-builtin/device
python -X utf8 scripts/test-three-game-shaders-device.py press --evidence-root build/stella-builtin/device
python -X utf8 scripts/test-three-game-shaders-device.py collect --game 1 --round builtin --evidence-root build/stella-builtin/device
# 对 game 2 重复，再分析；测试后恢复偏好并返回启动器。
wsl -d Ubuntu-24.04 -- python3 /mnt/f/WorkSpaceAI2/art3m1s-psv-gxm/scripts/analyze-stella-builtin-device.py
python -X utf8 scripts/test-three-game-shaders-device.py restore --evidence-root build/stella-builtin/device
```

详细日志／截图保存在 `build/stella-builtin/device/`，摘要在 `evidence/stella-builtin-20260920/`。这些结果验证上述参数、透明度和采样案例；不将其他平台的 GLSL 或 DX11 代码算作新增支持。
