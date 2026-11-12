// Breakout Mayhem - game logic
#include "game.h"
#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace {

std::mt19937 rng(12345);
float frand(float a = 0, float b = 1) { return std::uniform_real_distribution<float>(a, b)(rng); }

constexpr float PHYS_DT = 1.0f / 480.0f;
constexpr float DISC_R = 30;
constexpr int MAX_BALLS = 8;

bool breakable(const Body& b) {
    if (b.kind != Kind::Brick) return false;
    switch (b.btype) {
        case BrickType::Normal: case BrickType::Explosive: case BrickType::Multi: case BrickType::BigBall:
        case BrickType::Magnet: case BrickType::Nest: case BrickType::Bulb:
            return true;
        default:
            return false;
    }
}

constexpr int MAX_ENEMIES = 6;

} // namespace

uint32_t brickColor(int idx) {
    static const uint32_t pal[9] = {0xE23B3B, 0xF28A1E, 0xF2D21E, 0x4CC94C, 0x2EC4D6,
                                    0x3B6BE2, 0x9B4BE2, 0xE25BB0, 0xC8CCD4};
    return pal[((idx % 9) + 9) % 9];
}

uint32_t playerColor(int idx) {
    static const uint32_t pal[4] = {0xE5322E, 0x2B4BE8, 0xF2C81E, 0xC02BE8};
    return pal[idx & 3];
}

const char* pillLabel(int t) {
    static const char* l[PILL_COUNT] = {"E", "3", "S", "M", "G", "+", "-", "L"};
    return l[t];
}
const char* pillName(int t) {
    static const char* l[PILL_COUNT] = {"BIG BAT", "MULTIBALL", "SLOW-MO", "MEGA BALL", "MAGNET", "EXTRA LIFE", "SMALL BAT", "LASER"};
    return l[t];
}
uint32_t pillColor(int t) {
    static const uint32_t c[PILL_COUNT] = {0x3FBF4F, 0x2EC4D6, 0xF29A1E, 0xE23B3B, 0x3B6BE2, 0xE25BB0, 0x6A6A70, 0xE8C020};
    return c[t];
}

// ------------------------------------------------------------------------------------------

void Game::init() {
    rng.seed(uint32_t(std::random_device{}()));
    state = State::Title;
    demo = true;
    numPlayers = 1;
    startSet = std::min(startSet, setChoices() - 1);
    startDemoLevel(int(frand(0, float(levelCount()))) % levelCount(), 1);
    state = State::Title;
    demo = true;
}

void Game::startDemoLevel(int lvl, int players) {
    worldPlayers = players;
    for (int i = 0; i < MAX_PLAYERS; i++) aiPlayers[i] = true;
    loadLevel(lvl);
    state = State::Playing;
    demoTimer = 0;
}

// level range of a menu choice: sets of five, the last choice plays every level in sequence
void Game::setRange(int set, int& first, int& last) {
    if (set >= levelSetCount()) {
        first = 0;
        last = levelCount() - 1;
    } else {
        first = set * LEVELS_PER_SET;
        last = std::min(levelCount(), first + LEVELS_PER_SET) - 1;
    }
}

void Game::startGame(int players, int set) {
    int lvl;
    setRange(set, runFirst, runLast);
    lvl = runFirst;
    demo = false;
    numPlayers = worldPlayers = players;
    for (int i = 0; i < MAX_PLAYERS; i++) aiPlayers[i] = false;
    score = 0;
    lives = 3;
    nextExtraLife = 20000;
    loadLevel(lvl);
    state = State::Intro;
    stateTime = 0;
    Audio::play(SFX_MENU);
}

void Game::loadLevel(int lvl) {
    level = lvl;
    info = buildLevel(world, lvl);
    info.gravity0 = world.gravity;
    particles.clear();
    ghosts.clear();
    texts.clear();
    blasts.clear();
    tEnlarge = tShrink = tSlow = tMega = tMagnet = tGun = 0;
    accumulator = 0;
    bulbsTotal = 0;
    for (const auto& b : world.bodies)
        if (b.btype == BrickType::Bulb) bulbsTotal++;
    darkness = 0;
    spawnActors();
    levelChanged++;
}

void Game::spawnActors() {
    auto& bs = world.bodies;
    bs.erase(std::remove_if(bs.begin(), bs.end(),
                            [](const Body& b) {
                                return b.kind == Kind::Player || b.kind == Kind::Ball || b.kind == Kind::Pill ||
                                       b.kind == Kind::Enemy;
                            }),
             bs.end());
    world.bands.clear();
    shots.clear();
    tEnlarge = tShrink = tSlow = tMega = tMagnet = tGun = 0;
    world.gravity = info.gravity0;
    int n = std::max(1, std::min(worldPlayers, MAX_PLAYERS));
    bool vertical = info.playerCenter.x < 400;
    float spacing = info.discBat ? 90.f : 190.f;
    for (int i = 0; i < n; i++) {
        Body p;
        p.kind = Kind::Player;
        p.player = i;
        p.color = i;
        float off = (i - (n - 1) * 0.5f) * spacing;
        p.pos = info.playerCenter + (vertical ? Vec2(0, off) : Vec2(off, 0));
        if (info.discBat) { p.shape = Shape::Circle; p.r = DISC_R; }
        else { p.shape = Shape::Capsule; p.hw = BAT_HW - BAT_R; p.r = BAT_R; p.hh = BAT_R; }
        p.invMass = 1.0f / 0.125f;
        p.damping = info.batDamping;
        p.gravScale = 0;
        p.maxSpeed = 1400;
        p.thrust = 12;
        world.add(p);
    }
    std::vector<int> ballIds;
    for (const auto& s : info.balls) ballIds.push_back(spawnBall(s));
    for (const auto& bs : info.bands) {
        if (bs.a < 0 || bs.a >= int(ballIds.size())) continue;
        Band band;
        band.a = ballIds[bs.a];
        if (bs.b >= 0 && bs.b < int(ballIds.size())) band.b = ballIds[bs.b];
        else if (bs.brickId > 0 && world.find(bs.brickId)) band.b = bs.brickId;   // tether to a brick
        else continue;
        band.rest = bs.rest;
        band.k = bs.k;
        band.damp = bs.damp;
        world.bands.push_back(band);
    }
}

