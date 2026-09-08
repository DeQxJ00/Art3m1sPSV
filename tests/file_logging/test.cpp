#include <borealis/core/logger.hpp>
#include <cassert>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
int main() {
    FILE* file=tmpfile();assert(file);
    brls::Logger::setLogOutput(file);
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    brls::Logger::setThreadSafeLogging(true);
    brls::Logger::info("[frame-perf] test {}",42);
    struct stat st={};assert(fstat(fileno(file),&st)==0&&st.st_size>0);
    auto size=st.st_size;
    brls::Logger::debug("filtered");
    assert(fstat(fileno(file),&st)==0&&st.st_size==size);
    rewind(file);char line[512]={};assert(fgets(line,sizeof(line),file));
    assert(strstr(line,"[INFO] [frame-perf] test 42"));
    assert(!strchr(line,'\033'));
    brls::Logger::setLogOutput(stdout);fclose(file);
    puts("PASS: production PSV logger writes and flushes file with debug console disabled; level filtering preserved");
}
