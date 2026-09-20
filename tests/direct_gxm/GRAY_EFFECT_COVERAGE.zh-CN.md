# 回想背景被黑色覆盖

## 证据与原因

`script/c20_01a.ast` 的 `0003_00014` 段创建 `bg01fs_s` 灰度背景、
灰度朝日立绘，以及 `:cg/回想_白枠`。`system/image/shader.lua` 的 `ar_gray`
发出 `lyprop grayscale=1 intermediate_render=2`。

2026-09-15 对 otomeriron EXE 的 IDA 查询（13342，输入为
`F:/HikariFieldGames/Otomeriron/otomeriron.exe`）：

- `0x1400D76B0` 解析 intermediate_render：0 关闭，字符串 1 映射内部 2，
  字符串 2 映射内部 1。这里没有“脚本 2 必须输出不透明”的证据。
- `0x140114150` 解析 grayscale。当前 core 已解析这两个属性，没有漏掉命令。
- 当前 PFS 的背景、立绘和白边均能加载。白边 PNG 中央 Alpha=0；
  `bg01fs_s` 中央 Alpha=255，不能归因于白边中央被压成不透明。

另外用已有原版 otomeriron EXE 在独立 build 目录运行以下原语探针：

```text
[lyc id="0" color="0x2060c0" width="640" height="360"]
[lyc id="1.child" color="0xff8000" width="100" height="100"]
[lyprop id="1.child" left="40" top="40"]
[lyprop id="1" grayscale="1" intermediate_render="2"]
[flip]
[stop]
```

原版显示灰色方块和周围蓝底；修正前 Direct 版显示灰色方块和黑底。
使用当前 PFS 的背景、人物、白边构造独立场景，同样复现用户截图的黑底。
因此本次修正的是已解析属性的合成语义，既非补未知指令，也非修改图片透明度。

## 修正边界

core compositor 不再因 `intermediate_render=2` 把合成结果 Alpha 强制设为 1。
中间图层仍正常渲染，灰度、反色、颜色乘算和组 Alpha 仍在子层合成后应用。
移除只对带遮罩子树保留透明度的例外，将正确覆盖率保留扩展到普通中间图层。
头像遮罩的本地坐标、灰度转遮罩以及宿主缓存行为没有改动。

此次没有修改 Direct shader、PFS、游戏 AST、PNG、缓存额度或时钟。
模式 1/2 的其他内部生命周期差异尚未完全还原；此次对照只闭合透明覆盖问题。

## 重现与验证

- core 修正提交 `202257d`，基于 `1a6bcb37210989e57fa959a90379b2f5e21a4890`，增量补丁见
  `patches/intermediate-coverage-core.patch`；已有该修正时不要重复应用。
- 构建仍用 `scripts/build-native-commands-core.ps1` 和
  `scripts/build-native-commands-host.sh`。
- 485 项核心测试通过，15 项手工/性能测试忽略；新增回归覆盖 mode 1/2、
  背景与人物组顺序、组 Alpha、灰度和未带遮罩的透明覆盖。已有头像遮罩测试通过。
- 本地原版探针、当前资源提取、旧/新包截图和部署校验存于
  `build/otomeriron-gray-background/`。分析解包文件不放入安装的游戏目录。
  模拟器使用的是另外创建的临时独立验收包 `TEST_GRAY_COVERAGE`，完成后已移出游戏列表。
- 可用 `python scripts/prepare-intermediate-coverage-test.py <游戏PFS目录>` 重建
  独立包到 build；该脚本不部署、不启动 EXE、不改源资源。
- Vita3K MCP 的旧/新对照：旧包合成方块与真实资源均出现黑底；新包蓝底恢复，
  实际资源背景恢复；第二背景 + 人物组 Alpha=128 正常，模式 1 切换后外观一致。
  截图分别为 `before-synthetic.png`、`after-synthetic.png`、`before-assets.png`、
  `after-assets.png`、`after-alpha.png`、`after-mode1.png`。
- 打包产物 `build/otomeriron-gray-background/art3m1s-gray-coverage-fix.vpk`，
  SHA256 `5c1dd8d4b296df0652f941d76a0569a4d30f93a887a2752371837456b200009e`。
  模拟器 eboot 已回读校验，实机尚未部署。

验收应检查：背景能穿过人物周围透明区域；白边仍渐变；人物仍灰度；
组 Alpha=128 时背景能穿过人物；切换背景后不残留旧的黑底。
模拟器验证不能替代 PSV 实机验收，也不能用于判断实机帧率。
