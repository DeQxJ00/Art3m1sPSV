# OGV GPU 映射输入队列候选

2026-09-12，基于 `3c0a345`。保留独立启动观察线程、当前 core、全部 shader，
不改输出等待或 GPU 完成边界。已部署并完成333MHz开/关对照，帧率没有显著收益。

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

初次尝试时前台/日志状态不明确，暂未部署；随后日志恢复增长且投影为樱花标题。
部署记录 `build/direct-deploy/deploy-20260912-234121/manifest.json`，SELF读回一致。
实机自检14336像素，RGB/alpha最大误差均0，三个映射槽均通过。

| 同包标题采样 | 完成上传fps | 中位上传us | 前次GPU等待us | 输入复制us |
|---|---:|---:|---:|---:|
| 映射开，50.173秒 | 16.722 | 35069 | 23322 | 1 |
| 映射关，80.455秒 | 16.705 | 35124 | 9269.5 | 14091 |

日志 `build/startup-watch/mapped-steady-host.log` 和 `mapped-off-entry-host.log`，
对应 `.consumer.json` 记录实际消费计数。CPU333MHz，显示均约17FPS。
关闭前后的总上传时间相同：省下的复制时间被更长的GPU等待抵消，不能报告为帧率优化。
投影观察到开映射时CPU占用略低，但不是长时间同负载CPU统计结论。
两次均自然进入标题；没有复现录像中的持续卡住，不能认为卡住已修复。
