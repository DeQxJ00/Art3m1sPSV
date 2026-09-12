# 启动器设置与 CPU 频率选项

入口与返回层级：

- 游戏选择页：START、△ 或点击右上角“设置”，进入第二级。
- 第二级：CapUnlocker 开关、超频设置、返回游戏选择。
- 第三级（超频设置）：全局超频、OGV 动画超频、保存并应用。

× / START 从第三级返回第二级，保留第二级焦点；再次返回才到游戏列表。
启动器不再直接展示 CapUnlocker 横条，△ 也不再直接改变开关。
设置页文字统一使用启动器字体的 24px 字号；进入游戏时沿用菜单字体释放流程。
CapUnlocker 仍写原 `cpu3.off`，下次启动生效，不变更当前线程亲和性。

## 时钟策略

配置 `ux0:data/art3m1s-gxm/cpu-clock.conf` 格式 `1 <global> <ogv>`，
每项可为 0 / 444 / 500，默认均为 0。保存成功才应用；取消不保存。
写入通过临时文件和备份替换，支持上次重命名中断后的备份读取。

全局关闭时不主动设频；需要超频时记录接管前 CPU 频率。
OGV 关闭时沿用全局；开启时按实际 Theora 解码器进入/离开切换频率，
不依据扩展名猜测，也不将转成 H.264 的替代资源当作 Theora。
全局 500 / OGV 444 表示动画期间选择 444，选项是独立覆盖关系。
只在主线程生命周期边界修改 CPU 时钟，没有每帧写频率、GPU/总线设置或解码线程抢占改动。
关闭视频前先按原逻辑停止并回收解码线程，再恢复频率。
正常 EOF、跳过、播放失败、切换视频和退出游戏汇合到同一关闭处理；
应用正常退出时释放时钟控制，恢复接管前频率。系统强制终止不保证执行退出回调。

## 已知 500 MHz 限制

使用 `scePowerSetArmClockFrequency` 并读回实际 CPU 频率，检查错误和插件锁频。
早期菜单包实机确认 444 请求成功；OGV 请求 500 返回 `0x802b0000`、实际仍为 444。
因此这台设备的 500 MHz **没有通过应用接口验收**。用户决定先用已有插件手动
比较 CPU/GPU，已停止继续尝试其他接口，程序内两个选项恢复关闭。
保留选项不等于宣称它在所有设备上生效；不安装插件或绕过系统限制。

依据：[VitaSDK 电源接口](https://docs.vitasdk.org/group__ScePowerUser.html)，
[PSVshell 的时钟 Hook](https://github.com/Electry/PSVshell/blob/master/src/main.c)。
插件有自己的锁频策略，不能仅凭 setter 返回成功就声称频率已切换。

## 验证记录

- `clock_settings_test.cpp`：默认值、选项合法性、双向调整、配置持久化、备份恢复、
  生命周期恢复、重复关闭、外部频率变化及被拒绝/锁定频率的策略测试，通过 ASan/UBSan。
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
