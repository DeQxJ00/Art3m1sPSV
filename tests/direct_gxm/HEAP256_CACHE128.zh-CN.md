# 256MiB堆与128MiB共享缓存试验

2026-09-11，用户要求整体内存直接申请256MiB，并同比扩大缓存。此前建议的104/32对照只完成核心测试，未编译、部署；本轮依用户追加要求改为组合试验。

相对已安装的104/40包：host-direct启动newlib堆192→256MiB（268435456字节）；核心ready+idle总保留额104→128MiB（134217728字节）；闲置纹理最大40→32MiB（33554432字节）。后台pending时既有5/6策略给ready请求111848107字节，剩余22369621字节，非固定双向队列。无pending时idle可用到32MiB，但仍受总额度限制。

idle不随堆增长：上一轮纹理已有6MiB从CDRAM分配失败后回退到USER_RW_UNCACHE；增加newlib堆不增加CDRAM，反而占用更多堆外普通内存。保留32MiB闲置上限，新增总额主要服务后台解码保留。活跃纹理、codec、线程栈和临时空间另计；128MiB不是应用总占用上限。

只改上述三个容量常量。shader、GPU同步、333MHz、队列64、压缩备份、单张解码上限16MiB及等待/取消策略不变。v1.20和v1.20-core标签、原始VPK保留。本轮同时改变三项，不能单独归因堆或总缓存。

核心a47c8a5：443项测试通过、14忽略（build/heap256-cache128-test.log），Vita核心编译通过（build/heap256-cache128-vita.log）。host源副本与canonical核验通过，编译日志build/heap256-cache128-host.log。待冻结和实机启动核验。

实机验收：先关性能浮窗以避免覆盖启动像素自检，安装后确认alpha/shared/retained/local_base/overlay自检与256MiB堆；通过后再打开浮窗。保持333MHz，SHUF00002人物+放射首次切入、重走一次、继续OP硬解并返回剧情及短快进。采样缓存总额、heap实际used/arena、CDRAM/uncached、ledger faults、分配失败与长帧；特别检查新增堆是否挤压媒体和堆外分配。启动或媒体失败时保留日志并恢复已备份包，不将启动成功等同于所有场景安全或帧率改善。

## 候选核验

候选build/direct-candidates/heap256-cache128-a47c8a5，主实现08c9434、核心a47c8a5。VPK SHA256 cf19cd08aa2101d8c1334fabb71e7ed09b8baae667c4e349330813ec56c32b30；SELF c002f8a5f626f28d1812be163d52826ac3b8788efe8b2a5954f2112023329720；core archive 1a9eac512a0f077207977837681da8c3682f3d500ad038a2a56be4f584fbec87。

host编译通过，有既有GNU-stack/约0.035秒WSL时差警告。新ELF/SELF/VPK CRC与嵌入eboot、SFO哈希核验通过；按ELF符号表定位_newlib_heap_size_user对应数据，读取为268435456。两个候选的sources.zip逐文件比较，唯一变更是host-direct/src/main.cpp，shader及其余host源码字节一致（heap-source-verification.json）。

安装前两次VitaCompanion version健康检查连接超时；未执行停止/上传/启动，仍等待用户关闭浮窗及实机连接恢复。不能据此认定256MiB申请失败：新包尚未在实机运行。
