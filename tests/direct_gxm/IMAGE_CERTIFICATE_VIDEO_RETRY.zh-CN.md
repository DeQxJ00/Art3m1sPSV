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

## 04:22 放射进入复测未通过，暴露准入丢失备份

用户到点后只读日志042201-current，SHA256 c95e1fc14eaa8c6cb970b5558b7892f8cff29b551cf59ec8d1600040b6eea18d。85个预算快照、44个完整账本均无错误/faults；ready峰值71846196，账本heap峰值116430452、CDRAM89653248。无video-memory-retry/video-cache-reclaim事件，此时尚未验收OP，不是OP回收删除了这些备份。

边界预备在实际命中的资源上生效，例如2629行tor 984x993上传bounds9us、opacity13us、prepared_alpha=1；多处1920x1080 bounds约13–15us，共享重解码也有prepared_alpha=1。目标kun及line却在177秒发布时因已有CG/人物占用先降成Encoded，预算约70.3MB中用约68.4MB；后续小图逐步填满ready。

2516行ready71351060/limit71353161，仅余2101字节；新lth/no的65064字节备份准入后ready71322243，差值精确对应淘汰line21的93881字节。随后zev_lth_01d需要690678字节，即使淘汰剩下Encoded也不足；实现先删旧Encoded，最后放弃新结果，ready从71322243降到70821013（删除501230字节）。结合worker唯一旧Encoded回收路径，解释line22/23、kun备份消失及221秒前台重新读取；具体单条删除未独立打日志，不能将差值视作逐项观测。

221.756504秒frame688106us（logic105752、present582288），kun读72869、decode73095、publish135568us；publish内bounds41047、opacity25147、pack69242、prepared_alpha=0。line21首次读取/解码，wipe13也读取80023、decode50639、publish24516us；随后line22/23分别产生180637/132907us帧。后面四个完整5秒窗口均300帧，max<=17.577ms，decoded/uploads/builds0。与此前325ms样本不是相同命中状态，不能判定多32字节证书本身导致这次退化；本轮目标点的整体效果未通过。

需要修正准入操作：先判定候选是否能完整入缓存，再执行旧备份淘汰；同时评估后来的推测任务是否应挤掉仍绑定的早期备份，避免只保护Pixels而损失Encoded与证书。优先级保护并非完整前后窗口，早绑定CG仍可能长期占用像素额度。此次仅定位和记录，未改安装包；保留现场继续OP测试。


## OP确认与88MiB准入修正候选

用户确认OP正常。只读日志042606-current，SHA256 fbb2da16c5077688afb3f17b0506d41dc90eb0b81f524a287e419856eb43a310：5139行h264_vita首帧NV12-direct成功，5141行播放movie/kf5nz92d.mp4；两个视频窗口decode2692/2764us。没有video-memory-retry/reclaim，说明本次直接硬解成功，不能当作内存不足恢复分支验收。

用户要求再扩大缓存。core 11a8f59将ready+idle共享保留额80提升到88MiB（92274688），pending请求ready_goal=76895574、余15379114字节供idle使用；idle最大32MiB、单解码16MiB、压缩备份及GPU存活规则均不变。当前192MiB程序堆上限不变。042606日志最大采样heap used137073656，arena161288192/free24214536；不是连续可分配空间或峰值保证。旧96MiB独立ready试验发生过OOM，当前共享和可恢复解码已不同，仍仅按8MiB一档验证，不声称所有场景安全。

修正推测准入：后到结果放不下时只降级/放弃新结果，不先淘汰已绑定的旧Encoded备份及alpha证书。前台demand仍沿原有优先处理；线程/排队/等待/取消协议不变。可能代价是后排推测任务未保留、以后仍需读盘；早绑定但较远的CG仍可能占据像素。不是完整消费距离预测，也不是GPU预上传完成。

443项核心回归通过、14忽略（cache88-test.log）；新增用例覆盖后排大结果最终拒收时旧Encoded与证书仍能取用且不重解码，已有前台需求/取消回归通过。Vita及host编译通过（cache88-vita.log、cache88-host.log）。容量与准入一起改变，后续实机结果只评价组合包，不将全部收益单独归因于容量。shader/host源码不改。等待冻结、部署及同流程首次人物/放射→OGV/OP→返回剧情验证。
