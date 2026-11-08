// Breakout Mayhem - tiny procedural sound synth
#pragma once

enum Sfx {
    SFX_BAT, SFX_WALL, SFX_BRICK, SFX_BREAK, SFX_METAL, SFX_BUMPER, SFX_EXPLODE, SFX_HOLE,
    SFX_PILL, SFX_PORTAL, SFX_GRAVITY, SFX_CLEAR, SFX_LOSE, SFX_GAMEOVER, SFX_PUCK, SFX_SINK, SFX_MENU,
    SFX_COUNT
};

namespace Audio {
bool init();
void shutdown();
void play(Sfx s, float volume = 1.0f, float pitch = 1.0f);
void setEnabled(bool on);
bool enabled();
}
