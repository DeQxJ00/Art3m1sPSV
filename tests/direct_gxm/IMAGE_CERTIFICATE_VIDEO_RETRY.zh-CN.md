# 首用透明边界预备及 OP 硬解显存让位

## 实测问题

放射线/人物进入阶段证据见 BOUND_FIRST_USE_PRIORITY.zh-CN.md：像素命中但 kun 首次上传 bounds 扫描约27ms；GPU初次分配/复制仍约32ms，不是完成GPU驻留的缓存命中。

用户随后报告旧包MP4 OP持续低帧。只读日志 build/hardware-logs/20260911-040705-current/host.log（SHA256 3340f1b3d82d4dacc147c9c2e27b4bad0aa3f66ca3d92a6af1ced94f53d2e2d2）确认仍运行 bound-first-use 80MiB包，shared自检通过。logo.mp4使用h264_vita/NV12-direct，约150帧/5秒。239.96秒开启movie/kf5nz92d.mp4时，codec_mb申请6291456字节、type=0x09408060失败0x80024309；NV12-direct和硬件RGBA均失败，退回软件h264。后续每5秒约73–80帧，decode约39–44ms/视频帧，prepare约9ms、upload约9ms。CDRAM类型由本地VitaSDK psp2common/kernel/sysmem.h确认。游戏普通RAM仍有空闲不能证明这块CDRAM可分配。

失败前账本Direct CDRAM占用81264640字节，纹理50855936、固定19922944、离屏10485760；未覆盖FFmpeg解码器/驱动所有分配，无法由此推算当时CDRAM最大连续空闲块。已检查host_video_close和FFmpeg buffers_free：都有现成关闭路径，尚无证据证明前段Logo泄漏。直接原因是硬件工作区申请失败，空间不足与碎片的细分仍待实测；不归咎于shader或断言80MiB普通RAM额度直接吃掉CDRAM。

## A：不可变像素检查结果（root d6385b5 / core 184130a）

现有TileProof前加32字节带版本/尺寸的头部：非零alpha边界、整图是否alpha=255。原分块flags不变。在原后台CPU预载的检查回调中计算边界；随对应Pixels/Encoded转移和记账。C++上传/共享surface seal核验尺寸、长度、边界范围、flag一致性后复用；没有匹配证明则沿用原扫描。Rust provider不再对同一有证书资源重扫整图不透明度。host整图opaque的尺寸限制保持原样，裁剪过滤边界和shader不变。没有GPU worker调用，没有变更CPU预载像素的分配类型。

仍只覆盖原TileProof门槛width>=960、height>=540的资源。每份多32字节，包含在现有预算内。CPU工作被提前，不代表消失；后台worker耗时需复测。没有证明的wipe_13首次解码仍走原整理路径。GPU首次分配/复制、转场遮罩像素预载、分帧上传预热及后台直接解码到共享GPU内存仍未实现，不能称“全部共用一份预载surface”。

新增版本解析/回退测试240组（含稀疏线、半透明、单边像素、异常证书、尺寸不符及旧格式），ASan+UBSan通过；440核心测试通过，14忽略。实机启动CPU探针比较生成证书与原边界/不透明度，失败关闭新证书路径；17x9共享surface绘制比较使用证书。编译日志image-certificate-{test,vita,host}.log。

独立保留未部署候选build/direct-candidates/image-certificate-184130a：VPK80bc2338f0b234bec4031bdd0336e4f884815a0520568e755aabe9186a5aea63，eboot3dd6e5feaaaaacbdfd56b8b09c8a8898c90c4c265cce61c6a00f37078c9923f2。

## B：硬解失败后的有界重试（root 46ab69e / core fb0244f）

硬解首次失败后先释放该次decoder/AVFrame/NV12池，在下一次硬解尝试前通过Direct专有弱hook请求释放闲置GPU纹理。bridge要求在GXM scene外，并等待已提交GPU读者完成。core仅挑选上次retain确认闲置的cacheable条目，LRU次序，释放目标16MiB（以纹理分配估算，最后一项可能超过目标）；resolve/显式上传/更新重新固定条目。当前场景、固色及动态对象不作回收。已有CPU像素只转到原decoded层；共享surface不复制回CPU；Encoded备份不删除。重新计算同一共享预算idle账本，不改ready额度/worker/等待取消协议。

释放出估算GPU空间才额外重试同一硬解模式一次，此后仍走原NV12→硬件RGBA→软件回退；不无限重试，也不保证碎片环境下一定能分配。hook没有实现的其他host沿原路径。日志video-memory-retry记录实际系统free CDRAM前后值及查询返回码，video-cache-reclaim记录纹理数与估算释放字节；free CDRAM仍不是最大连续块。可能代价是OP之后旧闲置图片需要从保留CPU/Encoded重建，应一并复测。

442核心测试通过、14忽略，包括固定当前/刚resolve资源、零请求/目标边界、共享GPU释放不回读、压缩备份命中、CPU降级账本等。两轮Vita Release及host编译成功，源文件/新ELF与SELF标记、VPK内容验证通过；WSL报告约0.023秒时间差后已核对新产物。编译用shaders.hpp、builtin_shader.hpp与旧包sources.zip字节一致；canonical副本仅CRLF差异。

组合候选build/direct-candidates/video-cache-fb0244f：VPK b5287c0417c7a1f7a8130d6a175d695044bddef6c38c634c61423e5144be24d0，eboot34d8fc7465594c23bf68a15709222de9e3631e867f779a39da1e1b7e6a9ae81a，core archive1d54b961c994146d5ce9b1f46a2098d5b4bb1a6b2e9c469abb62fd1757a27739。

## 实机验收要求

先关闭性能浮窗通过启动自检；再保持333MHz，按原流程进入人物+放射效果，核对prepared_alpha=1、bounds耗时和首段连续慢帧；继续到同一OP，必须看到h264_vita首帧成功和实际播放速率，而不能只看avcodec_open成功或启动菜单。最后播放结束返回剧情、换人物/快进，检查资源重新载入及画面完整。当前仍待实机验证，未把编译或单元测试当作性能修复结论。

部署deploy-20260911-041707完成健康检查、旧SELF/日志备份、上传读回校验、launch。kill返回cannot kill app，随后安装校验及新程序启动成功；旧SELF确认为a3cb8ae7...，新SELF34d8fc74...。启动日志041848-current（SHA256 c15d04129eefade74ffd142ddf01a8249887ab4e34a2f0f06f1f54a823f96b3a）5个image-certificate CPU用例均ok=1，17x9 seal prepared_alpha=1，shared max_delta=0/ok=1，retained/local_base/overlay通过，333MHz。用户可重新开启性能浮窗，仍需实际人物/放射及OP流程复测；没有远程发送游戏按键。
