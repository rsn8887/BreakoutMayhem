// Breakout Mayhem
// A physics-heavy bat-and-ball game in the spirit of Breakout, Arkanoid and Bolo.
#include <SDL.h>
#include <SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include "game.h"
#include "render.h"
#include "audio.h"

#include "platform.h"

namespace {

SDL_Window* window = nullptr;
SDL_Renderer* sdlRenderer = nullptr;
Renderer renderer;
Game game;
std::string settingsPath;

SDL_GameController* pads[MAX_PLAYERS] = {};
float stickRepeat[MAX_PLAYERS] = {};
int stickDir[MAX_PLAYERS] = {};
Vec2 mouseRel;
bool hasFocus = true;

const float KEY_SPEED = 800.f;     // desired bat speed from keys/pads
const float TURBO = 2.0f;
const float MOUSE_SCALE = 1.0f;

std::string dataPath(const char* file) {
#if defined(__SWITCH__)
    return std::string("romfs:/data/") + file;
#else
    char* base = SDL_GetBasePath();
    std::string p = base ? base : "./";
    if (base) SDL_free(base);
    return p + "data/" + file;
#endif
}

void loadSettings() {
#ifdef __SWITCH__
    makeDir("sdmc:/switch");
    makeDir("sdmc:/switch/BreakoutMayhem");
    settingsPath = "sdmc:/switch/BreakoutMayhem/settings.txt";
#else
    char* pref = SDL_GetPrefPath("rsn8887", "BreakoutMayhem");
    settingsPath = std::string(pref ? pref : "./") + "settings.txt";
    if (pref) SDL_free(pref);
#endif
    std::ifstream f(settingsPath);
    std::string key;
    Settings& s = game.settings;
    while (f >> key) {
        if (key == "hiscore") f >> s.hiscore;
        else if (key == "maxlevel") f >> s.maxLevel;
        else if (key == "mousegain") f >> s.mouseGain;
        else if (key == "sound") f >> s.sound;
        else if (key == "fullscreen") f >> s.fullscreen;
        else { std::string skip; f >> skip; }
    }
    s.maxLevel = std::max(1, std::min(s.maxLevel, levelCount()));
    s.mouseGain = clampf(s.mouseGain, 0.25f, 3.0f);
}

void saveSettings() {
    std::ofstream f(settingsPath);
    const Settings& s = game.settings;
    f << "hiscore " << s.hiscore << "\nmaxlevel " << s.maxLevel << "\nmousegain " << s.mouseGain << "\nsound "
      << s.sound << "\nfullscreen " << s.fullscreen << "\n";
}

void setFullscreen(bool on) {
#ifndef __SWITCH__
    SDL_SetWindowFullscreen(window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#endif
    game.settings.fullscreen = on;
}

int padSlot(SDL_JoystickID id) {
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) return i;
    return -1;
}

void openPad(int deviceIndex) {
    if (!SDL_IsGameController(deviceIndex)) return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    if (padSlot(id) >= 0) return;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (!pads[i]) {
            pads[i] = SDL_GameControllerOpen(deviceIndex);
            return;
        }
}

void closePad(SDL_JoystickID id) {
    int s = padSlot(id);
    if (s < 0) return;
    SDL_GameControllerClose(pads[s]);
    pads[s] = nullptr;
}

// combine two inputs per axis: the stronger one wins
Vec2 combine(Vec2 a, Vec2 b) {
    return Vec2(std::fabs(b.x) > std::fabs(a.x) ? b.x : a.x, std::fabs(b.y) > std::fabs(a.y) ? b.y : a.y);
}

Vec2 padInput(int slot) {
    SDL_GameController* c = pads[slot];
    if (!c) return {};
    auto ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(c, a) / 32767.f; };
    Vec2 v(ax(SDL_CONTROLLER_AXIS_LEFTX), ax(SDL_CONTROLLER_AXIS_LEFTY));
    if (v.len() < 0.15f) v = Vec2(0, 0);
    if (v.len() > 1) v = v.norm();
    v *= KEY_SPEED;
    auto bt = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b) != 0; };
    if (bt(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) v.x = -KEY_SPEED;
    if (bt(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) v.x = KEY_SPEED;
    if (bt(SDL_CONTROLLER_BUTTON_DPAD_UP)) v.y = -KEY_SPEED;
    if (bt(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) v.y = KEY_SPEED;
    bool turbo = bt(SDL_CONTROLLER_BUTTON_A) || bt(SDL_CONTROLLER_BUTTON_B) || bt(SDL_CONTROLLER_BUTTON_X) ||
                 bt(SDL_CONTROLLER_BUTTON_Y) || ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f ||
                 ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.5f;
    if (turbo) v *= TURBO;
    return v;
}

void gatherInputs(float dt, Vec2 out[MAX_PLAYERS]) {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    auto keyDir = [&](SDL_Scancode l, SDL_Scancode r, SDL_Scancode u, SDL_Scancode d) {
        Vec2 v;
        if (k[l]) v.x -= KEY_SPEED;
        if (k[r]) v.x += KEY_SPEED;
        if (k[u]) v.y -= KEY_SPEED;
        if (k[d]) v.y += KEY_SPEED;
        return v;
    };
    Vec2 wasd = keyDir(SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_S);
    if (k[SDL_SCANCODE_LCTRL]) wasd *= TURBO;
    Vec2 arrows = keyDir(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    if (k[SDL_SCANCODE_RSHIFT] || (game.worldPlayers == 1 && k[SDL_SCANCODE_RCTRL])) arrows *= TURBO;

    // mouse: bat follows the mouse velocity (relative mode)
    Vec2 mouse;
    if (dt > 0) mouse = mouseRel / dt * (game.settings.mouseGain * MOUSE_SCALE);
    if (mouse.len() > 2600) mouse = mouse.norm() * 2600;
    mouseRel = Vec2(0, 0);

    for (int i = 0; i < MAX_PLAYERS; i++) out[i] = padInput(i);
    out[0] = combine(combine(out[0], wasd), mouse);
    if (game.worldPlayers == 1) out[0] = combine(out[0], arrows);
    else out[1] = combine(out[1], arrows);
}

void menuNavFromSticks(float dt) {
    if (game.state == State::Playing) return;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!pads[i]) continue;
        float x = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTX) / 32767.f;
        float y = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTY) / 32767.f;
        int dir = 0;
        if (y < -0.6f) dir = 1;
        else if (y > 0.6f) dir = 2;
        else if (x < -0.6f) dir = 3;
        else if (x > 0.6f) dir = 4;
        if (dir == 0) { stickDir[i] = 0; continue; }
        stickRepeat[i] -= dt;
        if (dir != stickDir[i] || stickRepeat[i] <= 0) {
            stickRepeat[i] = dir != stickDir[i] ? 0.4f : 0.15f;
            stickDir[i] = dir;
            const Action a[5] = {Action::Up, Action::Up, Action::Down, Action::Left, Action::Right};
            game.action(a[dir], i);
        }
    }
}

