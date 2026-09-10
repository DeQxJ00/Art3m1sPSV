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
