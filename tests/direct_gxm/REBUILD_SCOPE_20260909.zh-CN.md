# 当前硬件候选的重建范围核对

核对对象：独立 core `982a4a6`（`build/heap-audit/message-source`），已部署宿主 `optL-message-input`。这是代码路径分析，不是新的实机性能测量。

## 已确认的路径

- `runtime/render_gxm.rs::render_current_frame` 在 `frame_visual_dirty`、首次提交、纹理 content revision 改变、转场进行中任一成立时重建。它有整份 DrawList 复用，但没有在此层按“只更新头像/等待图标”局部修补。
- 重建路径会执行 `sync_backlog_snapshot`、`build_text_commands`、`build_emote_commands`、场景遍历及 `materialize_stencil_groups`，然后重新收集纹理保留集合。MessageInput 候选仅减少第一项里的消息内容相等性比较。
- `runtime.rs::advance_logic` 将合成器时钟引起的变化、转场、emote、文字揭示/翻译变化合并为全局脏标记；`runtime/events.rs::flush_host_events` 对非空事件批次保守标脏。
- `compositor/reduce.rs::advance` 检查 tween 实际值变化和 anime 帧变化，不是只要时钟走动就标脏。`show_click_wait_icon` 对同一图标、同一可见状态及位置有返回 false 的路径；不能断言它每帧无条件标脏。
- `backend/gxm/provider.rs::upload_impl` 成功上传后递增 content revision，即使 TextureId 与尺寸未变。替换为“只比较 ID”会忽略尺寸、不透明性及内容相关依赖，不能直接采用。

## 与现有实机证据的对应边界

此前头像前后对比的稳定窗口没有纹理解码/上传、没有活动语音提示，不能把那一组稳态低帧归因于每帧视频上传。事件、tween、文字等究竟是谁持续触发重建，现有计时日志没有独立的原因计数，因此仍未实测闭合。也不能把增加的 103 个 quad 全部归到一张头像上，因为句子和场景内容同时变化。

原生持久图层对象与变化标记的地址证据见 `NATIVE_CACHE_OFFSCREEN.zh-CN.md`。它支持优先研究按变化复用 CPU 绘制数据，但不证明“所有层都离屏合成”，更不支持去掉现有 GPU 等待。

## 下一步的判定顺序

1. 完成已部署 MessageInput 的实机固定页面 ON/OFF/ON：333MHz、同句、语音结束、无输入，丢弃开关跨越窗口。先判断文字比较实际节省多少整帧时间。
2. 若 CPU 构建仍占主导，在保留当前包的独立诊断构建中汇总重建原因，以及动画变化涉及的层 ID；不要每帧打印或通过禁用动画测出虚假的性能提升。
3. 根据原因区分文字命令复用、静态图层几何/变换复用、仅纹理像素更新的列表复用。失效范围必须覆盖父层变换、透明度、遮罩、shader 组、排序、纹理尺寸与资源淘汰。
4. 对获证的路径做可切换单项优化，再验证字体揭示、头像/立绘、转场、灰度和读档。实机 FPS 验收不能用模拟器速率替代。

2026-09-09 19:09、19:11 读取的实机日志尚未出现候选游戏内测量，仍为选游戏菜单数据。等待用户进入指定页面，不把菜单 60FPS 作为游戏内目标完成证据。
