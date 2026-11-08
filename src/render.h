// Breakout Mayhem - renderer (procedural 90s-style shaded sprites, SDL2 + SDL2_ttf)
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <map>
#include <string>
#include <vector>
#include "game.h"

enum FontSize { FONT_SMALL, FONT_MEDIUM, FONT_LARGE, FONT_HUGE, FONT_COUNT };
enum Align { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

class Renderer {
public:
    bool init(SDL_Renderer* r, const std::string& fontPath);
    void shutdown();
    void draw(Game& g);
    std::string helpLine;   // control summary shown on the title screen (platform dependent)

private:
    struct Tex { SDL_Texture* t = nullptr; int w = 0, h = 0; int used = 0; };

    SDL_Renderer* R = nullptr;
    TTF_Font* fonts[FONT_COUNT] = {};
    std::map<std::string, Tex> sprites;
    std::map<std::string, Tex> textCache;
    SDL_Texture* bg = nullptr;
    SDL_Texture* glow = nullptr;
    SDL_Texture* logo = nullptr;
    int logoW = 0, logoH = 0;
    int bgLevel = -1;
    int frame = 0;
    float time = 0;
    Vec2 off;               // screen shake offset
    std::vector<SDL_Vertex> verts;
    std::vector<int> idx;

    // sprite factories
    Tex& sphere(int r, uint32_t color, int style);
    Tex& brick(int w, int h, uint32_t color, int style);
    Tex& capsule(int len, int r, uint32_t color, int style);
    Tex& hole(int r);
    void makeBackground(float hue);
    void makeGlow();
    void makeLogo();

    // primitives
    void flush();
    void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, SDL_Color col);
    void line(Vec2 a, Vec2 b, float th, SDL_Color col);
    void ring(Vec2 c, float r, float th, SDL_Color col, int segs = 48);
    void disc(Vec2 c, float r, SDL_Color col, int segs = 32);
    void rect(float x, float y, float w, float h, SDL_Color col);
    void glowAt(Vec2 p, float size, SDL_Color col);
    void blit(Tex& t, float cx, float cy, float w, float h, float angle = 0, Uint8 alpha = 255, bool shadow = false,
              bool additive = false);
    void text(const std::string& s, FontSize f, float x, float y, uint32_t color, Align a = ALIGN_LEFT,
              bool shadow = true, Uint8 alpha = 255);
    int textWidth(const std::string& s, FontSize f);

    // scene
    void drawWorld(Game& g);
    void drawFields(Game& g);
    void drawBody(Game& g, const Body& b, float scale, Uint8 alpha, bool shadow);
    void drawBat(const Body& b, float scale, Uint8 alpha, bool shadow);
    void drawFrame();
    void drawHud(Game& g);
    void drawOverlay(Game& g);
    void panel(float x, float y, float w, float h);
    void drawMenu(const std::vector<std::string>& items, int sel, float y);
};
