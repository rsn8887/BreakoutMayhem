// Breakout Mayhem - console SDK glue (Switch)
// The SDK header switch.h is only included in platform.cpp, so their many generic type
// names can never clash with the game's own types.
#pragma once

void platformInit();     // before SDL_Init: romfs
bool platformRunning();  // false when the console asks the app to quit
void platformExit();     // after SDL_Quit
void makeDir(const char* path);

// Switch: a single Joy-Con held sideways. SDL's Switch port reports no analog axes for it (only the stick as
// digital D-pad buttons), so its stick is read here directly. Returns true for a single Joy-Con and fills
// x/y (-1..1, screen orientation: +x right, +y down). joyIndex = SDL joystick instance id (= npad number).
bool platformSingleJoyConStick(int joyIndex, float& x, float& y);
