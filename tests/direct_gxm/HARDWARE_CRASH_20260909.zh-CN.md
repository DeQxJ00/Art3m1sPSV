# optL 实机崩溃：2026-09-09 15:07 取证

## 已确认的直接原因

主线程在构建 DrawList 时，扩容 `Vec<DrawCommand>` 申请内存失败，进入 Rust OOM → abort。不是从异常 PC 推测 shader 故障。

只读取实机，没有重启、kill、安装新包、修改时钟或诊断开关。
证据保存于 `build/hardware-logs/20260909-150727-companion/`：

- `host.log`：1469196 字节，SHA256 `3528EB3CC34343CF73B106859D420DA3A0A6383CE44A3206F3C6D711435F2C99`。
- `crash.psp2dmp`：1741776 字节，SHA256 `2bf633d8f3eae99559beed0ce7d6819db0d46d911ba3a686628cdcdfd3886c62`。远端原名 `psp2core-1788937556-0x000b032295-eboot.bin.psp2dmp`。
- `installed-eboot.bin`：SHA256 `8f0d3d5ce03a3dfab96b57beb4634930f63805e5a2d2a42bd8089ae415b3fb84`，与本地 `build/direct-optL/art3m1s_direct.self` 完全一致。
- `crash-parsed.json`、`stack-symbol-candidates.txt`：线程寄存器、模块段和栈中地址候选。栈扫描结果不是完整 unwind，不把所有候选都当作调用栈。

使用 [xyzz/vita-parse-core](https://github.com/xyzz/vita-parse-core) 的模块/线程记录布局读取 dump，在隔离的 build 工具目录中适配 Python 3 的字节读取，使用本地匹配 ELF 和 VitaSDK addr2line/objdump 核对。没有向网站上传 dump。

## 地址与调用证据

异常线程 `ART3DIR01`，UID `0x40010003`；stop reason `0x30002`（undefined instruction）。
运行时模块代码段起点 `0x81035000`，对应 ELF 代码段起点 `0x81000000`，必须先换算再符号化。

| 运行时地址/位置 | ELF 地址/含义 |
| --- | --- |
| PC `0x814D9586` | `0x814A4586`，`_kill_r` 内 `udf #255` |
| LR `0x814D9553` | `_kill_r` 内调用进程 ID 后的返回位置 |
| R2 = 6 | `_kill_r` 请求终止信号；结合 abort 路径判断，不能只按异常类型解释为不支持某条正常指令 |
| SP+0x0c | abort 返回地址候选 |
| SP+0x24/+0x2c/+0x44 | Rust 分配错误处理与 `rust_oom` 路径 |
| SP+0x5c = `0x81035665` | 对应 ELF 返回地址 `0x81000665`；返回前 `0x81000660` 是 DrawCommand grow 的 `handle_error` 调用 |
| SP+0x84 = `0x810C3AF1` | 对应 `0x8108EAF1`；其前 `0x8108EAEC` 从 compositor `visit` 调用 `Vec<DrawCommand>::grow_one` |

注意 `addr2line` 把部分返回地址标到了紧邻的下一函数（例如 ShaderGroup、capacity_overflow）。原始指令确认实际调用为 DrawCommand 扩容失败，不能照抄地址名称判成 ShaderGroup 无限增长或容量整数溢出。

保存的分配 layout 为对齐 16、大小 `0x15000` = 86016 字节（84 KiB）；与 256 个、每个 336 字节的 DrawCommand 一致。失败发生于正常规模的分配请求，不是申请一个异常巨大的向量。

## 尚未确定的根因

OOM 确认的是分配器没能满足请求，不等于已经证明某个缓存泄漏。需要区分长期持有、累计增长、碎片或堆状态损坏。

宿主配置 newlib heap 为 192 MiB。dump 中缺少用于读取相关 newlib 堆全局变量的内存区域，不能据此报告“剩余堆为 0”或给出虚假的 malloc 统计。

末尾日志：140 quads / 29 draws，纹理上传为 0，texture CPU 约 12.68 MiB，空闲纹理计数与淘汰次数稳定，语音活动为 0。最后 Circle 记录在约 666 秒之前，运行日志持续到约 1150 秒，因此不是刚加载 1920×1080 背景时立即崩溃。没有同轮堆用量曲线，仍不能断言每帧泄漏。

这与上一轮图片同步解码造成的长帧分开处理。当前不扩大 16 MiB 纹理预算，也不删除 GPU 安全等待；扩大内存持有可能使问题恶化。新 CPU 候选尚未安装，不能归咎于未运行的 optQ/optR 修改。

后续优先补齐堆已用/空闲/最大空闲块、绘制/文字/事件队列与缓存数量的时间序列；在同页面长时间停留复现增长，再定位对应持有方。必须先修稳定性，再继续判断性能收益。此次已保存现场，尚未完成泄漏定位或生成修复安装包。
