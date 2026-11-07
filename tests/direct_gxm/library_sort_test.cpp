#include "../../host-direct/src/library_sort.hpp"
#include <cassert>
int main() {
    std::vector<art3m1s::GameEntry> games(4);
    games[0].id="first"; games[0].title=u8"星风";
    games[1].id="second"; games[1].title="alpha";
    games[2].id="third"; games[2].title=u8"测试";
    games[3].id="fourth"; games[3].title="123";
    size_t selected=0;
    direct::sort_library(games,selected,false);
    assert(games[0].id=="second" && games[1].id=="third" && games[2].id=="first");
    assert(selected==2 && games.back().id=="fourth");
    direct::sort_library(games,selected,true);
    assert(games[0].id=="first" && games[1].id=="third" && games[2].id=="second");
    assert(selected==0 && games.back().id=="fourth");
    games.clear();direct::sort_library(games,selected,false);assert(selected==0);
}
