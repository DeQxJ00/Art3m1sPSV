# 启动器设置与 CPU / ES4 频率选项

入口与返回层级：

- 游戏选择页：START 或 △ 进入第二级，底部保留 START 提示；右上角按钮及其触摸区域已移除。
- 第二级：CapUnlocker 开关、超频设置、返回游戏选择。
- 第三级（超频设置）：全局 CPU、全局 ES4、OGV 动画 CPU、OGV 动画 ES4、保存并应用。

× / START 从第三级返回第二级，保留第二级焦点；再次返回才到游戏列表。
启动器不再直接展示 CapUnlocker 横条，△ 也不再直接改变开关。
设置页文字统一使用启动器字体的 24px 字号；进入游戏时沿用菜单字体释放流程。
CapUnlocker 仍写原 `cpu3.off`，下次启动生效，不变更当前线程亲和性。

## 时钟策略

配置 `ux0:data/art3m1s-gxm/cpu-clock.conf` 格式
`2 <cpu-global> <cpu-ogv> <es4-global> <es4-ogv>`。
CPU 每项可为 0 / 444，ES4 每项可为 0 / 111 / 166 / 222，默认均为 0。
兼容读取旧格式 `1 <global> <ogv>`，保留 CPU 设置并让新增 ES4 设置为关闭。
保存成功才应用；取消不保存。
写入通过临时文件和备份替换，支持上次重命名中断后的备份读取。

CPU / ES4 各自管理频率：全局关闭时不主动设频，需要超频时记录该时钟的接管前频率。
OGV 关闭时沿用全局；开启时按实际 Theora 解码器进入/离开切换频率，
不依据扩展名猜测，也不将转成 H.264 的替代资源当作 Theora。
例如 ES4 全局 222 / OGV 166 表示动画期间选择 166，选项是独立覆盖关系。
只在主线程生命周期边界修改 CPU / ES4 时钟，没有每帧写频率、总线/XBAR 设置或解码线程抢占改动。
关闭视频前先按原逻辑停止并回收解码线程，再恢复频率。
正常 EOF、跳过、播放失败、切换视频和退出游戏汇合到同一关闭处理；
应用正常退出时释放两路时钟控制，分别恢复接管前频率。系统强制终止不保证执行退出回调。
ES4 使用 `scePowerSetGpuClockFrequency` / `scePowerGetGpuClockFrequency`，不依赖 kuBridge。
接口调用失败或读回频率不符时记录失败，界面展示 CPU 和 ES4 的实际频率。

## 已知 500 MHz 限制

使用 `scePowerSetArmClockFrequency` 并读回实际 CPU 频率，检查错误和插件锁频。
早期菜单包实机确认 444 请求成功；OGV 请求 500 返回 `0x802b0000`、实际仍为 444。
因此这台设备的 500 MHz **没有通过应用接口验收**。用户决定先用已有插件手动
比较 CPU/GPU，已停止继续尝试其他接口，程序内两个选项恢复关闭。
现按用户要求移除两个 CPU 500 MHz 选项；旧 v1/v2 配置中的 500 按该项关闭处理，
保留其他有效设置，新配置拒绝保存 500。

依据：[VitaSDK 电源接口](https://docs.vitasdk.org/group__ScePowerUser.html)，
[PSVshell 的时钟 Hook](https://github.com/Electry/PSVshell/blob/master/src/main.c)。
插件有自己的锁频策略，不能仅凭 setter 返回成功就声称频率已切换。

## 验证记录

- `clock_settings_test.cpp`：默认值、选项合法性、双向调整、配置持久化、备份恢复、
  生命周期恢复、重复关闭、外部频率变化及被拒绝/锁定频率的策略测试，通过 ASan/UBSan。
  ES4 扩展增加旧配置迁移、四档频率和 CPU/GPU 独立接管/恢复测试。
  模拟策略调用覆盖关闭路径，不替代真实视频 EOF/跳过的完整系统测试。
- 第一个菜单包的 Vita3K 按键、触摸入口、保存、取消检查通过；
  实机启动像素自检通过，444 MHz 生效，MP4 阶段没有触发 OGV 请求。
- 500 请求失败证据：`build/startup-watch/cpu-clock-ogv-active-host.log`。
- 恢复关闭的证据：`build/startup-watch/cpu-clock-restored-off-host.log`，
  global=0、ogv=0、CPU=333、GPU=111 MHz。

分级菜单包使用当前 host-direct 源码，保留 core archive `ed72c5e7…` 和
相同 shader 内容；`tests/startup_watch/prepare.py --current-host` 避免从旧镜像
构建时漏掉新菜单文件。此包先在 Vita3K 检查，不自动打断用户的实机手动超频测试。

分级包验证完成：MCP 会话 `0ce55cf7-86db-40ec-92a5-a186b0c7a865`，
每次调用先检查 health。检查启动器、第二级、第三级截图，CapUnlocker 开→关→开，
方向键/确认进入、× 逐级返回并保留焦点、触摸进入两层及 START 逐级返回。
完成后仍在游戏选择页，CPU3 开关恢复开，时钟配置为 `1 0 0`。
截图：`build/native-five-v125/settings-level1.png`、`settings-level2.png`、
`settings-level3.png`、`settings-return-focus.png`、`settings-touch-verified.png`。
候选 VPK：`build/direct-candidates/launcher-settings-levels/art3m1s_direct.vpk`，
SHA256 `9e7f013d56b679ff66bd41ac9daa629b233a0ad754065b8460a639d637cddb1e`。
分级包尚未部署实机；模拟器菜单流畅不作为实机游戏性能结论。

## ES4 扩展与启动器入口精简

ES4 页面通过 Vita3K 布局与保存验证：会话 `be07a7f5-c5d9-462d-9f44-a51b9a6d2e3e`。
从旧 `1 0 0` 配置启动，新 ES4 两项均关闭；按键选全局 166 / OGV 222，
保存结果为 `2 0 0 166 222`，CPU 两项没有变化。该模拟器读回 GPU 恒为 222，
请求 166 后界面正确提示未生效，不能把此结果解释成实机 ES4 调频通过。
通过触摸将 ES4 两项恢复关闭，读回文件 `2 0 0 0 0`。
截图 `build/native-five-v125/settings-es4-menu.png`、`es4-saved.png`。

用户随后要求移除右上角按钮：启动器只留底部 START 提示，不保留隐形触摸热区。
最新候选 `build/direct-candidates/launcher-es4-settings/art3m1s_direct.vpk`，
SHA256 `99ef67db830ea08c14fafe5b6e83fa0087e84a46bb1915fa6f8fb58a6d2fcd1c`。
此轮没有连接、重启或部署实机，以保留用户手动超频测试。

最终去除 CPU 500 的包：MCP 会话 `d525afb0-6739-4620-bfb0-32cc32d5523e`。
启动器右上按钮已消失，点击原区域不会打开设置；START 仍可进入。
全局 CPU 右键连续两次为关闭→444→关闭，OGV CPU 左键也按两档循环。
两次 × 返回游戏选择页，配置保留 `2 0 0 0 0`；实机未部署。
截图：`build/native-five-v125/clocks-final-launcher.png`、`clocks-final-444.png`、
`clocks-final-off.png`。旧 v1/v2 中 500 的逐项迁移及新保存拒绝 500 测试通过 ASan/UBSan。
