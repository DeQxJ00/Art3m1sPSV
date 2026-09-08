#pragma once

#include <borealis.hpp>
#include "game_library.hpp"

namespace art3m1s {

class LibraryActivity final : public brls::Activity {
public:
    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    std::vector<GameEntry> games_;
    brls::Box* list_ = nullptr;
    brls::Label* status_ = nullptr;

    void rebuild();
    void select_game(const GameEntry& game);
};

} // namespace art3m1s