// menu item under the mouse (title and pause menus, same layout as Renderer::drawOverlay), or -1
int menuItemAt(int x, int y) {
    int top, count, left, right;
    if (game.state == State::Title) { top = 330; count = game.titleItemCount(); left = 390; right = 890; }
    else if (game.state == State::Paused) { top = 318; count = game.pauseItemCount(); left = 440; right = 840; }
    else return -1;
    if (x < left || x > right || y < top) return -1;
    int i = (y - top) / 44;
    return i < count ? i : -1;
}

void menuHover(int x, int y) {
    int i = menuItemAt(x, y);
    if (i >= 0) game.menuIndex = i;
}

void handleEvent(const SDL_Event& e, bool& running) {
    switch (e.type) {
        case SDL_QUIT: running = false; break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                hasFocus = false;
                if (game.state == State::Playing) game.action(Action::Pause);
            } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                hasFocus = true;
            }
            break;
        case SDL_KEYDOWN: {
            SDL_Keycode k = e.key.keysym.sym;
            bool menu = game.state != State::Playing;
            if ((k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) || k == SDLK_F11 || k == SDLK_f) {
                if (!e.key.repeat) setFullscreen(!game.settings.fullscreen);
                break;
            }
            if (k == SDLK_m && !e.key.repeat) {
                game.settings.sound = !game.settings.sound;
                Audio::setEnabled(game.settings.sound);
                game.settingsDirty = true;
            } else if (k == SDLK_k || k == SDLK_l) {
                game.settings.mouseGain = clampf(game.settings.mouseGain + (k == SDLK_l ? 0.25f : -0.25f), 0.25f, 3.f);
                game.settingsDirty = true;
            } else if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) {
                game.action(Action::Back);
            } else if (k == SDLK_p) {
                game.action(Action::Pause);
            } else if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_KP_ENTER) {
                game.action(Action::Confirm);
            } else if (menu) {
                if (k == SDLK_UP || k == SDLK_w) game.action(Action::Up);
                else if (k == SDLK_DOWN || k == SDLK_s) game.action(Action::Down);
                else if (k == SDLK_LEFT || k == SDLK_a) game.action(Action::Left);
                else if (k == SDLK_RIGHT || k == SDLK_d) game.action(Action::Right);
                else if (game.state == State::Title && k >= SDLK_1 && k <= SDLK_4) game.startGame(int(k - SDLK_0), game.startSet);
            }
            break;
        }
        case SDL_MOUSEMOTION:
            if (SDL_GetRelativeMouseMode()) mouseRel += Vec2(float(e.motion.xrel), float(e.motion.yrel));
            else menuHover(e.motion.x, e.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.which == SDL_TOUCH_MOUSEID) break;
            if (e.button.button == SDL_BUTTON_LEFT) {
                int item = menuItemAt(e.button.x, e.button.y);
                if (item >= 0) {
                    game.menuIndex = item;
                } else if (game.state == State::Title) {
                    game.menuIndex = 0;   // click anywhere else on the title screen: start the game
                } else if (game.state == State::Paused) {
                    break;                // clicks outside the pause menu do nothing
                }
                game.action(Action::Confirm);
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                game.action(game.state == State::Title ? Action::Right : Action::Back);
            }
            break;
        case SDL_MOUSEWHEEL:
            if (game.state == State::Title) game.action(e.wheel.y > 0 ? Action::Up : Action::Down);
            break;
        case SDL_FINGERDOWN:
            if (game.state == State::Title) game.menuIndex = 0;
            if (game.state != State::Playing) game.action(Action::Confirm);
            break;
        case SDL_FINGERMOTION:
            if (game.state == State::Playing) mouseRel += Vec2(e.tfinger.dx * SCREEN_W, e.tfinger.dy * SCREEN_H);
            break;
        case SDL_CONTROLLERDEVICEADDED: openPad(e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: closePad(e.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN: {
            int slot = padSlot(e.cbutton.which);
            bool menu = game.state != State::Playing;
            switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A: if (menu) game.action(Action::Confirm, slot); break;
                case SDL_CONTROLLER_BUTTON_B: if (menu) game.action(Action::Back, slot); break;
                case SDL_CONTROLLER_BUTTON_START: game.action(Action::Pause, slot); break;
                case SDL_CONTROLLER_BUTTON_BACK: game.action(Action::Back, slot); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: if (menu) game.action(Action::Up, slot); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: if (menu) game.action(Action::Down, slot); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: if (menu) game.action(Action::Left, slot); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: if (menu) game.action(Action::Right, slot); break;
                default: break;
            }
            break;
        }
        default: break;
    }
}

