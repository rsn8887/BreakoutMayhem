// Breakout Mayhem - console SDK glue (Switch)
#include "platform.h"
#include <sys/stat.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

void platformInit() {
#if defined(__SWITCH__)
    romfsInit();
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
#endif
}

void makeDir(const char* path) {
#if defined(_WIN32)
    (void)path;
#else
    mkdir(path, 0777);
#endif
}
