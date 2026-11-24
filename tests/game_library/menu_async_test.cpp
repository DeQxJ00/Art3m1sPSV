#include "../../host-direct/src/menu_font_loader.hpp"
#include "../../host-direct/src/menu_glyph_cache.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <thread>

static std::atomic<bool> reading{false},releaseRead{false};
static std::vector<uint8_t> read_font(){
    reading=true;
    while(!releaseRead)std::this_thread::yield();
    return {10,20,30};
}

int main(){
    direct::MenuFontLoader font;
    assert(font.begin(read_font));
    while(!reading)std::this_thread::yield();
    assert(font.take(false).empty()&&font.started());
    releaseRead=true;
    auto bytes=font.take(true);
    assert((bytes==std::vector<uint8_t>{10,20,30}));
    assert(!font.started());
    font.clear();
    assert(font.begin(read_font));
    font.clear();
    assert(!font.started()&&font.take(false).empty());

    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("menu-async-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto path=(root/"glyphs.bin").string(),temporary=(root/"glyphs.tmp").string();
    assert(::mkfifo(temporary.c_str(),0600)==0);
    direct::MenuGlyphCache cache;
    const uint64_t first=(uint64_t(24)<<32)|'A',second=(uint64_t(24)<<32)|'B';
    assert(cache.insert(first,{2,2,0,2,3,{10,20,30,40}}));
    direct::MenuGlyphWriter writer;
    // The writer blocks opening its FIFO while the UI continues using its cache.
    assert(writer.submit(cache,path,temporary,42));
    assert(!writer.idle());
    assert(cache.insert(second,{1,1,0,1,2,{255}}));
    std::ifstream pipe(temporary,std::ios::binary);
    std::vector<char> saved((std::istreambuf_iterator<char>(pipe)),{});
    writer.wait();
    assert(!saved.empty()&&writer.idle());
    fs::remove(path); // The FIFO was renamed after its output drained.
    {
        std::ofstream file(path,std::ios::binary);
        file.write(saved.data(),saved.size());
    }
    const auto firstSnapshot=direct::MenuGlyphCache::load(path.c_str(),42);
    assert(firstSnapshot.find(first)&&!firstSnapshot.find(second));
    assert(writer.submit(cache,path,temporary,42));
    writer.wait();
    const auto restored=direct::MenuGlyphCache::load(path.c_str(),42);
    assert(restored.find(first)&&restored.find(second));
    assert(writer.submit(cache,(root/"missing/glyphs.bin").string(),(root/"missing/glyphs.tmp").string(),42));
    writer.wait();
    assert(writer.retry_needed()&&!writer.retry_needed());
    fs::remove_all(root);
}