int Game::spawnBall(const BallSpawn& s) {
    Body b;
    b.kind = Kind::Ball;
    b.shape = Shape::Circle;
    b.r = s.r;
    b.pos = s.pos;
    b.vel = s.vel;
    b.invMass = 1.0f / s.mass;
    b.damping = s.damping;
    b.gravScale = s.gravScale;
    b.maxSpeed = 1700;
    b.mega = tMega > 0;
    return world.add(b);
}

void Game::spawnPill(Vec2 pos, Vec2 dir, int forcedType) {
    int weights[PILL_COUNT] = {18, 16, 12, 12, 14, 6, 12, 0};
    if (info.enemies) weights[PILL_GUN] = 16;   // lasers only where there are enemies to shoot
    int total = 0;
    for (int w : weights) total += w;
    int r = int(frand(0, float(total)));
    int type = 0;
    while (type < PILL_COUNT - 1 && r >= weights[type]) r -= weights[type++];
    if (forcedType >= 0) type = forcedType;
    Body p;
    p.kind = Kind::Pill;
    p.shape = Shape::Rect;
    p.hw = 22;
    p.hh = 11;
    p.pos = pos;
    p.pill = type;
    p.life = 12;
    p.invMass = 1.0f / 0.03f;
    p.maxSpeed = 500;
    float g = world.gravity.len();
    if (g > 100 && world.gravityRot == 0) {
        p.gravScale = 0.25f;
        p.vel = world.gravity / g * 80;
    } else {
        p.gravScale = 0;
        p.damping = 0.1f;
        p.vel = dir * 160;
    }
    world.add(p);
}

void Game::spawnEnemy(Vec2 pos) {
    Body e;
    e.kind = Kind::Enemy;
    e.shape = Shape::Circle;
    e.r = 15;
    e.pos = pos;
    e.vel = Vec2(frand(-60, 60), 80);
    e.invMass = 1.0f / 0.04f;
    e.damping = 3;
    e.thrust = 3;      // terminal speed = steering input
    e.gravScale = 0;
    e.maxSpeed = 420;
    e.phase = frand(0, 6.28f);
    world.add(e);
}

int Game::bricksLeft() const {
    int n = 0;
    for (const auto& b : world.bodies)
        if (b.alive && b.counts) n++;
    return n;
}

int Game::ballsAlive() const {
    int n = 0;
    for (const auto& b : world.bodies)
        if (b.alive && b.kind == Kind::Ball) n++;
    return n;
}

// ------------------------------------------------------------------------------------------

void Game::update(float dt, const Vec2 inputs[MAX_PLAYERS]) {
    realTime += dt;
    stateTime += dt;
    updateEffects(dt);

    auto setInputs = [&]() {
        for (auto& b : world.bodies) {
            if (b.kind != Kind::Player || !b.alive) continue;
            int i = b.player;
            b.input = (demo || aiPlayers[i]) ? aiInput(b) : inputs[i];
            if (b.stun > 0) {   // hit by an enemy: the bat can't be steered for a moment
                b.stun -= dt;
                b.input = Vec2(0, 0);
            }
        }
    };

    switch (state) {
        case State::Title:
            demoTimer += dt;
            setInputs();
            updateEnemies();
            updateBatSizes();
            simulate(dt * GAME_SPEED);
            updateShots(dt * GAME_SPEED);
            for (auto* t : {&tEnlarge, &tShrink, &tSlow, &tMega, &tMagnet, &tGun}) *t = std::max(0.f, *t - dt);
            darkness += (targetDarkness() - darkness) * std::min(1.f, dt * 2.5f);
            if (demoTimer > 35 || bricksLeft() == 0 || (ballsAlive() == 0 && stateTime > 1)) {
                int next = (level + 1 + int(frand(0, 3))) % levelCount();
                startDemoLevel(next, 1);
                state = State::Title;
                stateTime = 0;
            }
            break;
        case State::Playing: {
            setInputs();
            updateEnemies();
            updateBatSizes();
            simulate(dt * GAME_SPEED * (tSlow > 0 ? 0.6f : 1.f));
            updateShots(dt * GAME_SPEED * (tSlow > 0 ? 0.6f : 1.f));
            for (auto* t : {&tEnlarge, &tShrink, &tSlow, &tMega, &tMagnet, &tGun}) *t = std::max(0.f, *t - dt);
            darkness += (targetDarkness() - darkness) * std::min(1.f, dt * 2.5f);
            for (auto& b : world.bodies)
                if (b.kind == Kind::Ball) b.mega = tMega > 0;
            if (!demo) checkProgress();
            break;
        }
        case State::LevelClear:
            if (stateTime > 2.8f) {
                if (level < runLast) {
                    loadLevel(level + 1);
                    state = State::Intro;
                    stateTime = 0;
                } else {
                    state = State::Victory;
                    stateTime = 0;
                    // back on the title screen, the next set is preselected (after set 6: the complete run)
                    if (!completeRun()) startSet = std::min(runLast / LEVELS_PER_SET + 1, setChoices() - 1);
                    menuIndex = 0;
                    if (score > settings.hiscore) { settings.hiscore = score; settingsDirty = true; }
                }
            }
            break;
        case State::Victory:
            if (int(realTime * 3) != int((realTime - dt) * 3)) {
                Vec2 p(frand(200, 1080), frand(150, 450));
                burst(p, 50, brickColor(int(frand(0, 8))), 420, 5, 300);
                Audio::play(SFX_BREAK, 0.5f, frand(0.6f, 1.4f));
            }
            break;
        default:
            break;
    }
}

