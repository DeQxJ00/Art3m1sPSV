# 游戏加载阶段的 PS 键保护

从启动游戏选择页确认游戏时获取 PS 键锁，覆盖资源归档读取、引擎初始化和首帧准备。首个包含游戏绘制内容的帧结束后，一次性排空显示队列，再解锁；不能只根据 `phase=4` 或 `game loaded` 日志解锁，因为这时可能还没有游戏图像。

使用 VitaSDK 的 `sceShellUtilInitEvents(0)`、`sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN)` 和对应 Unlock，链接 `SceShellSvc_stub`。依据：[VitaSDK 接口定义](https://docs.vitasdk.org/shellutil_8h.html)、[VitaShell 的耗时操作锁定用法](https://github.com/TheOfficialFloW/VitaShell/blob/master/utils.c)、[VitaShell 初始化用法](https://github.com/TheOfficialFloW/VitaShell/blob/master/init.c)。不拦截普通游戏按键、不调整电源菜单或内核插件。

生命周期：

- 只在创建 Game 时尝试一次加锁，不重复叠加锁；初始化或加锁失败记录错误，继续原来的加载流程。
- 加载报错画面、游戏对象销毁会请求解锁，销毁时在等待加载线程退出之前先尝试释放。
- 请求解锁失败保留锁的所有权，每秒重试；对象析构再做兜底。不会每帧反复等待显示队列。
- 如果加载主循环仍正常运行，但 120 秒都没有产生游戏绘制内容，会自动放开 PS 键并记录 timeout。此回退不声称能从阻塞的主线程或系统崩溃中恢复。
- 正常游戏开始后不再获取锁，也没有额外的逐帧显示等待；shader、核心、图像缓存、音频及 CPU3 策略均未修改。

`[loading-ps]` 日志包括 init、lock、first-game-frame、load-failed、game-destroy、timeout 等原因、返回码和时间戳。成功值为非负数；失败不会被记录为成功。

## 验证

宿主生命周期测试已通过：锁只获取一次、首次游戏帧后释放且不重复解锁、加锁失败不解锁别人的锁、解锁失败限频重试、120 秒回退、析构释放。Vita VPK 编译通过，核心仍为 `d015d02`。没有重跑未改动的核心测试，也没有把模拟 API 测试视为实机 PS 键验证。

实机测试：

1. 在游戏选择页按 PS，仍应能正常切出。
2. 回到应用，选择 SHUF00002；读取进度条期间按 PS，应留在应用内。
3. 第一幅游戏画面出来后再按 PS，应正常返回系统。
4. 重新进入应用并正常游玩，确认 PS、声音和画面无异常。日志应有一对成功 lock／first-game-frame；若加载失败则应是 lock／load-failed。

不修改已安装游戏资源来人为制造失败。错误路径的系统行为仍需后续遇到实际失败时结合日志验证。
