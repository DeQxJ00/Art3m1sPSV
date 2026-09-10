# 原生 PNG 压缩源与 surface 的寿命

2026-09-11只读复核IDA 13341，先server_health确认PCSG01297/eboot.bin.elf，再查询反编译／导入表；没有改数据库、补丁、调试器或实机程序。当前实机仍为待测32MiB候选7db4f17。

## 已确认

- PNG插件工厂0x812EBFD4将插件+4标志初始化为0。加载函数0x81016EB4在标志为0时使用输入stream的读回调；这个分支本身没有常驻整份压缩PNG数组。
- 标志非0时，取得输入大小、分配v29[0]并读入完整压缩数据，以内存回调解码。正常完成路径在surface+68后，条件调用0x81336950(v29[0])，随后释放行指针表v14并销毁libpng读结构。因此该分支的完整压缩副本是临时输入，不被PNG插件长期保留。
- 重新读取原生导入表而非猜测stub语义：0x81336950的NID 0x91B0DC47对应VitaSDK SceLibstdcxx `_ZdaPv`（operator delete[]）；0x81336A50的NID 0xE7FB2BF4对应`_Znaj`（operator new[]）。PNG函数0x81017208／0x81017210确有delete[]调用。
- PNG按surface+52返回的各行地址写入结果。此前已核对CGpuSurface行地址来自对象+4像素指针，纹理描述也引用该存储；目标为CGpuSurface时，CPU解码写入和GPU纹理采样可以使用同一份像素，不要求先保留独立CPU整图再上传复制一份。
- CSurfaceManager保留按名关联的surface引用，worker完成后发布。0x812375A4按surface+44给出的用量扣账并移除可回收对象；未在此路径发现另一个压缩源缓存层。

## 结论边界

所确认的PCSG01297 PNG→CGpuSurface路径是“流／临时压缩输入→解码后的surface→复用surface”，不是当前移植版刻意保留压缩源作淘汰后回退的分层策略。不能把这个结论扩大为五个游戏、所有格式或PFS底层都绝无压缩缓存；archive流内部及其他插件尚未完整追踪。没有从正常完成路径推定所有异常退出分支均无泄漏。loader+20的0x8100D334仍被Hex-Rays合并进前函数，本次未以合并伪代码建立新的所有权结论。

当前移植保留最多4MiB压缩源，是为已观测到的重复读盘作补救。它能省IO，但像素被淘汰仍要解码；这不是原生已验证方案的逐项复刻。更接近原生的方向是完成后surface复用，以及向未发布、GPU可用的像素存储直接解码，避免独立CPU/GPU整图的常驻重复占用与复制。还须保留线程发布／取消／GPU引用寿命与预算约束，不能据此直接在worker调用现有GXM上传函数或删除安全等待。

原始私有证据：build/native-five-audit/20260911-source-lifetime.json、20260911-png-source-release-imports.json；结合20260910-transition-png-factory.json、transition-surface-row-asm.json等已有证据。此次只做调查与文档，未修改缓存策略或安装新包。
