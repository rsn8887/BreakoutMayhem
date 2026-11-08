// Breakout Mayhem - level definitions
#pragma once
#include <string>
#include <vector>
#include "physics.h"

struct BallSpawn {
    Vec2 pos, vel;
    float gravScale = 1, damping = 0, mass = 0.025f, r = 13;
};

// rubber band created together with the balls: between balls a and b (indices into LevelInfo::balls),
// or between ball a and a brick (brickId, b = -1)
struct BandSpawn {
    int a = -1, b = -1;
    int brickId = -1;
    float rest = 100, k = 1.0f, damp = 0.03f;
};

struct LevelInfo {
    std::string name;
    std::string hint;
    float hue = 220;          // background tint
    bool discBat = false;
    float batDamping = 12;    // 12 = normal, low = ice
    Vec2 playerCenter{640, 610};
    std::vector<BallSpawn> balls;
    bool pills = true;        // power-ups may drop
    bool enemies = false;     // enemies appear: laser power-ups may drop
    std::vector<BandSpawn> bands;
    Vec2 gravity0;            // gravity at level start (restored after a lost life)
};

int levelCount();
constexpr int LEVELS_PER_SET = 5;
inline int levelSetCount() { return (levelCount() + LEVELS_PER_SET - 1) / LEVELS_PER_SET; }
LevelInfo buildLevel(World& w, int index); // index 0-based
