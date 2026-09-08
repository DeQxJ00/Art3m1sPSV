# 文字区域更新回归

- core：`cargo test --manifest-path core/Cargo.toml --lib --no-default-features --features gl-backend,gxm-backend`
- 宿主复制路径（WSL/Linux，需g++与Python3）：`python3 tests/text_region/test_host_update.py`

宿主测试直接提取gxm_bridge.cpp生产函数，使用模拟NanoVG/GXM接口及带保护边缘的内存，ASan/UBSan检查13像素宽到16像素stride的区域复制、不变像素/行填充、边界拒绝、阶段限制与每批一次等待。它不验证真实GPU缓存一致性、实际Finish等待时长或画面表现。

GXM字体prepare位于脚本推进后、场景开始前；首次非空文字同时保留链接高亮白块，避免draw阶段指针输入使图集晚变脏。首次完整初始化、区域更新失败保留脏范围，均有core回归。
