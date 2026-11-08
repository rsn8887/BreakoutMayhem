// Breakout Mayhem - level definitions
#include "levels.h"
#include <cmath>

namespace {

World* W = nullptr;

Body& last() { return W->bodies.back(); }

Body brick(float x, float y) {
    Body b;
    b.kind = Kind::Brick;
    b.pos = Vec2(x, y);
    b.invMass = 0;
    b.counts = true;
    return b;
}

Body& rectBrick(float x, float y, float w, float h, int color, int hp = 1, int score = 0) {
    Body b = brick(x, y);
    b.shape = Shape::Rect;
    b.hw = w * 0.5f;
    b.hh = h * 0.5f;
    b.color = color;
    b.hp = b.maxHp = hp;
    b.score = score ? score : 100 * hp;
    W->add(b);
    return last();
}

Body& circBrick(float x, float y, float r, int color, int hp = 1, int score = 0) {
    Body b = brick(x, y);
    b.shape = Shape::Circle;
    b.r = r;
    b.color = color;
    b.hp = b.maxHp = hp;
    b.score = score ? score : 100 * hp;
    W->add(b);
    return last();
}

Body& metal(float x, float y, float w, float h) {
    Body& b = rectBrick(x, y, w, h, 8);
    b.btype = BrickType::Metal;
    b.counts = false;
    return b;
}

Body& bumper(float x, float y, float r, float kick = 380) {
    Body& b = circBrick(x, y, r, 7);
    b.btype = BrickType::Bumper;
    b.counts = false;
    b.kick = kick;
    return b;
}

Body& explosive(float x, float y, float w, float h) {
    Body& b = rectBrick(x, y, w, h, 0, 1, 150);
    b.btype = BrickType::Explosive;
    return b;
}

Body& multi(float x, float y, float w, float h) {
    Body& b = rectBrick(x, y, w, h, 4, 1, 150);
    b.btype = BrickType::Multi;
    return b;
}

Body& gswitch(float x, float y, float s) {
    Body& b = rectBrick(x, y, s, s, 6);
    b.btype = BrickType::GravSwitch;
    b.counts = false;
    return b;
}

Body& rail(float x0, float y0, float x1, float y1, float r) {
    Body b = brick((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
    b.shape = Shape::Capsule;
    b.btype = BrickType::Rail;
    b.counts = false;
    b.r = r;
    b.hw = Vec2(x1 - x0, y1 - y0).len() * 0.5f;
    b.angle = std::atan2(y1 - y0, x1 - x0);
    W->add(b);
    return last();
}

Body& spinner(float x, float y, float halfLen, float r, float angVel, float angle0 = 0) {
    Body b = brick(x, y);
    b.shape = Shape::Capsule;
    b.btype = BrickType::Rail;
    b.counts = false;
    b.kinematic = true;
    b.r = r;
    b.hw = halfLen;
    b.angle = angle0;
    b.angVel = angVel;
    W->add(b);
    return last();
}

Body& conveyor(float x, float y, float w, float h, float speed) {
    Body& b = rectBrick(x, y, w, h, 8);
    b.btype = BrickType::Conveyor;
    b.counts = false;
    b.surfaceSpeed = speed;
    return b;
}

Body& puck(float x, float y, float r, int color) {
    Body b = brick(x, y);
    b.shape = Shape::Circle;
    b.btype = BrickType::Puck;
    b.r = r;
    b.color = color;
    b.invMass = 1.0f / 0.05f;
    b.damping = 0.45f;
    b.gravScale = 0;
    b.maxSpeed = 1500;
    b.score = 500;
    W->add(b);
    return last();
}

Body& pendulum(float ax, float ay, float len, float w, float h, int color, int hp) {
    Body& b = rectBrick(ax, ay + len, w, h, color, hp);
    b.invMass = 1.0f / 0.12f;
    b.spring = true;
    b.anchor = Vec2(ax, ay);
    b.springRest = len;
    b.springK = 140;
    b.springDamp = 0.6f;
    b.damping = 0.15f;
    b.maxSpeed = 1200;
    return b;
}

void moving(Body& b, float ampX, float ampY, float freq, float phase = 0) {
    b.kinematic = true;
    b.path = Path::Osc;
    b.base = b.pos;
    b.amp = Vec2(ampX, ampY);
    b.freq = freq;
    b.phase = phase;
}

void hole(float x, float y, float r, float suctionR = 0, float suction = 3000) {
    Hole h;
    h.pos = Vec2(x, y);
    h.r = r;
    h.suctionR = suctionR > 0 ? suctionR : r * 2.6f;
    h.suction = suction;
    W->holes.push_back(h);
}

void wind(float x, float y, float w, float h, float ax, float ay, bool players = false) {
    Field f;
    f.type = FieldType::Wind;
    f.pos = Vec2(x, y);
    f.size = Vec2(w, h);
    f.accel = Vec2(ax, ay);
    f.affectsPlayers = players;
    W->fields.push_back(f);
}

void goo(float x, float y, float w, float h, float k, bool players = true) {
    Field f;
    f.type = FieldType::Goo;
    f.pos = Vec2(x, y);
    f.size = Vec2(w, h);
    f.strength = k;
    f.affectsPlayers = players;
    W->fields.push_back(f);
}

void well(float x, float y, float radius, float G) {
    Field f;
    f.type = FieldType::Well;
    f.pos = Vec2(x, y);
    f.radius = radius;
    f.strength = G;
    W->fields.push_back(f);
}

void repulsor(float x, float y, float radius, float acc) {
    Field f;
    f.type = FieldType::Repulsor;
    f.pos = Vec2(x, y);
    f.radius = radius;
    f.strength = acc;
    W->fields.push_back(f);
}

void portal(float ax, float ay, float bx, float by) {
    Portal p;
    p.a = Vec2(ax, ay);
    p.b = Vec2(bx, by);
    W->portals.push_back(p);
}

LevelInfo info(const char* name, const char* hint, float hue) {
    LevelInfo L;
    L.name = name;
    L.hint = hint;
    L.hue = hue;
    return L;
}

BallSpawn ball(float x, float y, float vx = 0, float vy = 0) {
    BallSpawn b;
    b.pos = Vec2(x, y);
    b.vel = Vec2(vx, vy);
    return b;
}

// --------------------------------------------------------------------------------------------

LevelInfo L01() {
    LevelInfo L = info("Warm Up", "Gravity pulls the ball down. Keep it out of the holes!", 225);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            rectBrick(150 + i * 92, 120 + j * 38, 84, 30, j);
            rectBrick(854 + i * 92, 120 + j * 38, 84, 30, j);
        }
    for (int i = 0; i < 3; i++) rectBrick(548 + i * 92, 120, 84, 30, 6, 2);
    hole(150, 640, 26, 70, 3200);
    hole(1130, 640, 26, 70, 3200);
    L.playerCenter = Vec2(640, 620);
    L.balls.push_back(ball(640, 470));
    return L;
}

LevelInfo L02() {
    LevelInfo L = info("Zero G", "No gravity at all. Two balls drift until you hit them.", 260);
    W->gravity = Vec2(0, 0);
    for (int i = 0; i < 8; i++) hole(120 + i * 146, 670, 26, 64, 2800);
    for (int i = 0; i < 7; i++) hole(193 + i * 146, 560, 26, 64, 2800);
    const int cols[4] = {6, 5, 4, 3};
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) rectBrick(640 + (i - 3.5f) * 126, 110 + j * 46, 110, 34, cols[j], j == 0 ? 2 : 1, 100 * (4 - j));
    L.playerCenter = Vec2(640, 470);
    BallSpawn a = ball(512, 380), b = ball(768, 380);
    a.gravScale = b.gravScale = 0;
    L.balls = {a, b};
    return L;
}

LevelInfo L03() {
    LevelInfo L = info("Bubbles", "Round bricks deflect at odd angles. Gold ones take three hits.", 190);
    for (int i = 0; i < 8; i++) hole(120 + i * 148, 660, 18, 54, 2600);
    for (int k = 0; k < 2; k++) {
        float x0 = k == 0 ? 140 : 1040;
        circBrick(x0, 150, 28, 2, 3, 500);
        circBrick(x0 + 100, 150, 28, 2, 3, 500);
        circBrick(x0, 250, 28, 7, 1, 100);
        circBrick(x0 + 100, 250, 28, 7, 1, 100);
    }
    for (int i = 0; i < 5; i++) {
        float x = 400 + i * 120;
        rectBrick(x, 110, 100, 26, 0, 2, 500);
        rectBrick(x, 140, 100, 26, 0, 2, 500);
        rectBrick(x, 170, 100, 26, 1, 1, 250);
        rectBrick(x, 200, 100, 26, 1, 1, 250);
        rectBrick(x, 270, 50, 50, 3);
        if (i == 2) multi(x, 350, 50, 50);
        else rectBrick(x, 350, 50, 50, 3);
    }
    L.playerCenter = Vec2(640, 600);
    L.balls.push_back(ball(640, 500));
    return L;
}

LevelInfo L04() {
    LevelInfo L = info("Orbit", "A disc bat in zero gravity. Holes hide under the bricks!", 40);
    W->gravity = Vec2(0, 0);
    const Vec2 c(640, 380);
    for (int i = 0; i < 12; i++) {
        float a = 2 * PI_F * i / 12;
        float x = c.x + 200 * std::sin(a), y = c.y + 190 * std::cos(a);
        if (i % 2) hole(x, y, 18, 50, 2400);
        circBrick(x, y, 26, 2, 1);
    }
    for (int i = 0; i < 10; i++) {
        float a = 2 * PI_F * (i + 0.5f) / 10;
        float x = c.x + 470 * std::sin(a), y = c.y + 280 * std::cos(a);
        hole(x, y, 18, 50, 2400);
        circBrick(x, y, 28, 0, 2);
    }
    L.discBat = true;
    L.playerCenter = c;
    BallSpawn b = ball(640, 290);
    b.gravScale = 0;
    b.damping = 0.6f;
    L.balls.push_back(b);
    return L;
}

LevelInfo L05() {
    LevelInfo L = info("Pinball", "Bumpers kick the ball. Rails roll it toward the drain - guard it!", 300);
    W->gravity = Vec2(0, 1300);
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 3; j++) rectBrick(640 + (i - 4.5f) * 112, 100 + j * 36, 100, 28, j);
    bumper(380, 300, 32);
    bumper(640, 260, 32);
    bumper(900, 300, 32);
    bumper(510, 400, 28);
    bumper(770, 400, 28);
    rail(10, 470, 470, 640, 9);
    rail(1270, 470, 810, 640, 9);
    hole(640, 680, 30, 80, 3200);
    hole(120, 330, 24, 62, 2800);
    hole(1160, 330, 24, 62, 2800);
    L.playerCenter = Vec2(640, 590);
    L.balls.push_back(ball(640, 480));
    return L;
}

