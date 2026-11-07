#include "../../host-direct/src/loading_diagnostics.hpp"
#include <cassert>
#include <thread>
int main(){
    direct::LoadingDiagnostics d;
    d.record("W","GXM texture source missing: image/unused.png");assert(!d.snapshot());
    d.begin();
    d.record("W","[include:missing] optional.lua: skipped");assert(!d.snapshot());
    d.record("I","GXM texture source missing: image/probe.png");assert(!d.snapshot());
    d.record("W","GXM texture source missing: ui/960/lang_bg");
    d.record("W","GXM texture source missing: ui/960/lang_bg");
    auto issue=d.snapshot();assert(issue&&!issue.fatal&&issue.count==1&&issue.details[0]=="ui/960/lang_bg");
    std::thread a([&]{for(int i=0;i<1000;i++)d.record("W","GXM texture source missing: ui/second.png");});
    std::thread b([&]{for(int i=0;i<1000;i++)d.record("W","GXM texture source missing: ui/third.png");});
    a.join();b.join();assert(d.snapshot().count==3);
    d.ignore_resources();d.record("W","GXM texture source missing: ui/fourth.png");assert(!d.snapshot());
    d.record("E","解释器错误: missing.lua:12: failed");
    issue=d.snapshot();assert(issue.fatal&&issue.details[0].find("missing.lua:12")!=std::string::npos);
    d.end();assert(!d.snapshot());d.record("E","解释器错误: another failure");assert(!d.snapshot());
    d.begin();d.record("W","GXM texture source missing: image/new.png");assert(d.snapshot().count==1);
    d.record("E","解释器错误: fatal startup");assert(d.snapshot().fatal);
    d.begin();d.record("E","art3m1s_runtime_load_project_bytes: BOOT first.iet missing");
    assert(d.snapshot().fatal&&d.snapshot().details[0].find("first.iet")!=std::string::npos);
}
