#ifndef EM_APPEND_LOG_ASYNC_H
#define EM_APPEND_LOG_ASYNC_H
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
/* Producer fields are main-thread-only. Worker never retains this object's
 * address: queue records own the entire path and formatted line. */
typedef long long (*EmLogClock)(void);
typedef struct {long long total_us,last_us;unsigned records,errors,opens,closes;} EmAppendLog;
typedef struct {uint32_t accepted,written,dropped,errors,opens,closes,io_us_u32,max_io_us,high_water,queued,pending;int mode;} EmLogStats;
int em_log_start(EmLogClock clock);
void em_log_shutdown(void);
void em_log_stats(EmLogStats *out);
int em_log_printf(EmAppendLog *log,const char *path,EmLogClock clock,const char *format,...) __attribute__((format(printf,4,5)));
/* Model reload must not wait for disk I/O. Writer owns streams until shutdown. */
static inline int em_log_close(EmAppendLog *log,EmLogClock clock){(void)log;(void)clock;return 1;}

#ifdef EM_APPEND_LOG_IMPLEMENTATION
#define EM_LOG_CAPACITY 64u
#define EM_LOG_PATH 128u
#define EM_LOG_LINE 1024u
#ifndef EM_LOG_DIAGNOSTIC
#define EM_LOG_DIAGNOSTIC "ux0:data/emote-log-writer.txt"
#endif
#ifndef EM_LOG_BEFORE_POP
#define EM_LOG_BEFORE_POP() ((void)0)
#endif
#ifdef EM_LOG_ASYNC_TEST
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
static pthread_t em_writer_thread;
static sem_t em_writer_wake;
#ifndef EM_LOG_TEST_FAIL_INIT
#define EM_LOG_TEST_FAIL_INIT 0
#endif
#else
#include <psp2/kernel/threadmgr/thread.h>
#include <psp2/kernel/threadmgr/semaphore.h>
static SceUID em_writer_thread=-1,em_writer_wake=-1;
#endif
typedef struct {char path[EM_LOG_PATH],line[EM_LOG_LINE];unsigned length;} EmLogRecord;
typedef struct {char path[EM_LOG_PATH];FILE *file;} EmLogSink;
static struct {
 EmLogRecord queue[EM_LOG_CAPACITY];EmLogSink sinks[8];
 uint32_t head,tail,stop;int mode;EmLogStats counts;EmLogClock clock;
 int wake_ready,thread_created,started;
} em_logger;
static uint32_t em_atomic_get(const uint32_t *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void em_atomic_add(uint32_t *p,uint32_t n){__atomic_fetch_add(p,n,__ATOMIC_RELAXED);}
static void em_log_wake(void){
#ifdef EM_LOG_ASYNC_TEST
 if(sem_post(&em_writer_wake))abort();
#else
 if(sceKernelSignalSema(em_writer_wake,1)<0)abort();
#endif
}
static void em_log_wait(void){
#ifdef EM_LOG_ASYNC_TEST
 int rc;do{rc=sem_wait(&em_writer_wake);}while(rc&&errno==EINTR);if(rc)abort();
#else
 if(sceKernelWaitSema(em_writer_wake,1,NULL)<0)abort();
#endif
}
void em_log_stats(EmLogStats *s){
#define EM_COPY(n) s->n=em_atomic_get(&em_logger.counts.n)
 EM_COPY(accepted);EM_COPY(written);EM_COPY(dropped);EM_COPY(errors);EM_COPY(opens);EM_COPY(closes);EM_COPY(io_us_u32);EM_COPY(max_io_us);EM_COPY(high_water);EM_COPY(pending);
#undef EM_COPY
 s->mode=__atomic_load_n(&em_logger.mode,__ATOMIC_ACQUIRE);
 /* Called by producer: only tail can advance, so this never exceeds capacity. */
 s->queued=em_atomic_get(&em_logger.head)-em_atomic_get(&em_logger.tail);
}
static void em_log_io_time(long long start){
 long long elapsed=em_logger.clock()-start;uint32_t us=elapsed>UINT32_MAX?UINT32_MAX:elapsed>0?(uint32_t)elapsed:0;
 em_atomic_add(&em_logger.counts.io_us_u32,us);
 if(us>em_atomic_get(&em_logger.counts.max_io_us))__atomic_store_n(&em_logger.counts.max_io_us,us,__ATOMIC_RELEASE);
}
static void em_log_sink_close(EmLogSink *sink){if(sink->file){if(fclose(sink->file))em_atomic_add(&em_logger.counts.errors,1);sink->file=NULL;em_atomic_add(&em_logger.counts.closes,1);}}
static void em_log_write(const EmLogRecord *r){
 long long start=em_logger.clock();EmLogSink *sink=NULL;
 for(unsigned i=0;i<8;++i)if(!strcmp(em_logger.sinks[i].path,r->path)){sink=em_logger.sinks+i;break;}
 if(!sink)for(unsigned i=0;i<8;++i)if(!em_logger.sinks[i].path[0]){sink=em_logger.sinks+i;strcpy(sink->path,r->path);break;}
 int ok=0;
 if(sink){if(!sink->file){sink->file=fopen(sink->path,"a");if(sink->file)em_atomic_add(&em_logger.counts.opens,1);}
  if(sink->file){ok=fwrite(r->line,1,r->length,sink->file)==r->length&&fflush(sink->file)==0&&!ferror(sink->file);if(!ok)em_log_sink_close(sink);}
 }
 if(ok)em_atomic_add(&em_logger.counts.written,1);else em_atomic_add(&em_logger.counts.errors,1);
 em_log_io_time(start);
}
static void em_log_diagnostic(const char *phase,int rc){
 FILE*f=fopen(EM_LOG_DIAGNOSTIC,"a");if(!f)return;
 fprintf(f,"writer_%s rc=%d capacity=%u accepted=%u written=%u dropped=%u errors=%u opens=%u closes=%u io_us_u32=%u max_io_us=%u high_water=%u pending=%u\n",phase,rc,EM_LOG_CAPACITY,em_atomic_get(&em_logger.counts.accepted),em_atomic_get(&em_logger.counts.written),em_atomic_get(&em_logger.counts.dropped),em_atomic_get(&em_logger.counts.errors),em_atomic_get(&em_logger.counts.opens),em_atomic_get(&em_logger.counts.closes),em_atomic_get(&em_logger.counts.io_us_u32),em_atomic_get(&em_logger.counts.max_io_us),em_atomic_get(&em_logger.counts.high_water),em_atomic_get(&em_logger.counts.pending));fclose(f);
}
static void em_log_run(void){
 em_log_diagnostic("start",0);
 for(;;){em_log_wait();EM_LOG_BEFORE_POP();uint32_t tail=em_atomic_get(&em_logger.tail),head=em_atomic_get(&em_logger.head);
  if(tail!=head){EmLogRecord r=em_logger.queue[tail%EM_LOG_CAPACITY];__atomic_store_n(&em_logger.tail,tail+1,__ATOMIC_RELEASE);em_log_write(&r);em_atomic_add(&em_logger.counts.pending,UINT32_MAX);}
  else if(em_atomic_get(&em_logger.stop))break;
 }
 long long start=em_logger.clock();for(unsigned i=0;i<8;++i)em_log_sink_close(em_logger.sinks+i);em_log_io_time(start);em_log_diagnostic("drained",0);
}
#ifdef EM_LOG_ASYNC_TEST
static void *em_log_thread(void *arg){(void)arg;em_log_run();return NULL;}
#else
static int em_log_thread(SceSize size,void *arg){(void)size;(void)arg;em_log_run();return 0;}
#endif
int em_log_start(EmLogClock clock){
 if(em_logger.mode)return em_logger.mode==1;
 em_logger.clock=clock;int rc=-1;
#ifdef EM_LOG_ASYNC_TEST
 if(EM_LOG_TEST_FAIL_INIT==1||sem_init(&em_writer_wake,0,0))goto failed;em_logger.wake_ready=1;
 if(EM_LOG_TEST_FAIL_INIT==2||EM_LOG_TEST_FAIL_INIT==3||pthread_create(&em_writer_thread,NULL,em_log_thread,NULL))goto failed;em_logger.thread_created=1;
#else
 em_writer_wake=sceKernelCreateSema("emote logger wake",0,0,EM_LOG_CAPACITY+1,NULL);if(em_writer_wake<0){rc=em_writer_wake;goto failed;}em_logger.wake_ready=1;
 int priority=sceKernelGetThreadCurrentPriority();if(priority<0)priority=0x10000100;
 em_writer_thread=sceKernelCreateThread("emote log writer",em_log_thread,priority,64*1024,0,SCE_KERNEL_CPU_MASK_USER_ALL,NULL);if(em_writer_thread<0){rc=em_writer_thread;goto failed;}em_logger.thread_created=1;
 rc=sceKernelStartThread(em_writer_thread,0,NULL);if(rc<0)goto failed;
#endif
 em_logger.started=1;__atomic_store_n(&em_logger.mode,1,__ATOMIC_RELEASE);return 1;
failed:
#ifdef EM_LOG_ASYNC_TEST
 if(em_logger.wake_ready&&sem_destroy(&em_writer_wake))abort();
#else
 if(em_logger.thread_created&&sceKernelDeleteThread(em_writer_thread)<0)abort();
 if(em_logger.wake_ready&&sceKernelDeleteSema(em_writer_wake)<0)abort();
 em_writer_thread=em_writer_wake=-1;
#endif
 em_logger.wake_ready=em_logger.thread_created=0;__atomic_store_n(&em_logger.mode,-1,__ATOMIC_RELEASE);em_atomic_add(&em_logger.counts.errors,1);
 /* Only initialization, before rendering starts, may synchronously diagnose. */
 em_log_diagnostic("init_failed_no_sync_fallback",rc);return 0;
}
int em_log_printf(EmAppendLog *log,const char *path,EmLogClock clock,const char *format,...){
 long long start=clock();int ok=0;EmLogRecord r={0};size_t n=strlen(path);
 va_list args;va_start(args,format);int written=vsnprintf(r.line,sizeof r.line,format,args);va_end(args);
 if(__atomic_load_n(&em_logger.mode,__ATOMIC_ACQUIRE)==1&&n<sizeof r.path&&written>=0&&(unsigned)written<sizeof r.line){
  uint32_t head=em_atomic_get(&em_logger.head),tail=em_atomic_get(&em_logger.tail);
  if(head-tail<EM_LOG_CAPACITY){memcpy(r.path,path,n+1);r.length=(unsigned)written;em_logger.queue[head%EM_LOG_CAPACITY]=r;
   em_atomic_add(&em_logger.counts.accepted,1);em_atomic_add(&em_logger.counts.pending,1);__atomic_store_n(&em_logger.head,head+1,__ATOMIC_RELEASE);
   uint32_t depth=head+1-tail;if(depth>em_atomic_get(&em_logger.counts.high_water))__atomic_store_n(&em_logger.counts.high_water,depth,__ATOMIC_RELEASE);
   em_log_wake();ok=1;
  }
 }
 if(ok)++log->records;else{++log->errors;em_atomic_add(&em_logger.counts.dropped,1);}
 log->last_us=clock()-start;log->total_us+=log->last_us;return ok;
}
void em_log_shutdown(void){
 if(!em_logger.started){if(em_logger.mode==-1){em_log_diagnostic("unavailable_shutdown",-1);__atomic_store_n(&em_logger.mode,3,__ATOMIC_RELEASE);}return;}__atomic_store_n(&em_logger.mode,2,__ATOMIC_RELEASE);__atomic_store_n(&em_logger.stop,1,__ATOMIC_RELEASE);em_log_wake();
#ifdef EM_LOG_ASYNC_TEST
 if(pthread_join(em_writer_thread,NULL)||sem_destroy(&em_writer_wake))abort();
#else
 if(sceKernelWaitThreadEnd(em_writer_thread,NULL,NULL)<0||sceKernelDeleteThread(em_writer_thread)<0||sceKernelDeleteSema(em_writer_wake)<0)abort();em_writer_thread=em_writer_wake=-1;
#endif
 em_logger.started=em_logger.thread_created=em_logger.wake_ready=0;__atomic_store_n(&em_logger.mode,3,__ATOMIC_RELEASE);
 em_log_diagnostic("joined",0);
}
#endif
#endif
