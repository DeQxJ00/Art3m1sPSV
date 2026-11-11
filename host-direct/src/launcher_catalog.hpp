#pragma once
#include "launcher_cover_loader.hpp"
#include "launcher_icon_cache.hpp"
#include <atomic>
#include <memory>
#include <system_error>
#include <tuple>
#include <unordered_map>

namespace direct {
struct LauncherArtwork {
    uint64_t iconStamp=0,coverStamp=0;
    bool deferredCover=false;
    std::shared_ptr<const std::vector<uint8_t>> icon,cover;
};

class LauncherCatalog {
public:
    struct Progress { bool active; size_t done,total; };
    ~LauncherCatalog(){suspend();}
    bool loaded()const{return initialized_;}
    const auto& games()const{return current_.games;}
    const LauncherArtwork* artwork(const std::string& id)const{
        const auto found=current_.artwork.find(id);
        return found==current_.artwork.end()?nullptr:&found->second;
    }
    Progress loading()const{
        const auto total=total_.load();
        return {!initialized_||total!=0,done_.load(),total};
    }
    bool poll(uint64_t now){
        if(worker_.joinable()||finished_){
            if(!finished_)return false;
            if(worker_.joinable())worker_.join();
            if(pending_&&!publishNextFrame_){
                publishNextFrame_=true;
                return false;
            }
            finished_=false;
            publishNextFrame_=false;
            nextScan_=now+3000000;
            total_=0;
            if(!pending_)return false;
            current_=std::move(*pending_);
            pending_.reset();
            initialized_=true;
            return true;
        }
        if(now<nextScan_)return false;
        finished_=false;
        done_=0;
        try{
            worker_=std::thread([this]{scan();});
        }catch(const std::system_error&){
            scan();
        }
        return false;
    }
    void suspend(){
        stopping_=true;
        if(worker_.joinable())worker_.join();
        pending_.reset();
        finished_=false;
        publishNextFrame_=false;
        total_=0;
        done_=0;
        nextScan_=0;
        stopping_=false;
    }
private:
    struct Snapshot {
        std::vector<art3m1s::GameEntry> games;
        std::unordered_map<std::string,LauncherArtwork> artwork;
    };
    Snapshot current_;
    std::unique_ptr<Snapshot> pending_;
    std::thread worker_;
    std::atomic<bool> stopping_{false},finished_{false};
    std::atomic<size_t> done_{0},total_{0};
    uint64_t nextScan_=0;
    bool initialized_=false,publishNextFrame_=false;

    void scan(){
        try{rebuild();}catch(...){pending_.reset();}
        finished_=true;
    }

    struct FileStamp {
        uint64_t hash=14695981039346656037ULL;
        bool present=false;
        void add(const std::string& path){
            struct stat info{};
            if(::stat(path.c_str(),&info)!=0||!S_ISREG(info.st_mode))return;
            present=true;
            const auto key=path+"\n"+std::to_string(info.st_size)+":"+std::to_string(info.st_mtime)
                +":"+std::to_string(info.st_ctime)+"\n";
            for(unsigned char c:key)hash=(hash^c)*1099511628211ULL;
        }
    };
    static auto metadata(const art3m1s::GameEntry& game){
        return std::tie(game.id,game.title,game.path,game.matching_exe,game.exe_candidates,
            game.has_system_ini,game.has_pfs,game.bundled);
    }
    static std::shared_ptr<const std::vector<uint8_t>> load_icon(const art3m1s::GameEntry& game){
        auto rgba=std::make_unique<std::array<uint8_t,launcherIconBytes>>();
        if(launcher_icon_read_png(game,*rgba)
            ||((!game.matching_exe.empty()||!game.exe_candidates.empty())&&launcher_icon_read_cache(game,*rgba)))
            return std::make_shared<const std::vector<uint8_t>>(rgba->begin(),rgba->end());
        return {};
    }
    void rebuild(){
        auto next=std::make_unique<Snapshot>();
        next->games=art3m1s::scan_games();
        bool changed=!initialized_||next->games.size()!=current_.games.size();
        // 64 KiB of swizzled BC1 per cover. Larger libraries load excess covers on demand.
        size_t coverSlots=192;
        for(size_t i=0;i<next->games.size();++i){
            if(stopping_)return;
            const auto& game=next->games[i];
            FileStamp icon,cover;
            for(const auto* name:launcherIconNames)icon.add(game.path+"/"+name);
            if(!game.matching_exe.empty()||!game.exe_candidates.empty())icon.add(launcher_icon_cache_path(game));
            for(const auto* name:launcherCoverNames)cover.add(game.path+"/"+name);
            auto& image=next->artwork[game.id];
            image.iconStamp=icon.hash;
            image.coverStamp=cover.hash;
            if(cover.present){
                image.deferredCover=coverSlots==0;
                if(coverSlots)--coverSlots;
            }
            const auto* old=artwork(game.id);
            if(!changed)
                changed=metadata(game)!=metadata(current_.games[i])||!old
                    ||old->iconStamp!=image.iconStamp||old->coverStamp!=image.coverStamp
                    ||old->deferredCover!=image.deferredCover;
        }
        if(!changed||stopping_)return;
        total_=std::max<size_t>(1,next->games.size());
        for(const auto& game:next->games){
            if(stopping_)return;
            auto& image=next->artwork.at(game.id);
            const auto* old=artwork(game.id);
            image.icon=old&&old->iconStamp==image.iconStamp?old->icon:load_icon(game);
            if(!image.deferredCover){
                if(old&&old->coverStamp==image.coverStamp&&!old->deferredCover)image.cover=old->cover;
                else{
                    auto pixels=LauncherCoverLoader::load_cover(game,".catalog.tmp");
                    if(!pixels.empty())image.cover=std::make_shared<const std::vector<uint8_t>>(std::move(pixels));
                }
            }
            ++done_;
        }
        done_=total_.load();
        pending_=std::move(next);
    }
};
}
