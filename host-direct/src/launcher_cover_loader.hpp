#pragma once
#include "game_library.hpp"
#include "runtime_api.h"
#include "compressed_layout.hpp"
#define STB_DXT_STATIC
#define STB_DXT_IMPLEMENTATION
#include "../third_party/stb_dxt.h"
#undef STB_DXT_IMPLEMENTATION
#undef STB_DXT_STATIC
#include <array>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <sys/stat.h>

namespace direct {
// The worker owns file IO and decoding; GPU resources remain on the UI thread.
class LauncherCoverLoader {
public:
    struct Result { size_t slot; std::vector<uint8_t> pixels; };
    ~LauncherCoverLoader(){stop();}
    // Called before game creation: join also releases in-flight decode buffers.
    void stop(){
        {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;pending_.clear();}
        wake_.notify_one();if(thread_.joinable())thread_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;std::deque<Job>().swap(pending_);std::deque<Result>().swap(ready_);
        stopping_=false;
    }
    void cancel(){std::lock_guard<std::mutex> lock(mutex_);++generation_;pending_.clear();ready_.clear();}
    bool request(const art3m1s::GameEntry& game,size_t slot){
        if(!thread_.joinable())try{thread_=std::thread([this]{run();});}catch(...){return false;}
        {std::lock_guard<std::mutex> lock(mutex_);pending_.push_back({game,slot,generation_});}
        wake_.notify_one();return true;
    }
    bool take(Result& result){
        std::lock_guard<std::mutex> lock(mutex_);if(ready_.empty())return false;
        result=std::move(ready_.front());ready_.pop_front();return true;
    }
private:
    struct Job {art3m1s::GameEntry game;size_t slot;uint64_t generation;};
    std::mutex mutex_;std::condition_variable wake_;std::thread thread_;
    bool stopping_=false;uint64_t generation_=0;
    std::deque<Job> pending_;std::deque<Result> ready_;
    void run(){
        for(;;){
            Job job;
            {std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[this]{return stopping_||!pending_.empty();});
                if(stopping_)return;job=std::move(pending_.front());pending_.pop_front();}
            std::vector<uint8_t> pixels;
            try{pixels=load_cover(job.game);}catch(...){pixels.clear();}
            {std::lock_guard<std::mutex> lock(mutex_);
                if(!stopping_&&job.generation==generation_)ready_.push_back({job.slot,std::move(pixels)});}
        }
    }
    static std::vector<uint8_t> load_cover(const art3m1s::GameEntry& game){
        constexpr size_t rgbaBytes=288*162*4, bytes=72*41*8;
        for(const char* name:{"cover.png","cover.jpg","cover.jpeg"}){
            const std::string path=game.path+"/"+name;struct stat st{};
            if(::stat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode)||st.st_size<8||st.st_size>8*1024*1024)continue;
            uint64_t hash=14695981039346656037ULL;
            const auto key=path+":"+std::to_string(st.st_size)+":"+std::to_string(st.st_mtime);
            for(unsigned char c:key)hash=(hash^c)*1099511628211ULL;
            const auto dir=std::string(art3m1s::kDataRoot)+"/cover-cache";
            const auto cache=dir+"/"+game.id+".bc1";
            std::vector<uint8_t> pixels(bytes);uint64_t header[2]{};
            FILE* f=std::fopen(cache.c_str(),"rb");bool ok=false;
            if(f){ok=std::fread(header,1,sizeof(header),f)==sizeof(header)&&header[0]==0x31424345564f4341ULL&&header[1]==hash
                    &&std::fread(pixels.data(),1,bytes,f)==bytes&&std::fgetc(f)==EOF;std::fclose(f);}
            if(!ok){
                f=std::fopen(path.c_str(),"rb");if(!f)continue;
                std::vector<uint8_t> source(size_t(st.st_size));
                ok=std::fread(source.data(),1,source.size(),f)==source.size();std::fclose(f);
                std::vector<uint8_t> rgba(rgbaBytes);
                if(!ok||!art3m1s_launcher_decode_cover(source.data(),source.size(),rgba.data(),rgba.size()))continue;
                // Flatten alpha onto the cover card, including aspect-fit padding.
                const unsigned background[]={28,40,56};
                for(size_t i=0;i<rgba.size();i+=4){const unsigned a=rgba[i+3];
                    for(unsigned c=0;c<3;++c)rgba[i+c]=(rgba[i+c]*a+background[c]*(255-a)+127)/255;
                    rgba[i+3]=255;
                }
                uint8_t tile[64];
                for(unsigned by=0;by<41;++by)for(unsigned bx=0;bx<72;++bx){
                    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
                        std::memcpy(tile+(y*4+x)*4,rgba.data()+(std::min(by*4+y,161u)*288+bx*4+x)*4,4);
                    stb_compress_dxt_block(pixels.data()+(by*72+bx)*8,tile,0,STB_DXT_HIGHQUAL);
                }
                ::mkdir(dir.c_str(),0777);const auto temp=cache+".tmp";f=std::fopen(temp.c_str(),"wb");
                if(f){header[0]=0x31424345564f4341ULL;header[1]=hash;
                    bool written=std::fwrite(header,1,sizeof(header),f)==sizeof(header)&&std::fwrite(pixels.data(),1,bytes,f)==bytes;
                    if(std::fclose(f)!=0)written=false;
                    if(written){if(std::rename(temp.c_str(),cache.c_str())!=0){std::remove(cache.c_str());std::rename(temp.c_str(),cache.c_str());}}
                    std::remove(temp.c_str());}
            }
            std::vector<uint8_t> swizzled(compressed_bytes(1,288,162,true));
            if(!compressed_swizzle(swizzled.data(),swizzled.size(),pixels.data(),pixels.size(),1,288,162))return {};
            return swizzled;
        }
        return {};
    }
};
}