LevelInfo L06() {
    LevelInfo L = info("Windy City", "Storm bands blow the ball sideways. Read the streaks!", 180);
    W->gravity = Vec2(0, 1000);
    wind(10, 260, 1260, 90, 1100, 0);
    wind(10, 400, 1260, 90, -1100, 0);
    const float xs[5] = {180, 410, 640, 870, 1100};
    const int rows[5] = {4, 3, 5, 3, 4};
    for (int t = 0; t < 5; t++)
        for (int j = 0; j < rows[t]; j++)
            for (int i = 0; i < 2; i++) {
                float x = xs[t] - 32 + i * 64, y = 96 + j * 32;
                if (t == 2 && j == 4 && i == 0) multi(x, y, 60, 26);
                else rectBrick(x, y, 60, 26, (t * 2 + j) % 8, j == 0 ? 2 : 1);
            }
    hole(250, 650, 26, 70, 3000);
    hole(640, 670, 26, 70, 3000);
    hole(1030, 650, 26, 70, 3000);
    L.playerCenter = Vec2(640, 600);
    L.balls.push_back(ball(640, 520));
    return L;
}

LevelInfo L07() {
    LevelInfo L = info("Ice Rink", "Your bat slides on the ice. Mind its momentum!", 195);
    W->gravity = Vec2(0, 500);
    for (int j = 0; j < 7; j++) {
        int k = 4 - std::abs(j - 3);
        for (int i = 0; i < k; i++)
            rectBrick(640 + (i - (k - 1) * 0.5f) * 98, 120 + j * 34, 90, 28, (j % 2) ? 8 : 4, j == 3 ? 2 : 1);
    }
    for (int i = 0; i < 3; i++) {
        rectBrick(150 + i * 96, 140, 88, 28, 5);
        rectBrick(1130 - i * 96, 140, 88, 28, 5);
    }
    hole(70, 110, 28, 70, 2800);
    hole(1210, 110, 28, 70, 2800);
    hole(70, 650, 28, 70, 2800);
    hole(1210, 650, 28, 70, 2800);
    hole(640, 690, 24, 60, 2800);
    L.batDamping = 0.8f;
    L.playerCenter = Vec2(640, 600);
    L.balls.push_back(ball(640, 520));
    return L;
}