void Game::simulate(float dt) {
    accumulator += dt;
    int steps = 0;
    while (accumulator >= PHYS_DT && steps < 40) {
        world.step(PHYS_DT);
        // chain-reaction explosions
        for (auto& bl : blasts) {
            bl.delay -= PHYS_DT;
            if (bl.delay <= 0) {
                Body* b = world.find(bl.brickId);
                if (b && b->alive) {
                    b->hp--;
                    b->flash = 0.2f;
                    if (b->hp <= 0) destroyBrick(*b, nullptr);
                }
            }
        }
        blasts.erase(std::remove_if(blasts.begin(), blasts.end(), [](const PendingBlast& b) { return b.delay <= 0; }),
                     blasts.end());
        processEvents();
        accumulator -= PHYS_DT;
        steps++;
    }
    if (steps >= 40) accumulator = 0;
}

namespace {
enum class SpawnType { Ball, Pill, Enemy };
struct Spawn {
    SpawnType type = SpawnType::Ball;
    Vec2 pos, dir;
    BallSpawn ball;
    int pillType = -1;
};
std::vector<Spawn> spawnQueue;
}

void Game::flushSpawns() {
    int enemies = 0;
    for (const auto& b : world.bodies)
        if (b.kind == Kind::Enemy && b.alive) enemies++;
    for (const auto& s : spawnQueue) {
        switch (s.type) {
            case SpawnType::Pill: spawnPill(s.pos, s.dir, s.pillType); break;
            case SpawnType::Ball: if (ballsAlive() < MAX_BALLS) spawnBall(s.ball); break;
            case SpawnType::Enemy: if (enemies < MAX_ENEMIES) { spawnEnemy(s.pos); enemies++; } break;
        }
    }
    spawnQueue.clear();
}

void Game::removeDead() {
    auto& bs = world.bodies;
    bs.erase(std::remove_if(bs.begin(), bs.end(), [](const Body& b) { return !b.alive; }), bs.end());
}

void Game::processEvents() {
    bool quiet = state == State::Title;
    auto sfx = [&](Sfx s, float v = 1, float p = 1) { if (!quiet) Audio::play(s, v, p); };

    for (const PhysEvent& ev : world.events) {
        Body* A = world.find(ev.a);
        if (!A) continue;
        switch (ev.type) {
            case EvType::Hit: {
                Body* B = world.find(ev.b);
                if (!B || !A->alive || !B->alive) break;
                Body* ball = A->kind == Kind::Ball ? A : (B->kind == Kind::Ball ? B : nullptr);
                Body* other = ball == A ? B : A;
                float vol = clampf(ev.speed / 900.f, 0.15f, 1.f);
                if (ball) {
                    if (other->kind == Kind::Player) {
                        ball->combo = 1;
                        other->flash = 0.15f;
                        sfx(SFX_BAT, vol, 0.9f + 0.2f * vol);
                    } else if (other->kind == Kind::Brick) {
                        brickHit(*ball, *other, ev.speed);
                    } else if (other->kind == Kind::Ball) {
                        sfx(SFX_PUCK, vol * 0.6f, 0.7f);
                    } else if (other->kind == Kind::Enemy) {
                        killEnemy(*other, true);
                    }
                } else if (A->kind == Kind::Enemy || B->kind == Kind::Enemy) {
                    // enemies bump into bricks, walls and each other: nothing happens
                } else {
                    Body* br = A->kind == Kind::Brick ? A : B;
                    Body* ot = br == A ? B : A;
                    if (br->btype == BrickType::Bumper) { br->flash = 0.2f; sfx(SFX_BUMPER, vol); }
                    else if (br->btype == BrickType::Puck || ot->btype == BrickType::Puck) sfx(SFX_PUCK, vol);
                    else if (ot->kind == Kind::Player && ev.speed > 120) sfx(SFX_WALL, vol * 0.6f, 0.8f);
                }
                break;
            }
            case EvType::Wall:
                if (A->kind == Kind::Ball) sfx(SFX_WALL, clampf(ev.speed / 1500.f, 0.1f, 0.8f));
                else sfx(SFX_PUCK, clampf(ev.speed / 1500.f, 0.1f, 0.8f), 0.8f);
                break;
            case EvType::Swallow: {
                Ghost g;
                g.body = *A;
                g.body.alive = true;
                g.mode = 1;
                g.dur = 0.45f;
                g.target = ev.pos;
                ghosts.push_back(g);
                if (A->kind == Kind::Ball) {
                    sfx(SFX_HOLE);
                    if (!quiet) addScore(-1000, ev.pos);
                } else if (A->kind == Kind::Brick) {
                    sfx(SFX_SINK);
                    addScore(A->score, ev.pos);
                    burst(ev.pos, 16, brickColor(A->color), 260, 4, 0);
                }
                break;
            }
            case EvType::Teleport: {
                sfx(SFX_PORTAL, 0.7f, frand(0.9f, 1.1f));
                burst(ev.pos, 12, 0xB070FF, 200, 4, 0);
                burst(A->pos, 12, 0xFFA040, 200, 4, 0);
                break;
            }
            case EvType::Touch: {
                Body* O = world.find(ev.b);
                if (!A->alive || !O || !O->alive) break;
                if (O->kind == Kind::Enemy) {
                    killEnemy(*O, false);
                    A->stun = 1.5f;
                    A->flash = 0.3f;
                    sfx(SFX_ZAP);
                    popText(A->pos + Vec2(0, -30), "STUNNED!", 0x90FF90);
                } else if (O->deadly) {
                    batDeath(*A);
                }
                break;
            }
            case EvType::Collect: {
                Body* P = world.find(ev.b);
                if (!A->alive || !P) break;
                A->alive = false;
                Ghost g;
                g.body = *A;
                g.body.alive = true;
                g.dur = 0.3f;
                ghosts.push_back(g);
                applyPill(A->pill, *P);
                break;
            }
        }
    }
    world.events.clear();
    removeDead();
    flushSpawns();
}

