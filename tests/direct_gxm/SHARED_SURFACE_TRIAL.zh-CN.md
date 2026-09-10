# 单份静态 surface 首版试验

用户要求试验原生的单份像素存储，同时保留已恢复的压缩备份。以ccaa7e0（内容等同7db4f17）为基线；压缩回退4MiB、闲置缓存32MiB、预载16MiB、shader和现有GPU安全等待均保留。

## 实现范围

1. 静态图resolve成功、像素数至少256×256时，确认host可提供有效surface像素视图后释放provider的独立RGBA副本，标记shared。透明度查询按真实stride读同一份像素；只有显式请求完整pixels或CPU合成mask时才临时复制，避免长期重复占用。动态视频、字体更新、截图目标与显式上传继续原路径。
2. 前台需解码、输出不超过16MiB、解码原始通道数据不大于RGBA8输出的图像，先在渲染线程分配新的未发布surface，直接解码到这块映射内存。封装时计算原有alpha／opacity信息，再把非对齐宽度的紧密行从后向前原地扩展为GXM stride，最后初始化descriptor并发布。width已8对齐时没有整图上传或行搬移。16位大于RGBA8的临时输出、分配失败等继续旧路径。
3. 后台预载线程未接入GXM、没有改变等待／取消／ticket协议。它仍产生CPU像素，主线程上传一次，随即释放重复像素。故此阶段实现单份**常驻静态像素**，不是所有后台解码均零复制，也不是完整原生加载架构。
4. private surface在解码／封装失败时由RAII释放，未发布对象从未被GPU引用。发布后的旧纹理沿用destroy/retired和既有完成等待；不能套用未发布对象的即时释放规则。CPU只读共享像素不改写在用纹理。
5. shared对象闲置时按一份GPU存储计费，淘汰时不制造新的CPU备份，压缩源继续保留。当前GPU预算仍是既有估算，实际分配账本有256KiB对齐，尚不是全局A2准入。

## 验证与限制

- core生产改动50ebf1b，随后仅增加失败解码回归。426项通过、14项忽略；日志build/shared-surface-test.log。Vita交叉编译build/shared-surface-vita.log通过。
- 新测试覆盖外部目标PAL8／RGBA32与保护字节、513宽逐像素读取、cache复用／淘汰后源回退、分配失败走旧上传、发布失败和半截PNG不发布并释放临时对象。原有文字、动画、透明证明、异步取消回归保持通过。
- host增加实际GXM启动对照：17×9源、24像素stride、半透明多彩图案，新旧texture分别绘制到屏幕并readback比较。shared路径仅在该项成功时启用，失败保持旧路径；其结果不决定原有shader是否启用。
- Shader字节与效果公式未改。CPU解码直接写CDRAM／uncached内存的速度可能低于写普通堆；必须观察总长帧、常驻内存、重复加载和画面正确性，不能预先认定零复制一定更快。
- 用户仍操作游戏。候选部署后先验证启动对照，再请复现多人／头像、背景平移与切换、OGV、短快进和读档。若出现缺块、崩溃或明显变慢，原idle32-7db4f17包可直接恢复。

## 首次实机部署

最终core 6d88e04、host e4deccf；最后一次Vita编译后重新链接，候选build/direct-candidates/shared-surface-6d88e04。VPK SHA256 c2386bddd17911b6adb2de8a678fbc6e15700e09d6946437f53a01b2d4002af4，eboot b478178ddcda6bf58bdfc5715da04edb3ae21f88e44bb681430ab599015a68c7。host最初clean构建通过，最后重链有0.016秒WSL时钟偏差警告；新ELF标记、源码一致性、VPK CRC、VPK内eboot一致及SFO核对均通过。

deploy-20260911-013205完成备份、读回与启动。启动日志20260911-013333-current（SHA256 05393378bc186a147c09961d4d4c44872488ef3efb7d778c10d788fe56436a87）：`shared-surface-self-test odd_stride=24 alpha=128 max_delta=0 ok=1`，新路径通过真实GPU像素对照并启用；retained/local_base/overlay原检查均通过，333MHz。此时只完成启动验证，不能宣称游戏中的延迟、内存或稳定性改善。待用户按同组场景测试并采集shared-surface-decode、cpu_released、heap/CDRAM和frame-spike记录。
