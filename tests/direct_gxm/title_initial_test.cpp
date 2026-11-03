#include "../../host-direct/src/title_initial.hpp"
#include <cassert>
int main() {
    // Chinese preference also applies when a Han title contains kana later.
    assert(direct::title_initial(u8"月の空") == 'Y');
    assert(direct::title_initial(u8"月空") == 'Y');
    assert(direct::title_initial(u8"《つきの空》") == 'T');
    assert(direct::title_initial(u8"ん") == 'N');
    assert(direct::title_initial(u8"ヴァイオリン") == 'V');
    assert(direct::title_initial(u8"さくら") == 'S');
    assert(direct::title_initial(u8"シロイソラ") == 'S');
    assert(direct::title_initial(u8"つき") == 'T');
    assert(direct::title_initial(u8"チャイム") == 'C');
    assert(direct::title_initial(u8"フユ") == 'F');
    assert(direct::title_initial(u8"星风观测站") == 'X');
    assert(direct::title_initial(u8"《测试标题》") == 'C');
    assert(direct::title_initial(u8"測試標題") == 'C');
    assert(direct::title_initial("  alpha") == 'A');
    assert(direct::title_initial("123") == '#');
    assert(direct::title_initial("") == '#');
    assert(direct::title_initial(std::string("\xff")) == '#');
    assert(direct::title_with_initial(u8"星风观测站") == u8"X · 星风观测站");
}
