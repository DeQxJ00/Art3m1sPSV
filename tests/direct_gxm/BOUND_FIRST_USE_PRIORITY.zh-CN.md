# 待用像素准入优先级试验

用户要求修正优先级，并先参考原生eboot。以80MiB候选adb906f为基线，保留像素格式、shader、GPU保护、共享surface、压缩备份和原有单worker队列/取消协议。

## 原生只读证据

2026-09-11通过13341查询，每次先server_health确认PCSG01297/eboot.bin.elf。原始记录在build/native-five-audit/20260911-priority-{manager,xrefs,methods,refcount-asm}.json，未修改IDA数据库。

- CSurfaceManager构造0x8101DC64：+40预算、+44用量；+48按名保存已加载surface，+72反向索引，+96单独引用计数，+120另有可回收surface表，+148..160维护释放顺序队列，+168..180为加载队列。
- worker 0x8101F6CC解码成功后发布到+48/+72，+96设初始计数1，并增加+44用量。解码调用期间释放manager锁，返回后重新核对队列项，再发布。
- 真实入口0x8101E478：命中+48时增加+96引用计数；未命中则查+120，存在从闲置表恢复到已加载表的路径。
- 真实入口0x8101F1DC：递减+96计数；非零直接返回，归零时才转到+120及释放顺序队列，或在已超预算时释放并扣账。0x812375A4超预算回收循环只从+120及释放队列取对象。
- Hex-Rays把0x8101E478/0x8101F1DC等合并进相邻函数；引用计数和转移结论使用独立入口原始汇编确认，没有把合并函数开头错当入口。0x81236D58也返回合并伪代码，不据此增加语义结论。

可借鉴的是“持有引用的对象与可回收缓存分开”，未证明原生按背景文件名加权、预测下一句或前后固定N张。原生可以把仍引用资源计入当前用量，即使超过可回收预算；当前loader有硬ready保留上限，不能照搬为所有bind对象无限驻留。

## 实现 core fe4f662

给加载任务标明是否来自异步预载；重复同步bind和实际take优先于推测任务。后来的纯异步结果如无法在ready实际剩余额度中完整保留，先降级新结果的Pixels，保留其Encoded和TileProof，而不先降级已有绑定的待用Pixels。Encoded仍放不下时仅回收旧Encoded-only结果，保留其绑定记录；还不够则拒绝新结果入缓存，按原前台缺失回退。引用数、key、pending结束和notify协议不变。

同步bind或正在被take等待的结果继续原有前台优先回收路径。take/unbind/shutdown释放的额度可用于后续任务；不会为已降级结果自动安排重解码，不等待预算、不新增worker，不在worker调用GXM。总额度仍80MiB，idle侧原32MiB最大值和LRU不变；ready保护不是锁死额外内存。

这是有界的首次使用准入保护，不是完整的前后预测窗口，也未保证绑定顺序就是剧情消费顺序。风险是很早绑定却长期不使用的像素占住ready，后续真正需要的图退回Encoded甚至缺失，需要在实机同流程查看收益及代价；没有把游戏特定bg/fg/ev路径写成优先级规则。压缩回退仍保留，只有空间不足才淘汰或拒绝单项，未恢复此前整体删除压缩备份的方案。

新增有界demote日志reason=incoming-speculative、admission-declined日志；worker启动标记priority=bound-first-use。应同时检查被保住的背景和后来CG的首次使用，不能只统计背景命中。

## 验证和复测

439通过、14忽略（build/cache-priority-test.log）。新测试覆盖后来异步CG不能挤掉已有背景/人物、先回收Encoded-only但保留绑定、全占满时拒绝新结果且不自动重复解码/等待、同步需求仍能优先取得像素、在途异步任务被消费方标记需求后走前台优先。旧取消、重绑、快进队列、共享额度/资源生命周期回归均通过。Vita核心编译成功（build/cache-priority-vita.log）。

实机保持333MHz，启动前关闭性能浮窗，自检通过后可再开启。重复80MiB基线开篇到背景zbg27k、tor、kun与line21..23流程，再继续推进到后续CG并短时快进；核对背景不再因后续CG发布而降级、后续退回Encoded的范围和长帧、总额度及faults、画面和取消/读档行为。纯文字换句波动仍另行处理，不能用这一轮加载政策宣称解决。上一包cache80-adb906f保留作为回退。

## 部署和启动

候选build/direct-candidates/cache-priority-fe4f662（root c4628d1/core fe4f662），VPK SHA256 b455c3cc2061d37ebe5844be3e935e61f0c1a29e290fa02cfa9162a79a5c428a、eboot a3cb8ae7b1acc2e98f3f1fc6cbd23d2e3e40d515a7624e57ef42441863c49eaa、core archive e74ef59b0fdaebc548955c4c4b9a5c2006c9094ecc5c62b1d26c5f10f6cd3ee1。host源码与上个80MiB包相同，仅重链；build/cache-priority-host.log有约0.014秒WSL时钟差，随后源码/ELF标记、新SELF、VPK CRC和SFO检查通过。

