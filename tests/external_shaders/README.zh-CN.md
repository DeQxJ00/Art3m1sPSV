# 内置 Artemis PC shader 与外置缓存

本轮以 Toshiue_Kanojo2 的 31 个文件为基准。otomeriron 的 20 个同名文件与它逐字节相同，因此 51 个来源文件只需 31 个预编译程序。

| 来源 | 文件数 | 与另一款相同 | 独有 |
|---|---:|---:|---:|
| otomeriron | 20 | 20 | 0 |
| Toshiue_Kanojo2 | 31 | 20 | 11 |

共有：add、blur_h、blur_v、cadd、cmul、compbr、compbrc、compdk、compdkc、gray、mosaic、mul、nega、radial、raster、reset、rgb、screen、sepia、sepia2。

新增：blur_k / blur_kx / blur_ky（三种 Kawase 采样）；dimhole / dimover / dimring（圆形扭曲、暗化与环形高亮）；noise（噪声位移）；trapezoid_dw / lt / rt / up（四方向梯形变形）。算法和参数保留源文件的定义；同名 shader 不自动视为同一实现。

## 加载顺序

1. 读取原 shader，按源码内容的 FNV64 标识查找内置 AGX1/GXP。共约 25 KiB GXP；只在脚本请求时注册，在游戏退出时释放。内置程序不依赖 libshacccg 或两个自动开关。
2. 未匹配内置版本：优先读取与源码、转换器版本匹配的 Cg 和参数布局缓存。
3. 缓存缺失且允许自动转换：将支持的固定 DX9 HLSL 包装转换为 Cg，并立即缓存，即使自动编译关闭也会保存。
4. 优先加载与 Cg、源码、编译设置和编译器版本匹配的 GXP。仅在缓存缺失且自动编译开启时调用 PSV 的 vitaShaRK/libshacccg。

两个开关在游戏选择界面的 START 设置中，默认开启、全局保存，下次进入游戏生效。关闭不会删除已有缓存；不匹配、损坏的缓存不会作为当前源码的编译结果加载。

缓存保持 PFS 相对目录。例如：

```
games/<游戏>/shader-cache/system/shader/pc/example.hlsl.cg
games/<游戏>/shader-cache/system/shader/pc/example.hlsl.conversion.json
games/<游戏>/shader-cache/system/shader/pc/example.hlsl.gxp
games/<游戏>/shader-cache/system/shader/pc/example.hlsl.hash
```

保留 `.hlsl` 后缀是为了区分同名不同类型的源文件。旧的扁平缓存保留，但新路径不复用旧格式。转换元数据包含参数布局与源码/产物校验；手工修改 `.cg` 后不再视为有效的自动转换缓存。

## 修复与边界

- Cg 全局常量显式使用 `static const`。实机第一次测试发现灰度与两种褐色调读到零常量、呈现黑色，修复后通过像素验证。
- 核心与宿主统一接受 GXP 合法的四字节尾部对齐，拒绝截断、超长和不一致的包。
- 多层连续自定义滤镜保留每一次采样，通过两个交替使用的离屏目标执行。真实模糊不再当成普通拷贝；预编译减少的是加载成本，不能消除滤镜自身的 GPU 运算成本。
- 普通场景不会因“已注册某个 shader”而自动开启该效果。原有内置 sprite、转场、视频 shader 的程序字节不变。
- 自动转换仅支持已实现的固定顶点包装/像素效果子集；自定义顶点阶段、samplerBack 等不宣称支持。

## 验证与复现

- 核心测试：482 通过，15 忽略（含转换、缓存失效、实际 31 个 GXP 包的读取回归）。
- `tests/external_shaders/cache_test.cpp`：四种开关组合、冷/热缓存、不同游戏隔离、源码变化、损坏恢复、目录穿越拒绝、设置保存。
- `bundled-shader-probe.once` 是显式启动诊断开关，平常不执行。实机 31/31 程序通过两组颜色/遮罩/裁剪像素断言；每个程序另记录一组空间效果图。空间图用于观察，不等价于所有参数范围的原版逐像素比较。
- `scripts/prepare-shader-bundle-test.py` 生成独立 TEST_SHADERS_31 与 TEST_SHADER_CACHE，全部测试资源在 build 下；安装也只放独立 TEST 游戏目录。
- 实机完整脚本验收：关闭转换/编译，51 次注册全部命中内置，无 shader 加载错误；灰度、反色、褐色、模糊、波浪、梯形页面成功保存并查看。停留页采样约 60 FPS。
- 外置实机测试：修改注释生成不匹配内置哈希的灰度 shader，首次转换/编译成功（约 1.91 秒）；关闭两个开关并重启后命中 Cg/GXP 缓存，没有再次加载编译器。首次核对 libshacccg 文件指纹仍有 I/O 成本，本次缓存读取总计约 1.34 秒；内置 31 项不经过这个路径。
- 真实游戏所有剧情组合和高负载多级滤镜仍需继续回归，不能由上述单项测试推断全程性能。

离线重建：`compile-external-shaders.py` 使用本机 psp2cgc（O1/no-fastmath/bestprecision）生成 AGX1；`bundle-artemis-shaders.py` 生成内置资源表和实机诊断数据。运行期未知 shader 使用 libshacccg；没有把这 31 个程序改成启动时现编译。

核心提交 `86c63dd`，补丁 `patches/artemis-bundled-and-external-shaders-core.patch` 基于核心 `c939398`。内置二进制与表同时保存在 `shaders/artemis-pc`，完整核心补丁也包含它们。当前 VPK 为 `build/external-shaders/art3m1s-bundled-31.vpk`，SHA256 `61bdcb0a70e2fc864f653fb51e50fca0674d825fc3e2a9901a7fd85b17f03739`。

本轮日志：`build/native-command-port/bundled-31-static2.log`（31 项像素）；`build/external-shaders/fixture-device.log`（51 次注册和六个页面）；`build/native-command-port/shader-cache-cold2.log` / `shader-cache-warm2.log`（外置缓存）。文件均已复制到电脑，不依赖实机日志继续保留。
