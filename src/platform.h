// Breakout Mayhem - console SDK glue (Switch / Vita)
// The SDK headers (switch.h, psp2/...) are only included in platform.cpp, so their many generic type
// names can never clash with the game's own types.
#pragma once

void platformInit();     // before SDL_Init: romfs, CPU clocks
bool platformRunning();  // false when the console asks the app to quit
void platformExit();     // after SDL_Quit
void makeDir(const char* path);