bool initVideo(bool software) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __SWITCH__
    flags = SDL_WINDOW_FULLSCREEN;
#endif
    window = SDL_CreateWindow("Breakout Mayhem", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, flags);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }
    if (!software) sdlRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdlRenderer) sdlRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!sdlRenderer) {
        SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(sdlRenderer, SCREEN_W, SCREEN_H);
    return true;
}


// --shot <level|0=title|tN=title over level N|-N=intro> <file.bmp> [seconds] [players]: renders a screenshot (used for testing)
int screenshot(int argc, char** argv) {
    int lvl = std::atoi(argv[2]);
    const char* file = argv[3];
    float secs = argc > 4 ? float(std::atof(argv[4])) : 2.0f;
    int players = argc > 5 ? std::atoi(argv[5]) : 1;
    if (!initVideo(true)) return 1;
    if (TTF_Init() != 0 || !renderer.init(sdlRenderer, dataPath("Poppins-Bold.ttf"))) return 1;
    renderer.helpLine = "P1: Mouse / WASD + Ctrl / Pad 1     P2: Arrows + Shift / Pad 2     P3-4: Pads";
    game.settings.maxLevel = levelCount();
    if (lvl == 0) {
        game.init();
        if (argv[2][0] == 't') { // "t8": title screen with level 8 running in the background
            game.startDemoLevel(std::atoi(argv[2] + 1) - 1, 1);
            game.state = State::Title;
        }
    } else {
        game.startDemoLevel(std::abs(lvl) - 1, players);
        game.score = 12340;
    }
    Vec2 in[MAX_PLAYERS];
    for (float t = 0; t < secs; t += 1 / 60.f) {
        game.update(1 / 60.f, in);
        if (lvl > 0 && game.state == State::Intro) game.state = State::Playing; // keep the demo running
    }
    if (lvl < 0) { game.state = State::Intro; game.stateTime = 0.1f; }
    renderer.draw(game);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(sdlRenderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, file);
    SDL_FreeSurface(s);
    std::printf("saved %s (level %d, %.1fs, bricks left %d, balls %d)\n", file, lvl, secs, game.bricksLeft(), game.ballsAlive());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2 && std::strcmp(argv[1], "--trace") == 0) { // --trace <level> [seconds]: debug print
        Game g;
        g.startDemoLevel(std::atoi(argv[2]) - 1, 1);
        float secs = argc > 3 ? float(std::atof(argv[3])) : 10.f;
        Vec2 in[MAX_PLAYERS];
        for (int f = 0; f < int(secs * 60); f++) {
            g.update(1 / 60.f, in);
            if (g.state == State::Intro) g.state = State::Playing;
            if (f % 15) continue;
            std::printf("t=%5.2f bricks=%d", f / 60.f, g.bricksLeft());
            for (const auto& b : g.world.bodies)
                if (b.kind == Kind::Ball || b.kind == Kind::Player)
                    std::printf("  %s(%4.0f,%4.0f v%5.0f,%5.0f)", b.kind == Kind::Ball ? "ball" : "bat", b.pos.x, b.pos.y, b.vel.x, b.vel.y);
            std::printf("\n");
        }
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) {
        int secs = argc > 2 ? std::atoi(argv[2]) : 120;
        return Game::runSelfTest(secs, true) ? 0 : 1;
    }
    platformInit();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (argc > 3 && std::strcmp(argv[1], "--shot") == 0) {
        int r = screenshot(argc, argv);
        SDL_Quit();
        return r;
    }
    if (TTF_Init() != 0) {
        SDL_Log("TTF_Init: %s", TTF_GetError());
        return 1;
    }
    loadSettings();
    if (!initVideo(false)) return 1;
    if (!renderer.init(sdlRenderer, dataPath("Poppins-Bold.ttf"))) return 1;