LevelInfo L08() {
    LevelInfo L = info("Pendulums", "Bricks swing on springs. Knock them about!", 95);
    const float lens[7] = {300, 240, 200, 170, 200, 240, 300};
    for (int i = 0; i < 7; i++) pendulum(640 + (i - 3) * 160, 50, lens[i], 90, 30, i, 2);
    for (int i = 0; i < 6; i++) rectBrick(640 + (i - 2.5f) * 160, 92, 64, 24, 8);
    hole(150, 650, 26, 70, 3000);
    hole(640, 680, 26, 70, 3000);
    hole(1130, 650, 26, 70, 3000);
    L.playerCenter = Vec2(640, 610);
    L.balls.push_back(ball(640, 510));
    return L;
}

LevelInfo L09() {
    LevelInfo L = info("Black Hole", "The ball orbits a black hole. Don't let it fall in!", 275);
    W->gravity = Vec2(0, 0);
    const Vec2 c(640, 300);
    well(c.x, c.y, 760, 1.0e7f);
    hole(c.x, c.y, 30, 70, 2500);
    for (int i = 0; i < 14; i++) {
        float a = 2 * PI_F * i / 14;
        circBrick(c.x + 140 * std::cos(a), c.y + 140 * std::sin(a), 22, 6, 2);
    }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 2; j++) {
            rectBrick(100 + i * 90, 90 + j * 36, 82, 28, 4);
            rectBrick(1180 - i * 90, 90 + j * 36, 82, 28, 4);
            rectBrick(100 + i * 90, 400 + j * 36, 82, 28, 5);
            rectBrick(1180 - i * 90, 400 + j * 36, 82, 28, 5);
        }
    L.playerCenter = Vec2(640, 640);
    BallSpawn b = ball(640, 520, 210, 0);
    b.gravScale = 0;
    L.balls.push_back(b);
    return L;
}

