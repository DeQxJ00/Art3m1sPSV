#pragma once
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#define SCE_O_RDONLY O_RDONLY
#define SCE_O_WRONLY O_WRONLY
#define SCE_O_CREAT O_CREAT
#define SCE_O_TRUNC O_TRUNC
#define SCE_SEEK_END SEEK_END
#define SCE_SEEK_SET SEEK_SET
#define SCE_S_ISDIR S_ISDIR
typedef struct { uint64_t st_size; unsigned st_mode; } SceIoStat;
int sceIoOpen(const char*,int,int);
int sceIoClose(int);
int64_t sceIoLseek(int,int64_t,int);
int sceIoRead(int,void*,int);
int sceIoWrite(int,const void*,int);
int sceIoMkdir(const char*,int);
int sceIoRemove(const char*);
int sceIoRename(const char*,const char*);
int sceIoGetstat(const char*,SceIoStat*);
