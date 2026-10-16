#pragma once
#include <cstdlib>
#include <cstdint>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#else
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cfenv>
#endif
// Two bounded CPU evaluation workers. Every dispatch is joined before returning;
// workers never enter the facade registry or texture/GPU ownership code.
class RecoveredEvalPool {
public:
 using Evaluate=void(*)(void *);
 struct Worker {
  Evaluate evaluate=nullptr;void *object=nullptr;bool stop=false,started=false;
#ifdef __vita__
  pthread_t thread{};int start=-1,done=-1,priority=0,affinity=0,setup_result=0;uint32_t fpscr=0;
#else
  std::thread thread;std::mutex mutex;std::condition_variable cv;
  bool pending=false,complete=false;fenv_t fp{};
#endif
 };
 Worker workers[2];bool attempted=false,ready=false;
#ifdef __vita__
 static void signal(int sem){__sync_synchronize();if(sceKernelSignalSema(sem,1)<0)std::abort();}
 static void wait(int sem){if(sceKernelWaitSema(sem,1,nullptr)<0)std::abort();__sync_synchronize();}
 static void *entry(void *arg){Worker &w=*static_cast<Worker *>(arg);
  const auto self=sceKernelGetThreadId();w.setup_result=sceKernelChangeThreadPriority(self,w.priority);
  if(w.setup_result>=0)w.setup_result=sceKernelChangeThreadCpuAffinityMask(self,w.affinity);
  signal(w.done);
  for(;;){wait(w.start);if(w.stop)break;__asm__ volatile("vmsr fpscr, %0"::"r"(w.fpscr):"memory");w.evaluate(w.object);signal(w.done);}
  return nullptr; /* pthread TLS destructors and join own runtime cleanup. */}
#else
 static void entry(Worker *p){Worker &w=*p;std::unique_lock<std::mutex> lock(w.mutex);
  for(;;){w.cv.wait(lock,[&]{return w.pending||w.stop;});if(w.stop)break;
   lock.unlock();fesetenv(&w.fp);w.evaluate(w.object);lock.lock();w.pending=false;w.complete=true;w.cv.notify_one();}}
#endif
 bool init(){if(attempted)return ready;attempted=true;
  for(unsigned n=0;n<2;++n){Worker &w=workers[n];
#ifdef __vita__
   w.start=sceKernelCreateSema("emote evaluate start",0,0,1,nullptr);if(w.start<0){shutdown();attempted=true;return false;}
   w.done=sceKernelCreateSema("emote evaluate done",0,0,1,nullptr);if(w.done<0){shutdown();attempted=true;return false;}
   w.priority=sceKernelGetThreadCurrentPriority();if(w.priority<0)w.priority=0x10000100;
   w.affinity=n?SCE_KERNEL_CPU_MASK_USER_2:SCE_KERNEL_CPU_MASK_USER_1;
   pthread_attr_t attr;if(pthread_attr_init(&attr)){shutdown();attempted=true;return false;}
   int rc=pthread_attr_setstacksize(&attr,128*1024);
   if(!rc)rc=pthread_create(&w.thread,&attr,entry,&w);pthread_attr_destroy(&attr);
   if(rc){shutdown();attempted=true;return false;}
   w.started=true;wait(w.done);if(w.setup_result<0){shutdown();attempted=true;return false;}
#else
   try{w.thread=std::thread(entry,&w);}catch(...){shutdown();attempted=true;return false;}
#endif
   w.started=true;
  }ready=true;return true;
 }
 void dispatch(unsigned n,Evaluate fn,void *object){Worker &w=workers[n];
#ifndef __vita__
  std::lock_guard<std::mutex> lock(w.mutex);fegetenv(&w.fp);w.complete=false;
#else
  __asm__ volatile("vmrs %0, fpscr":"=r"(w.fpscr)::"memory");
#endif
  w.evaluate=fn;w.object=object;
#ifdef __vita__
  signal(w.start);
#else
  w.pending=true;w.cv.notify_one();
#endif
 }
 void join(unsigned n){Worker &w=workers[n];
#ifdef __vita__
  wait(w.done);
#else
  std::unique_lock<std::mutex> lock(w.mutex);w.cv.wait(lock,[&]{return w.complete;});
#endif
 }
 void shutdown(){for(auto &w:workers){if(w.started){
#ifdef __vita__
   w.stop=true;signal(w.start);if(pthread_join(w.thread,nullptr))std::abort();
#else
   {std::lock_guard<std::mutex> lock(w.mutex);w.stop=true;w.cv.notify_one();}w.thread.join();
#endif
   w.started=false;}
#ifdef __vita__
  if(w.start>=0)sceKernelDeleteSema(w.start);if(w.done>=0)sceKernelDeleteSema(w.done);
  w.start=w.done=-1;
#else
  w.pending=w.complete=false;
#endif
  w.stop=false;w.object=nullptr;w.evaluate=nullptr;
 }ready=attempted=false;}
 ~RecoveredEvalPool(){shutdown();}
};