LevelInfo L10() {
    LevelInfo L = info("Billiards", "Sink every coloured puck. Only the ball can push them!", 140);
    W->gravity = Vec2(0, 0);
    const float pr = 32;
    hole(44, 84, pr, 64, 2200);
    hole(640, 70, pr, 64, 2200);
    hole(1236, 84, pr, 64, 2200);
    hole(44, 676, pr, 64, 2200);
    hole(640, 690, pr, 64, 2200);
    hole(1236, 676, pr, 64, 2200);
    int col = 0;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= k; i++) puck(820 + k * 37, 380 + (i - k * 0.5f) * 42, 20, (col++) % 8);
    L.discBat = true;
    L.pills = false;
    L.playerCenter = Vec2(220, 380);
    BallSpawn b = ball(360, 380);
    b.gravScale = 0;
    b.damping = 0.35f;
    L.balls.push_back(b);
    return L;
}

LevelInfo L11() {
    LevelInfo L = info("Conveyor", "Belts carry the ball off to the side holes.", 25);
    conveyor(302, 340, 525, 22, -320);
    conveyor(977, 340, 525, 22, 320);
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 3; j++) rectBrick(640 + (i - 4.5f) * 112, 100 + j * 34, 100, 26, (i + j) % 8);
    Body& m1 = rectBrick(640, 230, 110, 26, 6, 2);
    moving(m1, 450, 0, 0.12f);
    Body& m2 = rectBrick(640, 270, 110, 26, 5, 2);
    moving(m2, 450, 0, 0.17f, 1.5f);
    hole(70, 650, 28, 74, 3000);
    hole(1210, 650, 28, 74, 3000);
    L.playerCenter = Vec2(640, 620);
    L.balls.push_back(ball(640, 520));
    return L;
}

