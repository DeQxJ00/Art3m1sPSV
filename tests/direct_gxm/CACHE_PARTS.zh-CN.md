# 解码与压缩缓存分项统计

用户要求将解码结果和压缩数据分别统计。旧ready包含Pixels像素、随附压缩备份、Encoded-only源和alpha证书；旧idle包含闲置GPU/CPU像素、decoded-only、压缩备份及证书。因此不能把旧ready直接解释为纯解码像素，也不能把旧encoded_bytes（含proof）当成纯压缩数据。

在既有image-cache-budget日志添加parts_version=1及7项，单位字节：

- ready_decoded：worker已完成结果的CPU像素capacity。
- ready_encoded：Pixels随附备份及Encoded-only源的capacity。
- ready_proof：这些结果的透明度证书capacity。
- idle_decoded：闲置纹理保有的CPU副本，加decoded-only像素capacity。
- idle_gpu_est：闲置GPU纹理沿用旧预算的行对齐估算，不含CDRAM分配块对齐。
- idle_encoded：provider压缩备份capacity。
- idle_proof：provider压缩备份携带的证书capacity。

ready前三项相加等于ready；idle后四项相加等于idle；七项之和等于总缓存。CPU/GPU共用surface的CPU副本为空，CPU项为零。若确实有独立CPU/GPU两份，分别记其实际保留，不把GPU估算当作额外未计入总预算的容量。活跃对象、解码临时内存和codec等仍在总缓存统计之外。

沿用loader已有汇总遍历，一次累加分项并在同一budget锁内发布，GPU侧不反向获取loader锁；idle仅在原120次retain日志点遍历对象元数据，不读像素、不新增日志频率/线程。没有改变256MiB堆、128MiB总额、32MiB闲置上限、shader、缓存准入/回收/异步等待策略。

summarize-image-cache-budget.py校验分项和，输出last_breakdown的CPU解码/GPU估算/压缩/证书；旧日志last_breakdown=null，不伪造零。446项核心测试通过、14忽略；新增测试覆盖demote/take/unbind/shutdown及共享GPU无CPU重复，capacity与length不同、proof分离。Python3项测试覆盖新旧格式、错误分项和、不完整字段与负数。日志build/cache-parts-test.log、cache-parts-vita.log；host打包日志cache-parts-host.log。待实机验证。

候选build/direct-candidates/cache-parts-b214823，主实现3ef3f64/core b214823。VPK SHA2561592b200c9533f5ec625a474340781e004b2f65c809d1660d76a7e2f1f1f52a2，SELF97f19d9615a9ada5a4c62bd55f3801fb650d5f05923c729908347e354258c7bf，core archive2b5b3d6d771a5b95df3c09df549a335041ca8c297d4c5f1e329971b748bd4e45。host sources.zip SHA2569eb105d2a2fc6c0b72d83be012e7d80989d05d2931deb544d4388b3f52fcda32与前包完全一致。新ELF分项字段、VPK/SELF/SFO及源副本校验通过；等待关闭浮窗后安装。

## 安装中断状态（需续完）

用户关闭浮窗后执行deploy-20260911-060748。健康检查和旧文件备份完成，eboot正式文件已更新且最终读回SHA为97f19d96...，原c002f8a5...保留在该目录。随后SFO临时文件STOR等待完成响应超时，尚未执行该文件rename/正式替换，也未执行launch。正式SFO旧SHA3573329f...与新包相同。不能将此状态称为整个安装/启动成功，也不能称旧eboot仍未改变。

事后只读验证及用户要求的再次部署均在FTP欢迎响应阶段超时；version命令仍返回1.06。已请用户恢复FTP服务后继续。此前基于异常位置误以为eboot未替换，随后以manifest已保存new_sha256证据更正。不可丢弃原始备份，后续以实际读回和启动日志确认。

用户另提出“总320M”，目前等待澄清是程序堆还是图片缓存；尚未改任何容量，统计包仍heap256/cache128/idle32。

## 续装与启动完成

用户恢复FTP后deploy-20260911-061423完成备份、两个正式文件读回和launch。备份已证实正式eboot是97f19d96...，重传后相同；SFO前后3573329f...一致。最初061653-current只有首行日志，等待后再次只读抓取061735-current，SHA256 3ca1c7f756f8476a99c3b4472c8a721796ebb8474602182b008a25b06678cda1：5个证书测试通过，shared max_delta=0，retained/local_base/overlay均通过，arm333，heap_limit268435456。未修改游戏按键或设备设置。此时正在游戏加载阶段，尚未有目标场景分项数据；已请用户进入双人＋放射场景停留后采样。320MiB仍未修改。

061826-current首批运行分项日志SHA2563251f6b549cf102b37aa02511f1f636203887bd649b2efaccb2f205d7e54e552：17份分项预算、14份完整账本校验无错误。末次ready_decoded75591432、ready_encoded3735611、ready_proof4963；idle_decoded346384、idle_gpu_est23176640、idle_encoded1521589、idle_proof1251，七项合计104377870，与旧ready+idle总账一致。此批尚无目标放射line21等命中记录，不作为双人放射场景占用。分项统计已经实机验证工作，等待用户目标场景采样。

## 双人＋放射场景分项结果

用户就位后，version连接超时，独立FTP NOOP健康检查成功后只读下载062117-ftp-current/host.log，SHA256 bb372091677716c9bc42fef167a54f9da7c5c0f8bc3430ae7278fc8912fc15e5。81份分项预算及44份完整账本校验无错误。

末次ready_decoded47091456、ready_encoded2483101、ready_proof3168；idle_decoded1149240、idle_gpu_est16599232、idle_encoded3640023、idle_proof3456。因此CPU解码保留合计48240696（46.01MiB）、闲置GPU估算16599232（15.83MiB）、压缩备份合计6123124（5.84MiB）、透明度证书6624字节（6.47KiB），合计70969676（67.68MiB）/128MiB。不是整游戏全部像素或活跃场景总占用；独立CDRAM账本texture54001664（51.5MiB，含活跃和闲置，不能与闲置GPU项重复相加），CDRAM全部80.5MiB，uncached0。

最后4个完整约5秒窗口各300帧，max17.655/17.682/17.976/17.941ms，over20ms0；纹理decode/upload0，retained每窗600hits/0builds。其前一变化窗口260帧/max68.098ms，不能以稳定20秒表现声称切入无卡顿。日志派生cache-budget.json、ledger.json、dual-ray-parts.json均保留。未改容量/设置或操作游戏。
