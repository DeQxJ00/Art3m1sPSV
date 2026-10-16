#ifndef EM_APPEND_LOG_H
#define EM_APPEND_LOG_H
#include <stdio.h>
#include <stdarg.h>
/* Host integrations own their diagnostics; never start a demo file writer. */
#if defined(EM_LOG_DISABLED)
typedef long long (*EmLogClock)(void);
typedef struct {long long total_us,last_us;unsigned records,errors,opens,closes;} EmAppendLog;
static inline int em_log_close(EmAppendLog *log,EmLogClock clock){(void)log;(void)clock;return 1;}
static inline int em_log_printf(EmAppendLog *log,const char *path,EmLogClock clock,const char *format,...){(void)log;(void)path;(void)clock;(void)format;return 1;}
#elif defined(__arm__) || defined(EM_LOG_ASYNC_TEST)
#include "append_log_async.h"
#else
/* Main-thread owned append stream. Each successful record is flushed for FTP
 * readers. Counters survive close/reopen; no asynchronous work or time removal. */
typedef long long (*EmLogClock)(void);
typedef struct {FILE *file;long long total_us,last_us;unsigned records,errors,opens,closes;} EmAppendLog;
static inline void em_log_elapsed(EmAppendLog *log,EmLogClock clock,long long start){log->last_us=clock()-start;log->total_us+=log->last_us;}
static inline int em_log_close(EmAppendLog *log,EmLogClock clock){
 if(!log->file)return 1;long long start=clock();int ok=fclose(log->file)==0;
 log->file=NULL;++log->closes;if(!ok)++log->errors;em_log_elapsed(log,clock,start);return ok;
}
static inline int em_log_printf(EmAppendLog *log,const char *path,EmLogClock clock,const char *format,...) __attribute__((format(printf,4,5)));
static inline int em_log_printf(EmAppendLog *log,const char *path,EmLogClock clock,const char *format,...){
 long long start=clock();int ok=0;
 if(!log->file){log->file=fopen(path,"a");if(log->file)++log->opens;}
 if(log->file){va_list args;va_start(args,format);int written=vfprintf(log->file,format,args);va_end(args);
  ok=written>=0&&fflush(log->file)==0&&!ferror(log->file);
  if(!ok){fclose(log->file);log->file=NULL;++log->closes;}
 }
 if(ok)++log->records;else ++log->errors;em_log_elapsed(log,clock,start);return ok;
}
#endif
#endif
