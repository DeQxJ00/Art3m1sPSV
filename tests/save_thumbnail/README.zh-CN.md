# GXM 存档缩略图验收

> 历史记录：本文包含已删除的旧 UI 宿主与探针，不能作为当前构建说明。相关旧源码可在 `v1.2.14` 及更早的 Git 历史中查阅；当前渲染器为 `host-direct/`。

优先在实体 PSVita 测试 T01 / SHUF00002。构建包为 `build/gxm-host/art3m1s_gxm.vpk`，应用 ID 为 ART3GXM01。保留原游戏和存档；测试应用使用 `ux0:data/art3m1s-gxm`。

1. 安装修复版 VPK，从游戏选择菜单启动 T01，进入可存档的夜空对话场景。
2. 打开 Save，在一个空格保存。确认该格出现进入菜单前的场景，而不是透明图、游戏标志占位图或 Save 菜单本身。
3. 退出应用后重新启动，从标题进入 Load；确认该格仍显示同一缩略图，并可读取存档回到相应场景。
4. 将该格 PNG 从 `ux0:data/art3m1s-gxm/saves/SHUF00002/savedataHD/saveNNNN.png` 复制到电脑，可用 `python tests/save_thumbnail/check_png.py <PNG路径>` 检查。需要 Pillow；该检查专用于 T01 的 120×67 夜空场景缩略图，并不适用于所有游戏尺寸或纯色场景。
5. 检查 `ux0:data/art3m1s-gxm/host.log` 中的 `takess cached completed GXM frame` 与保存缩略图日志，记录实体机卡顿、保存耗时、连续保存及重启结果。

旧版本已写入的透明 PNG 不会自动恢复，需要重新保存对应槽位。先用空格测试，避免覆盖需要保留的进度。

实现读取已结束 GXM scene、经过 sceGxmFinish 的上一帧显示缓冲，按实际 stride 复制并换算到舞台尺寸；存档 PNG 的 alpha 固定为 255。宿主不调用 MCP 截图。逻辑在 Borealis 开始场景前运行；只有 takess 或转场截图读回时等待 GXM，正常帧仅记录 front buffer 地址。输入在 draw 收集、下一轮逻辑消费。实体机性能仍须实测。显示缓冲到舞台的重采样也仍是功能基线，后续离屏舞台可提高原分辨率一致性。

Vita3K 需要开启表面同步（`disable-surface-sync=false`），否则 CPU 读回可能是全零。此次仅为 ART3GXM01 建立 `E:/EmuGame/vita3k_mcp/config/config_ART3GXM01.xml` 应用配置，保留全局配置。这项模拟器设置与实体机安装无关。

游戏档案未解压到运行目录，游戏脚本与字体资产未修改。模拟器结果不等于实体机验收通过。
