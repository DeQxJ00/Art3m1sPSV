# 96 MiB 预取缓存实机崩溃及撤回

2026-09-10，用户报告 OGV 切换附近出现 PSV 系统错误。实验包 core e1a2c8b，host 9d17ae2，已完成预取结果预算从 16 MiB 提高到 96 MiB；没有改变 worker/等待协议。

证据保存在 `build/hardware-logs/20260910-070356-current/`：系统转储、host.log、threads.json 和 stack-addresses.json。转储 SHA256 为 93d659b98045619809e84feffcf9811850117fbd1982ac6aa437e1cbeba9039d。

异常线程 pthread 0x40020169，stop_reason 0x30002。运行时 PC 0x81501a86 映射到当前实验 ELF 的 0x814e8a86（代码段加载偏移 0x19000），符号 `_kill_r`。异常线程栈含 abort、rust_oom、__rust_alloc_error_handler、image::decoder_to_vec、ImageReader<CancelReader>::decode 和 surface_loader::Loader::with_budget worker。decoder_to_vec 返回地址 0x8105afb6 前的指令调用 raw_vec::handle_error，保留的分配大小寄存器为 0x7e9000，即 8,294,400 字节。

结论：直接退出原因是图片预载 worker 解码分配失败触发 Rust OOM abort，不是 Theora worker 的非法访问。日志最后正在启动第二段 OGV，视频与预载并发可能加剧内存压力；不能由日志推断视频内存泄漏，亦不能仅凭最后一条 heap 采样推断崩溃瞬间仍有足够连续内存。栈内扫描地址并非全部都是活动调用帧，结论由 OOM 调用链、分配失败指令及 worker 栈联合支持。

已撤回实验：实机安装用户确认修复此前 30 帧场景的 e369f5e 稳定包，eboot SHA256 1e6575f709731bb1c45ed5b6074ba22657b32dc3ba3486dd367d39b915fb2725。部署记录 `build/direct-deploy/deploy-20260910-070916/manifest.json`。保留绘制优化与 shader，预取预算恢复 16 MiB。

源码提交 5bca8c9 恢复 READY_BUDGET=BUDGET，保留只读诊断；396 项测试通过、13 项忽略。该源码修正没有另行构建部署，实机运行上述既有稳定包。后续预算改进必须计入解码中间内存、视频队列和活动资源，先保障分配失败可恢复，再测试扩容。尚需用户实机重跑同一切换确认。

后续源码改动：预载不再调用 ImageReader::decode 的整图输出分配，改为 into_decoder + try_reserve_exact + read_image。L8/La8/Rgb8 在同一输出缓冲中逆序扩展为 RGBA，Rgba8 直接写入。输出分配失败返回 Encoded 路径；其他色深也暂保留压缩源，由原前台路径处理，因此不能声称所有格式都预解码。解码器内部、源文件读取、容器等分配仍未全部变为可恢复，预算仍为 16 MiB。

验证：397 项主机测试通过，13 项忽略；新增四种颜色类型的 PNG 输出与 image 库逐像素比较，以及输出分配拒绝后源数据仍可正常解码的测试。尚未完成 Vita 交叉构建和实机验证，未替换稳定实机包。这不是最终的 GPU 存储直接解码实现。
