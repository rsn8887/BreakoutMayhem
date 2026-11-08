// Breakout Mayhem - console SDK glue (Switch / Vita)
#include "platform.h"
#include <sys/stat.h>

#if defined(__SWITCH__)
#include <switch.h>
#elif defined(__vita__)
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
int _newlib_heap_size_user = 160 * 1024 * 1024;
#endif

void platformInit() {
#if defined(__SWITCH__)
    romfsInit();
#elif defined(__vita__)
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
#endif
}

bool platformRunning() {
#if defined(__SWITCH__)
    return appletMainLoop();
#else
    return true;
#endif
}

void platformExit() {
#if defined(__SWITCH__)
    romfsExit();
#elif defined(__vita__)
    sceKernelExitProcess(0);
#endif
}

void makeDir(const char* path) {
#if defined(_WIN32)
    (void)path;
#else
    mkdir(path, 0777);
#endif
}