LevelInfo L12() {
    LevelInfo L = info("Windmills", "Spinning blades fling the ball around.", 120);
    spinner(260, 330, 75, 9, 2.2f);
    spinner(520, 400, 75, 9, -2.2f, 0.8f);
    spinner(760, 400, 75, 9, 2.2f, 0.3f);
    spinner(1020, 330, 75, 9, -2.2f, 1.2f);
    for (int i = 0; i < 11; i++)
        for (int j = 0; j < 3; j++) {
            float x = 640 + (i - 5) * 106, y = 100 + j * 36;
            if (i == 5 && j == 2) multi(x, y, 96, 28);
            else rectBrick(x, y, 96, 28, (j * 3 + i / 4) % 8, j == 0 ? 2 : 1);
        }
    hole(200, 660, 26, 70, 3000);
    hole(640, 680, 26, 70, 3000);
    hole(1080, 660, 26, 70, 3000);
    L.playerCenter = Vec2(640, 600);
    L.balls.push_back(ball(640, 510));
    return L;
}

LevelInfo L13() {
    LevelInfo L = info("Upside Down", "Gravity points up! The holes are on the ceiling.", 330);
    W->gravity = Vec2(0, -1450);
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 4; j++) rectBrick(640 + (i - 4.5f) * 112, 600 - j * 36, 100, 28, 7 - j, j == 0 ? 2 : 1);
    hole(200, 90, 26, 70, 3000);
    hole(640, 90, 26, 70, 3000);
    hole(1080, 90, 26, 70, 3000);
    L.playerCenter = Vec2(640, 160);
    L.balls.push_back(ball(640, 260));
    return L;
}

LevelInfo L14() {
    LevelInfo L = info("Chain Reaction", "Red-striped bricks explode and set off their neighbours.", 10);
    auto isExp = [](int i, int j) {
        return (i == 2 && j == 1) || (i == 8 && j == 1) || (i == 5 && j == 3) || (i == 1 && j == 4) ||
               (i == 9 && j == 4) || (i == 5 && j == 0);
    };
    for (int i = 0; i < 11; i++)
        for (int j = 0; j < 6; j++) {
            float x = 640 + (i - 5) * 104, y = 100 + j * 34;
            if (isExp(i, j)) explosive(x, y, 96, 28);
            else if (j == 5 && (i == 3 || i == 7)) metal(x, y, 96, 28);
            else rectBrick(x, y, 96, 28, 1 + j % 3);
        }
    hole(180, 660, 26, 70, 3000);
    hole(640, 680, 26, 70, 3000);
    hole(1100, 660, 26, 70, 3000);
    L.playerCenter = Vec2(640, 600);
    L.balls.push_back(ball(640, 500));
    return L;
}

