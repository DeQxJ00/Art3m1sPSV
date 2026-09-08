# 帧首等待探针

目的：为原生 PCSG01084 / PCSG01127 / PCSG01201 的帧首 Finish 位置准备可验证候选，让下一帧的 CPU 准备工作有机会与上一帧 GPU 执行重叠。不是删除等待，也不以模拟器 FPS 宣称实机加速。

`DIRECT_DEFERRED_FINISH_PROBE` 控制实验路径，默认游戏构建不启用。独立 `direct_probe` 可显式启用；后续 optL 游戏候选通过 `DIRECT_DEFERRED_FINISH_CANDIDATE` 显式接入同一保护代码。即使编入实验路径，运行时也默认帧尾等待，必须调用 `set_deferred_finish(true)`。不允许在打开的场景内切换。

候选保护点：

- 下一次 begin 重置单个顶点 arena 前。
- CPU 原位改写纹理前。
- 非 active 时释放纹理或导入包装对象前；调用者可能紧接着释放外部 AVFrame。
- active 内销毁先退役，下一次实际 Finish 后统一回收。
- CPU 截图/转场读回前；显示队列提交不等于像素完成。
- 显式等待、关闭候选、进程退出；退出仍排空显示队列。

调用者必须继续在场景外释放外部视频资源。当前游戏的 media pump 位于 begin 之前。不能在 active 场景里删掉外部包装对象后立即释放它的外部存储；导入对象不拥有该存储。

## 延迟消费测试

在 Linux / WSL 设置 `VITASDK` 后运行：

```sh
python3 scripts/test-deferred-finish.py
```

测试直接 include 生产 `gpu.cpp`，使用真实 VitaSDK 声明，以主机内存构造资源，替换底层 GXM/OS 调用。模拟 GPU 保存提交时的数据快照，到 Finish 才再次读取实际顶点/纹理；内存释放也检查是否仍被在途任务引用。ASan/UBSan 同时启用。不是重写一份同步状态机再测试它。

覆盖 902 帧、3604 次延迟读取：连续顶点覆盖、原位更新、batch 尚未提交时退役、非 active 销毁、外部纹理存储立即释放、截图读取、模式切换、Queue 失败、BeginScene 失败及退出排空。四份负向对照分别删去 Begin/Update/Destroy/Readback 等待，必须在运行时断言失败；编译失败或无关崩溃不算通过。

局限：不模拟 GXM 光栅、硬件 cache 一致性、显示 DMA 或 FFmpeg 解码线程；不能由此证明实机完全安全。

## Vita 像素探针

```sh
cmake -S host-direct -B build/direct-sync-probe -DCMAKE_BUILD_TYPE=Release \
  -DDIRECT_GXM_PROBE=ON -DDIRECT_DEFERRED_FINISH_PROBE=ON
cmake --build build/direct-sync-probe --target direct_probe.vpk-vpk --parallel 4
```

探针 APPID 为 ART3DPR01，只写自己的测试结果目录。先提交 360 帧纹理改写/导入/销毁压力场景，再显示原有像素参考图，包含批量文字、索引边界、裁剪、规则渐变、混合、透明边界和读回缩放。Cross 退出。`DEFERRED_WAIT` 分站点记录等待次数与总耗时；`gxm-perf.finish_avg_us=0` 仅指帧尾，没有表示 GPU 不再等待。

2026-09-09 证据位于 `build/deferred-finish/`：

- `mutation-results.json`：完整保护路径 ASan/UBSan 通过，四份缺失保护对照按预期失败。
- `pixel-results.json`：56 项通过。与已有 no-MSAA 参考作对应区域/解析值比较，旋转 NanoVG scissor 的旧参考单元仍按既有规则排除，另外检查 Direct 的轴对齐裁剪。
- `default-parity.json`：新帧首路径与本轮原帧尾路径完整截图逐像素相同。
- `emulator-result.log`：压力场景完成，批量/索引边界/三种读回缩放通过，资源与 GPU/显示排空均到达；不将 Vita3K 的 CPU 颜色内存当成真实像素 oracle。
- 默认 optK 游戏宿主重编译后 `gpu.cpp.obj` SHA256 仍为 `01075f1feff40f257b61d4f00d323bfa5ebc72e25580f38ea9cbf3d518a19afd`，与本轮前逐字节相同。shader 源码及字节码没有修改。

MCP 每次新运行先查 session_status：新路径 `627f1f2b-4b41-4c0b-92d2-5ccf544e3455`，默认路径 `0b812f41-3ecb-4e7f-825f-a3b7eca51dab`。两者正常显示参考图，Cross 后均记录 clean_exit，但随后宿主模拟器在 `MainWindow::on_game_closed` / Vulkan pipeline cache 保存之后发生 `0xC0000005`，读取地址均为 `0x484004410`。因此 **不能将进程退出检查标绿**，也不能将该异常归因于帧首等待；两种路径均可复现。没有修改模拟器设置来掩盖异常。

以上是独立探针阶段的证据，不能替代游戏与实机测试。后续 optL 已接入游戏候选，在初始化游戏时检查 `gxm-deferred-finish.off`，随后每秒轮询；文件存在为帧尾等待，不存在为受保护的帧首等待。退出当前游戏时先排空，再把选择菜单恢复为帧尾等待。`[gxm-wait]` 与宿主窗口对齐统计所有等待位置，`profile-direct-ab.py --mode waits` 可执行临时开/关/开并恢复原控制文件。游戏与实机的后续证据见 `baselines/01.02/OPTIMIZATION.zh-CN.md`，不能用本探针结果宣称实机性能提升。
