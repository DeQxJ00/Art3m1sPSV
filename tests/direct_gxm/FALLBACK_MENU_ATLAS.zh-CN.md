# 方块键备用菜单按需图集

基于 `v1.2.1`，核心仍为 `c796fc388574679b07dba6b9bf73ec660aeeb8c4`。

原先每次打开备用菜单都会重新读取约 8 MB 的 `menu.ttf`，现场生成字形并上传 512×512 RGBA 页面；关闭时释放，下次重复。现在将标题、八项操作和按键提示，用相同字体、stb 栅格化器、字号和字距预先烘焙成 256×128 图集。

- 每次按方块才展开内嵌的 19,430 字节 RLE，并上传 128 KiB RGBA 纹理；没有游戏启动时预载。
- CPU 临时 RGBA 在准备完成后释放；菜单显示期间只保留 GPU 纹理。
- 按叉、方块、返回游戏或选择一项操作，都会释放 GPU 纹理。再次打开重新准备；游戏销毁也做兜底释放。
- 19,430 字节只读压缩数据属于程序静态资源，不是常驻字体或展开的纹理缓存。
- 游戏原生菜单优先，操作映射与触摸区域不变。游戏选择页仍使用动态字体，进入游戏后仍会卸载该字体。
- Shader、图像缓存额度、音频和核心代码没有改动。

`[host-menu-atlas] ready=1 ... prepare_us=` 记录图集展开和上传耗时；`[host-menu] first_frame_us=` 是程序观察到按键后至首帧完成的时间，包含渲染等待，不是物理按键到屏幕显示延迟。关闭应出现 `[host-menu-atlas] released`。

## 构建与验证

在仓库根目录，用宿主 C++17 编译器生成资源：

```sh
g++ -std=c++17 -O2 scripts/bake-fallback-menu.cpp -o build/bake-fallback-menu
./build/bake-fallback-menu host-direct/src/fallback_menu_atlas.hpp build/fallback-menu-atlas.pgm
```

已通过 Vita 构建；已验证 RLE 解码像素逐字节等于 stb 生成的参考图、10 个文本区域均在图集内，并检查 960×544 菜单预览无缺字或越界。图集 alpha SHA256：`ceabea3fd1ba001ff986bc013e2e58ece1aed673634a769d1c390d51208bd7a8`。

实机待验证：进入 SHUF00002 的停句画面，按方块，分别用叉、方块和“返回游戏”关闭，反复三次；再验证存档／读档／设置入口。检查每次打开速度、文字和选择高亮，以及日志中 ready/released 是否配对。原生菜单优先分支未修改。实机结果确认前，TODO 不标为验收完成。

候选包：`build/direct-candidates/menu-atlas-0f71c49/art3m1s_direct.vpk`，VPK SHA256 `758df664db92c57fb82ac75641183ecaa868ee70e235d7a4e958b4819350a185`，SELF SHA256 `8e40852f45dbf2493cffb8582e98dcb67d7542c226f6b3e5885920e8ace28a1e`。源快照检查通过，核心静态库哈希与 v1.2.1 相同。

本轮安装前连接检查：192.168.1.50 的命令端口与 FTP 均超时，尚未更改实机程序。等待联网恢复及性能浮窗关闭后部署、检查启动自检，再由用户手动测试菜单。

2026-09-11 09:11：连接恢复后已安装，备份及新 SELF/SFO 回读校验通过，VitaCompanion 返回 Killed/Launched。部署记录 `build/direct-deploy/deploy-20260911-090859/manifest.json`。首轮启动日志 `build/hardware-logs/20260911-091153-current/host.log`（SHA256 `824356fce553a59e0a52c633cf49adc72d317aa05350b0d959a1889d0c4997d5`）：retained/local-base/overlay 均通过，333 MHz，heap_limit=335544320；共享 surface 检查未通过，差异 102 像素全部在测试图形外、左上角 (11,14)–(32,28)，绿色数字形状，inside_quad=0。要求关闭浮窗后重新启动自检，尚不能标记全部启动检查通过。菜单尚待用户操作验证。
