#pragma once
#include "game_library.hpp"
#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include <unordered_set>

namespace direct {
struct LauncherSnapshot {
    uint64_t folderStamp=0;
    std::vector<art3m1s::GameEntry> games;
};

class LauncherCatalogCache {
public:
    static std::unique_ptr<LauncherSnapshot> load(const std::string& path){
        struct stat info{};
        if(::stat(path.c_str(),&info)!=0||!S_ISREG(info.st_mode)||info.st_size<=0
            ||uint64_t(info.st_size)>maxBytes)return {};
        LauncherCatalogCache in(path,"rb",size_t(info.st_size));
        if(!in.file_)return {};

        auto snapshot=std::make_unique<LauncherSnapshot>();
        uint64_t version=0;
        uint32_t count=0;
        if(!in.read(version)||version!=magic||!in.read(snapshot->folderStamp)
            ||!in.read(count)||count>maxEntries)return {};
        snapshot->games.reserve(count);
        std::unordered_set<std::string> ids;
        for(uint32_t i=0;i<count;++i){
            art3m1s::GameEntry game;
            if(!in.read_entry(game)||!ids.emplace(game.id).second)return {};
            snapshot->games.push_back(std::move(game));
        }
        if(in.remaining_!=0||std::fgetc(in.file_.get())!=EOF||std::ferror(in.file_.get()))return {};
        return snapshot;
    }

    static void save(const std::string& path,const LauncherSnapshot& snapshot){
        if(snapshot.games.size()>maxEntries)return;
        const auto temp=path+".tmp";
        LauncherCatalogCache out(temp,"wb",maxBytes);
        if(!out.file_)return;
        bool ok=out.write(magic)&&out.write(snapshot.folderStamp)&&out.write(uint32_t(snapshot.games.size()));
        for(const auto& game:snapshot.games){
            if(!ok)break;
            ok=out.write_entry(game);
        }
        if(std::fclose(out.file_.release())!=0)ok=false;
        if(ok&&std::rename(temp.c_str(),path.c_str())!=0){
            // Vita's rename cannot replace an existing file. A missing cache can be rebuilt.
            if(std::remove(path.c_str())==0)std::rename(temp.c_str(),path.c_str());
        }
        std::remove(temp.c_str());
    }

private:
    static constexpr uint64_t magic=0x3230435441433341ULL; // A3CATC02
    static constexpr size_t maxBytes=4*1024*1024,maxEntries=4096,maxExeCandidates=4096;
    static constexpr size_t maxExePath=512;
    struct CloseFile {
        void operator()(FILE* file)const{std::fclose(file);}
    };
    std::unique_ptr<FILE,CloseFile> file_;
    size_t remaining_;

    LauncherCatalogCache(const std::string& path,const char* mode,size_t budget)
        :file_(std::fopen(path.c_str(),mode)),remaining_(budget){
        if(file_)std::setvbuf(file_.get(),nullptr,_IOFBF,64*1024);
    }
    bool read_bytes(void* value,size_t size){
        if(size>remaining_||(size&&std::fread(value,1,size,file_.get())!=size))return false;
        remaining_-=size;
        return true;
    }
    bool write_bytes(const void* value,size_t size){
        if(size>remaining_||(size&&std::fwrite(value,1,size,file_.get())!=size))return false;
        remaining_-=size;
        return true;
    }
    template<class T> bool read(T& value){return read_bytes(&value,sizeof(value));}
    template<class T> bool write(T value){return write_bytes(&value,sizeof(value));}

    bool read_string(std::string& value,size_t maxLength){
        uint32_t length=0;
        if(!read(length)||length>maxLength||length>remaining_)return false;
        value.resize(length);
        return read_bytes(value.data(),length)&&value.find('\0')==std::string::npos;
    }
    bool write_string(const std::string& value,size_t maxLength){
        return value.size()<=maxLength&&write(uint32_t(value.size()))&&write_bytes(value.data(),value.size());
    }
    static bool valid_exe_path(const std::string& path,const std::string& file){
        const auto offset=path.size()+1;
        return file.size()>path.size()+1&&file.compare(0,path.size(),path)==0
            &&file[path.size()]=='/'&&file.find_first_of("/\\:",offset)==std::string::npos
            &&file.compare(offset,std::string::npos,".")!=0&&file.compare(offset,std::string::npos,"..")!=0;
    }
    bool read_entry(art3m1s::GameEntry& game){
        uint8_t flags=0;
        uint32_t count=0;
        if(!read(flags)||flags>7||!read_string(game.id,100)||!art3m1s::valid_game_id(game.id.c_str())
            ||!read_string(game.title,511)||!read_string(game.matching_exe,maxExePath)
            ||!read(count)||count>maxExeCandidates)return false;
        game.has_system_ini=flags&1;
        game.has_pfs=flags&2;
        game.bundled=flags&4;
        game.path=art3m1s::game_path(game.id,game.bundled);
        if(!game.matching_exe.empty()&&!valid_exe_path(game.path,game.matching_exe))return false;
        for(uint32_t i=0;i<count;++i){
            std::string file;
            if(!read_string(file,maxExePath)||!valid_exe_path(game.path,file))return false;
            game.exe_candidates.push_back(std::move(file));
        }
        return true;
    }
    bool write_entry(const art3m1s::GameEntry& game){
        const uint8_t flags=(game.has_system_ini?1:0)|(game.has_pfs?2:0)|(game.bundled?4:0);
        if(!write(flags)||!write_string(game.id,100)||!write_string(game.title,511)
            ||!write_string(game.matching_exe,maxExePath)||game.exe_candidates.size()>maxExeCandidates
            ||!write(uint32_t(game.exe_candidates.size())))return false;
        for(const auto& file:game.exe_candidates)
            if(!write_string(file,maxExePath))return false;
        return true;
    }
};
}
