# Extended Memory 配置核验（2026-09-10）

当前 Direct 已请求扩展内存，不需要再添加同一开关。

- `host-direct/CMakeLists.txt` 和实际构建副本均设置 `VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12"`。
- 当前 gates-v2 VPK 与实机 `ux0:/app/ART3DIR01/sce_sys/param.sfo` 均解析出 `ATTRIBUTE2=12`、`ATTRIBUTE=32768`。
- 两份 SFO SHA256 均为 `3573329f6392213b7901909701ba92b5a10eb84ca27ae16087bb14278ec07cd7`。
- `_newlib_heap_size_user` 仍为 192MiB；它是程序堆上限，与应用扩展内存请求不同。程序代码、线程栈、直接分配的 GPU/媒体内存也需要空间。
- VitaSDK 的 SELF `MEMSIZE` 参数描述为 system app memory budget，不应当作普通游戏的 SFO 扩展内存开关直接套用。

只读检查先通过 VitaCompanion version 健康检查，再下载 SFO 和系统数据库副本；没有修改实机数据库、重启、重新部署或更改缓存大小。数据库中 ART3DIR01 的 key=435879887 值为 `0xc00008000`，与两个 SFO 属性拼合一致，但本次未独立验证该 key 的语义，因此不作为运行时内存额度证据。

证据：`build/hardware-logs/20260910-083710-memory-config/manifest.json`。系统数据库原件仅保留在未跟踪的本地证据目录，不提交。

本次未测量进程实际可分配上限/最大连续空闲块，不能仅凭配置声称进程有某个确定大小的空闲内存。扩展内存增加可用容量，不直接降低逐帧绘制或上传耗时；后续继续通过资源账本完成预算、回收和连续分配失败保护。不要因此直接恢复曾导致 OOM 的 96MiB ready 缓存。

参考：
- [Electry/nfshp_vita 配置](https://github.com/Electry/nfshp_vita/blob/master/CMakeLists.txt)：注释列出 ATTRIBUTE2 的 4/8/12 扩展档位，12 对应 +109MiB。
- [VitaSDK vita.cmake](https://github.com/vitasdk/vita-toolchain/blob/master/cmake_toolchain/vita.cmake)：MEMSIZE 与 UNSAFE 参数定义。


## 2026-09-11 堆“上限”实际含义核对

用户询问能否继续增大堆。当前编译ELF cache96-5975e77的_init_vita_heap位于0x814ee8d4；读取_newlib_heap_size_user后，在0x814ee922将整块大小作为r2传给sceKernelAllocMemBlock（type0x0c20d060），并记录base+size作为sbrk边界。与[VitaSDK newlib sbrk.c](https://github.com/vitasdk/newlib/blob/vita/newlib/libc/sys/vita/sbrk.c)一致：它是启动整块申请的heap容量，不仅是一个未来按需增长时检查的数字。反汇编副本build/direct-candidates/cache96-5975e77/heap-init-disassembly.txt。

因此192MiB可以修改，但例如224MiB会在启动多申请32MiB，必须核对堆外系统分配余量及媒体/场景实际峰值。扩大heap不扩充CDRAM，也不自动扩大图片保留预算。045003-current最大newlib used采样145318944字节（约138.6MiB），尚无证据本轮触及192MiB；这不是瞬时峰值或碎片不会失败的证明。本轮保持192MiB，仅按用户要求把撤回窗口后的共享图片预算提高到96MiB；没有声称224MiB已实测可用。
