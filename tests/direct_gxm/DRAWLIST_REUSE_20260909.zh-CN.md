# optL 崩溃后的 CPU 绘制缓冲复用候选

本轮针对实机 OOM 的直接触发点减少分配，尚未证明实机崩溃根因已消除。原现场见 `HARDWARE_CRASH_20260909.zh-CN.md`：DrawCommand 向量扩容申请 84 KiB 失败，不能据此认定 shader、Lua 或纹理泄漏。

## 修改与边界

GXM 在确认转场截图已经就绪后，取走上一帧 CPU DrawList，清空 commands、command_keys、mask_commands、shader_groups 的内容并保留容量，构建下一帧。清空会释放旧命令持有的字符串、shader 参数等值；不会累积旧绘制内容。转场截图尚未就绪的路径继续提交完整旧帧，不提前清空。

Direct 的上一轮提交已经把几何及 uniform 写入宿主 GPU 缓冲，因此复用的是 CPU 列表，不是尚在使用的 GPU 缓冲。GPU 对象文件与 optL 逐字节相同，现有安全等待、显示三缓冲和 shader 保持原样。桌面 GL 默认仍从空列表构建。

这会去掉同等规模场景每帧的向量扩容/释放及双份列表同时存活的峰值。更复杂场景仍可正常扩容；它不构成对所有内存不足情况的兜底，也未更改 Lua GC 或扩大纹理缓存。

包名为 `art3m1s-direct-01.02-optL-reuse.vpk`，源码基底是 optL 的 `d8cf0f6` 独立工作树，仅加 CPU DrawList 复用。没有把主分支后续 optM–optR CPU 优化混入候选。构建产物、准确 ELF、源代码差异和哈希见 `build/01.02-optL-reuse/`。

## 诊断发现

临时分配来源探针使用原 optK 核心和 optL 宿主。在 Vita3K 上运行约 542 秒，newlib used 峰值 53,917,032 字节，末尾 51,711,208 字节，末尾 arena 63,574,016 字节。多次出现超过 1 MiB 的回落。静止页面主要增长来源解析为 `mlua::memory::allocator`；这与 Lua 垃圾回收周期相符，不能把短期增长直接当作泄漏。

该探针会拖慢分配，已从交付包中移除；原始代码和日志只保留在 `build/heap-audit/`。交付候选只保留每 5 秒一次的 mallinfo 统计，不改变分配策略。`keepcost` 不是最大空闲块，日志明确注明。

实机 192.168.1.50 本轮连接尝试的 FTP/命令端口都超时，未向实机部署候选。因此模拟器观察不能解释完实机 19 分钟后的 OOM，也不能替代实机稳定性/帧率验证。

## 检查

- 新回归覆盖长/短/空文字、隐藏根、命令标识、效果组、旧 mask 清空，240 次重建逐命令与全新构建相同，CPU commands 地址保持不变。
- 另一个回归逐帧比较淡入淡出与规则转场，清除转场后无旧 overlay 残留。
- 桌面 release 微基准：200 图层、4000 次重建，全新列表 26,582 ns/次、缓冲地址变化 4000 次；复用 12,929 ns/次、变化 0 次。仅代表该 CPU 微基准，不能换算成实机帧率。
- 主工作树完整测试脚本在 PF8 既有 Windows 路径分隔符测试处失败，此前 966 项通过；随后单独补跑 pfs-upk，6 项通过。候选 optL 源码 `cargo test --all-targets` 344 项通过。
- `cargo check --all-features --lib` 通过；完整 all-features 因仓库缺失 `emote_parity_probe.rs` 失败。`cargo fmt --check` 仍报告现有多文件格式差异，未进行无关全库格式化。
- Vita 交叉编译和 VPK 打包通过。GPU object SHA256：`0e59ea67933c205ce0d5f4eafeff0e67c1af8951871971ced9afc1b2a88938b7`；shader SHA256：`f3b4739b5c1a8aa8f906fe12fbcb28345a04b2cf6f695a212146da36767dc7e6`，与原 optL 相同。

模拟器首次候选会话 `cd5e9910-8ee3-4efb-923e-a5b615c0f464` 正常到达标题及正文，进入正文的转场截图已保存。第一次正文 Circle 加连续截图时 Windows 进程退出，退出码 `0xC0000409`。本轮旧核心诊断对照 `b986871b-a244-46e5-81b8-0c756fc6a3f9` 也出现同样退出。两份 Windows dump 都解析为 `vk::DeviceLostError`，栈在 `vk::Queue::submit → VKContext::stop_recording → cmd_handle_sync_surface_data`。这是本次模拟器宿主的异常证据，不能与先前实机 Rust OOM 混为一谈，也不能由此保证新代码完全无关；该轮仍计为失败。分析保存于 `build/heap-audit/*emulator-crash.txt`。

同包重试会话 `d40aff9b-83aa-4db4-8f1b-221c3d61a33d` 保持模拟器设置不变，只在输入完成后单次截图。成功完成启动、进入正文和 5 次 Circle，显示两行长文字。运行超过 264 秒时仍正常，最后统计 273 quads / 22 draws，heap used 51,267,200 字节、arena 59,351,040 字节。随后主动 shutdown；这属于功能冒烟检查，并非 20 分钟稳定性或实机性能验收。截图和日志为 `build/heap-audit/retry-reuse-*.png`、`reuse-emulator-retry.log`。

## 实机验收仍待完成

保持 333 MHz，在之前崩溃的文字页停留至少 20–30 分钟，期间再切换长短文字及多人页面，回收本次 host.log。需比较堆的长期上下界、换句长帧及是否再次异常；若重现，保存对应 psp2dmp，用本包 ELF 解析，不使用旧版符号。