用户确认关闭浮窗后，deploy-20260911-034210完成version健康检查、旧包及日志备份、kill、上传读回校验和launch。启动日志034310-current（SHA256 a6e057319b1e32ea93b681a74278240f714079d6d6dee3a7530456dd167f6157）shared max_delta=0/ok=1，retained/local_base/overlay通过，333MHz。尚停在选择游戏阶段，需用户游戏复测才能确认priority=bound-first-use实际worker运行、目标命中和副作用。未把启动自检当成性能验收。

## 首轮游戏复测：缓存命中仍有短暂停顿

实机日志build/hardware-logs/20260911-034643-current/host.log，SHA256 121f023ab7e1d60488aceb5aaf528bb728fb1cc740edf5cd75072934caa590c1。用户先描述卡死，随即纠正为短暂停顿后恢复，认为像读取；按长帧处理，没有重启或回退。

80个预算快照、40个完整资源账本快照均无解析/预算错误，faults=0。ready峰值76234162字节，最后ready54956790+idle17207481=72164271，低于83886080额度；账本heap峰值108847541、CDRAM峰值84148224，uncached=0。这些不包含所有未记账分配，不能据此保证无内存风险。zbg27k和tor_z2a0100本轮均命中Pixels，开篇背景保留策略已有正向证据；尚未推进到kun/line及后续CG，不能宣称完整通过。

- 最近一次212.305600秒长帧162.558ms，logic_menu63.599ms、direct_present98.888ms；下一帧91.248ms，其中present86.371ms。附近记录a0007.png预载像素命中、离屏目标分配及带facemask的非缓存组。对应5秒窗口纹理read合计44.303ms、decode1.244ms、upload4.029ms，不能把窗口累计数直接归到这一帧，也不能据此排除所有文件访问。现有证据不足以称它为“大图未缓存而重新读盘解码”，主要呈现为逻辑和绘制提交路径的长帧，离屏分配/重建需要进一步细分计时。随后三个5秒窗口各300帧、最大分别18.652/17.635/18.544ms，符合恢复运行。
- 更早196.299049秒239.838ms长帧，tor像素命中但PNG注释读取耗时68.078ms，且与BGM流读取等待重叠；首次GPU上传alloc8.442ms、copy15.457ms、bounds20.614ms，logic125.464ms、present114.275ms。像素命中没有消除元数据访问、首次上传和组重建。
- 更早169至171秒OGV阶段确有I/O等待：archive-stream-read等待424.004ms，mask file-size等待368.256ms；相应长帧862.168/731.404ms主要在media。不能与212秒事件混为同一个原因。

没有现场画面时间对齐，以上只定位日志事件，不能断言用户指出的那一下必然是哪一条。保留当前80MiB候选继续排查；下一步应分别核对PNG元数据缓存、首次上传和离屏重建开销，而不是仅凭短暂停顿增加额度或改回加载等待协议。此次只记录实测证据，未改运行代码/安装包。

## 放射线与人物进入时的实机长帧

用户触发放射线/人物后，只读获取035415-current（SHA256 e1e9db05b3e0c2d807664e0b26e8dc4685907acbadd8c2288fa3a402f15706f6）和035520-current（SHA256 88d93ecb8c6c3ab197e4fcd1f1102233ba186a5c94bbfaf3e161dc4e74bc8c74），两次均先VitaCompanion version健康检查。未按键、改开关或换包。用户确认“主要是刚出现时卡”。

最后一次进入效果at_us=654142455，frame324891us，logic68310us、present256506us。zbg27k Pixels命中仍首次上传54753us；kun_z2a0100 Pixels命中仍首次上传59584us（其中alloc9822、copy22351、bounds27260us）；line21 Pixels命中首次上传阶段约14.6ms。wipe_13命中Encoded而不是Pixels，decode54820us、publish24524us，后者内opacity23819us。上传/解码和present是嵌套区间，不能重复相加。随后line22/23首次上传约12.4/14.6ms，相邻frame65015/66793us；其间及之后还有约45–52ms帧，尚包含效果绘制/离屏预热成本，不能把全部长帧只归因于memcpy。

653.768–658.773秒混合窗口247帧，max324891us，>20ms17帧。随后663.778至748.863秒的18个完整窗口均300帧、max<=17.747ms、>20ms0；decoded/uploads0、retained每窗口hits300/builds0、composite/switch0。与用户确认一致，当前证据是进入阶段预热未完成，不是此前三帧序列不断上传/缓存槽竞争的持续低帧重现。日志证明该放射线资源是line21/22/23 PNG序列，本次不归因于OGV解码。未取得同步截图，不能据资源名独立确认当前可见人物数量。

327个共享预算快照和143个完整账本快照均无错误，faults0；本轮kun和line21..23也已Pixels命中。CDRAM峰值99352576字节，heap账本峰值仍108847541；缓存总额最后68593944/83886080。没有全部内存覆盖保证。

后续优化顺序：保留现有异步取消/等待协议和shader，先消除Pixels命中后的前台边界扫描重复工作、使转场遮罩也具备有界预备机会，再设计主渲染线程受预算控制的GPU上传预热（不能让worker直接调用GXM）；以整段进入效果的最大帧时间和连续慢帧数验收。现有CPU预载并不是已完成GPU驻留，仅加大ready容量无法消除以上步骤。此次仅检查并记录，未实施/部署新渲染改动。
