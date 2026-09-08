# PSV host.log 空文件修复

2026-09-08：Borealis Logger 在 `__PSV__` 分支仅使用 sceClibPrintf，忽略 setLogOutput 的 FILE，导致应用创建 host.log 后仍为空。这不是用户复制操作的问题，也不只是缺少 fflush。

修改 PSV Logger：保留调试输出，同时向显式配置的文件写入无 ANSI 色码日志并 fflush。主程序打开文件后直接写入带构建时间的启动标记，启用 Logger 并发互斥，并注册 FFmpeg 回调，将音频、视频性能消息写入同一 FILE；线程性能消息改经 FFmpeg 回调。每条写入均刷新。原有直接 printf/sceClibPrintf 的其他非 Logger 消息未全部重定向，不声称捕获所有调试控制台输出。

`tests/file_logging/run.sh` 编译生产 Logger 的 PSV 分支，令 sceClibPrintf 无输出，验证写入后 fstat 文件大小立即非零、内容正确且无色码、debug 过滤保持有效。测试通过，日志 `build/file-logging-tests.log`。此测试不替代实体机文件系统验收。

实机先启动到菜单，host.log 应立即包含 `[host] file logging initialized; build ...`。进入游戏复现 20 秒以上后复制 `ux0:data/art3m1s-gxm/host.log`，其中应有 `[frame-perf]`、`[draw-perf]`、`[main-thread]`；媒体活跃时增加 `[audio-detail]`、`[audio-perf]`、`[video-async]`、`[thread-perf]` 等内容。重启程序仍会覆盖当前日志，复制后再重启。
