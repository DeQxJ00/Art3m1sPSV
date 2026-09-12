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

## 实机首轮与完成批次修正

23:58重试部署成功（deploy-20260912-235850），开启重叠标记并自然进入标题。
333MHz、RGB/alpha自检0、樱花正常，但80.447秒采样只有14.593FPS。
日志 `build/startup-watch/overlap-regression-sample-host.log`：上传中位36.438ms，
setup约23.415ms、复制12.589ms；该版不得作为性能改善交付。

代码复核发现：videoYuvaPending在renderer begin完成转换后没有清除，下一次
convert的开头wait误等了后来提交的显示工作，导致复制不能再与显示重叠。
改为记录GPU完成批次；仅完成批次未变化时才在覆盖私有平面前提前等待。
普通帧仍在写RGBA前等显示读者，在draw/CPU读取/释放前等转换，安全边界不变。
early_waits计数用于确认自然播放不再重复提前等待；连续两次不draw的转换仍必须等待。

修正版探针会话2147cf3d-d5f9-4b67-99e4-b0d0a5e86c21，屏幕最大误差1，均值
0.0166015625，超1像素0；DONE后宿主仍为已知退出异常，不扩大验收结论。
记录 `build/overlap-yuva-epoch/probe-pixels.json`。

候选 `build/direct-candidates/ogv-yuva-overlap-epoch/art3m1s_direct.vpk`，SELF
`70ec18d221bd79f6fb23cb03410c0f16112f6c1a8dcd8e91b358cc8bb5e1bec8`。
部署记录 `build/direct-deploy/deploy-20260913-000413/manifest.json`，正式SELF读回一致。
本次prepare显式复制gpu.cpp；复制前确认原canonical与build mirror规范化内容相同，
仅新增完成批次计数，没有引入其他渲染或shader变化。

修正版333MHz稳定采样80.516秒：1433帧，17.798FPS；中位上传23.930ms，
setup17us、复制13.726ms、前次显示等待9.597ms，所有稳定窗口early_waits=0。
证据 `build/startup-watch/overlap-epoch-steady-host.log` 及 `.consumer.json`。
相比原同步约16.7FPS仅约6.5%提升；不能称为大幅改善。
首次启动及持续日志没有出现同阶段长停顿，仍未复现用户录像的故障。

当前日志另显示标题group-composite为alpha=1、opaque=1、RGB=1、灰度/反色0、
mask=0、完整960x540裁剪。每帧仍走一次copy/composite，目标切换约15.4ms。
原样保留这段强制不透明组的语义是后续优化约束；不能直接删除组导致透明度改变。
