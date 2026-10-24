#include "../../host-direct/src/control_hints.hpp"
#include <cassert>
int main(){
    using namespace direct;
    auto hints=control_hints("↑↓ 选择   ○ 确认   ←→ 调整   L/R 翻页   × 返回");
    assert(hints.size()==5&&hints[0].button==HintButton::Direction&&hints[3].button==HintButton::Pages);
    assert(hints[4].label=="返回");
    assert(control_hints("○ 进入").size()==1);
    assert(control_hints("△ 使用说明")[0].button==HintButton::Triangle);
    hints=control_hints("○ / × / △ 返回设置，或点击屏幕返回");
    assert(hints.size()==3&&hints[0].label.empty()&&hints[1].label.empty());
    hints=control_hints("↑↓ 选择 / Select    ○ 确认 / OK    × 退出 / Exit");assert(hints.size()==3&&hints[0].label=="选择 / Select");
    assert(control_hints("L  ‹")[0].button==HintButton::L);
    assert(control_hints("›  R")[0].button==HintButton::R);
    assert(control_hints("START 设置   SELECT 关于").size()==2);
    assert(control_hints("按宽 × 高 × 4 计算").empty());
    assert(control_hints("○ 进入：压缩开关、大小门槛、自动或手动目录范围。").empty());
    assert(control_hints("8×8").empty());assert(control_hints(nullptr).empty());
    assert(hint_icon_width(HintButton::Pages)==52&&hint_icon_width(HintButton::Select)==56);
}
