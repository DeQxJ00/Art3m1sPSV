# OGV 私有平面 NEON 复制实验：撤回

2026-09-13，实机 333 MHz / GPU 111 MHz，在 otomeriron 标题樱花循环中，
对照同一个 SELF 的复制开关。两轮均自然进入标题，使用设备当前资源；
没有修改图片透明度、OGV、shader、核心库或 GPU 同步规则。

候选 SELF SHA256：
`2f25b529b6565f5952b3e54ad79f2bd82f301425adf02cb8f6253ac4283e99ba`。
开关 `video-yuva-neon-copy.on` 在进程首次转换时读取，切换后重启。
开启时，每轮四次 16 字节 NEON load/store，共复制 64 字节；
关闭时仍调用原来的 `sceClibMemcpy`。这不是解码器或颜色转换 shader 的改动。

## 结果

每轮取前八个完整视频消费窗口，剔除第一个启动窗口，比较其余七个窗口：

| 模式 | 完成视频帧 | 采样时长 | 实际消费帧率 | 上传耗时中位数 |
| --- | ---: | ---: | ---: | ---: |
| NEON 开 | 562 | 35.166 秒 | 15.98 FPS | 30.617 ms |
| 原复制 | 629 | 35.290 秒 | 17.82 FPS | 23.895 ms |

NEON 版退步约 10.34%。完整日志的复制耗时中位数约 30.05 ms / 13.86 ms；
GPU 等待也随执行重叠变化，因此结论采用完整消费窗口，而非复制单项。
主循环整帧日志同样显示开启后约 62.5 ms，支持退步判断。
尚未通过硬件计数器确定 NEON 写入较慢的底层原因，不归因为已确认的缓存机制。

两轮实机 YUVA 像素自检 RGB 最大误差 0、Alpha 最大误差 0。
325 组复制长度、偏移、源数据不变和目标边界测试通过。
MCP 探针截图 CPU / GXM 最大误差 1、超 1 像素数 0；退出时仍出现
此前非 NEON 探针也有的 Vita3K 0xC0000005，故不报告完整退出生命周期通过。
两次自然启动成功也不代表此前偶发标题加载挂起已修复。

## 收尾与证据

撤回六个实验源码文件的变更；实机与 MCP 的本次 NEON 标记均已移除。
实机保留同一候选包，已重启且日志确认 `neon=0`，实际走原复制方式，
避免为删除已关闭的实验分支再次打断用户测试。既有 YUVA 重叠优化保留。

- `build/ogv-neon-copy/on-host.log`、`off-host.log`：两轮完整日志。
- `build/ogv-neon-copy/comparison.json`：相同时长窗口对照。
- `build/ogv-neon-copy/probe-pixels.json`：MCP 像素对照。
- `build/ogv-neon-copy/experiment-tracked.patch`、`experiment-source/`：撤回源码存档。
- `build/ogv-neon-copy/experiment-sources.json`：撤回前逐文件 SHA256。
- `build/direct-deploy/deploy-20260913-004914/manifest.json`：正式 SELF 部署读回校验。

这些 build 文件是本机实验材料，不属于可发布源码。后续主线构建直接使用已恢复的
原复制实现；不把这次 NEON 实验作为性能优化发布。