LevelInfo L15() {
    LevelInfo L = info("Wormholes", "Portals warp the ball above the steel floor.", 290);
    W->gravity = Vec2(0, 1200);
    metal(152.5f, 330, 285, 24);
    metal(485, 330, 240, 24);
    metal(795, 330, 240, 24);
    metal(1127.5f, 330, 285, 24);
    portal(150, 560, 1130, 140);
    portal(1130, 560, 150, 140);
    for (int i = 0; i < 7; i++)
        for (int j = 0; j < 4; j++) rectBrick(640 + (i - 3) * 100, 110 + j * 36, 92, 28, (i + 2 * j) % 8, j == 0 ? 2 : 1);
    hole(400, 670, 24, 64, 2800);
    hole(880, 670, 24, 64, 2800);
    L.playerCenter = Vec2(640, 600);
    L.balls.push_back(ball(640, 500));
    return L;
}

LevelInfo L16() {
    LevelInfo L = info("Gravity Switch", "Purple blocks turn gravity by 90 degrees!", 265);
    W->gravity = Vec2(0, 1300);
    gswitch(420, 230, 46);
    gswitch(860, 230, 46);
    gswitch(420, 530, 46);
    gswitch(860, 530, 46);
    const Vec2 c(640, 380);
    for (int i = 0; i < 8; i++) {
        float a = 2 * PI_F * i / 8;
        circBrick(c.x + 95 * std::cos(a), c.y + 95 * std::sin(a), 24, 2, 2);
    }
    for (int i = 0; i < 14; i++) {
        float a = 2 * PI_F * (i + 0.5f) / 14;
        circBrick(c.x + 190 * std::cos(a), c.y + 160 * std::sin(a), 22, 6);
    }
    for (int i = 0; i < 4; i++) {
        rectBrick(150, 180 + i * 130, 70, 28, 5);
        rectBrick(1130, 180 + i * 130, 70, 28, 5);
    }
    hole(900, 85, 28, 70, 3000);
    hole(380, 675, 28, 70, 3000);
    hole(45, 540, 28, 70, 3000);
    hole(1235, 220, 28, 70, 3000);
    L.playerCenter = Vec2(640, 625);
    L.balls.push_back(ball(640, 565));
    return L;
}

LevelInfo L17() {
    LevelInfo L = info("Swamp", "Thick goo slows everything down. Force fields push back.", 100);
    W->gravity = Vec2(0, 1100);
    goo(10, 420, 1260, 290, 3.5f, true);
    repulsor(330, 230, 120, 2200);
    repulsor(950, 230, 120, 2200);
    for (int k = 0; k < 2; k++) {
        float cx = k ? 950 : 330;
        for (int i = 0; i < 8; i++) {
            float a = 2 * PI_F * i / 8;
            circBrick(cx + 78 * std::cos(a), 230 + 78 * std::sin(a), 22, 3, 1);
        }
    }
    for (int i = 0; i < 12; i++) rectBrick(640 + (i - 5.5f) * 102, 96, 94, 26, i % 2 ? 3 : 2, 2);
    for (int i = 0; i < 3; i++) rectBrick(640, 180 + i * 40, 96, 28, 2);
    hole(250, 640, 28, 75, 2600);
    hole(640, 660, 28, 75, 2600);
    hole(1030, 640, 28, 75, 2600);
    L.playerCenter = Vec2(640, 560);
    L.balls.push_back(ball(640, 470));
    return L;
}

