# 上传复制与 alpha 认证合并实验

`texture_copy_opacity.hpp` 是尚未接入宿主上传路径的独立实验，当前实机包不含这项行为。

现有大图不透明认证因历史实机额外扫描 12–15 ms 而限额至 1024 像素。本实验在复制可见像素时用已加载的数据归约 alpha；发现非 255 后，剩余数据回退调用方的 memcpy。NEON 每组最多处理 1024 字节再归约一次，不使用逐像素 ARM/NEON 往返，不读回目标 CDRAM。行尾 padding 清零，但不参加可见像素的透明度认证。调用方须保证源/目标不重叠、stride>=width，缓冲区容量正确。

测试 `texture_copy_opacity_test.cpp` 的 1449 个案例通过 x86 AddressSanitizer/UBSan：奇数尺寸、16/64/256 边界、错位缓冲、RGB 为零但 alpha 全 255、首/中/末透明像素、逐个移动透明像素跨归约边界、padding 和 guard、源像素不变。VitaSDK ARM NEON 分支交叉编译通过；尚未执行 ARM 版本，不能用 x86 测试宣称 NEON 分支运行验证通过。

未验证它相对 sceClibMemcpy 的实机复制带宽，亦未证明能消除历史加载峰值。接入前需在 PSV 上对相同源/目标内存类型进行计时，并覆盖实际图片、更新失效、转场和存档截图。透明度认证不能从 alpha bounds 或文件名推断。