void Game::brickHit(Body& ball, Body& brick, float speed) {
    bool quiet = state == State::Title;
    auto sfx = [&](Sfx s, float v = 1, float p = 1) { if (!quiet) Audio::play(s, v, p); };
    float vol = clampf(speed / 900.f, 0.2f, 1.f);
    switch (brick.btype) {
        case BrickType::Metal:
        case BrickType::Conveyor:
        case BrickType::Rail:
            brick.flash = 0.15f;
            sfx(SFX_METAL, vol * 0.7f, frand(0.95f, 1.05f));
            break;
        case BrickType::Bumper:
            brick.flash = 0.25f;
            sfx(SFX_BUMPER, vol);
            addScore(10, brick.pos, false);
            burst(ball.pos, 8, 0xFFF0A0, 250, 3, 0);
            break;
        case BrickType::GravSwitch:
            if (brick.hitCooldown <= 0) {
                brick.hitCooldown = 0.8f;
                brick.flash = 0.4f;
                world.gravity = Vec2(-world.gravity.y, world.gravity.x);
                sfx(SFX_GRAVITY);
                popText(brick.pos, "GRAVITY!", 0xC890FF);
                shake = std::max(shake, 0.15f);
            }
            break;
        case BrickType::Puck:
            sfx(SFX_PUCK, vol);
            break;
        case BrickType::Skull:
        case BrickType::Barrier:
            brick.flash = 0.15f;
            sfx(SFX_METAL, vol * 0.6f, 0.7f);
            break;
        case BrickType::Switch:
            if (brick.hitCooldown <= 0) {
                brick.hitCooldown = 0.5f;
                toggleGroup(brick.group, brick.pos);
            }
            break;
        default: {
            if (speed >= 9999) {
                brick.hp = 0;
            } else if (brick.hitCooldown <= 0) {
                brick.hp--;
                brick.hitCooldown = 0.06f;
                addScore(10, brick.pos, false);
            }
            brick.flash = 0.12f;
            if (brick.hp <= 0) destroyBrick(brick, &ball);
            else sfx(SFX_BRICK, vol, 1.0f + 0.15f * (brick.maxHp - brick.hp));
            break;
        }
    }
}

void Game::destroyBrick(Body& brick, Body* ball) {
    if (!brick.alive) return;
    bool quiet = state == State::Title;
    brick.alive = false;
    int combo = ball ? ball->combo : 1;
    int pts = brick.score * combo;
    if (!quiet) {
        score += pts;
        char buf[32];
        if (combo > 1) std::snprintf(buf, sizeof buf, "%d x%d", brick.score, combo);
        else std::snprintf(buf, sizeof buf, "%d", pts);
        popText(brick.pos, buf, combo > 1 ? 0xFFE060 : 0xFFFFFF);
        addScore(0, brick.pos, false);
    }
    if (ball) ball->combo = std::min(combo + 1, 8);

    Ghost g;
    g.body = brick;
    g.body.alive = true;
    g.dur = 0.25f;
    ghosts.push_back(g);
    burst(brick.pos, 14, brickColor(brick.color), 380, 5);
    if (!quiet) Audio::play(SFX_BREAK, 0.8f, 0.9f + 0.06f * combo);

    if (brick.btype == BrickType::Explosive) explode(brick.pos);
    if (brick.btype == BrickType::BigBall) {
        Spawn s;
        s.type = SpawnType::Ball;
        s.ball = info.balls.empty() ? BallSpawn{} : info.balls[0];
        s.ball.pos = brick.pos;
        s.ball.r = 26;
        s.ball.mass = 0.1f;
        s.ball.vel = Vec2(frand(-200, 200), 150);
        spawnQueue.push_back(s);
        popText(brick.pos + Vec2(0, -24), "BIG BALL", 0xFFB060);
    }
    if (brick.btype == BrickType::Nest) {
        Spawn s;
        s.type = SpawnType::Enemy;
        s.pos = brick.pos;
        spawnQueue.push_back(s);
        burst(brick.pos, 16, 0x70FF60, 240, 5, 0);
        if (frand() < 0.4f) {   // a laser to fight back with
            Spawn p;
            p.type = SpawnType::Pill;
            p.pos = brick.pos + Vec2(0, 20);
            p.dir = (info.playerCenter - brick.pos).norm();
            p.pillType = PILL_GUN;
            spawnQueue.push_back(p);
        }
    }
    if (brick.btype == BrickType::Bulb) {
        burst(brick.pos, 18, 0xFFF4C0, 300, 3, 600);
        if (!quiet) Audio::play(SFX_METAL, 0.6f, 1.8f);
    }
    if (brick.btype == BrickType::Multi) {
        Spawn s;
        s.type = SpawnType::Ball;
        s.ball = info.balls.empty() ? BallSpawn{} : info.balls[0];
        s.ball.pos = brick.pos;
        float a = frand(0, 2 * PI_F);
        s.ball.vel = Vec2(std::cos(a), std::sin(a)) * 450;
        spawnQueue.push_back(s);
        popText(brick.pos + Vec2(0, -24), "MULTIBALL", 0x80F0FF);
    }
    int pills = 0;
    for (const auto& b : world.bodies)
        if (b.kind == Kind::Pill && b.alive) pills++;
    if (info.pills && pills < 3 && frand() < 0.12f) {
        Spawn s;
        s.type = SpawnType::Pill;
        s.pos = brick.pos;
        Vec2 toPlayer = info.playerCenter - brick.pos;
        s.dir = toPlayer.norm();
        spawnQueue.push_back(s);
    }
}

