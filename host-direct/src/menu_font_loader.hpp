#pragma once
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>

namespace direct {
class MenuFontLoader {
public:
    using Read=std::vector<uint8_t>(*)();
    ~MenuFontLoader(){clear();}
    bool started()const{return worker_.joinable();}
    bool begin(Read read){
        if(started())return true;
        ready_=false;
        try{
            worker_=std::thread([this,read]{
                try{bytes_=read();}catch(...){bytes_.clear();}
                ready_=true;
            });
        }catch(...){return false;}
        return true;
    }
    std::vector<uint8_t> take(bool wait){
        if(!started()||(!wait&&!ready_))return {};
        worker_.join();
        return std::move(bytes_);
    }
    void clear(){
        if(started())worker_.join();
        std::vector<uint8_t>().swap(bytes_);
    }
private:
    std::thread worker_;
    std::atomic<bool> ready_{false};
    std::vector<uint8_t> bytes_;
};
}
