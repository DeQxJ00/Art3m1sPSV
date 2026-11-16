#pragma once
#include "launcher_cover_loader.hpp"
#include "launcher_catalog_cache.hpp"
#include <atomic>
#include <system_error>
#include <unordered_map>

namespace direct {
struct LauncherArtwork {
    bool iconLoaded=false,coverLoaded=false;
    std::vector<uint8_t> icon,cover;
};

class LauncherCatalog {
public:
    struct Progress { bool active; size_t done,total; };
    ~LauncherCatalog(){suspend();}
    bool loaded()const{return initialized_;}
    bool reindexed()const{return reindexed_;}
    const auto& games()const{return current_.games;}
    const LauncherArtwork* artwork(const std::string& id)const{
        const auto found=artwork_.find(id);
        return found==artwork_.end()?nullptr:&found->second;
    }
    void collect_artwork(){
        LauncherCoverLoader::Result result;
        while(artworkLoader_.take(result)){
            auto& art=artwork_[result.id];
            if(result.iconLoaded){
                art.iconLoaded=true;
                art.icon=std::move(result.icon);
            }
            if(result.coverLoaded){
                art.coverLoaded=true;
                art.cover=std::move(result.pixels);
            }
        }
    }
    void request_page_artwork(const std::vector<art3m1s::GameEntry>& games,size_t selected,size_t pageSize,bool covers){
        collect_artwork();
        const auto first=games.begin()+std::min(selected/pageSize*pageSize,games.size());
        const auto last=first+std::min(pageSize,size_t(games.end()-first));
        // Keep visited pages in memory within the existing thumbnail budget.
        if(artwork_.size()+pageSize>192){
            for(auto it=artwork_.begin();it!=artwork_.end();){
                const bool visible=std::any_of(first,last,[&](const auto& game){return game.id==it->first;});
                if(visible)++it;
                else it=artwork_.erase(it);
            }
        }
        std::vector<LauncherCoverLoader::Request> requests;
        for(auto it=first;it!=last;++it){
            const auto* art=artwork(it->id);
            const bool needsIcon=!art||(!art->iconLoaded&&(!covers||art->cover.empty()));
            const bool needsCover=covers&&(!art||!art->coverLoaded);
            if(needsIcon||needsCover)requests.push_back({*it,needsIcon,needsCover});
        }
        // Index checks and thumbnail reads share the device's storage.
        if(worker_.joinable()||finished_)deferredArtwork_=std::move(requests);
        else artworkLoader_.replace_pending(std::move(requests));
    }
    bool io_idle(){return !worker_.joinable()&&!finished_&&artworkLoader_.idle();}
    Progress loading()const{
        const auto total=total_.load();
        return {!initialized_||indexing_.load(),done_.load(),total};
    }
    bool poll(uint64_t now){
        if(worker_.joinable()||finished_){
            if(!finished_)return false;
            if(worker_.joinable())worker_.join();
            if(pending_&&!restoredCache_&&!publishNextFrame_){
                publishNextFrame_=true;
                return false;
            }
            finished_=false;
            publishNextFrame_=false;
            nextScan_=restoredCache_?now:now+3000000;
            reindexed_=!restoredCache_&&bool(pending_);
            restoredCache_=false;
            indexing_=false;
            total_=0;
            if(!pending_){
                artworkLoader_.replace_pending(std::move(deferredArtwork_));
                deferredArtwork_.clear();
                return false;
            }
            artworkLoader_.cancel();
            artwork_.clear();
            deferredArtwork_.clear();
            current_=std::move(*pending_);
            pending_.reset();
            checkedFolders_=current_.folderStamp;
            initialized_=true;
            return true;
        }
        if(now<nextScan_||!artworkLoader_.idle())return false;
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
        artworkLoader_.stop();
        if(worker_.joinable())worker_.join();
        pending_.reset();
        std::vector<LauncherCoverLoader::Request>().swap(deferredArtwork_);
        finished_=false;
        publishNextFrame_=false;
        restoredCache_=false;
        total_=0;
        done_=0;
        indexing_=false;
        nextScan_=0;
        stopping_=false;
    }
    void release_for_game(){
        suspend();
        current_=LauncherSnapshot{};
        decltype(artwork_){}.swap(artwork_);
        checkedFolders_.reset();
        initialized_=false;
        triedCache_=false;
    }
private:
    LauncherSnapshot current_;
    std::unique_ptr<LauncherSnapshot> pending_;
    const std::string cachePath_=std::string(art3m1s::kDataRoot)+"/library-catalog.bin";
    std::optional<uint64_t> checkedFolders_;
    LauncherCoverLoader artworkLoader_;
    std::unordered_map<std::string,LauncherArtwork> artwork_;
    std::vector<LauncherCoverLoader::Request> deferredArtwork_;
    std::thread worker_;
    std::atomic<bool> stopping_{false},finished_{false},indexing_{false};
    std::atomic<size_t> done_{0},total_{0};
    uint64_t nextScan_=0;
    bool initialized_=false,publishNextFrame_=false,restoredCache_=false,reindexed_=false;
    bool triedCache_=false;

    void scan(){
        try{
            if(!triedCache_){
                triedCache_=true;
                pending_=LauncherCatalogCache::load(cachePath_);
            }
            if(pending_)restoredCache_=true;
            else rebuild();
        }catch(...){pending_.reset();}
        finished_=true;
    }

    void rebuild(){
        const auto folders=art3m1s::game_folder_stamp();
        if(initialized_&&(!folders||folders==checkedFolders_))return;
        auto next=std::make_unique<LauncherSnapshot>();
        next->folderStamp=folders.value_or(0);
        indexing_=true;
        next->games=art3m1s::scan_games();
        if(stopping_)return;
        if(folders)LauncherCatalogCache::save(cachePath_,*next);
        if(stopping_)return;
        total_=1;
        done_=1;
        pending_=std::move(next);
    }
};
}