void Game::explode(Vec2 pos) {
    bool quiet = state == State::Title;
    if (!quiet) Audio::play(SFX_EXPLODE);
    shake = std::max(shake, 0.35f);
    burst(pos, 40, 0xFFB030, 520, 7, 200);
    burst(pos, 20, 0xFFF080, 300, 5, 100);
    burst(pos, 14, 0x505050, 160, 9, -100);
    for (auto& b : world.bodies) {
        if (!b.alive) continue;
        Vec2 d = b.pos - pos;
        float dist = d.len();
        if (b.dynamic() && dist < 230 && dist > 1) {
            float f = 900 * (1 - dist / 230);
            if (b.kind == Kind::Player) f *= 0.5f;
            b.vel += d / dist * f;
        }
        if (breakable(b) && b.counts) {
            float ext = b.shape == Shape::Circle ? b.r : std::max(b.hw, b.hh);
            if (dist - ext < 110) {
                bool queued = false;
                for (auto& bl : blasts) queued |= bl.brickId == b.id;
                if (!queued) blasts.push_back({b.id, 0.12f + dist / 1600.f});
            }
        }
    }
}

void Game::applyPill(int type, Body& player) {
    if (state != State::Title) Audio::play(SFX_PILL);
    popText(player.pos + Vec2(0, -30), pillName(type), pillColor(type), true);
    addScore(100, player.pos, false);
    switch (type) {
        case PILL_ENLARGE: tEnlarge = 20; tShrink = 0; break;
        case PILL_SHRINK: tShrink = 15; tEnlarge = 0; break;
        case PILL_SLOW: tSlow = 10; break;
        case PILL_MEGA: tMega = 10; break;
        case PILL_MAGNET: tMagnet = 12; break;
        case PILL_LIFE: lives++; break;
        case PILL_GUN: tGun = 12; gunCooldown = 0; break;
        case PILL_MULTI: {
            int n = ballsAlive();
            int made = 0;
            for (const auto& b : world.bodies) {
                if (b.kind != Kind::Ball || !b.alive) continue;
                for (int k = -1; k <= 1; k += 2) {
                    if (n + made >= MAX_BALLS) break;
                    Spawn s;
                    s.type = SpawnType::Ball;
                    s.ball = info.balls.empty() ? BallSpawn{} : info.balls[0];
                    s.ball.pos = b.pos;
                    Vec2 v = b.vel;
                    if (v.len() < 80) {
                        float a = frand(0, 2 * PI_F);
                        v = Vec2(std::cos(a), std::sin(a)) * 400;
                    }
                    s.ball.vel = v.rotated(0.45f * k);
                    spawnQueue.push_back(s);
                    made++;
                }
            }
            break;
        }
    }
}

void Game::updateBatSizes() {
    float s = (tEnlarge > 0 ? 1.5f : 1.f) * (tShrink > 0 ? 0.65f : 1.f);
    for (auto& b : world.bodies) {
        if (b.kind != Kind::Player) continue;
        if (b.shape == Shape::Capsule) b.hw = BAT_HW * s - BAT_R;
        else b.r = DISC_R * s;
        b.magnet = tMagnet > 0 ? 2600.f : 0.f;
    }
}

void Game::addScore(int pts, Vec2 pos, bool show) {
    if (state == State::Title) return;
    score = std::max(0, score + pts);
    if (show && pts != 0) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%+d", pts);
        popText(pos, buf, pts < 0 ? 0xFF6060 : 0xFFFFFF);
    }
    while (score >= nextExtraLife) {
        lives++;
        nextExtraLife += 20000;
        popText(Vec2(640, 360), "EXTRA LIFE!", 0xFF80C0, true);
        Audio::play(SFX_PILL, 1, 1.3f);
    }
}

void Game::burst(Vec2 pos, int n, uint32_t color, float speed, float size, float grav) {
    if (particles.size() > 1500) return;
    for (int i = 0; i < n; i++) {
        Particle p;
        float a = frand(0, 2 * PI_F);
        float s = frand(0.2f, 1.f) * speed;
        p.pos = pos;
        p.vel = Vec2(std::cos(a), std::sin(a)) * s;
        p.maxLife = p.life = frand(0.4f, 0.9f);
        p.size = frand(0.5f, 1.f) * size;
        float shade = frand(0.75f, 1.15f);
        p.r = uint8_t(std::min(255.f, ((color >> 16) & 255) * shade));
        p.g = uint8_t(std::min(255.f, ((color >> 8) & 255) * shade));
        p.b = uint8_t(std::min(255.f, (color & 255) * shade));
        p.grav = grav;
        particles.push_back(p);
    }
}

void Game::popText(Vec2 pos, const std::string& t, uint32_t color, bool big) {
    if (state == State::Title) return;
    FloatText ft;
    ft.pos = pos;
    ft.text = t;
    ft.color = color;
    ft.big = big;
    ft.dur = big ? 1.6f : 0.9f;
    texts.push_back(ft);
}

