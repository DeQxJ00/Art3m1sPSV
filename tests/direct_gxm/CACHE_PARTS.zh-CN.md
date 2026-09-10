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
