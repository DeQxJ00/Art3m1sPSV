# 菜单字体生命周期

2026-09-08：GameSurface 加载成功后，在下一次场景外 tick 等待 GPU 完成，卸载 NanoVG/FontStash 菜单字体、字形记录和旧字形图集。保留默认大小 512×512 的空 atlas（CPU/GPU 各一份，实际 GPU 分配有对齐），避免过小图集导致重新显示时首字放不下。游戏 core 字体和普通图像纹理不经过此清理。

返回菜单请求移到场景外执行：关闭媒体、等待 GPU、销毁游戏运行时，再通过原 FontLoader 按相同打包配置和注册顺序恢复菜单字体，最后 pop 游戏 Activity。保留轻量菜单 View 与游戏列表，以保留焦点；没有销毁共享 GXM/NanoVG 渲染器。加载失败时保留菜单字体用于错误文字。后续按用户要求已移除 Select 返回菜单绑定；游戏运行时主动请求退出仍走此清理路径。

`bash tests/menu_fonts/run.sh` 用真实 menu.ttf 和生产 NanoVG 代码执行 20 次卸载/重载，并主动扩大字形图集。验证相同字体 ID/文字宽度、字体与扩大图集被释放、独立游戏图像保留、分配失败保持原字体可用；ASan/UBSan 通过。模拟渲染后端不能验证实际 GXM 同步或真机内存下降量。

日志：`build/menu-font-tests.log`、`build/menu-font-final-build.log`。实机仍需验证加载页面、进入游戏、返回菜单及连续切换；观察 `[menu-memory]` 的 released/restored 日志，确认中文和图标正常。

最终 VPK 构建成功（仍有 WSL 时钟偏差警告），SHA256：`76D8821CE8840336D3B1B44DE55DAF08BF8A628632410FE7137F016AEC87D3E5`。