void Game::updateEffects(float dt) {
    Vec2 gdir = world.gravity.len() > 1 ? world.gravity.norm() : Vec2(0, 0);
    for (auto& p : particles) {
        p.life -= dt;
        p.vel += gdir * (p.grav * dt);
        p.vel *= (1 - 1.5f * dt);
        p.pos += p.vel * dt;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0; }),
                    particles.end());
    for (auto& g : ghosts) g.t += dt;
    ghosts.erase(std::remove_if(ghosts.begin(), ghosts.end(), [](const Ghost& g) { return g.t >= g.dur; }), ghosts.end());
    for (auto& t : texts) {
        t.t += dt;
        t.pos.y -= (t.big ? 25 : 45) * dt;
    }
    texts.erase(std::remove_if(texts.begin(), texts.end(), [](const FloatText& t) { return t.t >= t.dur; }), texts.end());
    shake = std::max(0.f, shake - dt);
    if (state == State::Playing || state == State::Title) {
        for (auto& b : world.bodies)
            if (b.kind == Kind::Pill) {
                b.life -= dt;
                if (b.life <= 0) b.alive = false;
            }
    }
}

void Game::checkProgress() {
    if (bricksLeft() == 0) {
        state = State::LevelClear;
        stateTime = 0;
        Audio::play(SFX_CLEAR);
        int bonus = 1000 + 250 * level + 500 * (ballsAlive() - 1);
        addScore(bonus, Vec2(640, 400), false);
        popText(Vec2(640, 340), "LEVEL CLEAR!", 0xFFFFFF, true);
        char buf[32];
        std::snprintf(buf, sizeof buf, "BONUS %d", bonus);
        popText(Vec2(640, 400), buf, 0xFFE060, true);
        if (level + 2 > settings.maxLevel) {
            settings.maxLevel = std::min(level + 2, levelCount());
            settingsDirty = true;
        }
        if (score > settings.hiscore) { settings.hiscore = score; settingsDirty = true; }
        return;
    }
    bool sinking = false;
    for (const auto& g : ghosts) sinking |= g.mode == 1 && g.body.kind == Kind::Ball;
    if (ballsAlive() == 0 && !sinking) {
        if (lives > 0) {
            lives--;
            Audio::play(SFX_LOSE);
            spawnActors();
            state = State::Intro;
            stateTime = 0;
        } else {
            state = State::GameOver;
            stateTime = 0;
            Audio::play(SFX_GAMEOVER);
            if (score > settings.hiscore) { settings.hiscore = score; settingsDirty = true; }
        }
    }
}

Vec2 Game::aiInput(const Body& p) const {
    const Body* ball = nullptr;
    float bd = 1e9f;
    for (const auto& b : world.bodies) {
        if (b.kind != Kind::Ball || !b.alive) continue;
        float d = (b.pos - p.pos).len();
        if (d < bd) { bd = d; ball = &b; }
    }
    int n = std::max(1, worldPlayers);
    bool vertical = info.playerCenter.x < 400;
    float spacing = info.discBat ? 90.f : 190.f;
    float off = (p.player - (n - 1) * 0.5f) * spacing;
    Vec2 home = info.playerCenter + (vertical ? Vec2(0, off) : Vec2(off, 0));
    Vec2 desired = (home - p.pos) * 5;
    if (ball) {
        // aim at the brick closest to the ball (or the closest puck)
        Vec2 aim(640, 200);
        float pd = 1e9f;
        bool pucks = false;
        for (const auto& b : world.bodies) {
            if (!b.alive || !b.counts) continue;
            bool isP = b.btype == BrickType::Puck;
            if (isP && !pucks) { pucks = true; pd = 1e9f; }
            if (pucks && !isP) continue;
            float d = (b.pos - ball->pos).len();
            if (d < pd) { pd = d; aim = b.pos; }
        }
        Vec2 aimDir = (aim - ball->pos).norm();
        float ext = p.r;
        float g = world.gravity.len();
        if (g > 100) {
            Vec2 gd = world.gravity / g;
            Vec2 side = gd.perp();
            float lat = dot(aimDir, side);
            // home line: a little above the "floor" in the current gravity direction
            float floorA = std::max(std::max(dot(Vec2(world.left, world.top), gd), dot(Vec2(world.right, world.top), gd)),
                                    std::max(dot(Vec2(world.left, world.bottom), gd), dot(Vec2(world.right, world.bottom), gd)));
            float homeLine = std::min(dot(home, gd), floorA - 90);
            float homeA = homeLine - (ball->r + ext);       // where the ball meets the bat on the home line
            float s0 = dot(ball->pos, gd), v0 = dot(ball->vel, gd);
            float l0 = dot(ball->pos, side), vl = dot(ball->vel, side);
            float gap = dot(p.pos - ball->pos, gd);          // >0: ball is "above" the bat
            // predict when the ball reaches the home line: s0 + v0 t + g/2 t^2 = homeA
            float disc = v0 * v0 + 2 * g * (homeA - s0);
            float t = disc > 0 ? (-v0 + std::sqrt(disc)) / g : 0.f;
            float latHit = l0 + vl * t;
            float lo = dot(Vec2(world.left + 60, world.top + 60), side), hi = dot(Vec2(world.right - 60, world.bottom - 60), side);
            if (lo > hi) std::swap(lo, hi);
            latHit = clampf(latHit, lo, hi);
            Vec2 target = gd * (homeA + ball->r + ext) + side * (latHit - lat * 50);
            desired = (target - p.pos) * 7;
            float latErr = dot(ball->pos - p.pos, side);
            if (gap > 0 && gap < 120 && v0 > -150 && std::fabs(latErr) < 90) {
                // swing hard against gravity and swipe sideways to steer the ball
                desired = -gd * 1100 + side * (lat * 500 + latErr * 4);
            }
        } else {
            Vec2 pred = ball->pos + ball->vel * 0.18f;
            Vec2 behind = pred - aimDir * (ball->r + ext + 14);
            float dist = (ball->pos - p.pos).len();
            if ((p.pos - behind).len() < 30 || dist < ball->r + ext + 24)
                desired = aimDir * 750 + (pred - p.pos) * 3;
            else
                desired = (behind - p.pos) * 7 + ball->vel;
        }
    }
    float dl = desired.len();
    if (dl > 1300) desired *= 1300 / dl;
    // compensate damping so the AI behaves on ice as well
    Vec2 in = desired - p.vel + p.vel * (p.damping / p.thrust);
    float il = in.len();
    if (il > 1600) in *= 1600 / il;
    return in;
}

