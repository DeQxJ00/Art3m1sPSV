#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace direct {
inline constexpr const char* updateApi="https://api.github.com/repos/DeQxJ00/Art3m1sPSV/releases/latest";
inline constexpr size_t updateMaxBody=128*1024;
struct UpdateResponse { long status=0; std::string body; std::string error; };
using UpdateFetch=UpdateResponse(*)(const std::atomic<bool>&);
// Stable numeric versions only. Local build suffixes do not make a matching
// release an upgrade (v1.3.11-dev.N -> v1.3.11 must not prompt).
std::string newer_release(const std::string& json,const std::string& installed);
UpdateResponse fetch_github_release(const std::atomic<bool>& cancelled);
class UpdateCheck {
public:
    explicit UpdateCheck(std::string installed,UpdateFetch fetch=fetch_github_release);
    ~UpdateCheck();
    UpdateCheck(const UpdateCheck&)=delete;
    UpdateCheck& operator=(const UpdateCheck&)=delete;
    void start();
    void tick(bool gameReady);
    bool visible(bool gameReady)const;
    const std::string& tag()const{return tag_;}
    const std::string& diagnostic()const{return diagnostic_;}
    bool completed()const{return consumed_;}
private:
    struct State;
    static void* worker(void*);
    std::shared_ptr<State> state_;
    std::string installed_,tag_,diagnostic_;
    bool started_=false,consumed_=false,shown_=false;
};
}
