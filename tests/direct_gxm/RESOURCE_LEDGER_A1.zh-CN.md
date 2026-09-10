# A1 资源账本首批接入

保留当前 Rust PNG 解码器、16MiB预载预算、单worker取消/等待协议及现有 shader/GPU 同步。该版本只记录资源事件，**reserved 是观察到的计划分配字节，不是已通过总预算准入**，没有启用 A2 回收或限制。

## 已覆盖

- CPU源文件：预载在已知大小时先记录reserved，读取成功转actual capacity；前台读取回调返回后登记实际容量。
- CPU解码输出：共享解码帮助函数仍原样；包装其可失败分配回调，记录reservation和Vec实际capacity。RGBA16缩为RGBA8后仍记完整capacity。
- ready、provider保留像素、GPU降级后的CPU缓存：移动记账令牌，不复制缓冲或重复记账；取消、替换、淘汰及销毁按存储所有权释放。
- Direct内存块：CDRAM和uncached分域，按256KiB对齐后的真实申请量计费。CDRAM失败先归还该域reservation，再记录uncached尝试；映射失败和释放失败不假装内存已归还。
- GPU纹理、离屏目标和固定渲染资源分组；三个显示缓冲和命令/顶点/索引/USSE内存包含在fixed。导入的外部纹理不再次记作本模块分配。
- 仍在GPU使用的已销毁纹理标为retired；retired是live子集。现有GPU完成等待之后实际释放，才扣live和retired。没有缩短fence或提前复用。

账本为固定大小数组，无逐分配HashMap或新增线程。只在分配、转移和释放发生时更新；未变化的容量不产生事件。锁内只做整数更新，快照先解锁再写日志，不在账本锁内IO、等GPU或调用回收。现有5秒heartbeat输出三个内存域的摘要，未增加每帧文件查询。

## 不能据此声称已完成的部分

- codec内部临时分配、FFmpeg/音频及OGV显式队列、字体CPU图集、readback/遮罩临时副本、容器和GXM驱动内部开销尚未逐项接入。
- 通过借用切片上传的数据由调用方持有，账本只记录provider真正保留的副本，不把借用当作另一次分配。回调读取过程、Vec重分配期间分配器内部新旧块重叠及分配器头/对齐不计入当前CPU峰值。
- 部分固定GPU/离屏对象按现有设计保留到进程结束；离开游戏回到选游戏菜单后，不应要求所有GPU字节归零。
- `heap live`应与newlib used做差核对；CDRAM/uncached不能直接从newlib used里减去。差额需要逐类解释，目前不能称为完整进程预算。
- A2预算阈值、抢占/回收，以及B未发布最终纹理存储直接写入尚未启用。禁止依据这次部分统计扩到96MiB。

## 版本与验证

- core `0b901cfedc0cdc315bdcbf6f9da384b81e573f01`；407项测试通过，13项既有测试忽略。
- 新测试覆盖：reservation取消归还、RGBA缓冲所有权往返不重分配、跨owner不翻倍、CDRAM失败转uncached、retired子集以及错误事件不破坏计数。原有异步取消/重绑/队列测试全部通过。
- C++生命周期测试 `resource_ledger_test.cpp` 用独立事件断言验证区域回退、retire幂等、实际释放和外部导入不重复计费；g++ -Wall -Wextra -Werror通过。
- Vita release核心和VPK交叉编译通过。候选 `build/direct-candidates/resource-ledger-0b901cf/art3m1s_direct.vpk`。
- eboot SHA256 `48ddce36d3f9f7c9f06f41240bb8fcce46e8d80bb692b98bd348c0b102d9774e`；VPK SHA256 `0770f78f3c41d25d1a53ce0a472f6f8b00757679d4d85af70bb9700ee95249ee`。
- host基于实际使用的9d17ae2隔离副本加入账本。与根目录现有main的后台诊断刷新/长帧日志有既有差异，未在此候选混入；精确源文件快照、CMakeCache和哈希保存在候选目录。稳定latest指针未更新。

## 实机对账流程

1. 333MHz，SHUF00002先测试；进入背景+文字场景停10秒，再切到头像/双人场景停10秒。
2. 连续切人物/背景，然后同一路线快进约20秒并停止，检查无卡死及缺块。
3. 播放/退出OGV效果后停10秒；有语音页面等语音结束。
4. 从游戏内可用入口回到选游戏菜单，停10秒。请勿用Select退出（Select仍为Auto），也不必为此覆盖存档。
5. 核对每个snapshot的owner之和等于live、retired≤live、faults=0，停句时reserved最终回0；对比进入/退出、重复换场景的CPU/GPU占用是否收敛。

该流程用于确认生命周期记账与当前A0候选的稳定性，不是宣告帧率提升。待对账后继续补媒体等差额，再决定A2额度；实际长帧收益还要后续整条加载路径对比。

## 首次实机启动记录

已部署并读回校验eboot；param.sfo与原版一致。启动的 retained/local-base/overlay 像素自检全部通过，333/222/111MHz。菜单稳定窗口302帧，平均present 16.483ms，最大17.233ms，>20ms为0；不把启动初始化3.34秒窗口算作稳定帧率，也不据此声称游戏内收益。

首次3组完整snapshot均 faults=0、reserved=retired=0。CDRAM 31,719,424bytes，其中fixed 19,922,944、texture 1,310,720、offscreen 10,485,760；离屏包含刚执行自检所保留目标。菜单尚无游戏图像CPU存储，因此账本heap=0，新lib used约9.49MB属于当前未覆盖的菜单字体/宿主等；这已经说明账本尚非完整堆用量。

记录：[启动日志](evidence/resource-ledger-20260910/startup.log)、[对账汇总](evidence/resource-ledger-20260910/startup-summary.json)、[部署](evidence/resource-ledger-20260910/deployment.json)、[候选哈希](evidence/resource-ledger-20260910/candidate.json)。脚本 `scripts/summarize-resource-ledger.py` 校验每域owner合计、retired子集、reserved/peak、faults以及同一snapshot的事件序号。

游戏内场景切换、快进和OGV生命周期对账仍待实机操作完成。当前没有启用A2。
