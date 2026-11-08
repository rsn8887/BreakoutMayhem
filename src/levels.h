// Breakout Mayhem - level definitions
#pragma once
#include <string>
#include <vector>
#include "physics.h"

struct BallSpawn {
    Vec2 pos, vel;
    float gravScale = 1, damping = 0, mass = 0.025f, r = 13;
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
    Vec2 gravity0;            // gravity at level start (restored after a lost life)
};

int levelCount();
LevelInfo buildLevel(World& w, int index); // index 0-based