void Game::killEnemy(Body& e, bool scored) {
    if (!e.alive) return;
    e.alive = false;
    burst(e.pos, 22, 0x70FF60, 320, 5, 0);
    burst(e.pos, 8, 0xFFFFFF, 200, 3, 0);
    if (state != State::Title) Audio::play(SFX_ZAP, 0.7f, 1.4f);
    if (scored) addScore(200, e.pos);
}

// a bat touched a deadly brick: it blows up and the player loses a life (all balls are lost)
void Game::batDeath(Body& bat) {
    if (ballsAlive() == 0) return; // already dying
    bool quiet = state == State::Title;
    if (!quiet) Audio::play(SFX_EXPLODE);
    shake = std::max(shake, 0.4f);
    burst(bat.pos, 40, playerColor(bat.color), 450, 7, 300);
    burst(bat.pos, 20, 0xFFE080, 300, 5, 200);
    popText(bat.pos + Vec2(0, -30), "CRASH!", 0xFF6060, true);
    for (auto& b : world.bodies) {
        if (b.kind != Kind::Ball || !b.alive) continue;
        b.alive = false;
        Ghost g;
        g.body = b;
        g.body.alive = true;
        g.dur = 0.3f;
        ghosts.push_back(g);
    }
}

void Game::toggleGroup(int group, Vec2 pos) {
    bool on = false;
    for (auto& b : world.bodies) {
        if (b.btype == BrickType::Barrier && b.group == group) {
            b.solid = !b.solid;
            on = b.solid;
            b.flash = 0.3f;
        }
        if (b.btype == BrickType::Switch && b.group == group) b.flash = 0.4f;
    }
    if (state != State::Title) Audio::play(SFX_SWITCH, 1.0f, on ? 1.2f : 0.8f);
    popText(pos + Vec2(0, -28), on ? "ON" : "OFF", on ? 0x80FFFF : 0xFF9090);
}

// enemies home in on the closest bat, wobbling a little
void Game::updateEnemies() {
    for (auto& e : world.bodies) {
        if (e.kind != Kind::Enemy || !e.alive) continue;
        const Body* best = nullptr;
        float bd = 1e9f;
        for (const auto& p : world.bodies) {
            if (p.kind != Kind::Player) continue;
            float d = (p.pos - e.pos).len();
            if (d < bd) { bd = d; best = &p; }
        }
        Vec2 dir = best ? (best->pos - e.pos).norm() : Vec2(0, 1);
        float wobble = std::sin(world.time * 3.0f + e.phase) * 110.f;
        e.input = dir * 230.f + dir.perp() * wobble;
    }
}

// laser power-up: bats fire upwards automatically
void Game::updateShots(float dt) {
    bool quiet = state == State::Title;
    if (tGun > 0) {
        gunCooldown -= dt;
        if (gunCooldown <= 0) {
            gunCooldown = 0.3f;
            for (const auto& p : world.bodies) {
                if (p.kind != Kind::Player) continue;
                if (p.shape == Shape::Capsule) {
                    shots.push_back({p.pos + Vec2(-p.hw, -p.r - 4)});
                    shots.push_back({p.pos + Vec2(p.hw, -p.r - 4)});
                } else {
                    shots.push_back({p.pos + Vec2(0, -p.r - 4)});
                }
            }
            if (!quiet) Audio::play(SFX_LASER, 0.5f);
        }
    }
    if (shots.empty()) return;
    const float speed = 1100.f;
    for (auto& sh : shots) {
        if (!sh.alive) continue;
        // move in small steps so thin bricks can't be skipped
        float dist = speed * dt;
        int n = std::max(1, int(dist / 8.f));
        for (int k = 0; k < n && sh.alive; k++) {
            sh.pos.y -= dist / n;
            if (sh.pos.y < world.top) { sh.alive = false; break; }
            for (auto& b : world.bodies) {
                if (!b.alive || !b.solid) continue;
                if (b.kind == Kind::Enemy) {
                    if ((b.pos - sh.pos).len() < b.r + 5) { killEnemy(b, true); sh.alive = false; break; }
                    continue;
                }
                if (b.kind != Kind::Brick || !pointInBody(b, sh.pos, 2)) continue;
                sh.alive = false;
                if (breakable(b)) {
                    b.hp--;
                    b.flash = 0.12f;
                    if (b.hp <= 0) destroyBrick(b, nullptr);
                    else if (!quiet) Audio::play(SFX_BRICK, 0.6f, 1.2f);
                } else if (b.btype == BrickType::Switch && b.hitCooldown <= 0) {
                    b.hitCooldown = 0.5f;
                    toggleGroup(b.group, b.pos);
                } else {
                    b.flash = 0.1f;
                }
                burst(sh.pos, 4, 0xFFF080, 150, 3, 0);
                break;
            }
        }
    }
    shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot& s) { return !s.alive; }), shots.end());
    removeDead();
    flushSpawns();
}

float Game::targetDarkness() const {
    if (bulbsTotal <= 0) return 0;
    int left = 0;
    for (const auto& b : world.bodies)
        if (b.btype == BrickType::Bulb && b.alive) left++;
    return 0.93f * (1.0f - float(left) / bulbsTotal);
}

// ------------------------------------------------------------------------------------------

