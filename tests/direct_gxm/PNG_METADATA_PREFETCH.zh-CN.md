# 预载 PNG 时缓存附加信息

用户要求沿提前准备图片附加信息优化，并明确希望在解码时直接缓存、提高小信息缓存数量。本轮核心371cb6a基于缓存分项版b214823，保持heap256MiB、图片总缓存128MiB、idle32MiB、shader和纹理发布/回收/异步等待策略。

现有SurfaceLoader读取整份PNG后，在图像解码前从同一份已读bytes运行既有tEXt解析器，额外文件读取为0。结果放进与FfiCallbacks共享的Arc<Mutex<CommentCache>>；主线程load_png_comments先按实际resolved文件名查缓存，未准备好/被淘汰/异常时保持原同步回退，不等待worker、不新增线程或请求。按实际命中的候选文件路径发布，不把foo和foo.png自动混作同一文件。

缓存上限由256项/256KiB提高为1024项/1MiB，限制路径与字符串capacity及容器个数；HashMap/VecDeque控制结构另计。这是既有独立附加信息小缓存，不计入ready+idle的128MiB图片保留额，实际占用包含在newlib堆内。不是提前分配1MiB，不保存第二份图像数据。

沿用tEXt语义（Latin-1、重复字段最后一个生效、支持IDAT后文本、忽略CRC）；PNG像素IDAT通过偏移跳过。预解析累计读取内存片段上限1MiB，超限/取消不发布空或截断结果，保持回退；空合法结果则缓存，避免重复读没有附加信息的PNG。记录[ png-comments-prefetch ]（实际标记无空格）read_bytes=0、计数/耗时/缓存字符串占用；主线程缓存命中日志增加prepared_hits。

每次在worker读文件前取得缓存generation。file_write开始和结束各失效一次，file_operation继续失效；预解析和取消检查均不持元数据锁，发布时校验generation并让现有前台结果优先。loader state/budget/GPU锁与元数据锁不嵌套持有。解析结果不与消耗型Pixels payload绑定，因此Pixels被take或降级不会丢失元数据；游戏关闭时现有loader shutdown/join释放其Arc，不跨项目沿用。

451项核心测试通过、14忽略（build/png-metadata-prefetch-test.log）；Vita核心编译通过（png-metadata-prefetch-vita.log），host打包日志png-metadata-prefetch-host.log。新增测试覆盖与原解析一致、IDAT不复制、空结果、超额不伪造空结果、取消/失效/前台结果优先、取消回调期间不持元数据锁、脚本callback直接取得预解析坐标而不请求host文件。最初新callback测试缺trait import已修复，随后全量通过。

实机验收：关闭性能浮窗完成启动自检，再保持333MHz、后台下载暂停；走到原kun/flu人物切入、换表情/头像和双人放射，检查prepared_hits增长、对应PNG附加信息慢读取减少及缓存分项正常。画面位置/表情必须不变；再短快进以确认没有引入等待或取消挂起。本轮不声称元数据预载解决首次GPU上传和持续运动时合成缓存失效。

## 候选包

build/direct-candidates/png-metadata-371cb6a，主实现7677eeb/core371cb6a。VPK SHA25688e0b186cc95ea8bd915b0215fa04304f8178605b730ad51adf017cce295ea96，SELF f675f83a1fa37cb46b3c4a3b36839b75eb224e93988102c4e9eeca86a0143a34，core archive93db5da9fa70a75f80bb1f23fb9aa9849ca41e5e0ca7c89ac90e1d62641de578。host sources.zip SHA2569eb105d2a2fc6c0b72d83be012e7d80989d05d2931deb544d4388b3f52fcda32与统计包完全一致。打包通过，有约0.035秒WSL时差警告；新ELF预载/分项字段、源副本、VPK CRC/eboot/SFO核验通过。等待用户关闭浮窗后安装。

## 实机安装和启动

用户关闭浮窗后deploy-20260911-063312完成version检查、原文件备份、临时及正式文件读回验证、launch。旧SELF97f19d96...，新f675f83a...，SFO3573329f...不变；kill返回cannot kill app，未声称成功终止运行程序。063400-current只有初始日志，等待后063431-current（SHA25626ce624e19d5c83777b0b5dceabcf2114925745a3835b3bbd1828f685750005d）确认5个证书测试、shared max_delta0、retained/local_base/overlay通过，arm333、heap268435456。此时加载游戏中尚无预解析命中证据。已请用户恢复浮窗、暂停后台下载，测试人物/表情及双人放射，并核对视觉位置。等待实机切入日志；不能据启动通过声称低帧已解决。
