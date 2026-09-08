#include "gpu.hpp"
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
extern "C" {
#include "uvdb.h"
volatile unsigned art3_uvdb_checkpoint_count = 0;
// A named, non-inlined second stop verifies relocation, breakpoints and memory.
__attribute__((noinline)) void art3_uvdb_checkpoint() {
    art3_uvdb_checkpoint_count = art3_uvdb_checkpoint_count + 1;
    asm volatile("" ::: "memory");
}
void art3_uvdb_startup() {
    direct::log("[uvdb] optional debug build; Release host/core, shader unchanged");
    SceIoStat info{};
    const char* flag = "ux0:data/art3m1s-gxm/uvdb-start.flag";
    if (sceIoGetstat(flag, &info) < 0) return;
    // Consume the request so a disconnected session cannot trap every restart.
    if (sceIoRemove(flag) < 0) {
        direct::log("[uvdb] cannot consume startup request; skipped");
        return;
    }
    // Match upstream's newlib socket initialization before its raw net syscalls.
    int socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        direct::log("[uvdb] network initialization failed errno=%d", errno);
        return;
    }
    close(socketFd);
    direct::menu_prepare("UVDB: waiting for GDB on port 1234", 24);
    direct::begin();
    direct::menu_text(48, 240, 24, "UVDB: waiting for GDB on port 1234", 0xffffffff);
    direct::end();
    direct::log("[uvdb] waiting at startup; connect matching ELF to Vita port 1234");
    std::fflush(nullptr);
    uvdb_enter();
    art3_uvdb_checkpoint();
    direct::log("[uvdb] startup resumed checkpoint=%u", art3_uvdb_checkpoint_count);
    std::fflush(nullptr);
}
}
