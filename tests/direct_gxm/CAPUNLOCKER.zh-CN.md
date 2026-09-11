# 可选 CapUnlocker 第四核支持

基于 `v1.2.2`。用户自行安装插件，程序及部署脚本不安装插件、不修改 taiHEN 配置、不自动重启系统。

[CapUnlocker 官方源码](https://github.com/GrapheneCt/CapUnlocker/blob/master/main.c) 解除线程核心掩码的限制，不负责为应用分配工作线程。VitaSDK 中 CPU3 对应 `SCE_KERNEL_CPU_MASK_SYSTEM=0x80000`；常规三个用户核心为 `0x70000`。

## 调度范围

- `archive-loader`：打开游戏资源包的后台线程。
- `surface-loader`：现有单个 PNG／图片预载、读取、解码线程，仍为原优先级 180。没有增加图片解码并发数或改动队列、缓存、锁顺序。
- `theora`：现有 OGV/Theora 软件解码线程。MP4 硬件解码路径不改变。
- `theora-codec`：FFmpeg 内部的两条 Theora 帧解码线程。通过 `get_buffer2` 在线程实际执行时应用一次调度设置，然后原样调用 `avcodec_default_get_buffer2`；不改变分配器、像素格式、线程数或帧同步。也覆盖仍在主线程组织播放的循环／含音轨 Theora 所用的内部解码线程。
- 主逻辑／GXM 提交、音频准备、音频解码／混音／输出及日志线程保持原设置。

后台线程启动时读取自己的核心掩码，并加入 `0x80000`；原掩码为默认值 0 时申请 `0xf0000`。已有非零自定义掩码保留，只加入 CPU3，不把工作强制绑定到 CPU3。调度器仍决定实际使用哪个核心，不能据此承诺恒定四核占用或帧率提高。

线程局部标记通过 pthread key 实现，同一线程只申请一次，避免每帧系统调用；检测到主线程时立即返回，单线程解码回调也不会意外修改主线程。所有 `[cpu3-...]` 信息已加入宿主日志白名单。

`sceKernelChangeThreadCpuAffinityMask` 返回后再次读取实际掩码，只有调用成功且结果等于请求才记录 `enabled=1`。拒绝请求时保持原值；静默裁剪或无法确认时尝试恢复原值，并记录恢复结果。没有 CapUnlocker 的设备仍能运行。

启动时额外创建一个立即退出的能力检查线程，对相同接口做一次检测，不更改主线程掩码，也不运行压力测试；工作线程仍逐一核对。`cpu3-policy` 表示是否允许尝试，`cpu3-worker enabled=1` 才表示系统实际接受请求。

## 配置与日志

构建选项 `DIRECT_CAPUNLOCKER=ON`，需要带 `art3m1s_register_worker_init_callback` 的核心（本次 `f29007a`）；默认 OFF 兼容仓库旧核心基线。

如需同包对照，在 `ux0:data/art3m1s-gxm/` 创建空文件 `cpu3.off`，重新启动应用；删掉并重新启动则恢复自动尝试。文件只在启动时检查，不在每帧或音频线程访问。状态读取失败也不启用。关闭表示不修改线程，不强行覆盖用户其他线程插件的设置。

`[cpu3-worker]` 记录角色、线程 ID、原／请求／实际掩码、各接口返回值、当前／上次核心、优先级；掩码代表可运行范围，`cpu=3` 或 `last_cpu=3` 才证明采样时确实运行过 CPU3。Theora 的原有 `[thread-perf]` 也会持续记录核心。

## 验证

- 456 项核心测试通过，14 项原有忽略项；Vita 核心已编译。
- 7 项原生 C 策略测试通过：默认掩码、自定义掩码、插件缺失拒绝、静默裁剪、读取失败、回读失败恢复、已包含 CPU3。
- 实机待核对：启动检查、后台工作线程的实际掩码及拒绝时回退。插件安装生效后，进入 SHUF00002，测试切背景／人物、OGV 放射效果和 OP。保持 333 MHz，可以开左上角性能浮窗。支持验收与实际性能提升应分别判断。

测试包 `build/direct-candidates/cpu3-codec-acad9bf/art3m1s_direct.vpk`：宿主 `acad9bf31ce8bfc3d56364a0ba47f084b63b2e4d`，核心 `f29007a0c87152828d83eb5281b7fac59d434432`；VPK SHA256 `944b7840ffc13b4bd99cfff510d1d6a9921f4bad262593dd0e7108530c938078`，SELF SHA256 `e96f8f855182c664bc0aecd55d84a811579a0e5397fa5dab61eb2e641b2ad267`。包含内部 Theora 帧线程补齐及 CPU3 日志白名单。之前的 `cpu3-5160b19` 仅包含外层线程，且 CPU3 信息被旧日志过滤器隐藏，不能作为最终支持验证包。

最终包已部署并完成 SELF/SFO 回读校验，VitaCompanion 返回 Killed/Launched；记录在 `build/direct-deploy/deploy-20260911-093310/manifest.json`。随后抓取日志时命令端口超时，未取得最终包的启动或 CPU3 接受结果。用户表示插件可能需重启生效，等待其自行重启并恢复连接；没有操作插件配置或发送系统重启命令。TODO 保留实机验证待完成状态。

## 恢复连接后的实机证据

2026-09-11 安装字号设置前备份了最终 CPU3 包的日志：`build/direct-deploy/deploy-20260911-100016/host.log`，SHA256 `bacb2712defeaaece3348f644a5170d563435a51ebe8ee2bd15e1bd4112273ec`；旧 SELF 哈希对应上述最终 CPU3 包。333MHz，启动渲染自检通过。归档加载、surface-loader、Theora 外层与两条内部帧解码线程均记录 requested=f0000、after=f0000、enabled=1。第 1382 行 Theora 外层性能采样为 cpu=3、last_cpu=3；第 2672 行内部 theora-codec 也为 cpu=3、last_cpu=3，因此已有实际在 CPU3 运行的证据。以上确认调度支持，不作为帧率改善的同场景对比结论。
