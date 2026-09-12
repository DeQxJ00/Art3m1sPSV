# OGV 颜色转换与主线程重叠实验

映射队列同包对照约16.72/16.70FPS，没有显著收益。下一实验保留shader/core，
只调整转换完成的等待位置。默认关闭，视频打开时存在 `video-yuva-overlap.on` 才开启。

## 边界

- 实验模式使用私有复制平面，解码队列释放后GPU仍可安全读取复制结果。
- 第一帧与映射槽始终同步；复用输出时可在EndScene后登记gpuPending，返回处理脚本。
- renderer begin/update/destroy/readback仍会等待；共享视频CPU视图也显式wait。
- 再次转换前，即使期间没有draw，也先等上一转换完成再覆盖平面或描述符。
- 尺寸改变、关闭、错误路径保留等待，不放宽输入或输出资源的释放边界。
- 旧GPU时间日志在deferred_frames>0时仅是提交耗时，不能解释成已完成GPU耗时。
  帧率结论必须使用完整主循环/显示及持续消费计数，不能由上传函数变快单独推断。

## 验证

- 独立GXM探针：36轮混合映射/复制输入，复制输入重复提交且中间不draw；
  立即改写调用者存储，随后begin完成同步，退出前另测提交后立即release。
- MCP先通过health检查，会话 `e47b232c-64e9-4f81-bec0-2c85b9e706b3`。
  最终复制重叠帧左右屏幕最大误差1，均值0.0166015625，超1像素0。
  证据 `build/overlap-yuva/probe-pixels.json`、`build/native-five-v125/overlap-final.png`。
  CPU读回仍为模拟器不可靠结果，未用于验收；DONE后宿主退出0xC0000005，
  与之前的非重叠探针一致，因此不报告完整退出生命周期通过。
- 主程序/SELF/VPK编译通过，core SHA256仍为
  `ed72c5e7f2b3fa295e84943cb7847d6edfeb26338c22d3d540f20f64cc129265`。
- 候选 `build/direct-candidates/ogv-yuva-overlap/art3m1s_direct.vpk`，
  SELF SHA256 `8ac3e7f81e1d8f636251c49bd6d896503d50e2dbe9b5f8322c4419a34de6cdf9`。
  目前无实机收益结论，尚未启用此实验。

23:54部署先完成旧文件备份，kill提示无法结束应用，FTP写临时SELF返回550，
没有替换正式文件。记录 `build/direct-deploy/deploy-20260912-235457/manifest.json`。
随后观察到旧包重新载入游戏，已询问是否用户重开，避免重复控制。
已删除本轮映射队列A/B临时关闭标记，恢复默认选择；没有创建重叠实验开启标记。
