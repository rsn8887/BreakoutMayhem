// Breakout Mayhem - game logic
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include "physics.h"
#include "levels.h"

constexpr int MAX_PLAYERS = 4;
constexpr int SCREEN_W = 1280;
constexpr int SCREEN_H = 720;
// bat: a capsule BAT_HW * 2 wide in total, with round ends of radius BAT_R (so 2 * BAT_R thick)
constexpr float BAT_HW = 70;
constexpr float BAT_R = 15;
// Overall game speed: the whole simulation (ball, bat, gravity, moving bricks, enemies) runs at this
// fraction of real time, which makes the game calmer. 1.0 = original speed.
constexpr float GAME_SPEED = 0.8f;

enum class State { Title, Intro, Playing, Paused, LevelClear, GameOver, Victory };
enum class Action { Up, Down, Left, Right, Confirm, Back, Pause };

enum PillType { PILL_ENLARGE, PILL_MULTI, PILL_SLOW, PILL_MEGA, PILL_MAGNET, PILL_LIFE, PILL_SHRINK, PILL_GUN, PILL_COUNT };

struct Particle {
    Vec2 pos, vel;
    float life, maxLife, size;
    uint8_t r, g, b;
    float grav;
};

struct Ghost {           // fading copy of a removed body (destroyed brick, sinking ball...)
    Body body;
    float t = 0, dur = 0.3f;
    int mode = 0;        // 0 = pop (grow + fade), 1 = sink into target
    Vec2 target;
};

struct FloatText {
    Vec2 pos;
    std::string text;
    float t = 0, dur = 1.0f;
    uint32_t color = 0xffffffff;
    bool big = false;
};

struct Shot {            // laser bolt fired by bats with the laser power-up
    Vec2 pos;
    bool alive = true;
};

struct PendingBlast {
    int brickId = 0;
    float delay = 0;
};

struct Settings {
    float sensitivity = 1.0f;   // control sensitivity: scales mouse, stick, D-pad and keyboard bat speed
    bool sound = true;
    bool shadows = true;
    bool fullscreen = false;
    int hiscore = 0;
    int maxLevel = 1; // highest level reached (1-based)
};

class Game {
public:
    World world;
    LevelInfo info;
    State state = State::Title;
    Settings settings;

    int numPlayers = 1;    // chosen in the menu
    int worldPlayers = 1;  // bats currently in the world
    int level = 0;       // 0-based
    int score = 0;
    int lives = 3;
    int nextExtraLife = 20000;
    int menuIndex = 0;
    int startSet = 0;    // level set chosen in the title menu, 0-based; levelSetCount() = all levels
    int runFirst = 0, runLast = 0; // first and last level (0-based) of the set being played

    float stateTime = 0;
    float shake = 0;
    float realTime = 0;
    float bgPulse = 0;

    // power-up timers (seconds remaining)
    float tEnlarge = 0, tShrink = 0, tSlow = 0, tMega = 0, tMagnet = 0, tGun = 0;
    float gunCooldown = 0;

    int bulbsTotal = 0;  // light bulb bricks at level start
    float darkness = 0;  // 0 = fully lit, shown smoothly

    std::vector<Particle> particles;
    std::vector<Ghost> ghosts;
    std::vector<FloatText> texts;
    std::vector<PendingBlast> blasts;
    std::vector<Shot> shots;

    bool demo = false;            // attract mode on the title screen
    bool aiPlayers[MAX_PLAYERS] = {false, false, false, false};
    bool requestQuit = false;
    bool requestFullscreen = false;
    bool settingsDirty = false;
    int levelChanged = 0;         // increments whenever a new level was built (renderer rebuilds bg)

    void init();
    void update(float dt, const Vec2 inputs[MAX_PLAYERS]);
    void action(Action a, int player = 0);

    // menus
    int titleItemCount() const { return 6; }
    std::string titleItem(int i) const;
    int pauseItemCount() const { return 3; }
    std::string pauseItem(int i) const;

    int bricksLeft() const;
    int setChoices() const { return levelSetCount() + 1; }  // every set, plus "Complete" (all levels)
    bool completeRun() const { return runFirst == 0 && runLast == levelCount() - 1; }
    static void setRange(int set, int& first, int& last);
    int ballsAlive() const;
    bool playing() const { return state == State::Playing; }
    bool mouseCaptured() const { return state == State::Playing || state == State::Intro; }
    void startGame(int players, int set);

    // headless self-test: AI plays every level, returns false on physics trouble
    static bool runSelfTest(int seconds, bool verbose);
    // for screenshots
    void startDemoLevel(int lvl, int players);

private:
    float accumulator = 0;
    float demoTimer = 0;

    void loadLevel(int lvl);
    void spawnActors();
    int spawnBall(const BallSpawn& s);
    void spawnPill(Vec2 pos, Vec2 dir, int forcedType = -1);
    void spawnEnemy(Vec2 pos);
    void flushSpawns();
    void removeDead();
    void killEnemy(Body& e, bool scored);
    void batDeath(Body& bat);
    void toggleGroup(int group, Vec2 pos);
    void updateShots(float dt);
    void updateEnemies();
    float targetDarkness() const;
    void simulate(float dt);
    void processEvents();
    void brickHit(Body& ball, Body& brick, float speed);
    void destroyBrick(Body& brick, Body* ball);
    void explode(Vec2 pos);
    void applyPill(int type, Body& player);
    void loseBall(Body& ball, Vec2 holePos);
    void updateBatSizes();
    void updateEffects(float dt);
    void checkProgress();
    Vec2 aiInput(const Body& player) const;
    void addScore(int pts, Vec2 pos, bool show = true);
    void burst(Vec2 pos, int n, uint32_t color, float speed, float size, float grav = 900);
    void popText(Vec2 pos, const std::string& t, uint32_t color, bool big = false);
};

uint32_t brickColor(int idx);
const char* pillLabel(int type);
const char* pillName(int type);
uint32_t pillColor(int type);
uint32_t playerColor(int idx);