LevelInfo L18() {
    LevelInfo L = info("Heavy Metal", "A heavy cannonball - it shoves your bat around!", 210);
    W->gravity = Vec2(0, 1150);
    Body& bar = metal(640, 330, 220, 22);
    moving(bar, 380, 0, 0.15f);
    metal(240, 245, 30, 80);
    metal(1040, 245, 30, 80);
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 3; j++) {
            int hp = j == 0 ? 3 : 2;
            rectBrick(640 + (i - 4.5f) * 112, 100 + j * 36, 100, 28, j == 0 ? 8 : (j == 1 ? 5 : 4), hp);
        }
    hole(160, 660, 30, 76, 3000);
    hole(640, 680, 30, 76, 3000);
    hole(1120, 660, 30, 76, 3000);
    L.playerCenter = Vec2(640, 600);
    BallSpawn b = ball(640, 500);
    b.mass = 0.06f;
    b.r = 17;
    L.balls.push_back(b);
    return L;
}

LevelInfo L19() {
    LevelInfo L = info("Carousel", "Gravity slowly rotates around the room.", 60);
    W->gravity = Vec2(0, 1300);
    W->gravityRot = 0.35f;
    const Vec2 c(640, 380);
    bumper(c.x, c.y, 36, 300);
    for (int i = 0; i < 10; i++) {
        float a = 2 * PI_F * i / 10;
        circBrick(c.x + 110 * std::cos(a), c.y + 110 * std::sin(a), 24, 1, 2);
    }
    for (int i = 0; i < 16; i++) {
        float a = 2 * PI_F * (i + 0.5f) / 16;
        circBrick(c.x + 250 * std::cos(a), c.y + 200 * std::sin(a), 24, 7);
    }
    hole(70, 110, 28, 70, 3000);
    hole(1210, 110, 28, 70, 3000);
    hole(70, 650, 28, 70, 3000);
    hole(1210, 650, 28, 70, 3000);
    hole(640, 90, 26, 66, 3000);
    hole(640, 670, 26, 66, 3000);
    L.playerCenter = Vec2(640, 625);
    L.balls.push_back(ball(640, 545));
    return L;
}

LevelInfo L20() {
    LevelInfo L = info("Mayhem", "Everything at once. Good luck!", 340);
    W->gravity = Vec2(0, 1300);
    spinner(300, 430, 70, 9, 2.6f);
    spinner(980, 430, 70, 9, -2.6f);
    bumper(640, 420, 34);
    bumper(470, 320, 26);
    bumper(810, 320, 26);
    wind(10, 220, 1260, 60, 900, 0);
    portal(80, 600, 1200, 300);
    for (int i = 0; i < 11; i++)
        for (int j = 0; j < 4; j++) {
            float x = 640 + (i - 5) * 106, y = 95 + j * 30;
            if ((i == 2 && j == 1) || (i == 8 && j == 1) || (i == 5 && j == 2)) explosive(x, y, 96, 24);
            else if ((i == 0 || i == 10) && j == 3) multi(x, y, 96, 24);
            else rectBrick(x, y, 96, 24, (i + j) % 8, j == 0 ? 2 : 1);
        }
    hole(200, 670, 26, 70, 3000);
    hole(640, 690, 24, 64, 3000);
    hole(1080, 670, 26, 70, 3000);
    hole(60, 360, 24, 64, 3000);
    hole(1220, 360, 24, 64, 3000);
    L.playerCenter = Vec2(640, 610);
    L.balls = {ball(560, 520), ball(720, 520)};
    return L;
}

using Builder = LevelInfo (*)();
const Builder builders[] = {L01, L02, L03, L04, L05, L06, L07, L08, L09, L10,
                            L11, L12, L13, L14, L15, L16, L17, L18, L19, L20};

} // namespace

int levelCount() { return int(sizeof(builders) / sizeof(builders[0])); }

LevelInfo buildLevel(World& w, int index) {
    W = &w;
    w.clear();
    if (index < 0) index = 0;
    if (index >= levelCount()) index = levelCount() - 1;
    return builders[index]();
}
