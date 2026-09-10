# 人物切入卡顿：2026-09-10

实机当前仍是 prepared-opacity-bb7a545，用户报告平移 5x 帧、人物出现明显卡顿。本轮没有重启或替换正在测试的应用。

只读日志 100434（SHA256 `0718f2980acd4e79c3bd03c8c67a025ddfa2f4850b1aa00424f052a1a370e5c1`）显示两类问题：

- `kun_z2a0100.png` 命中压缩源缓存后，1020×1008 重新解码 77.058ms，上传约51.5ms，其中透明范围扫描25.088ms，准备表导入仅15μs；`flu_z2a0900.png` 684×879 解码63.279ms，上传各阶段合计约21.8ms。准备透明表不等于保留解码像素。
- 同一人物图在主线程重复整文件读取，kun 每次约72ms，部分还等待文件锁18–27ms。对应部分长帧159/183/170ms。读取计时本身未记录调用栈，不能据此断言全部都来自同一个调用者。

代码核对发现 `FfiCallbacks::load_png_comments` 在每次脚本查询时读取整张PNG再解析tEXt，绕过纹理缓存。这是可确定的冗余图像数据I/O路径；先修该路径，无需扩大RGBA缓存或改变异步队列。

core `58a283e` 改为读取签名、块头及tEXt内容，按块长度跳过IDAT等数据；保留原来的Latin-1转换、重复键后者覆盖、IEND停止及IDAT后注释支持。没有新增持久缓存，也不缓存读取失败，不改变PNG图片解码、shader或GPU同步。超过20ms的注释查询单独记录文件总字节/实际读取字节，以便实机归因。

只读原始 `SHUF00002/root.pfs.000` 验证，未向游戏目录解压：

| 文件 | 原整文件读取字节 | 新读取字节 | 注释内容 |
|---|---:|---:|---|
| image/fg/kun/z2/kun_z2a0100.png |134194|118|一致|
| image/fg/flu/z2/flu_z2a0900.png |147785|116|一致|
| image/fg/kun/z2/a0006.png |10119|97|一致|
| image/bg/zbg04a.png |655408|136|一致，空注释|

核心413项测试通过，14项忽略，包含跨1MiB IDAT前后注释、重复键、Latin-1、坏长度、短读和零长度块。本地字节量比较不能换算成实机耗时收益；小范围读取仍有文件查询/锁/seek成本。新修正尚未部署验证。

下一次实机验收：同一人物反复换表情，再切另一人物；核对头像/身体位置、透明边缘、转场，比较重复整文件读取是否消失及逻辑长帧。首次显示仍需单独检查ready像素被降级后的重新解码；维持16MiB额度，不恢复已撤回的异步重解码/等待协议。

证据：[日志节选](evidence/character-load-20260910/load-excerpt.txt)、[原始资源查询对照](evidence/character-load-20260910/png-comment-audit.txt)。

## 安装与待验收

补充真实RGBA32夹具（此前实机解码对照使用的618×597、SHA256 `02f586768855a3c33cd2d6bbafaf71d38c1464f6b97ca5aa5337bb16f43b917e`）查询：270794字节降至98字节，1项注释内容一致。Vita核心、host clean构建通过。

2026-09-10 10:14已备份后部署候选 `png-comments-3a57966`：host 1b82b00/core 3a57966（含注释修正58a283e和A1显式音频记账）。SELF读回SHA256 `9b545a976cd7692b18a8be7c1d0abad2619ba1a4367e3b048f7c231da84f142a`，VPK `e84ba4b9406200e1a9e54414e6c7fa3d3f5039c0cecf1424c58c2887a02c3f74`。SFO维持原Extended Memory配置；shader、GPU同步、缓存额度和异步等待协议未改。stable latest未更新。

启动日志101555确认333MHz、账本v3/11 owners、透明表/保留合成/局部底图与覆盖层自检通过。2组完整账本快照faults=0，仅启动阶段，尚不能代表音频生命周期验收。已请求用户重走人物换表情／出场和背景切换；实际耗时收益待日志验证。

[候选](evidence/character-load-20260910/candidate.json)、[部署](evidence/character-load-20260910/deployment.json)、[启动](evidence/character-load-20260910/startup.log)。

## 实机否定了仅减少读取字节的收益假设

10:19用户完成测试，日志SHA256 `32f1f2b3eab95051bdff520779aa8d9a4352a073393740084a957df2afed9ae2`。kun人物注释虽仅读取118字节，仍需74.6–116.9ms；小头像/表情约54–81ms。单个8字节读取锁内也需5–8ms，另有18–43ms锁等待。此次按块读取没有达成预期，部分小文件比整读更慢；不能把读取量减少当成性能提升。

core 9fd98a3补充每个FfiCallbacks实例独立的LRU注释缓存，只存解析结果；最多256项、字符串capacity总和256KiB，容器额外开销另计。已解析空注释也可命中；I/O失败不插入。路径先解析再查缓存，脚本写文件/文件操作清空，重新构造游戏回调不继承旧结果。缓存命中直接返回克隆结果，无文件大小查询/读盘；前4次及每64次命中输出确认日志。首次查询小于等于32KiB的文件整读一次，大文件512字节预读以合并邻接块头/注释，继续跳过大段IDAT。

真实PAL8人物首次查询12次减到5次，10KiB表情10次减到1次；PAL8和RGBA32解析结果均与旧完整文件解析一致。416项测试通过，覆盖IEND前后、Latin-1、读短/错误、零长块、1MiB图像数据跨越、LRU保留/淘汰/内容限额、空注释命中和写操作失效。首次查询仍可能等待存储，需要实机验证，不宣称首次出场不再卡。

本轮45组完整账本快照faults=0，最后audio=35008字节、media=0，未退出到菜单，仍不能当作音频全部释放验收。最后连续三个300帧窗口max约17.2ms、over20ms=0；切入窗口仍有730.7ms长帧，大背景压缩源读取约417ms+解码308ms，预载保留/消费策略仍是后续重点。

[本轮日志](evidence/character-load-20260910/ranged-gameplay-host.log)、[注释采样](evidence/character-load-20260910/ranged-comment-samples.json)、[账本](evidence/character-load-20260910/ranged-gameplay-ledger.json)、[合并读取对照](evidence/character-load-20260910/buffered-audit.txt)。
