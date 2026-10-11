# 恢复版 E-mote 宿主接入

本次把 `emote-psv-test` 的快照接到实际 Rust 场景和 Direct GXM 渲染器。
它是可关闭的候选后端，不替换默认后端，也不声称全部原版播放器功能已恢复。

## 已接的路径

1. `emote-recovery` feature 提供 backend 2。`DIRECT_RECOVERED_PACKET=ON` 的宿主显式选择它；选择失败直接报错，不能退回旧求值器冒充成功。
2. 每个实例使用恢复端独立 timeline/control/view。脚本命令、查询、帧推进和 capture 走 C facade；输出保存在 `Arc<RecoveredPacket>`，按原位置进入普通场景与转场 DrawList。
3. `model_view_texture_host` 将 BC3 linear/swizzled 或 RGBA8 上传源交给宿主。未上传源由恢复端独立持有，PSB/view 销毁不使待上传快照失效；ready 后释放 CPU 源副本。
4. BeginScene 前 prepare；scene 中新上传返回 deferred，不解压 BC3 作为替代，也不在上传重试时推进姿态。纹理使用独立、单调、不复用的 token，和 Rust texture ID 无关。
5. packet/view 的异线程 Drop 只标记退休，owner 在线程安全注册表中收集。最终纹理 release 进入 `direct::destroy`，由现有 GPU 等待/退休路径释放。游戏退出先销毁 core，再清理恢复注册表，最后才退出 GPU。
6. 原 GXP/材质/显式遮罩命令在宿主唯一 GXM context 上提交。为当前父色目标临时附加独立 depth/stencil；软遮罩切换私有目标后回到原父目标。退出恢复宿主 shader、纹理绑定、viewport、裁剪和缓存状态，保留组嵌套层数。
7. 父 affine 应用于完整顶点；精确 clip 使用私有 stencil 高位，硬遮罩占低七位。非 identity affine 的软遮罩暂用全目标 ROI，避免把原画布外移入的图元裁掉；未降低网格或分辨率。

失败的 capture 保留一个失败快照占位，prepare/replay/不支持的效果会标记该帧不完整；不通过静默省略角色来掩盖错误。含恢复快照的帧暂绕过普通 quad/retained group 缓存。

## 构建

先运行 `scripts/build-recovered-core.ps1`，再在 WSL 运行：

```sh
bash scripts/build-recovered-host.sh /mnt/f/WorkSpaceAI2/art3m1s-psv-gxm/build/recovered-core/libart3m1s_core.a ON
```

恢复源码默认来自相邻 `../emote-psv-test`，也可显式设置 `EMOTE_RECOVERY_ROOT`。
脚本固定 core archive 副本、核验确有恢复后端符号引用，并对编译前后的宿主/core/恢复源码做 hash 比对。
结果在 `build/recovered-host/manifest.json`；默认后端对照在 `build/recovered-host-off/manifest.json`。
这是本地候选构建，不发布 release/tag。

候选固定 mesh=100%，保持完整 8×8，不修改用户保存的网格设置。
恢复端的 demo 文件日志被禁用，采用宿主原有诊断系统，不另开 demo writer 或 GXM context。

## 限制

- 固定 960×544。模型公开投影目前支持 scale、x/y；非零 pivot、angle、z 明确拒绝。
- Step/Skip 未恢复；mesh ratio 只支持 1。未知材质 provenance 保持 profile0，不能因文件名相似自动套 PSV profile1。
- 父 affine 和 clip 已接；逐命令 opacity≠1、继承 shader/color filter、非普通 blend 的尚未支持组合明确失败。外层组有自己的合成路径，不能靠逐网格乘 alpha 冒充整角色透明度。
- 非 identity affine 的软遮罩全目标 ROI、保守 GPU 等待和新快照复制仍需在完整游戏负载中测性能。旧 demo 三人 59.940 FPS 不属于本集成后端。
- 真实语音 CSV 时钟、完整 controller/join/其他材质版本仍在恢复项目待办中。

## 验证记录

- 恢复端：真实 sample01 linear/swizzled 与两个 PSV 样本的上传源保活/延迟/取消/永久失败测试；旧五组完整绘制字节对照、79 模型 30,031 姿态、多实例、C/C++ ABI 回归通过。
- 目标 CPU：真实资源桥 ASan/UBSan；真实 sample01 facade 两实例、变量/动作、view 销毁后 prepare、跨线程退休和下个 Game 重建；106 个 GPU API 故障注入点；Rust 快照/clip 测试及旧 renderer 63 项通过。
- 完整 ON/OFF ARM ELF 均成功链接；编译通过不能代替实机验收。
- 实机 444/222 的初次有效 13 阶段探针完整退出、26 次 scene 内资源退休完成。硬遮罩跨主屏/一层/两层父目标（含 affine/clip）全帧逐字节一致；不透明内部、clip 外和错误恢复 marker 全部通过。
- 该探针的透明软遮罩边界在 depth0 和 depth1 间仍有 RGB 最大 1/255 差异（identity 1329 像素、affine 607 像素），depth1 与 depth2 完全相同。原逐字节验收失败记录保留在 `build/recovered-packet-device/device-5b0a3449`，不能改写成通过。

独立探针前三次未进 main 的原因已通过 crash PC/LR 定位到遗漏 `-Wl,-q`，构造器表未正确重定位；目标 CMake 原本包含该参数。SELF/数学库/强弱导入的早期猜测不作为根因结论。

### 21 阶段补充实机验收

完整证据见 `tests/emote_recovery/host-device-verification.json`。实机 CPU 444 / GPU 222 下，真实宿主回调完成 40 次延迟上传、40 次就绪和 40 次场景内退休；最终引用归零，正常退出。测试后已读回校验并恢复原 eboot，重新启动原程序。

BC3 linear 与预先 swizzled 源的全帧 RGBA 完全相同；linear 源由宿主重新排列压缩块后上传，并非声称使用 linear GXM 描述符。六组不透明父目标上的软遮罩比较也全部逐字节一致。

透明父分组的上述边缘差异仍保留为 `not_exact`，总结果为 `scoped_pass_transparent_groups_not_exact`；没有放宽逐像素容差。首次严格失败报告另存为 `tests/emote_recovery/initial-parent-pixel-verification.json`。

完整候选包已由 `scripts/package-recovered-host.sh` 生成：`build/recovered-host/art3m1s_direct.vpk`，SHA-256 为 `93497c292f67e57f51aa3a8e62f8de3dc8e94ae9b845f7c455343a6962d09fea`。构建与打包记录保存在 `tests/emote_recovery/host-*-verification.json`。本轮实机运行的是使用生产 GPU 和上传桥的独立探针；完整宿主游戏、UI、转场及三人性能尚未实机验收。