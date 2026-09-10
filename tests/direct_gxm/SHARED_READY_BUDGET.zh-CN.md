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

## 候选部署

host/root bd0f855，core 0f90fed。build/direct-candidates/cache-pool-0f90fed/art3m1s_direct.vpk：SHA256 fdf1e6162bc37eeab29bf1106e12cfa1350af5dded0e2477b1c04bda0a3eded6；eboot 58059fb30f0b13106c8f98592e19756e0da044118e05e8f73ccc0f81a17ed560；core archive b136dabde627228ff2d220be3b2ff9855772d03ff6ffd7b60de7e70ed11f213e。host源码未改，仅重链core；WSL报0.016–0.019秒时钟差，随后新ELF内`image-cache-budget`/`shared_budget=50331648`标记、新SELF、VPK CRC与SFO均核对通过。

deploy-20260911-022344完成备份、kill、上传读回与launch。启动日志build/hardware-logs/20260911-022525-current/host.log（SHA256 817ddbbc505f8b00fda0909c664c80f09e1ca1d59655094f736f4ef5f9b3348f），shared GPU max_delta=0，retained/local_base/overlay通过，333/222/111/111MHz。此时仍为游戏选择菜单，没有游戏共享预算样本；仅完成安装和启动验证，等待用户同场景复测，不能宣称已命中目标解码结果。

## 首轮复测失败点与主动请求额度

日志build/hardware-logs/20260911-022927-current/host.log，SHA256 50c2066818e01285bda536108b8bca6b1e5fd83e483aaf293e742ca1f0c36ae0。81份共享额度快照通过，46份完整资源账本通过、faults=0；ready峰值30311852字节。但tor、kun、line21..23均仍为prefetch-encoded-hit。最后切换559380us，之前NEON版545081us，未达到目标。

新降级日志明确显示：tor和line21..23降级时idle_reserved=33458485（约31.9MiB），ready_limit=16873163（约16.1MiB）；kun降级时idle_reserved=32244973、ready_limit=18086675。上轮只允许借“已经空闲”的额度，旧闲置纹理先占满后没有让出的触发条件。不是计数错误，而是缺少跨缓存的优先级请求。

修正core 35bfe3a：异步bind确实排入新任务时提出ready_goal=40MiB（48MiB的5/6）；主线程retain按`48MiB-max(ready实际占用,ready_goal)`清理旧闲置纹理，此阶段闲置纹理上限约8MiB。worker仍只能使用已经清理出来的实际空闲量，不提前超支、不等待GPU、不新增重解码/取消协议。纯同步bind不主动提出40MiB请求。异步批次全部完成、取消、解绑至无pending或shutdown均解除请求；已完成ready结果仍按实际占用保留，使用后归还。当前场景仍排除在可淘汰对象外。

该40/8分配是本轮试验参数，不是已证实的原生配置，也不等于进程内存上限。仍没有脚本下一句预测，不能保证全部资源保住。434项测试通过、14忽略：新增测试验证请求发出后不能使用尚被GPU闲置对象占据的额度、取消释放请求、批次完成释放请求但保留Pixels，以及主线程在ready尚为0时主动回收旧缓存、保留活跃场景。Vita编译成功。下一轮查看ready_goal=41943040时idle是否让至<=8388608，目标图是否变为prefetch-hit，以及是否造成旧资源重载或帧时间倒退。

## 主动请求版本部署与启动异常记录

候选build/direct-candidates/cache-reserve-35bfe3a（root b4b1282、core 35bfe3a）。VPK SHA256 1cae727e8d7125126c207e2118edabc11ecdffa44f055924e91d90a7caabd36a；eboot ea6352a714b517f52b4a059c0f818040526cd4ee3a8912e8235e6e506f9f6470；core archive 0d70b2fcbb42a2f2040b9c552dceb1cc968904a21b0ceaf544cd627dd3f3ae19。host源码未变；重链有约0.016秒WSL时钟差，ELF新ready_goal标记、源码、SELF/VPK和SFO检查通过。deploy-20260911-023441完成备份、替换读回和启动。

第一次启动日志20260911-023553-current（SHA256 6215c1033ae6532870946ade81899fd6f4689dfbde7f58ed1da507620860a184）：五组CPU对照通过，但shared GPU对照max_delta=255、ok=0，按设计禁用了shared路径；retained/local_base/overlay均通过。这不是成功验收。缓存策略尚未进入游戏，host shader和测试源码均未改；异常原因未定位，不能直接归咎于缓存策略或断言是采集问题。

保留失败日志后，通过health→kill→launch对同一安装包重启一次。第二次日志20260911-023740-current（SHA256 d77eb7b107a9e0d007d0575c1784b7014df6f493a17e1369bf8cc9cf0932c3ce）：shared GPU max_delta=0、ok=1，retained/local_base/overlay通过，333/222/111/111MHz。未关闭测试、未修改shader或保护条件。本次进程可进行缓存复测，但首次启动的间歇性画面对照异常仍待调查，不能表述为已修复。当前尚无目标游戏像素命中或预算请求实际兑现的验收结果。
