#include "../../host-direct/src/ui_language.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <set>
int main(int argc,char** argv){
    assert(argc==2);
    const std::string path=std::string(argv[1])+"/language.txt";
    std::filesystem::create_directories(argv[1]);
    std::remove(path.c_str());std::remove((path+".bak").c_str());
    using namespace direct;
    bool configured=true;
    assert(load_ui_language(path,&configured)&&!configured&&uiLanguage==UiLanguage::Chinese);
    assert(save_ui_language(path,UiLanguage::English));
    uiLanguage=UiLanguage::Chinese;
    assert(load_ui_language(path,&configured)&&configured&&uiLanguage==UiLanguage::English);
    assert(std::string(ui_translate("字体设置"))=="Font settings");
    assert(std::string(ui_translate("image/fg/example.png"))=="image/fg/example.png");
    std::rename(path.c_str(),(path+".bak").c_str());
    assert(load_ui_language(path,&configured)&&configured&&uiLanguage==UiLanguage::English);
    assert(save_ui_language(path,UiLanguage::Chinese));
    assert(load_ui_language(path,&configured)&&configured&&uiLanguage==UiLanguage::Chinese);
    assert(std::string(ui_translate("字体设置"))=="字体设置");
    std::ofstream(path)<<"1 7\n";
    assert(!load_ui_language(path,&configured)&&!configured&&uiLanguage==UiLanguage::Chinese);
    assert(!save_ui_language(path+"/invalid",UiLanguage::English));
    assert(uiLanguage==UiLanguage::Chinese);
    std::set<std::string> keys;
    for(const auto& entry:uiTranslations){
        assert(*entry.chinese&&*entry.english);
        assert(keys.insert(entry.chinese).second);
    }
    std::remove(path.c_str());
}
