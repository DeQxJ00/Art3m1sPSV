# 章节入口的固定遮罩预载

本轮在当前主线 host-direct 上增加专用遮罩缓存。不是按章节静态分析引用，也不是重新启用双向预测队列。

## 名单和加载时机

- 使用当前游戏 `setMagicPath` 注册的 `rule` 目录；不写死 SHUF00002 的文件名或 `image/rule` 路径。
- 读取已挂载 PFS 的文件索引，并补充该目录下的松散文件。补丁覆盖仍由正常文件读取顺序决定；名单去重，保留真实文件路径和大小写。
- 在注册 rule 路径之后，首次通过脚本读取器载入 AST/ASB 时，同步预备当前目录下整套 PNG 遮罩。Lua `e:include` 与外部 ASB 使用同一读取器，均能覆盖。若标题/初始化使用 ASB，预备会早于第一个剧情章节。
- 后续章节复用同一集合，不再次扫描目录、读取或解码。rule 路径变化时重建；换游戏时缓存跟随运行时释放。
- 目前只识别已注册的 `rule` 路径以及 PNG 资源。未注册、非 PNG、超过额度、解码失败的资源走原有加载流程；没有宣称覆盖所有游戏的自定义动态遮罩。

## 内存和交付

固定缓存上限 48 MiB、128 张；不是额外增加 48 MiB：它占用现有 128 MiB 图片缓存中的 ready 额度。普通预载只能使用扣除固定遮罩后的剩余额度。heap 保持 256 MiB、闲置纹理上限保持 32 MiB。

遮罩保留 RGBA 解码结果和原有 alpha/bounds certificate。这里有意保留一份 CPU 解码结果，用于 GPU 纹理被普通 LRU 或视频内存回收释放后的再次使用；不能声称这种固定保留与 GPU 合起来永远只有一份像素。

GPU 纹理仍命中时直接使用 GPU。GPU 未命中时，从固定解码结果复制一份可转移的上传缓冲和 certificate，交给现有共享纹理路径；固定原件不被消费，不重新读取 PNG 或扫描 opacity。没有把所有遮罩永久钉在 CDRAM，也没有改 shader、GXM 同步、后台线程或 bind_surface_async 的等待协议。

该副本的瞬时内存由资源账本记录为 temporary，GPU 上传后按既有生命周期释放。固定遮罩不另留压缩副本；其他图片的压缩备份保留不变。首次上传和复制仍有成本，尚需实机测量。

SHUF00002 本地样本实际有 19 张 960×540 遮罩，RGBA 共 39,398,400 字节（37.573 MiB），另加少量 certificate。其他游戏按自己的目录重新列举。这 37.6 MiB 会减少普通背景/立绘缓存可用额度，需要同时观察是否增加了其他图片的重新解码。

## 验证与实机测试

核心测试 456 项通过、14 项忽略；缓存报表测试 4 项通过。覆盖固定结果重复取用、调用方修改不污染原件、共享额度边界、与 loader 独立释放、alpha certificate 相同、错误/Encoded 回退、不同目录和路径去重。Vita 核心交叉编译通过。尚未以这些结果替代实机性能验收。

安装后检查：

1. 启动自检通过，保持 333MHz、后台下载暂停。
2. 进入 SHUF00002；查看 `[fixed-mask-cache] chapter=... directory=...` 和 `prepared`，确认名单、实际保留张数、skipped 与实际内存。初次章节/脚本载入会集中承担预备耗时。
3. 复现人物切换及放射线切入的 wipe_13 转场，检查 `[fixed-mask-cache] hit` 的 `copy_us`、`read_bytes=0`、`png_decode=0`，并核对 provider 上传及整帧长帧。
4. 重复同一转场、隐藏/恢复文字框，再播放并退出 OP。GPU 回收后仍应从固定解码结果交付；画面、灰度和渐变效果必须保持正确。
5. 对比普通背景/人物的 Pixels/Encoded 命中及长帧，防止只改善遮罩却挤掉了更有价值的背景缓存。快进、换章节、返回选游戏菜单不能挂住。

`[image-cache-budget]` 的 ready 和分项已包含固定遮罩；新增 `fixed_mask_decoded` / `fixed_mask_proof` 是其中的子集，不能重复加到总量。旧版日志缺少这些字段时不推测为零。

## 2026-09-11 安装记录

- 最终核心提交 `f970c6e`，主仓实现提交 `141094d`（此前 `8cc08f3`）。
- 冻结目录：`build/direct-candidates/fixed-masks-f970c6e`。
- VPK SHA-256：`17731c7975c710e4bd9b6c43e429df9149ec26aeac2b6614243a69152005de28`。
- SELF SHA-256：`5016bdbf12439047fb583b4867aa1af7da188ce63b484b85adffb4c28a99e624`。
- 遮罩按清单中的物理完整路径读取，不追加扩展名探测。普通 surface loader 原有候选顺序不变。
- 与上一版 PNG metadata 包的宿主源码快照逐文件比较，只有 `host/files.c` / `host/files.h` 改变；shader 和 GPU 实现无改动。
- 部署记录 `build/direct-deploy/deploy-20260911-071117/manifest.json`：旧文件备份完成，两个新文件临时及正式读取校验成功，launch 返回 Launched。kill 返回 cannot kill，不能将此记录写成成功终止过应用。
- 启动日志 `build/hardware-logs/20260911-071254-current/host.log`，SHA-256 `0722b00a2d05e7b8b4fbffcb13236f1d902280b5dd3ebc03d4a3ecfcea21d90f`：5 项 certificate 通过，shared max_delta=0，retained/local_base/overlay 启用，333MHz。
- 本次启动抓取尚未出现 fixed-mask 预载记录，已请用户进入 SHUF00002 复现人物＋放射线、文字框切换。启动验证不等于遮罩预载或性能验收。
