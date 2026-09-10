# 显式音频内存记账

core 3a57966 将账本升到v3、11个owner。Audio记录HostVorbis对象、预载压缩数据、Prepared/Command/Generation/Finished对象及命令字符串的显式堆分配请求量。先预留、分配成功后转live；失败撤销预留，最后释放时归还。既有8MiB音频预载总额度、每文件限制、解码器、PCM精度、线程和播放完成协议均未改动。

这些是分配请求量，不是整个音频子系统实际RSS。Tremor/FFmpeg/cJSON内部、host_stream对象、分配器元数据和静态PCM数组不在Audio字段中，尚不能称为完全统一准入或整体内存上限。A1依旧observe-only；A2未启用。

`tests/audio/run.sh` 的ASan/UBSan验证通过：新增malloc/calloc失败撤销和空指针释放检查；真实Tremor音频覆盖预载、取消、读取失败回退、8MiB预算、同ID替换、stop_all、重启、退出及过期完成通知。结束后live/reserved均0，显式请求峰值8,554,128字节。测试手动创建的过期Finished对象也经同一分配入口进入账本。Vita核心交叉编译通过；实机多轮生命周期和峰值验收待完成。
