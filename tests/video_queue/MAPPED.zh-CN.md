# OGV GPU 映射输入队列候选

2026-09-12，基于 `3c0a345`。保留独立启动观察线程、当前 core、全部 shader，
不改输出等待或 GPU 完成边界。**候选尚未部署，尚无实机帧率结论。**

## 数据与生命周期

- 主线程创建3个 GPU 映射内存块，沿用渲染器 CDRAM 优先、主内存回退的分配与账本。
  960×540×4×3 有效载荷6220800字节，实际按分配器256KiB对齐。
- 队列支持外部存储，拒绝空指针、溢出范围与重叠槽；不重复记账或释放这些块。
- 解码线程直接填预留槽；主线程借用已提交槽，转换程序直接读取同一地址。
  三个平面槽都通过实际像素自检后才启用；只识别当前池的精确地址及匹配尺寸。
- `sceGxmFinish` 后才释放消费借用。停止时先 stop、join、销毁队列，再释放映射池。
  GPU 输入描述符每次重建，关闭后的旧地址不会自动沿用。
- 映射池分配失败时保留原 CPU 队列与 GXM 暂存复制；同一队列也兼容 RGBA 回退帧。
- `video-yuva-queue.off` 存在时仅关闭映射队列，在视频打开时读取。
  原 `video-yuva.off` 控制整体 GPU 颜色转换，两者不能混淆。

旧版平面复制约13ms，但与前次显示等待已有重叠。删掉复制不代表必然加速，
GPU可见内存的生产者写入成本也可能抵消收益；必须实测，不根据代码推断帧率。

## 已完成验证

- `tests/video_queue/run.sh`：ASan/UBSan，20×10000帧；交替使用自有/外部存储，
  检查未提交帧、借用期间覆盖、EOF、丢帧、stop/join、范围拒绝与不释放外部内存。
- `tests/video_async/run.sh`：真实双路 Theora，包括映射槽模拟、分配拒绝回退、
  8次在生产者填槽期间关闭再打开、循环与最终关闭。日志
  `build/mapped-yuva/async-tests-final.log`，退出码0。桌面映射模拟不证明GPU一致性。
- Vita GXM 独立探针：35次混合输入更新，轮换3槽及普通复制，保留 deferred display。
  最后一次确认为映射槽2；屏幕左右参考最大误差1、均值0.01953125、超1像素0。
  MCP会话 `3f452dc7-7593-42e6-8e2c-38b07bcecb8a`；
  `build/mapped-yuva/probe-pixels.json`、`probe-mcp.log` 和
  `build/native-five-v125/mapped-yuva-final-screen.png`。
- 模拟器 CPU 直接读取 GPU 结果仍返回无效内容，未拿它当像素验收。
  探针输出 DONE 后 Vita3K 宿主退出异常0xC0000005，旧非映射探针亦有同样问题；
  因此只报告屏幕对照通过，不报告探针完整生命周期通过。
- SELF/VPK编译与CRC通过，core hash仍为
  `ed72c5e7f2b3fa295e84943cb7847d6edfeb26338c22d3d540f20f64cc129265`。

## 候选与待验证

`build/direct-candidates/ogv-yuva-mapped-queue/art3m1s_direct.vpk`

- VPK SHA256 `a31aaa435f8b954b8ec4ddf64cf8e90c60d21309867aaf6e0f7de97226c8d6c7`
- SELF SHA256 `741d7ef737525b92e41adfdba839f732720a617ef9636ba4a67af1d63cd37c11`
- 匹配 ELF、源码 hash 清单保存在同目录。
- 构建：`python tests/startup_watch/prepare.py --output build/mapped-yuva --current-video-gpu`，
  然后 `wsl bash build/mapped-yuva/build.sh`。不覆盖既有性能包或构建镜像。

实机最后可用游戏日志仍属于上一轮观察包，止于樱花约16.6fps；本轮读取时没有增长。
投影显示黑底、约48/50MB，与该游戏会话不匹配，已询问用户是否切到其他应用。
VitaCompanion/FTP可连接，不因此判断程序退出或崩溃；暂未替换或重启。

下一步：确认前台应用，备份后部署；333MHz下自然进入标题，检查RGB/alpha自检、
mapped=1、mapped_frames、解码吞吐、实际完成上传帧数、CPU/内存占用。
至少对比映射开/关两轮，检查循环、退出/重进和标题前间歇性卡住。
只将真实完成上传/显示计数用于FPS结论，不能使用生产者提交数替代。
