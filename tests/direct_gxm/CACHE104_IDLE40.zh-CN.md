# 1.20之后：104MiB总保留额与40MiB闲置纹理试验

用户要求继续扩大缓存，并明确要求总96MiB也提高。本轮core1cd14e3相对于v1.20-core（5975e77）仅两项常量变化：共享ready+idle总额96→104MiB（109051904字节），idle最大32→40MiB（41943040）。没有修改shader、GPU同步、worker/队列64/等待取消、192MiB堆、单解码16MiB或压缩备份保护。

已有5/6请求策略仍保留：后台pending时ready_goal=90876587、余18175317字节（约17.33MiB）。这阶段40MiB只是idle上限，实际idle仍需给前向请求退让；因此不能声称40MiB会避免上轮重走剧情时所有GPU纹理淘汰。无pending且ready足够低时idle才可用到40MiB。两边计数共用104MiB，总保留额并不是程序总内存/CDRAM/堆上限；活跃场景、codec和临时分配另计。H264失败后闲置GPU释放重试机制保持，须复测OP，不能因总RAM足够就推断CDRAM有连续空间。

v1.20基线主标签与v1.20-core不移动，原包在build/01.20/art3m1s-direct-1.20-baseline.vpk完整保留。此包为组合试验，两项同时变化，后续结果不能独立归因某一项。较小图片数量限制和固定后向预留不恢复。

443项核心回归通过、14忽略（build/cache104-idle40-test.log），Vita编译通过（build/cache104-idle40-vita.log）。idle40但总96的中间配置也跑过443项，但未打包/部署；用户追加总额后重新验证最终组合。host仅重链，待冻结与实机安装。

验收：333MHz，启动自检通过后恢复浮窗；先同开篇人物+放射首次切入，再重走一次比较GPU上传/淘汰；继续OGV/OP及返回剧情、短快进，观察预算<=104MiB、无fault/分配失败和不退回软件H264。当前首次GPU发布、图片附加信息/遮罩读取、主线程I/O等待和逻辑长帧仍可能存在，不声称单靠容量全部解决。


## 安装和启动验证

候选build/direct-candidates/cache104-idle40-1cd14e3，root3771fa1/core1cd14e3。VPK SHA2561ffc633460d12a033a4da848a2c63910853251b5f43ce7a9a8f875196ffabdc8，SELF b8c155d75089c1cd867aa2d86d571b84b58aaf08b36da98cb27adb98e11ee796，core archive ed2e22f9497c965b0db888be55987c8b64312304e546a3ebf9aa4c858311dbf7。host sources.zip与v1.20原包完全一致；编译存在约0.024秒WSL时间差警告，随后新ELF/SELF/VPK内容与SFO校验通过。

用户关闭浮窗后deploy-20260911-053258健康检查、旧文件备份、上传读回与最终SHA校验、launch完成。kill返回cannot kill app，未因此声称旧程序正在运行或被成功终止；后续新SELF确认为b8c155d7...，原SELF e9085bb2...已备份。053353-current日志SHA25646ddcbc166ebe50b1cfb9c8da7a82d3da5897d98651746589e05fb45f4105469：5个alpha证书测试通过，shared max_delta=0/ok=1，retained/local_base/overlay通过，333MHz。尚停在选择游戏菜单，没有新容量的游戏内预算样本或性能结论。v1.20主标签3ba4ee5及核心标签5975e77仍保持。
