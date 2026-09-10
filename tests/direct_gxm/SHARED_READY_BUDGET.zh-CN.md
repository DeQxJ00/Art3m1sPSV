# 预载与闲置纹理共用保留额度试验

起点：host f1977b3、core 6d88e04（NEON shared surface），保留shader、333MHz、压缩备份、GPU等待和既有loader ticket/取消协议。用户要求继续解决“提前解码过，首次使用却只命中压缩数据”。

## 已确认的根因

20260911-020928-current日志中，kun_z2a0100在约141秒已有`payload=pixels`、4247090字节预载结果，约198秒使用时变为`prefetch-encoded-hit`。loader的独立16MiB预算按ticket降级旧像素，渲染provider的32MiB闲置预算未能借给预载。最近GPU扫描优化没有解决该问题。

## 本轮改动（core 0f90fed）

- 把原16MiB ready +32MiB inactive的保留额度组合成48MiB共享账本。loader发布结果时只可保留`48MiB - 已登记闲置资源`；provider retain只可保留`min(32MiB,48MiB - ready资源)`。不能同时花同一份空闲额度。
- loader仍在CPU内解码，实际使用时沿用既有主线程上传，随后释放CPU重复像素并保留单份shared surface。这不是提前把所有预载都发布为GPU surface，也不是已完成整个进程的物理内存准入管理。
- 保留单次解码16MiB上限、压缩回退及全部队列/等待/取消协议。没有预算不足时的worker等待，没有自动重解码或新worker。后续资源仍按原ticket顺序竞争额度，不承诺已具备脚本下一句预测。
- 取走、解绑、shutdown释放ready额度；provider retain/Drop更新闲置额度。共享锁只串行化保留计数和清理：loader state→budget；provider持有budget期间不调用loader，不等待ticket。活跃场景不因ready增加而被淘汰。
- 48MiB是保留对象的逻辑总额；当前场景、临时解码、上传过程中短暂双份、256KiB GPU分配对齐、GPU retired块等另受已有账本/生命周期约束。不得将其表述为进程总内存48MiB或所有物理分配严格小于48MiB。
- 新增`[image-cache-budget]`以及有界`[surface-prefetch] demote`日志。目标是用真实prefetch-hit和cpu_released验证保住了解码结果，不以额度增加推断成功。

## 验证

431项通过、14忽略（build/cache-budget-test.log）。新增测试覆盖两方并发争用额度不重复花费、ready借用后首次使用仍为Pixels、take/unbind/shutdown归还、空间占满仍保留Encoded回退、cancel后迟到结果不重新收费、闲置纹理退让但活跃场景保留。旧异步取消/队列/解码回归均保持通过。Vita编译build/cache-budget-vita.log成功。

实机需同组开篇到kun人物+line21..23转场流程：检查目标图`prefetch-hit`替代`prefetch-encoded-hit`，共享total<=limit、ready cache_bytes<=即时cache_budget、资源账本无fault；比较首用长帧与后续动画，不把加载界面的首次CPU预载时间当成切换耗时。再短快进并检查OGV切入、画面完整和退出游戏。若额度变化引入长等待、崩溃或明显倒退，可恢复shared-neon-f1977b3。
