// Standalone compile-only probe. No game data, GXM scene or renderer is opened.
#include <vitashark.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

unsigned int _newlib_heap_size_user = 64 * 1024 * 1024;
static FILE *report;
static void log_line(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(report, fmt, args);
    va_end(args);
    fputc('\n', report);
    fflush(report);
}
static void compiler_log(const char *msg, shark_log_level level, int line) {
    log_line("compiler level=%d line=%d %s", level, line, msg);
}

int main(void) {
    const char *directory = "ux0:/data/art3m1s-rule-compiler";
    sceIoMkdir(directory, 0777);
    report = fopen("ux0:/data/art3m1s-rule-compiler/compile.log", "w");
    if (!report) return 1;
    log_line("rule compiler build %s %s; SHARK_OPT_SAFE; fastmath/precision/int disabled", __DATE__, __TIME__);
    // Remove only our prior output, so a failed attempt cannot look successful.
    sceIoRemove("ux0:/data/art3m1s-rule-compiler/rule_transition_f.gxp");
    FILE *input = fopen("app0:/rule_transition_f.cg", "rb");
    if (!input) { log_line("FAIL source open"); fclose(report); return 1; }
    char source[16384];
    size_t length = fread(source, 1, sizeof(source)-1, input);
    int read_failed = ferror(input) || !feof(input);
    fclose(input);
    if (read_failed || !length) { log_line("FAIL source read/size"); fclose(report); return 1; }
    source[length] = 0;
    log_line("source_bytes=%u", (unsigned)length);
    shark_install_log_cb(compiler_log);
    shark_set_warnings_level(SHARK_WARN_MAX);
    int result = shark_init(NULL);
    if (result < 0) {
        log_line("FAIL shark_init=%08x; requires ur0:/data/libshacccg.suprx", (unsigned)result);
        fclose(report); return 1;
    }
    uint32_t size = (uint32_t)length;
    uint64_t started = sceKernelGetProcessTimeWide();
    SceGxmProgram *program = shark_compile_shader_extended(source, &size,
        SHARK_FRAGMENT_SHADER, SHARK_OPT_SAFE, SHARK_DISABLE, SHARK_DISABLE, SHARK_DISABLE);
    int ok = program != NULL;
    log_line("compile_us=%llu output_bytes=%u", (unsigned long long)(sceKernelGetProcessTimeWide()-started), size);
    if (program) {
        const char *names[] = {"frag", "oldFrame", "ruleMap"};
        for (unsigned i=0; i<3; ++i) {
            const SceGxmProgramParameter *parameter = sceGxmProgramFindParameterByName(program, names[i]);
            if (!parameter) { log_line("FAIL missing parameter=%s", names[i]); ok=0; continue; }
            unsigned index = sceGxmProgramParameterGetResourceIndex(parameter);
            log_line("parameter=%s resource_index=%u", names[i], index);
            if (i && index != i-1) { log_line("FAIL sampler binding"); ok=0; }
        }
        if (ok) {
            FILE *output = fopen("ux0:/data/art3m1s-rule-compiler/rule_transition_f.gxp", "wb");
            if (!output) ok=0;
            else {
                if (fwrite(program, 1, size, output) != size) ok=0;
                if (fclose(output)) ok=0;
            }
        }
        free(program);
    }
    shark_clear_output();
    shark_end();
    if (!ok) sceIoRemove("ux0:/data/art3m1s-rule-compiler/rule_transition_f.gxp");
    log_line("%s (compilation only; no GPU rendering validation)", ok ? "PASS" : "FAIL");
    fclose(report);
    return ok ? 0 : 1;
}
