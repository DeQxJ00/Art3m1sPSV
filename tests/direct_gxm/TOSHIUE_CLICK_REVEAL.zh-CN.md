# Toshiue 圆圈补全文字

## 原因与修正

原游戏 `system/script.asb` 的 click2 段先调用 `clickGlyphwait`，再执行 `@`。
`system/extend/adv_mw.lua:407` 中 `clickGlyphwait` 经队列执行
`wait scenario=1 input=1`，然后设置点击图标。旧解释器将 scenario 等待的
input 丢弃；运行时仅检查文字自然完成，因此确认键不能补全此阶段的文字。

现在直接标签和队列标签均保留 input。允许输入的文字入场等待收到物理确认
或脚本 overrideKey 的 decide 边沿时，完成当前文字动画，并标记画面更新。
该边沿只用于补全；后续 `@` 仍等待新的输入。input=0、退出自动模式的那次
点击、文字退场等待均不会被此补全分支误处理。

核心改动保存在 `patches/toshiue-circle-text-reveal-core.patch`，应用于
`build/heap-audit/controls-source`。没有修改游戏资源或玩家存档。

## 验证

- 核心测试：479 passed，15 ignored。
- 解释器测试：233 passed，1 ignored。包含直接/队列 scenario 输入策略及后续 @ 等待。
- 实机部署记录：`build/direct-deploy/deploy-20260915-050036/manifest.json`。
- 实机启动像素自检通过，CPU 333 MHz。
- 初版独立短句两页均可提前补全并停留，截图已检查完整显示。
  日志 `build/native-command-port/click-probe-second-stable.log`：
  第一页等待 1789419744595，按键后补全 1789419763760，另一输入后换页
  1789419798027；第二页补全 1789419813893，随后触摸才换页 1789419820667。
  初版音频标签不正确，不能作为语音播放验证；长句版已改用 voice 标签。

## 长句 Demo

生成：`python scripts/prepare-toshiue-click-test.py`。
输出仅在 `build/toshiue-click-test/game`，安装为独立游戏 `TEST_TOSHIUE_CLICK`。
两页原创中文段落为 189/183 字，100 ms/字符，字体按测试字符单独子集化。
第二页的 WAV 是生成的安静测试音，不是游戏原语音。

开始页按圆圈，文字显示中再次按圆圈：应立即显示整段并停在当前页。
再按圆圈进入第二页；第二页重复同样测试。结束页可以循环。
长句版已推送，等待用户验收。当前保留在该测试游戏，原启动器选择记录保存在
`build/toshiue-click-test/last-game-before.txt`。

Toshiue 实际语音句子切入的短暂停顿仍待该场景采样，不能用这个测试音 Demo
宣布性能问题已修复。外置 shader 工作也保持为独立的未验收改动。
