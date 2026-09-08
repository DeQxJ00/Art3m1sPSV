# 12 秒放射线效果录像：PNG 动画缓存

2026-09-08，录像 `Z:/Record/OBS/2026-09-07 23-57-33.mp4`，12.083 秒、1280×720、60 FPS 录制。抽帧覆盖 0–11.5 秒，叠加层持续显示约 5 FPS；白色放射线在对话期间持续出现。录制帧率不能视为游戏帧率。具体 VPK 版本尚待用户回复，不能仅用文件名推定。

原包 `root.pfs.000` 的 `image/anime/line2.ipt` 定义 `anime_full`，960×540、每帧 100 ms、三帧循环 `line21/22/23`。`line21.png` 的白色放射线形状与录像相符。这是 PNG 图像序列路径，与前轮 Theora OGV 箭头不同。IPT 和两张用于比对的图片只提取在 `build/recording-review-235733`，没有修改运行目录或游戏脚本。

已证实的代码问题：Scene 保留当前图层绑定的文件，GXM provider 的旧 retain 立即删除不在当前集合的纹理。三张图循环时，上一张一离开就被删除，下一圈再次读包、PNG 解码和上传。该行为与录像低帧高度相关，但没有此版本实机分项日志，不能量化它占总帧时的比例或排除其他瓶颈。

修复：GXM provider 保留近期未使用的来源图片，按最近访问顺序淘汰；非活动缓存预算为 16 MiB，计入 CPU 向量容量及按宽度对齐估算的 GPU RGBA 存储。当前场景和转场保留集合优先保护，不受非活动预算驱逐。视频、文本和捕获等直接上传目标沿用显式生命周期，不进入近期图片缓存。退出游戏销毁 provider 时释放全部纹理。

预算是非活动缓存的估算上限，不是整个应用或 GPU 分配器的精确内存上限。三张 960×540 RGBA 的 CPU＋GPU 估算共约 11.87 MiB，其中当前帧属活动资源，两张闲置帧约 7.91 MiB。未采用无限保留所有场景资源的策略。

回归使用生产 GXM provider、真实 PNG 编解码和模拟宿主上传：三图轮换 300 次，旧零闲置缓存策略读取 300 次，新策略读取及上传各 3 次，纹理 ID 稳定。另测最近访问淘汰、活动资源保护、零预算释放及动态目标及时回收。core 库测试 325 通过、4 忽略；日志 `build/texture-cache-tests.log`。Vita core 日志 `build/texture-cache-core-build.log`，宿主日志 `build/texture-cache-host-build.log`。

新增 `GXM texture-cache hits/misses/evictions/inactive_est_bytes/budget` 日志。实机验收先复现 T01 同一白色放射线对话：首轮之后 miss 不应每次换图持续增长，效果继续按原 100 ms 帧时间播放，再比较总 FPS 和内存。尚未获得新版本实机运行结果，不宣称已恢复到 60 FPS。完整场景 GPU 离屏缓存、首次图片解码停顿和其他效果仍需分别验证。

Vita core 与最终 VPK 构建成功（宿主仍有 WSL 时钟偏差警告）。`build/gxm-host/art3m1s_gxm.vpk` SHA256：`9BE3E8D202B7DBBAEA6FF6F55A7BFE9E794479DE9AC9984E297236FF57F4E5BC`。包含前轮视频与菜单字体生命周期修改。
