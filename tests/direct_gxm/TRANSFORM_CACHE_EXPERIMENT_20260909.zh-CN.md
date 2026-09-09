# 本地矩阵缓存实验（未部署）

独立工作树 `build/heap-audit/transform-source`，分支 `codex/optL-transform-cache`，以已部署 text-epoch core `18ee1d9` 为父提交。实机继续使用 text-epoch 包。

## 依据和范围

最近固定双人/放射线页面的 scene 构建约 3.8ms/render。代码表明 build::visit 每次遍历仍调用 LayerProps::local_transform，计算位置、锚点、角度、缩放组成的矩阵。静止属性已通过 Cow 借用，因此不能再把这个路径解释为每层都复制整份属性。

实验给每个 Layer 保存一个本地矩阵，比较九项有效原始值（七个浮点的位表示、两个翻转布尔值）。未命中调用原始运算，未更改公式。命中不影响父矩阵合成、透明度、裁剪、shader、纹理解析和绘制顺序。缓存不参与序列化；新恢复的 Layer 从空缓存开始。动画求值仍在缓存查询之前发生。

特性 `gxm-transform-cache` 默认关闭，仅接入 build，尚未接入命中检测，也没有接入宿主运行时 A/B。Cell 缓存会改变该特性下 Layer 的 Sync 属性，后续正式集成前需审计跨线程 API；目前只作为独立实验，不合入已验证包。

## 验证

- 特性开启：Release 347 passed、14 ignored、0 failed，`build/heap-audit/transform-tests.log`。
- 特性关闭：Release 346 passed、13 ignored、0 failed，`transform-feature-off.log`。
- 新增 2400 轮有限数值变更，对每个矩阵分量作位级比较，覆盖位置/缩放/锚点/旋转/翻转、默认恢复、负零、无关 alpha 变化、缓存克隆及序列化恢复。
- Windows 矩阵微基准，512 万次调用：原路径 25.62/26.20ms，缓存 31.21/30.56ms。此粒度缓存更慢，不能据“缓存”一词推断收益。日志 `transform-bench.log`。
- 200 层合成场景，复用 DrawList 的路径单次抽样：缓存 14.847µs/帧、原路径 15.566µs/帧。日志 `transform-scene-on.log` / `transform-scene-off.log`。只是桌面合成数据、单次抽样，不能当作约 4.6% 的可靠实机收益。

证据结论：单矩阵缓存成本不低，完整场景收益尚不充分。暂不打包部署。需要扩大缓存粒度，或先做 ARM 测量，避免用增加缓存查找和内存占用的方式拖慢现有实机版本。实机 FPS 和换句尖峰目标仍未完成。