std::string Game::titleItem(int i) const {
    char buf[64];
    switch (i) {
        case 0: return "Start Game";
        case 1: std::snprintf(buf, sizeof buf, "Players: %d", numPlayers); return buf;
        case 2: {
            int first, last;
            setRange(startSet, first, last);
            if (startSet >= levelSetCount())
                std::snprintf(buf, sizeof buf, "Level Set: Complete  (%d-%d)", first + 1, last + 1);
            else
                std::snprintf(buf, sizeof buf, "Level Set: %d  (%d-%d)", startSet + 1, first + 1, last + 1);
            return buf;
        }
        case 3: std::snprintf(buf, sizeof buf, "Control Sensitivity: %.2f", settings.sensitivity); return buf;
        case 4: return settings.sound ? "Sound: On" : "Sound: Off";
        case 5: return "Quit";
    }
    return "";
}

std::string Game::pauseItem(int i) const {
    switch (i) {
        case 0: return "Resume";
        case 1: return "Restart Level";
        case 2: return "Quit to Title";
    }
    return "";
}

void Game::action(Action a, int) {
    switch (state) {
        case State::Title: {
            int n = titleItemCount();
            if (a == Action::Up) { menuIndex = (menuIndex + n - 1) % n; Audio::play(SFX_MENU); }
            else if (a == Action::Down) { menuIndex = (menuIndex + 1) % n; Audio::play(SFX_MENU); }
            else if (a == Action::Left || a == Action::Right || a == Action::Confirm) {
                int d = a == Action::Left ? -1 : 1;
                switch (menuIndex) {
                    case 0:
                        if (a == Action::Confirm) startGame(numPlayers, startSet);
                        break;
                    case 1: numPlayers = (numPlayers - 1 + d + MAX_PLAYERS) % MAX_PLAYERS + 1; break;
                    case 2: startSet = (startSet + d + setChoices()) % setChoices(); break;
                    case 3:
                        settings.sensitivity = clampf(settings.sensitivity + 0.25f * d, 0.25f, 3.0f);
                        settingsDirty = true;
                        break;
                    case 4:
                        settings.sound = !settings.sound;
                        Audio::setEnabled(settings.sound);
                        settingsDirty = true;
                        break;
                    case 5:
                        if (a == Action::Confirm) requestQuit = true;
                        break;
                }
                if (state == State::Title) Audio::play(SFX_MENU);
            } else if (a == Action::Back) {
                if (menuIndex == 5) requestQuit = true;
                else menuIndex = 5;
            }
            break;
        }
        case State::Intro:
            if (a == Action::Confirm && stateTime > 0.25f) { state = State::Playing; stateTime = 0; }
            else if (a == Action::Back || a == Action::Pause) { state = State::Paused; menuIndex = 0; }
            break;
        case State::Playing:
            if (a == Action::Back || a == Action::Pause) { state = State::Paused; menuIndex = 0; }
            break;
        case State::Paused: {
            int n = pauseItemCount();
            if (a == Action::Up) { menuIndex = (menuIndex + n - 1) % n; Audio::play(SFX_MENU); }
            else if (a == Action::Down) { menuIndex = (menuIndex + 1) % n; Audio::play(SFX_MENU); }
            else if (a == Action::Pause || a == Action::Back || (a == Action::Confirm && menuIndex == 0)) {
                state = State::Intro;   // brief "get ready" so a ball is never lost on resume
                stateTime = 0.3f;
            } else if (a == Action::Confirm && menuIndex == 1) {
                loadLevel(level);
                state = State::Intro;
                stateTime = 0;
            } else if (a == Action::Confirm && menuIndex == 2) {
                init();
            }
            break;
        }
        case State::LevelClear:
            if (a == Action::Confirm && stateTime > 1.0f) stateTime = 100;
            break;
        case State::GameOver:
            if (stateTime < 1.0f) break;
            if (a == Action::Confirm) { // continue at this level, score reset
                score = 0;
                lives = 3;
                nextExtraLife = 20000;
                loadLevel(level);
                state = State::Intro;
                stateTime = 0;
            } else if (a == Action::Back) {
                init();
            }
            break;
        case State::Victory:
            if ((a == Action::Confirm || a == Action::Back) && stateTime > 2.0f) init();
            break;
    }
}

// ------------------------------------------------------------------------------------------

bool Game::runSelfTest(int seconds, bool verbose) {
    bool ok = true;
    int cleared = 0;
    for (int l = 0; l < levelCount(); l++) {
        Game g;
        g.numPlayers = 1;
        g.startDemoLevel(l, 1);
        g.demo = false;
        g.lives = 99;
        int startBricks = g.bricksLeft();
        int deaths = 0;
        float t = 0;
        Vec2 in[MAX_PLAYERS];
        const float dt = 1.0f / 60.0f;
        bool bad = false;
        while (t < seconds) {
            g.update(dt, in);
            t += dt;
            if (g.state == State::Intro) { deaths++; g.state = State::Playing; }
            if (g.state != State::Playing) break;
            for (const auto& b : g.world.bodies) {
                if (!std::isfinite(b.pos.x) || !std::isfinite(b.pos.y) || b.pos.x < -50 || b.pos.x > 1330 ||
                    b.pos.y < -50 || b.pos.y > 770) {
                    bad = true;
                }
            }
            if (bad) break;
        }
        bool clr = g.state == State::LevelClear;
        cleared += clr;
        if (bad) ok = false;
        if (verbose)
            std::printf("Level %2d %-15s %s  t=%5.1fs  bricks %3d -> %3d  balls lost %d  score %d\n", l + 1,
                        g.info.name.c_str(), bad ? "PHYSICS ERROR" : (clr ? "CLEARED" : "-------"), t, startBricks,
                        g.bricksLeft(), deaths, g.score);
    }
    if (verbose) std::printf("AI cleared %d of %d levels\n", cleared, levelCount());
    return ok;
}
