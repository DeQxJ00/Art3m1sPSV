# 方块键备用菜单按需图集

2026-09-11：用户确认“方块按键通过测试”，当前版本按要求标记为 `v1.2.2`（从 `v1.2.1` 增加 0.0.1）。对应实机包为 `menu-atlas-probe-3384b1b`，核心仍为 `c796fc3`，保留现有安装二进制及 SFO APP_VER `01.10`。

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

## 启动检查避开性能浮窗

用户要求无需每次关闭浮窗。共享 surface 的 340×180 测试图形移至右下方 `(560.25,320.25)`，比对范围限定为 `[540,920)×[300,520)`，完整覆盖图形、亚像素边缘以及约 20 像素背景保护区。范围内仍逐像素比对全部 RGBA 通道，容差仍为 1；CPU 数据、步幅、自检图像附加信息和两次读回成功要求保持不变。失败仍禁用共享路径。

范围外的变化只计入 `external_changed_pixels`，不会再把左上角数字变化当成纹理错误。浮窗若实际覆盖测试区仍会影响检查。本改动只涉及 `shared_surface_self_test()`，不修改 shader 或游戏渲染路径；其余启动测试原本就在中央／下方取样。需在浮窗开启的实机上核对区域内差异为零、启动优化开关正常。

## 方块菜单实机记录

下一轮部署前保存的 `build/direct-deploy/deploy-20260911-091504/host.log`（SHA256 `1e86452145947c8385ef4b12593f7c39e336a42770f9c07e0a77f290a2951476`）包含用户对菜单的 8 次操作：图集准备 2145～2344 μs，首帧完成 6685～12019 μs，8 次 ready 与 8 次 released 配对；期间有设置、存档、历史记录动作发出。这里只验证动作发出，不将其等同于存档文件或读档内容验证。游戏载入前的大字体释放为 8,364,840 字节，此后这些菜单开关没有重新加载该字体。

以上实测来自菜单图集版 `0f71c49`。后续 `3384b1b` 只修正启动自检范围，菜单代码不变。菜单速度与关闭释放 TODO 已完成；用户若发现视觉或具体功能异常，应另行检查，不能从计时日志推断完整功能验收。

## 右下角自检版安装结果

`menu-atlas-probe-3384b1b` 已部署并回读校验：VPK SHA256 `b28e7f6713a1c037b162c271609d93d4172ba8f6dd0b6bdea8493c94d0a8a348`，SELF SHA256 `caf217031a973c51d8c1fce8df32584021a8b75e5ec9dafed3db7efeb15dd500`。部署记录 `build/direct-deploy/deploy-20260911-091504/manifest.json`。

启动日志 `build/hardware-logs/20260911-091847-current/host.log`（SHA256 `712f7843b5941307461db734a5c3d662881fca9168e4de8429baf378fb9a9ced`）确认检查区 `540,300,380,220`、shared-surface max_delta=0/ok=1、retained/local_base/overlay 全部为 1、333 MHz、320 MiB 堆上限。此次 external_changed_pixels=0，只能证明此次启动检查通过，不能凭此声称实测捕捉到了正在变化的浮窗数字；范围外数字已由比较范围排除。自检期间不再要求关闭左上角浮窗，覆盖右下测试区的浮窗仍需避开。

## 后续字号设置扩展

2026-09-11：字号设置加入后，当前主线图集扩为 512×256、512 KiB（32 个区域，内嵌 RLE 44,760 字节），仍按需上传并在菜单关闭时释放。上文 128 KiB 和实机耗时是 v1.2.2 原包的历史数据，不能直接视为新版测量结果。参见 [FONT_SETTINGS.zh-CN.md](FONT_SETTINGS.zh-CN.md)。
