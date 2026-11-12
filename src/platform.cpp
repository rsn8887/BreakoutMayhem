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

bool platformSingleJoyConStick(int joyIndex, float& x, float& y) {
#if defined(__SWITCH__)
    if (joyIndex < 0 || joyIndex > 7) return false;
    HidNpadIdType id = HidNpadIdType(HidNpadIdType_No1 + joyIndex);
    u32 style = hidGetNpadStyleSet(id);
    if (style & HidNpadStyleTag_NpadJoyDual) return false;   // a pair of Joy-Cons acts like a full controller
    // The raw stick is in the Joy-Con's upright orientation; rotate it the same way SDL's Switch port rotates
    // the stick's digital directions (left Joy-Con: up -> left, right -> up; right Joy-Con: down -> left, left -> up).
    if (style & HidNpadStyleTag_NpadJoyLeft) {
        HidNpadJoyLeftState st;
        if (hidGetNpadStatesJoyLeft(id, &st, 1) == 0) return false;
        x = -st.analog_stick_l.y / 32767.f;
        y = -st.analog_stick_l.x / 32767.f;
        return true;
    }
    if (style & HidNpadStyleTag_NpadJoyRight) {
        HidNpadJoyRightState st;
        if (hidGetNpadStatesJoyRight(id, &st, 1) == 0) return false;
        x = st.analog_stick_r.y / 32767.f;
        y = st.analog_stick_r.x / 32767.f;
        return true;
    }
#else
    (void)joyIndex;
    (void)x;
    (void)y;
#endif
    return false;
}
