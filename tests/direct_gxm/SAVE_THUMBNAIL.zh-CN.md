# 存档缩略图被 Logo 替代

2026-09-15，基于头像局部遮罩修复 `af7b867`／core `03c3a1f`。

## 原因

No.01-04 的 `savedataHD/save0003.png` 实际存在，18042 字节，内容为教室、人物与对话。实机日志也记录成功读取 18042 字节；因此不是保存截图失败。

游戏 `system/ui/save.lua` 用 `isFile(...) and path or isFile(...) and path` 取得可选遮罩。归档没有对应的 save/mask.png 时结果为 Lua 布尔值 false，随后经 `lyc2` 传给 `e:tag{"lyc", ..., mask=false}`。桥接层将它字符串化为 `"false"`，GXM provider 尝试加载此遮罩，失败后放弃整张缩略图，露出槽位底图中的 Logo。旧日志有 `GXM texture source missing: false`。

## 修正范围

仅在 Lua `e:tag` 和 `e:enqueueTag` 的 `lyc.mask` 参数转换中，将布尔 false 作为未指定遮罩。真正的遮罩路径、字面字符串 `"false"`、其他布尔属性均保持原值。不按游戏名或图片目录做特判，不改 shader、纹理缓存额度、游戏脚本或归档。

回归测试覆盖两个队列、缺省遮罩、有效路径、字面文件名和 `visible=false` 的保留。232 项解释器测试通过（1 项既有忽略），466 项核心测试通过（15 项既有忽略）。

补丁：`patches/save-thumbnail-false-mask-core.patch`，应用于已包含 core `03c3a1f` 的构建树；不要重复应用。

## 本地证据

`build/toshiue-save-thumbnail/` 保存旧存档备份、必要资源的只读提取、测试及构建日志。解包文件没有放入游戏运行目录。

安装包 `build/toshiue-save-thumbnail/art3m1s-save-thumbnail-fix.vpk`，SHA256 `4c223c67ac8f2403ba99a15c82c219c718b42e1dc47cf0f48d20e52b623cec16`。

## 实机验收

- 部署记录 `build/direct-deploy/deploy-20260915-033050/manifest.json`；程序回读校验通过，启动自检 `retained=1 local_base=1 overlay=1`。333 MHz／ES4 111 MHz。
- LOAD 页中原有 No.01-01、No.01-04、No.01-05 的缩略图直接恢复；空槽保留 Logo。截图 `old-slots-fixed.jpg`。原档无需重存。
- No.01-04 读回剧情及透明头像正常。
- 在空槽 No.01-06 新增测试档；日志记录 `save0004.png` 为 129×72，新槽显示场景缩略图。
- 用户确认“测试通过了”。不再继续发送实机按键，版本号不变。
- 原始日志 `build/native-command-port/psv-save-thumbnail-fixed.log`。没有覆盖 No.01-01、No.01-04、No.01-05。