#if defined(__SWITCH__)
    renderer.helpLine = "Left stick / D-Pad: move bat     A/B/X/Y: turbo     +: pause     Touch: drag the bat";
#else
    renderer.helpLine = "P1: Mouse / WASD + Ctrl / Pad 1     P2: Arrows + Shift / Pad 2     P3-4: Pads     F: Fullscreen";
#endif
    if (game.settings.fullscreen) setFullscreen(true);
    Audio::init();
    Audio::setEnabled(game.settings.sound);
    SDL_GameControllerEventState(SDL_ENABLE);
    for (int i = 0; i < SDL_NumJoysticks(); i++) openPad(i);

    game.init();

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    bool running = true;
    while (running && platformRunning()) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e, running);
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = float(double(now - last) / double(freq));
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        bool wantRel = game.mouseCaptured() && hasFocus;
        if (bool(SDL_GetRelativeMouseMode()) != wantRel) {
            SDL_SetRelativeMouseMode(wantRel ? SDL_TRUE : SDL_FALSE);
            mouseRel = Vec2(0, 0);
        }

        menuNavFromSticks(dt);
        Vec2 inputs[MAX_PLAYERS];
        gatherInputs(dt, inputs);
        game.update(dt, inputs);

        if (game.requestQuit) running = false;
        if (game.settingsDirty) {
            saveSettings();
            game.settingsDirty = false;
        }

        renderer.draw(game);
        SDL_RenderPresent(sdlRenderer);
    }

    saveSettings();
    Audio::shutdown();
    renderer.shutdown();
    for (auto& p : pads)
        if (p) SDL_GameControllerClose(p);
    SDL_DestroyRenderer(sdlRenderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    platformExit();
    return 0;
}
