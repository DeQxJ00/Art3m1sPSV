#include "../../host-direct/src/log_queue.hpp"
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <cassert>
namespace {
std::mutex mutex;std::condition_variable gate;bool entered=false,released=false;
uint64_t micros(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void sink(const char*,size_t,bool flush){if(flush)return;std::unique_lock<std::mutex> lock(mutex);entered=true;gate.notify_all();gate.wait(lock,[]{return released;});}
void emit(LogQueue& q,const char* fmt,...){va_list a;va_start(a,fmt);q.append(fmt,a);va_end(a);}
}
int main(){
 auto q=std::make_unique<LogQueue>();assert(q->start(sink,micros));emit(*q,"first");
 {std::unique_lock<std::mutex> lock(mutex);assert(gate.wait_for(lock,std::chrono::seconds(2),[]{return entered;}));}
 auto task=std::async(std::launch::async,[&]{
  std::vector<std::thread> ts;
  for(unsigned n=0;n<4;++n)ts.emplace_back([&,n]{for(unsigned i=0;i<1000;++i)emit(*q,"producer=%u iteration=%u at=%llu",n,i,(unsigned long long)micros());});
  for(auto& t:ts)t.join();q->flush();return q->stats();
 });
 bool finished=task.wait_for(std::chrono::seconds(2))==std::future_status::ready;
 // Release before assertions: even a failed progress check must drain safely.
 {std::lock_guard<std::mutex> lock(mutex);released=true;}gate.notify_all();
 auto stats=task.get();q->stop();assert(finished);assert(stats.dropped>0);assert(stats.queued<=LogQueue::Slots);
 std::puts("PASS: blocked storage does not block four producers; bounded drops and clean stop");
}
