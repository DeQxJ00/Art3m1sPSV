#include "library_activity.hpp"
#include "game_surface.hpp"

#include <cstdio>
#include <psp2/io/stat.h>

namespace art3m1s {

brls::View* LibraryActivity::createContentView() {
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setPadding(18, 32, 18, 32);

    auto* intro = new brls::Label();
    intro->setText("选择游戏 / Choose a game");
    intro->setFontSize(24);
    intro->setHeight(42);
    intro->setSingleLine(true);
    intro->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    content->addView(intro);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    list_ = new brls::Box(brls::Axis::COLUMN);
    list_->setPadding(8, 4, 8, 4);
    scroll->setContentView(list_);
    content->addView(scroll);

    status_ = new brls::Label();
    status_->setText("扫描 ux0:data/art3m1s-gxm/games");
    status_->setFontSize(17);
    status_->setHeight(30);
    status_->setSingleLine(true);
    status_->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    content->addView(status_);

    auto* frame = new brls::AppletFrame(content);
    frame->setTitle("art3m1s GXM");
    return frame;
}

void LibraryActivity::onContentAvailable() {
    sceIoMkdir(kDataRoot, 0777);
    sceIoMkdir(kGamesRoot, 0777);
    games_ = scan_games();
    rebuild();
}

void LibraryActivity::rebuild() {
    list_->clearViews();
    if (games_.empty()) {
        auto* empty = new brls::Label();
        empty->setText("未找到游戏。请复制到 ux0:data/art3m1s-gxm/games/<TitleID>/");
        empty->setFontSize(20);
        empty->setHeight(90);
        empty->setSingleLine(false);
        empty->setHorizontalAlign(brls::HorizontalAlign::LEFT);
        list_->addView(empty);
        status_->setText("0 个游戏");
        return;
    }

    const std::string last = load_last_game();
    int default_index = 0;
    for (size_t i = 0; i < games_.size(); ++i) {
        const GameEntry game = games_[i];
        auto* button = new brls::Button();
        const std::string marker = game.ready() ? "  [可启动]" : "  [缺少 system.ini / PFS]";
        button->setText(game.title + "  (" + game.id + ")" + marker);
        button->setFontSize(20);
        button->setHeight(58);
        button->setMarginBottom(6);
        if (!game.ready()) button->setState(brls::ButtonState::DISABLED);
        button->registerClickAction([this, game](brls::View*) {
            select_game(game);
            return true;
        });
        list_->addView(button);
        if (game.id == last) default_index = static_cast<int>(i);
    }
    list_->setDefaultFocusedIndex(default_index);
    status_->setText(std::to_string(games_.size()) + " 个游戏；T01 SHUF00002 始终优先");
}

void LibraryActivity::select_game(const GameEntry& game) {
    if (!game.ready()) return;
    save_last_game(game.id);
    brls::Application::pushActivity(new brls::Activity(new GameSurface(game)), brls::TransitionAnimation::NONE);
}

} // namespace art3m1s
