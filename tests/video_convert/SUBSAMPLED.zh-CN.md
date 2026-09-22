# 下采样 OGV 颜色／透明度验证

`video.c` 的 GXM 转换入口支持经设备自检通过的 8 位 YUV420P、YUV422P，以及原有 YUV444P。420／422 色度按无缩放 swscale 默认路径展开到原有全尺寸平面；遮罩只读取 Y 平面，不依赖被 GRAY 解码省略的色度。保留现有 shader、队列、PTS、循环和资源释放流程。

每种新增格式首次使用时，以设备自身的 swscale 做小图比对。RGB 最大允许 3/255 的整数舍入差异，透明度要求完全相同；不通过便继续使用原转换路径。全范围／高位深等其他格式仍走原路径。ARM 无缩放 RGB 包装函数完成转换后可能返回 0；自检兼容这一返回值，同时预先用差值 128 的哨兵填充每个输出字节，必须全图通过像素比较。负数错误、未写入输出或颜色不符仍拒绝启用。

- `bash tests/video_convert/run.sh`：边界、非整块宽度、全部透明度等级、420／422 色度展开与 swscale 比对。
- `bash tests/video_async/run.sh`：444／420／422 的颜色与遮罩组合、映射队列、复制队列、回退、EOF、取消、循环和流所有权。
- `subsampled_recording.c`：读取真实颜色和遮罩 OGV，逐帧对照旧 swscale 路径，检查时间戳、颜色误差和透明度。仅读取资源，不修改原文件。

真实文件验证示例（输出可执行文件放入本地 `temp/`）：

```sh
cc -O2 -g -fsanitize=address,undefined tests/video_convert/subsampled_recording.c \
  -lswscale -lavformat -lavcodec -lavutil -o temp/subsampled-recording
temp/subsampled-recording /path/to/effect.ogv /path/to/effect_m.ogv
```

主机像素测试不能代替实机性能测量。应使用同一资源、同一场景及 CPU／ES4 设置，对照 `[video-consumer]` 的帧数和墙钟时间；屏幕整体 FPS 不等同于视频实际更新帧率。
