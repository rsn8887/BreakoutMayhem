// Breakout Mayhem - renderer
// Every sprite is generated procedurally at runtime: shaded, anti-aliased 90s-style graphics,
// so the game needs no image files (and no SDL_image).
#include "render.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace {

struct RGBf { float r, g, b, a; };

RGBf rgb(uint32_t c, float a = 1) {
    return {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, a};
}
RGBf mul(RGBf c, float s) { return {c.r * s, c.g * s, c.b * s, c.a}; }
RGBf add(RGBf c, float s) { return {c.r + s, c.g + s, c.b + s, c.a}; }
RGBf mix(RGBf a, RGBf b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
SDL_Color col(uint32_t c, Uint8 a = 255) { return SDL_Color{Uint8(c >> 16), Uint8(c >> 8), Uint8(c), a}; }

Uint32 pack(RGBf c, float cov) {
    auto ch = [](float v) { return Uint32(clampf(v, 0, 1) * 255.f + 0.5f); };
    return (ch(c.a * cov) << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

float hash2(int x, int y) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float(h ^ (h >> 16)) / 4294967295.f;
}

// 4x4 supersampled rasterizer: sdf < 0 is inside, shade(x, y, d) returns the color at a pixel center
template <class SDF, class SHADE>
SDL_Texture* rasterize(SDL_Renderer* R, int w, int h, SDF sdf, SHADE shade) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return nullptr;
    Uint32* px = static_cast<Uint32*>(s->pixels);
    int pitch = s->pitch / 4;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int cov = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++)
                    if (sdf(x + (sx + 0.5f) / 4.f, y + (sy + 0.5f) / 4.f) < 0) cov++;
            if (!cov) { px[y * pitch + x] = 0; continue; }
            float cx = x + 0.5f, cy = y + 0.5f;
            RGBf c = shade(cx, cy, std::min(sdf(cx, cy), -0.01f));
            px[y * pitch + x] = pack(c, cov / 16.f);
        }
    SDL_Texture* t = SDL_CreateTextureFromSurface(R, s);
    SDL_FreeSurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

const float LX = -0.45f, LY = -0.6f, LZ = 0.66f; // light from the top left

uint32_t groupColor(int g) {
    static const uint32_t c[4] = {0x40E0FF, 0xFFA040, 0xFF60C0, 0x80FF60};
    return c[((g % 4) + 4) % 4];
}
const Vec2 L2(-0.6f, -0.8f);

float sdRoundBox(float px, float py, float hx, float hy, float r) {
    float qx = std::fabs(px) - hx + r, qy = std::fabs(py) - hy + r;
    float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

RGBf hsv(float h, float s, float v) {
    h = std::fmod(h, 360.f) / 60.f;
    int i = int(h);
    float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i) {
        case 0: return {v, t, p, 1};
        case 1: return {q, v, p, 1};
        case 2: return {p, v, t, 1};
        case 3: return {p, q, v, 1};
        case 4: return {t, p, v, 1};
        default: return {v, p, q, 1};
    }
}

} // namespace

// ------------------------------------------------------------------------------------------

bool Renderer::init(SDL_Renderer* r, const std::string& fontPath) {
    R = r;
    const int sizes[FONT_COUNT] = {20, 28, 46, 104};
    for (int i = 0; i < FONT_COUNT; i++) {
        fonts[i] = TTF_OpenFont(fontPath.c_str(), sizes[i]);
        if (!fonts[i]) {
            SDL_Log("Cannot open font %s: %s", fontPath.c_str(), TTF_GetError());
            return false;
        }
    }
    makeGlow();
    makeLogo();
    return true;
}

void Renderer::shutdown() {
    for (auto& kv : sprites) SDL_DestroyTexture(kv.second.t);
    for (auto& kv : textCache) SDL_DestroyTexture(kv.second.t);
    sprites.clear();
    textCache.clear();
    if (bg) SDL_DestroyTexture(bg);
    if (glow) SDL_DestroyTexture(glow);
    if (logo) SDL_DestroyTexture(logo);
    if (lightMap) SDL_DestroyTexture(lightMap);
    bg = glow = logo = lightMap = nullptr;
    for (auto& f : fonts) {
        if (f) TTF_CloseFont(f);
        f = nullptr;
    }
}

// ------------------------------------------------------------------------------------------
// sprite factories

Renderer::Tex& Renderer::sphere(int r, uint32_t color, int style) {
    char key[64];
    std::snprintf(key, sizeof key, "s%d_%06x_%d", r, color, style);
    auto it = sprites.find(key);
    if (it != sprites.end()) return it->second;
    int size = 2 * r + 2;
    float c = size * 0.5f, R0 = float(r);
    RGBf base = rgb(color);
    auto sdf = [=](float x, float y) { return std::sqrt((x - c) * (x - c) + (y - c) * (y - c)) - R0; };
    auto shade = [=](float x, float y, float d) {
        float nx = (x - c) / R0, ny = (y - c) / R0;
        float r2 = nx * nx + ny * ny;
        if (r2 > 0.999f) { float k = std::sqrt(0.999f / r2); nx *= k; ny *= k; r2 = 0.999f; }
        float rad = std::sqrt(r2);
        float nz = std::sqrt(1 - r2);
        auto light = [&](float ax, float ay, float az, RGBf b, float amb, float dif, float specK, float specP) {
            float l = std::sqrt(ax * ax + ay * ay + az * az);
            ax /= l; ay /= l; az /= l;
            float diff = std::max(0.f, ax * LX + ay * LY + az * LZ);
            float hx = LX, hy = LY, hz = LZ + 1;
            float hl = std::sqrt(hx * hx + hy * hy + hz * hz);
            float sp = std::pow(std::max(0.f, (ax * hx + ay * hy + az * hz) / hl), specP) * specK;
            RGBf o = mul(b, amb + dif * diff);
            return add(o, sp);
        };
        RGBf o;
        switch (style) {
            case 0: // glossy ball
                o = light(nx, ny, nz, base, 0.28f, 0.85f, 1.0f, 48);
                o = add(o, 0.12f * std::max(0.f, ny) * rad); // floor bounce light
                break;
            case 1: case 2: case 3: { // dome bricks, 1/2/3 hits
                float bevel = rad > 0.8f ? 0.35f : 2.2f;
                o = light(nx, ny, nz * bevel + 0.25f, base, 0.42f, 0.7f, 0.45f, 30);
                if (style >= 2 && rad > 0.5f && rad < 0.6f) o = mul(o, 0.72f);
                if (style == 3 && rad > 0.8f) o = light(nx, ny, nz * 0.35f, rgb(0xF2C040), 0.45f, 0.8f, 0.8f, 20);
                break;
            }
            case 4: { // bumper: chrome rim, glossy center
                if (rad > 0.68f) {
                    float env = 0.55f - 0.45f * ny + 0.15f * std::sin(nx * 6);
                    o = light(nx, ny, nz * 0.6f, mul(rgb(0xC8D0DC), env + 0.3f), 0.5f, 0.5f, 1.0f, 30);
                } else {
                    o = light(nx, ny, nz * 1.5f, base, 0.35f, 0.8f, 0.9f, 40);
                    if (rad < 0.18f) o = mix(o, rgb(0xFFFFFF), 0.7f);
                }
                break;
            }
            case 5: // billiard puck
                o = light(nx, ny, nz, rad < 0.42f ? rgb(0xF4F4F0) : base, 0.32f, 0.8f, 0.9f, 48);
                break;
            case 7: // enemy blob
                o = light(nx, ny, nz * 1.2f, base, 0.35f, 0.85f, 0.9f, 30);
                if (rad > 0.55f && rad < 0.62f && ny > 0.1f) o = mul(o, 0.7f);
                break;
            case 6: { // disc bat
                o = light(nx, ny, nz * 1.6f + 0.2f, base, 0.38f, 0.75f, 0.8f, 36);
                if (rad > 0.66f && rad < 0.74f) o = mul(o, 0.7f);
                break;
            }
            default:
                o = base;
        }
        if (d > -1.2f) o = mul(o, 0.55f); // dark outline
        o.a = 1;
        return o;
    };
    Tex t;
    t.t = rasterize(R, size, size, sdf, shade);
    t.w = t.h = size;
    return sprites[key] = t;
}

Renderer::Tex& Renderer::brick(int w, int h, uint32_t color, int style) {
    char key[64];
    std::snprintf(key, sizeof key, "b%dx%d_%06x_%d", w, h, color, style);
    auto it = sprites.find(key);
    if (it != sprites.end()) return it->second;
    float hx = w * 0.5f - 0.5f, hy = h * 0.5f - 0.5f, cx = w * 0.5f, cy = h * 0.5f;
    float cr = std::min(6.f, h * 0.25f);
    float bev = clampf(std::min(w, h) * 0.16f, 3.f, 6.f);
    RGBf base = rgb(color);
    if (style == 3) base = rgb(0xA4ACB8);
    if (style == 7) base = rgb(0x4A5058);
    if (style == 8) base = rgb(0x2E2A30);
    if (style == 9) base = rgb(0x50555C);
    auto sdf = [=](float x, float y) { return sdRoundBox(x - cx, y - cy, hx, hy, cr); };
    auto shade = [=](float x, float y, float d) {
        float din = -d;
        float gx = sdRoundBox(x - cx + 0.5f, y - cy, hx, hy, cr) - sdRoundBox(x - cx - 0.5f, y - cy, hx, hy, cr);
        float gy = sdRoundBox(x - cx, y - cy + 0.5f, hx, hy, cr) - sdRoundBox(x - cx, y - cy - 0.5f, hx, hy, cr);
        float gl = std::sqrt(gx * gx + gy * gy);
        if (gl > 1e-4f) { gx /= gl; gy /= gl; }
        float fy = y / h;
        RGBf c = base;
        float s = 1.0f + 0.12f * (0.5f - fy);
        // faces
        float ix = x - cx, iy = y - cy;
        float inner = sdRoundBox(ix, iy, hx - bev - 2, hy - bev - 2, std::max(1.f, cr - 2));
        switch (style) {
            case 3: // brushed metal + rivets
                s += (hash2(0, int(y)) - 0.5f) * 0.12f;
                for (int k = 0; k < 4; k++) {
                    float rx = (k & 1) ? w - 7.f : 7.f, ry = (k & 2) ? h - 7.f : 7.f;
                    if (w < 30 || h < 20) break;
                    float dx = x - rx, dy = y - ry;
                    float dd = std::sqrt(dx * dx + dy * dy);
                    if (dd < 2.6f) s = 1.25f - 0.25f * (dx + dy) / 2.6f;
                }
                break;
            case 4: // explosive: hazard stripes
                if (inner < 0) {
                    bool stripe = int(std::floor((x + y) / 9.f)) % 2 == 0;
                    c = stripe ? rgb(0xF2C21E) : rgb(0x2A1C1C);
                }
                break;
            case 5: { // multiball: ball icon
                float rr = h * 0.28f;
                float dx = x - cx, dy = y - cy, dd = std::sqrt(dx * dx + dy * dy);
                if (dd < rr) {
                    float nx = dx / rr, ny = dy / rr;
                    float nz = std::sqrt(std::max(0.f, 1 - nx * nx - ny * ny));
                    float diff = std::max(0.f, nx * LX + ny * LY + nz * LZ);
                    c = rgb(0xEEF2F8);
                    s = 0.35f + 0.85f * diff + (dd > rr - 1.2f ? -0.4f : 0.f);
                }
                break;
            }
            case 7: // conveyor belt body
                if (inner < 0) s *= 0.75f;
                break;
            case 8: { // skull
                float k = h * 0.42f;
                float u = (x - cx) / k, v = (y - cy) / k;
                bool head = u * u + (v + 0.15f) * (v + 0.15f) < 0.62f * 0.62f;
                bool jaw = std::fabs(u) < 0.38f && v > 0.1f && v < 0.66f;
                if (head || jaw) {
                    c = rgb(0xF2EEE2);
                    s = 1.05f - 0.25f * (v + 0.6f) * 0.5f;
                    bool eye = (std::fabs(u) - 0.27f) * (std::fabs(u) - 0.27f) + (v + 0.12f) * (v + 0.12f) < 0.18f * 0.18f;
                    bool nose = u * u + (v - 0.17f) * (v - 0.17f) < 0.08f * 0.08f;
                    bool teeth = v > 0.44f && (std::fabs(u) < 0.025f || std::fabs(std::fabs(u) - 0.15f) < 0.025f);
                    if (eye || nose) { c = rgb(0x120808); s = 1; }
                    if (teeth) s *= 0.55f;
                }
                break;
            }
            case 9: { // magnet: red horseshoe with silver tips
                float k = h;
                float u = (x - cx) / k, v = (y - cy) / k + 0.08f;
                float r = std::sqrt(u * u + v * v);
                bool ring = v <= 0 && r > 0.18f && r < 0.36f;
                bool leg = v > 0 && v < 0.34f && std::fabs(u) > 0.18f && std::fabs(u) < 0.36f;
                if (ring || leg) {
                    c = (leg && v > 0.2f) ? rgb(0xE0E4EA) : rgb(0xD8302A);
                    float shadeU = ring ? (r - 0.27f) / 0.09f : (std::fabs(u) - 0.27f) / 0.09f;
                    s = 1.0f - 0.25f * shadeU;
                }
                break;
            }
            case 10: { // nest: a brick with two eyes
                float k = h;
                for (int e = -1; e <= 1; e += 2) {
                    float u = (x - cx) / k - e * 0.42f, v = (y - cy) / k + 0.02f;
                    if ((u / 0.2f) * (u / 0.2f) + (v / 0.27f) * (v / 0.27f) < 1) {
                        c = rgb(0xFFFFF0);
                        s = 1;
                        float pu = u - 0.03f, pv = v - 0.07f;
                        if (pu * pu + pv * pv < 0.1f * 0.1f) c = rgb(0x101010);
                    }
                }
                break;
            }
            case 12: { // big ball inside
                float rr = h * 0.36f;
                float dx = x - cx, dy = y - cy, dd = std::sqrt(dx * dx + dy * dy);
                if (dd < rr) {
                    float nx = dx / rr, ny = dy / rr;
                    float nz = std::sqrt(std::max(0.f, 1 - nx * nx - ny * ny));
                    float diff = std::max(0.f, nx * LX + ny * LY + nz * LZ);
                    c = rgb(0x6E7078);
                    s = 0.3f + 0.9f * diff + (dd > rr - 1.2f ? -0.4f : 0.f);
                }
                break;
            }
            default:
                break;
        }
        if ((style == 1 || style == 2) && std::fabs(inner) < 1.2f) s *= 0.68f;
        if ((style == 1 || style == 2) && inner > 1.2f && inner < 2.4f) s *= 1.15f;
        if (din < bev) {
            float k = 1 - din / bev;
            float lit = gx * L2.x + gy * L2.y;
            s += 0.55f * lit * k;
            if (style == 2) c = mix(c, rgb(0xF2C040), 0.75f);
        }
        c = mul(c, s);
        if (din < 1.0f) c = mul(c, 0.5f);
        c.a = 1;
        return c;
    };
    Tex t;
    t.t = rasterize(R, w, h, sdf, shade);
    t.w = w;
    t.h = h;
    return sprites[key] = t;
}

Renderer::Tex& Renderer::capsule(int len, int r, uint32_t color, int style) {
    char key[64];
    std::snprintf(key, sizeof key, "c%d_%d_%06x_%d", len, r, color, style);
    auto it = sprites.find(key);
    if (it != sprites.end()) return it->second;
    int w = len + 2, h = 2 * r + 2;
    float ax = r + 1.f, bx = len + 1.f - r, cy = h * 0.5f, R0 = float(r);
    RGBf base = rgb(color);
    auto spine = [=](float x) { return clampf(x, ax, bx); };
    auto sdf = [=](float x, float y) {
        float qx = spine(x);
        return std::sqrt((x - qx) * (x - qx) + (y - cy) * (y - cy)) - (R0 - 0.3f);
    };
    auto shade = [=](float x, float y, float d) {
        float qx = spine(x);
        float nx = (x - qx) / R0, ny = (y - cy) / R0;
        float r2 = std::min(nx * nx + ny * ny, 0.999f);
        float nz = std::sqrt(1 - r2);
        float diff = std::max(0.f, nx * LX + ny * LY + nz * LZ);
        float hx = LX, hy = LY, hz = LZ + 1, hl = std::sqrt(hx * hx + hy * hy + hz * hz);
        float sp = std::pow(std::max(0.f, (nx * hx + ny * hy + nz * hz) / hl), 40.f);
        RGBf o;
        if (style == 1) {
            float env = 0.6f - 0.4f * ny;
            o = add(mul(base, 0.3f + 0.55f * diff + 0.3f * env), sp * 0.9f);
        } else {
            o = add(mul(base, 0.3f + 0.85f * diff), sp * 0.85f);
        }
        if (d > -1.2f) o = mul(o, 0.5f);
        o.a = 1;
        return o;
    };
    Tex t;
    t.t = rasterize(R, w, h, sdf, shade);
    t.w = w;
    t.h = h;
    return sprites[key] = t;
}

Renderer::Tex& Renderer::hole(int r) {
    char key[32];
    std::snprintf(key, sizeof key, "h%d", r);
    auto it = sprites.find(key);
    if (it != sprites.end()) return it->second;
    int size = 2 * r + 2;
    float c = size * 0.5f, R0 = float(r);
    auto sdf = [=](float x, float y) { return std::sqrt((x - c) * (x - c) + (y - c) * (y - c)) - R0; };
    // A shaft going straight down into the floor: nothing is raised. The edge is semi-transparent so the level's
    // background runs right up to it; the near (top-left) side lies in the shadow of the floor's edge, the far
    // (bottom-right) inner wall catches the light, and the shaft fades to black with faint bands as it goes down.
    auto shade = [=](float x, float y, float) {
        float dx = x - c, dy = y - c;
        float dist = std::sqrt(dx * dx + dy * dy);
        float t = std::min(1.f, dist / R0);                      // 0 = centre, 1 = edge
        Vec2 dir = dist > 0.01f ? Vec2(dx / dist, dy / dist) : Vec2(0, 0);
        float facing = dir.x * L2.x + dir.y * L2.y;              // +1 on the top-left (near) side
        float lit = std::max(0.f, -facing);                      // far wall faces the light
        auto smooth = [](float e0, float e1, float v) {
            float k = clampf((v - e0) / (e1 - e0), 0.f, 1.f);
            return k * k * (3 - 2 * k);
        };
        // inner wall: dim on the near side, lit on the far side
        RGBf o = mul(rgb(0x4A5064), 0.35f + 1.05f * lit);
        // the floor's top-left edge throws a crisp shadow across the upper part of the wall
        Vec2 sp(dx + L2.x * R0 * 0.22f, dy + L2.y * R0 * 0.22f);    // shadow circle shifted towards the light
        float inShadow = smooth(R0 * 0.98f, R0 * 0.90f, std::sqrt(sp.x * sp.x + sp.y * sp.y));
        o = mul(o, 1.f - 0.7f * (1.f - inShadow));
        // depth: the wall recedes into black towards the middle, with faint bands like a tunnel
        float d = smooth(0.25f, 1.f, t);
        o = mul(o, d * d * (1.f + 0.12f * std::sin(t * 38.f)));
        // the background shows through at the very edge, so the floor visibly drops away
        o.a = 1.f - 0.6f * smooth(0.86f, 1.0f, t);
        return o;
    };
    Tex t;
    t.t = rasterize(R, size, size, sdf, shade);
    t.w = t.h = size;
    return sprites[key] = t;
}

void Renderer::makeGlow() {
    const int S = 64;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, S, S, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32* px = static_cast<Uint32*>(s->pixels);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float dx = (x + 0.5f - S / 2.f) / (S / 2.f), dy = (y + 0.5f - S / 2.f) / (S / 2.f);
            float d = std::sqrt(dx * dx + dy * dy);
            float a = d < 1 ? (1 - d) * (1 - d) : 0;
            px[y * (s->pitch / 4) + x] = (Uint32(a * 255) << 24) | 0xFFFFFF;
        }
    glow = SDL_CreateTextureFromSurface(R, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);
}

void Renderer::makeBackground(float hue) {
    if (bg) SDL_DestroyTexture(bg);
    const int W = 320, H = 180;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32* px = static_cast<Uint32*>(s->pixels);
    int seed = int(hue * 7);
    auto vnoise = [&](float x, float y) {
        int xi = int(std::floor(x)), yi = int(std::floor(y));
        float fx = x - xi, fy = y - yi;
        fx = fx * fx * (3 - 2 * fx);
        fy = fy * fy * (3 - 2 * fy);
        float a = hash2(xi + seed, yi), b = hash2(xi + 1 + seed, yi), c = hash2(xi + seed, yi + 1),
              d = hash2(xi + 1 + seed, yi + 1);
        return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
    };
    RGBf c1 = hsv(hue, 0.55f, 0.88f), c2 = hsv(hue, 0.07f, 1.0f);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float f = 0, amp = 0.55f, fr = 1.f / 22.f;
            for (int o = 0; o < 5; o++) {
                f += vnoise(x * fr, y * fr) * amp;
                amp *= 0.5f;
                fr *= 2.f;
            }
            f = clampf((f - 0.25f) * 1.6f, 0, 1);
            RGBf c = mix(c1, c2, f);
            float vx = (x - W / 2.f) / (W / 2.f), vy = (y - H / 2.f) / (H / 2.f);
            c = mul(c, 1.0f - 0.18f * (vx * vx + vy * vy));
            px[y * (s->pitch / 4) + x] = pack(c, 1);
        }
    bg = SDL_CreateTextureFromSurface(R, s);
    SDL_FreeSurface(s);
}

// Renders a word letter by letter with optical spacing. The bundled Poppins has no kerning data, so diagonal and
// round pairs (A-Y, K-O) end up with visibly wider gaps than straight pairs. For every pair the real closest
// distance between the letter shapes is measured row by row; pairs much more open than usual are pulled in.
static SDL_Surface* renderSpacedWord(TTF_Font* font, const char* word) {
    struct Glyph { SDL_Surface* s; int adv; std::vector<int> left, right; };
    std::vector<Glyph> gl;
    for (const char* c = word; *c; c++) {
        char str[2] = {*c, 0};
        SDL_Surface* r = TTF_RenderUTF8_Blended(font, str, SDL_Color{255, 255, 255, 255});
        if (!r) continue;
        Glyph g;
        g.s = SDL_ConvertSurfaceFormat(r, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(r);
        int minx, maxx, miny, maxy;
        if (TTF_GlyphMetrics32(font, Uint32(Uint8(*c)), &minx, &maxx, &miny, &maxy, &g.adv) != 0) g.adv = g.s->w;
        // ink extents per row (-1 = empty row)
        Uint32* p = static_cast<Uint32*>(g.s->pixels);
        g.left.assign(size_t(g.s->h), -1);
        g.right.assign(size_t(g.s->h), -1);
        for (int y = 0; y < g.s->h; y++)
            for (int x = 0; x < g.s->w; x++)
                if (((p[y * (g.s->pitch / 4) + x] >> 24) & 255) > 100) {
                    if (g.left[size_t(y)] < 0) g.left[size_t(y)] = x;
                    g.right[size_t(y)] = x;
                }
        gl.push_back(g);
    }
    if (gl.empty()) return nullptr;
    const size_t n = gl.size();
    std::vector<int> pos(n, 0);
    for (size_t i = 1; i < n; i++) pos[i] = pos[i - 1] + gl[i - 1].adv;
    // closest distance between neighbouring letters
    std::vector<float> gap(n > 1 ? n - 1 : 0, 0.f);
    for (size_t i = 0; i + 1 < n; i++) {
        float g = 1e9f;
        int h = std::min(gl[i].s->h, gl[i + 1].s->h);
        for (int y = 0; y < h; y++) {
            if (gl[i].right[size_t(y)] < 0 || gl[i + 1].left[size_t(y)] < 0) continue;
            g = std::min(g, float(pos[i + 1] + gl[i + 1].left[size_t(y)] - pos[i] - gl[i].right[size_t(y)]));
        }
        gap[i] = g < 1e8f ? g : 0.f;
    }
    std::vector<float> sorted = gap;
    std::sort(sorted.begin(), sorted.end());
    float ref = sorted.empty() ? 0.f : sorted[sorted.size() / 2];   // typical gap of this word
    int shift = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        if (gap[i] > ref * 1.3f) shift += int(std::lround((ref + 0.3f * (gap[i] - ref)) - gap[i]));
        pos[i + 1] += shift;
    }
    int W = 0, H = 0;
    for (size_t i = 0; i < n; i++) {
        W = std::max(W, pos[i] + gl[i].s->w);
        H = std::max(H, gl[i].s->h);
    }
    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32* dst = static_cast<Uint32*>(out->pixels);
    for (int i = 0; i < W * H; i++) dst[i] = 0x00FFFFFF;
    for (size_t i = 0; i < n; i++) {
        Uint32* p = static_cast<Uint32*>(gl[i].s->pixels);
        for (int y = 0; y < gl[i].s->h; y++)
            for (int x = 0; x < gl[i].s->w; x++) {
                int X = pos[i] + x;
                if (X < 0 || X >= W) continue;
                Uint32 a = p[y * (gl[i].s->pitch / 4) + x] >> 24;
                Uint32& d = dst[y * (out->pitch / 4) + X];
                if (a > (d >> 24)) d = (a << 24) | 0xFFFFFF;
            }
        SDL_FreeSurface(gl[i].s);
    }
    return out;
}

void Renderer::makeLogo() {
    SDL_Surface* sa = renderSpacedWord(fonts[FONT_HUGE], "BREAKOUT");
    SDL_Surface* sb = renderSpacedWord(fonts[FONT_HUGE], "MAYHEM");
    if (!sa || !sb) return;
    const int pad = 8, gap = -26;
    int W = std::max(sa->w, sb->w) + 2 * pad;
    int H = sa->h + sb->h + gap + 2 * pad;
    std::vector<float> alpha(size_t(W * H), 0.f), grad(size_t(W * H), 0.f);
    auto put = [&](SDL_Surface* s, int ox, int oy) {
        Uint32* p = static_cast<Uint32*>(s->pixels);
        for (int y = 0; y < s->h; y++)
            for (int x = 0; x < s->w; x++) {
                float al = ((p[y * (s->pitch / 4) + x] >> 24) & 255) / 255.f;
                int X = ox + x, Y = oy + y;
                if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                if (al > alpha[Y * W + X]) {
                    alpha[Y * W + X] = al;
                    grad[Y * W + X] = float(y) / s->h;
                }
            }
    };
    put(sa, (W - sa->w) / 2, pad);
    put(sb, (W - sb->w) / 2, pad + sa->h + gap);
    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32* px = static_cast<Uint32*>(out->pixels);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float outline = 0;
            for (int dy = -4; dy <= 4; dy++)
                for (int dx = -4; dx <= 4; dx++) {
                    if (dx * dx + dy * dy > 18) continue;
                    int X = x + dx, Y = y + dy;
                    if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                    outline = std::max(outline, alpha[Y * W + X]);
                }
            float fill = alpha[y * W + x];
            float t = grad[y * W + x];
            RGBf c = t < 0.5f ? mix(rgb(0xFFF59A), rgb(0xFFA21E), t / 0.5f) : mix(rgb(0xFFA21E), rgb(0xD8261C), (t - 0.5f) / 0.5f);
            if (t > 0.28f && t < 0.33f) c = mix(c, rgb(0xFFFFFF), 0.45f);
            RGBf dark = rgb(0x1A0E10);
            RGBf o = mix(dark, c, fill);
            o.a = 1;
            px[y * (out->pitch / 4) + x] = pack(o, std::max(outline, fill));
        }
    logo = SDL_CreateTextureFromSurface(R, out);
    SDL_SetTextureBlendMode(logo, SDL_BLENDMODE_BLEND);
    logoW = W;
    logoH = H;
    SDL_FreeSurface(out);
    SDL_FreeSurface(sa);
    SDL_FreeSurface(sb);
}

// ------------------------------------------------------------------------------------------
// primitives

void Renderer::quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, SDL_Color cl) {
    SDL_Vertex v[4];
    Vec2 p[4] = {a, b, c, d};
    for (int i = 0; i < 4; i++) {
        v[i].position = SDL_FPoint{p[i].x, p[i].y};
        v[i].color = cl;
        v[i].tex_coord = SDL_FPoint{0, 0};
    }
    const int ind[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(R, nullptr, v, 4, ind, 6);
}

void Renderer::line(Vec2 a, Vec2 b, float th, SDL_Color cl) {
    Vec2 d = (b - a).norm().perp() * (th * 0.5f);
    quad(a + d, b + d, b - d, a - d, cl);
}

void Renderer::ring(Vec2 c, float r, float th, SDL_Color cl, int segs) {
    verts.clear();
    idx.clear();
    for (int i = 0; i <= segs; i++) {
        float a = 2 * PI_F * i / segs;
        Vec2 d(std::cos(a), std::sin(a));
        Vec2 p0 = c + d * (r - th * 0.5f), p1 = c + d * (r + th * 0.5f);
        verts.push_back(SDL_Vertex{SDL_FPoint{p0.x, p0.y}, cl, SDL_FPoint{0, 0}});
        verts.push_back(SDL_Vertex{SDL_FPoint{p1.x, p1.y}, cl, SDL_FPoint{0, 0}});
        if (i < segs) {
            int k = i * 2;
            idx.insert(idx.end(), {k, k + 1, k + 3, k, k + 3, k + 2});
        }
    }
    SDL_RenderGeometry(R, nullptr, verts.data(), int(verts.size()), idx.data(), int(idx.size()));
}

void Renderer::disc(Vec2 c, float r, SDL_Color cl, int segs) {
    verts.clear();
    idx.clear();
    verts.push_back(SDL_Vertex{SDL_FPoint{c.x, c.y}, cl, SDL_FPoint{0, 0}});
    for (int i = 0; i <= segs; i++) {
        float a = 2 * PI_F * i / segs;
        verts.push_back(SDL_Vertex{SDL_FPoint{c.x + std::cos(a) * r, c.y + std::sin(a) * r}, cl, SDL_FPoint{0, 0}});
        if (i < segs) idx.insert(idx.end(), {0, i + 1, i + 2});
    }
    SDL_RenderGeometry(R, nullptr, verts.data(), int(verts.size()), idx.data(), int(idx.size()));
}

void Renderer::rect(float x, float y, float w, float h, SDL_Color cl) {
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, cl.r, cl.g, cl.b, cl.a);
    SDL_FRect r{x, y, w, h};
    SDL_RenderFillRectF(R, &r);
}

void Renderer::glowAt(Vec2 p, float size, SDL_Color cl) {
    SDL_SetTextureColorMod(glow, cl.r, cl.g, cl.b);
    SDL_SetTextureAlphaMod(glow, cl.a);
    SDL_FRect d{p.x - size, p.y - size, size * 2, size * 2};
    SDL_RenderCopyF(R, glow, nullptr, &d);
}

void Renderer::blit(Tex& t, float cx, float cy, float w, float h, float angle, Uint8 alpha, bool shadow, bool additive) {
    if (!t.t) return;
    if (shadow) {
        SDL_SetTextureColorMod(t.t, 0, 0, 0);
        SDL_SetTextureAlphaMod(t.t, Uint8(alpha * 0.36f));
    } else {
        SDL_SetTextureColorMod(t.t, 255, 255, 255);
        SDL_SetTextureAlphaMod(t.t, alpha);
    }
    if (additive) SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_ADD);
    SDL_FRect d{cx - w * 0.5f, cy - h * 0.5f, w, h};
    SDL_RenderCopyExF(R, t.t, nullptr, &d, angle, nullptr, SDL_FLIP_NONE);
    if (additive) SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(t.t, 255, 255, 255);
    SDL_SetTextureAlphaMod(t.t, 255);
}

void Renderer::text(const std::string& s, FontSize f, float x, float y, uint32_t color, Align a, bool shadow, Uint8 alpha) {
    if (s.empty()) return;
    std::string key = std::to_string(int(f)) + "|" + s;
    auto it = textCache.find(key);
    if (it == textCache.end()) {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(fonts[f], s.c_str(), SDL_Color{255, 255, 255, 255});
        if (!surf) return;
        Tex t;
        t.t = SDL_CreateTextureFromSurface(R, surf);
        t.w = surf->w;
        t.h = surf->h;
        SDL_FreeSurface(surf);
        SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
        it = textCache.emplace(key, t).first;
    }
    Tex& t = it->second;
    t.used = frame;
    float dx = a == ALIGN_LEFT ? x : (a == ALIGN_CENTER ? x - t.w * 0.5f : x - t.w);
    if (shadow) {
        float so = f >= FONT_LARGE ? 3.f : 2.f;
        SDL_SetTextureColorMod(t.t, 0, 0, 0);
        SDL_SetTextureAlphaMod(t.t, Uint8(alpha * 0.6f));
        SDL_FRect d{dx + so, y + so, float(t.w), float(t.h)};
        SDL_RenderCopyF(R, t.t, nullptr, &d);
    }
    SDL_SetTextureColorMod(t.t, Uint8(color >> 16), Uint8(color >> 8), Uint8(color));
    SDL_SetTextureAlphaMod(t.t, alpha);
    SDL_FRect d{dx, y, float(t.w), float(t.h)};
    SDL_RenderCopyF(R, t.t, nullptr, &d);
}

int Renderer::textWidth(const std::string& s, FontSize f) {
    int w = 0, h = 0;
    TTF_SizeUTF8(fonts[f], s.c_str(), &w, &h);
    return w;
}

// ------------------------------------------------------------------------------------------
// scene

void Renderer::draw(Game& g) {
    frame++;
    time = g.realTime;
    if (bgLevel != g.levelChanged) {
        makeBackground(g.info.hue);
        bgLevel = g.levelChanged;
    }
    off = Vec2(0, 0);
    if (g.shake > 0) {
        float s = g.shake * 26;
        off = Vec2(std::sin(time * 91.f) * s, std::cos(time * 77.f) * s);
    }
    SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
    SDL_RenderClear(R);
    SDL_RenderCopy(R, bg, nullptr, nullptr);
    drawWorld(g);
    drawDarkness(g);
    drawTexts(g);
    drawFrame();
    if (g.state != State::Title) drawHud(g);
    drawOverlay(g);

    // purge stale text textures (scores change constantly)
    if (frame % 300 == 0) {
        for (auto it = textCache.begin(); it != textCache.end();) {
            if (frame - it->second.used > 300) {
                SDL_DestroyTexture(it->second.t);
                it = textCache.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void Renderer::drawFields(Game& g) {
    const World& w = g.world;
    for (const auto& f : w.fields) {
        switch (f.type) {
            case FieldType::Wind: {
                rect(f.pos.x + off.x, f.pos.y + off.y, f.size.x, f.size.y, SDL_Color{20, 50, 90, 34});
                Vec2 dir = f.accel.norm();
                float sp = 260;
                for (int k = 0; k < 34; k++) {
                    float fx = std::fmod(hash2(k, 7) * 5000.f + time * sp * (0.7f + hash2(k, 3)), f.size.x);
                    if (dir.x < 0) fx = f.size.x - fx;
                    float fy = 8 + hash2(k, 11) * (f.size.y - 16);
                    Vec2 p(f.pos.x + fx, f.pos.y + fy);
                    float len = 30 + 30 * hash2(k, 5);
                    Vec2 a = p - dir * len * 0.5f, b = p + dir * len * 0.5f;
                    a.x = clampf(a.x, f.pos.x, f.pos.x + f.size.x);
                    b.x = clampf(b.x, f.pos.x, f.pos.x + f.size.x);
                    line(a + off + Vec2(1.5f, 1.5f), b + off + Vec2(1.5f, 1.5f), 3.f, SDL_Color{20, 40, 70, 70});
                    line(a + off, b + off, 3.f, SDL_Color{255, 255, 255, 200});
                }
                // arrow heads at both ends of the band
                for (int k = 0; k < 2; k++) {
                    Vec2 c(f.pos.x + (k ? f.size.x - 40 : 40), f.pos.y + f.size.y * 0.5f);
                    Vec2 n = dir.perp();
                    Vec2 tip = c + dir * 14;
                    quad(tip + off, c - dir * 6 + n * 12 + off, c + off, c - dir * 6 - n * 12 + off, SDL_Color{255, 255, 255, 210});
                }
                break;
            }
            case FieldType::Goo: {
                rect(f.pos.x + off.x, f.pos.y + 6 + off.y, f.size.x, f.size.y - 6, SDL_Color{70, 140, 40, 90});
                for (int x = 0; x < int(f.size.x); x += 16) {
                    float y0 = f.pos.y + 6 + 4 * std::sin(x * 0.03f + time * 2.f);
                    float y1 = f.pos.y + 6 + 4 * std::sin((x + 16) * 0.03f + time * 2.f);
                    line(Vec2(f.pos.x + x, y0) + off, Vec2(f.pos.x + x + 16, y1) + off, 3, SDL_Color{150, 220, 90, 170});
                }
                for (int k = 0; k < 24; k++) {
                    float bx = f.pos.x + hash2(k, 1) * f.size.x;
                    float ph = std::fmod(time * (0.15f + 0.2f * hash2(k, 2)) + hash2(k, 4), 1.f);
                    float by = f.pos.y + f.size.y - ph * (f.size.y - 10);
                    ring(Vec2(bx, by) + off, 3 + 4 * hash2(k, 6), 1.5f, SDL_Color{190, 255, 140, Uint8(140 * (1 - ph))}, 12);
                }
                break;
            }
            case FieldType::Well: {
                glowAt(f.pos + off, 170, SDL_Color{120, 60, 200, 90}); // no rings: its reach stays hidden
                break;
            }
            case FieldType::Repulsor: {
                glowAt(f.pos + off, f.radius * 0.7f, SDL_Color{60, 200, 255, 70});
                for (int k = 0; k < 4; k++) {
                    float ph = std::fmod(k / 4.f + time * 0.5f, 1.f);
                    ring(f.pos + off, 12 + ph * (f.radius - 12), 2.5f, SDL_Color{120, 230, 255, Uint8(170 * (1 - ph))});
                }
                break;
            }
        }
    }
    for (const auto& h : w.holes) {
        // the suction radius is deliberately not drawn
        Tex& t = hole(int(h.r));
        blit(t, h.pos.x + off.x, h.pos.y + off.y, float(t.w), float(t.h));
    }
    const uint32_t pc[2] = {0xB070FF, 0x40E0A0};
    for (size_t i = 0; i < w.portals.size(); i++) {
        const Portal& p = w.portals[i];
        for (int s = 0; s < 2; s++) {
            Vec2 c = (s ? p.b : p.a) + off;
            uint32_t cc = pc[(i + s) % 2];
            disc(c, p.r, col(0x10081C, 220));
            glowAt(c, p.r * 1.6f, col(cc, 160));
            for (int k = 0; k < 3; k++) {
                float a0 = time * (s ? -3.f : 3.f) + k * 2.094f;
                for (int j = 0; j < 6; j++) {
                    float a = a0 + j * 0.12f;
                    float rr = p.r * (0.95f - j * 0.08f);
                    Vec2 q = c + Vec2(std::cos(a), std::sin(a)) * rr;
                    disc(q, 4.5f - j * 0.5f, col(cc, 230), 10);
                }
            }
            ring(c, p.r, 3, col(cc, 255));
        }
    }
}

void Renderer::drawBat(const Body& b, float scale, Uint8 alpha, bool shadow) {
    Tex& t = capsule(int(2 * BAT_HW), int(BAT_R), playerColor(b.color), 0);
    if (!t.t) return;
    float w = 2 * (b.hw + b.r) * scale, h = t.h * scale;
    Vec2 p = b.pos + off + (shadow ? Vec2(12, 12) : Vec2(0, 0));
    float x0 = p.x - w * 0.5f, y0 = p.y - h * 0.5f;
    int cap = int(BAT_R) + 1;   // the round ends are drawn unstretched
    if (shadow) {
        SDL_SetTextureColorMod(t.t, 0, 0, 0);
        SDL_SetTextureAlphaMod(t.t, Uint8(alpha * 0.36f));
    } else {
        SDL_SetTextureColorMod(t.t, 255, 255, 255);
        SDL_SetTextureAlphaMod(t.t, alpha);
    }
    SDL_Rect sl{0, 0, cap, t.h}, sm{cap, 0, t.w - 2 * cap, t.h}, sr{t.w - cap, 0, cap, t.h};
    float cw = cap * scale;
    SDL_FRect dl{x0, y0, cw, h}, dm{x0 + cw, y0, std::max(0.f, w - 2 * cw), h}, dr{x0 + w - cw, y0, cw, h};
    SDL_RenderCopyF(R, t.t, &sl, &dl);
    SDL_RenderCopyF(R, t.t, &sm, &dm);
    SDL_RenderCopyF(R, t.t, &sr, &dr);
    SDL_SetTextureColorMod(t.t, 255, 255, 255);
    SDL_SetTextureAlphaMod(t.t, 255);
    if (!shadow && b.flash > 0) {
        SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(t.t, Uint8(std::min(1.f, b.flash / 0.15f) * 140));
        SDL_RenderCopyF(R, t.t, &sl, &dl);
        SDL_RenderCopyF(R, t.t, &sm, &dm);
        SDL_RenderCopyF(R, t.t, &sr, &dr);
        SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(t.t, 255);
    }
}

// Fake height for things that can fall into holes: inside a hole's pull the shadow moves in under the object,
// so it looks like it is being sucked down towards the hole. 1 = normal height, 0.1 = right at the hole.
static float heightAboveHoles(const World& w, const Body& b) {
    bool sinkable = b.kind == Kind::Ball || b.kind == Kind::Pill || (b.kind == Kind::Brick && b.btype == BrickType::Puck);
    if (!sinkable) return 1.f;
    float h = 1.f;
    for (const auto& hole : w.holes) {
        float d = (b.pos - hole.pos).len();
        if (d >= hole.suctionR) continue;
        float inner = hole.r * 0.5f;
        float t = clampf((d - inner) / std::max(1.f, hole.suctionR - inner), 0.f, 1.f);
        t = t * t * (3 - 2 * t);   // smooth
        h = std::min(h, 0.1f + 0.9f * t);
    }
    return h;
}

void Renderer::drawBody(Game& g, const Body& b, float scale, Uint8 alpha, bool shadow) {
    Vec2 p = b.pos + off + (shadow ? Vec2(12, 12) * heightAboveHoles(g.world, b) : Vec2(0, 0));
    switch (b.kind) {
        case Kind::Ball: {
            uint32_t c = b.mega ? 0xFF5A2A : (b.invMass < 20 ? 0x6E7078 : 0xD8DCE6);
            Tex& t = sphere(int(b.r), c, 0);
            if (!shadow && b.mega) glowAt(p, b.r * 3, SDL_Color{255, 90, 30, Uint8(alpha * 0.7f)});
            blit(t, p.x, p.y, t.w * scale, t.h * scale, 0, alpha, shadow);
            break;
        }
        case Kind::Player: {
            if (!shadow && b.magnet > 0) {
                float pulse = 0.5f + 0.5f * std::sin(time * 8);
                glowAt(p, 120 + 20 * pulse, SDL_Color{80, 160, 255, Uint8(60 + 40 * pulse)});
            }
            if (b.stun > 0 && std::fmod(time, 0.2f) < 0.1f) alpha = Uint8(alpha * 0.35f);
            if (!shadow && g.tGun > 0) {
                SDL_Color gun{170, 175, 185, alpha};
                if (b.shape == Shape::Capsule) {
                    rect(p.x - b.hw - 3, p.y - b.r - 7, 6, 9, gun);
                    rect(p.x + b.hw - 3, p.y - b.r - 7, 6, 9, gun);
                } else {
                    rect(p.x - 3, p.y - b.r - 7, 6, 9, gun);
                }
            }
            if (b.shape == Shape::Capsule) {
                drawBat(b, scale, alpha, shadow);
            } else {
                Tex& t = sphere(30, playerColor(b.color), 6);
                float s = b.r / 30.f * scale;
                blit(t, p.x, p.y, t.w * s, t.h * s, 0, alpha, shadow);
                if (!shadow && b.flash > 0)
                    blit(t, p.x, p.y, t.w * s, t.h * s, 0, Uint8(std::min(1.f, b.flash / 0.15f) * 140), false, true);
            }
            break;
        }
        case Kind::Enemy: {
            Tex& t = sphere(int(b.r), 0x58D058, 7);
            blit(t, p.x, p.y, t.w * scale, t.h * scale, 0, alpha, shadow);
            if (shadow) break;
            Vec2 look = b.vel.len() > 1 ? b.vel.norm() : Vec2(0, 1);
            for (int e = -1; e <= 1; e += 2) {
                Vec2 ep = p + Vec2(e * b.r * 0.38f, -b.r * 0.2f) * scale;
                disc(ep, b.r * 0.3f * scale, SDL_Color{255, 255, 255, alpha}, 12);
                disc(ep + look * (b.r * 0.12f * scale), b.r * 0.15f * scale, SDL_Color{10, 10, 10, alpha}, 10);
            }
            break;
        }
        case Kind::Pill: {
            bool blink = b.life < 3 && std::fmod(b.life, 0.3f) < 0.12f;
            if (blink && !shadow) alpha = Uint8(alpha * 0.35f);
            Tex& t = capsule(44, 11, pillColor(b.pill), 0);
            blit(t, p.x, p.y, t.w * scale, t.h * scale, 0, alpha, shadow);
            if (!shadow) text(pillLabel(b.pill), FONT_SMALL, p.x, p.y - 15, 0xFFFFFF, ALIGN_CENTER, true, alpha);
            break;
        }
        case Kind::Brick: {
            uint32_t c = brickColor(b.color);
            Tex* t = nullptr;
            if (b.shape == Shape::Capsule) {
                Tex& ct = capsule(int(2 * (b.hw + b.r)), int(b.r), 0xA8B0BC, 1);
                blit(ct, p.x, p.y, ct.w * scale, ct.h * scale, b.angle * 180.f / PI_F, alpha, shadow);
                if (b.kinematic) {
                    Tex& hub = sphere(int(b.r * 1.5f), 0x707884, 1);
                    blit(hub, p.x, p.y, hub.w * scale, hub.h * scale, 0, alpha, shadow);
                }
                break;
            }
            if (b.btype == BrickType::Barrier) {
                SDL_Color bc = col(groupColor(b.group));
                float x0 = p.x - b.hw, y0 = p.y - b.hh, w = 2 * b.hw, h = 2 * b.hh;
                if (shadow) {
                    if (b.solid) rect(x0, y0, w, h, SDL_Color{0, 0, 0, Uint8(alpha * 0.25f)});
                    break;
                }
                if (b.solid) {
                    float fl = 0.75f + 0.25f * std::sin(time * 23.f + p.x * 0.05f);
                    rect(x0, y0, w, h, SDL_Color{bc.r, bc.g, bc.b, Uint8(150 * fl)});
                    rect(x0 + 2, y0 + 2, w - 4, h - 4, SDL_Color{255, 255, 255, Uint8(90 * fl)});
                    // soft halo along the wall
                    rect(x0 - 3, y0 - 3, w + 6, h + 6, SDL_Color{bc.r, bc.g, bc.b, Uint8(45 * fl)});
                } else {
                    // switched off: dashed outline only
                    bool horiz = w > h;
                    float len = horiz ? w : h;
                    for (float d = 0; d < len; d += 16) {
                        float seg = std::min(8.f, len - d);
                        if (horiz) {
                            rect(x0 + d, y0, seg, 2, SDL_Color{bc.r, bc.g, bc.b, 130});
                            rect(x0 + d, y0 + h - 2, seg, 2, SDL_Color{bc.r, bc.g, bc.b, 130});
                        } else {
                            rect(x0, y0 + d, 2, seg, SDL_Color{bc.r, bc.g, bc.b, 130});
                            rect(x0 + w - 2, y0 + d, 2, seg, SDL_Color{bc.r, bc.g, bc.b, 130});
                        }
                    }
                }
                break;
            }
            if (b.shape == Shape::Circle) {
                int style = 1;
                if (b.btype == BrickType::Bumper) style = 4;
                else if (b.btype == BrickType::Puck) style = 5;
                else if (b.btype == BrickType::Bulb) style = 0;
                else style = b.maxHp >= 3 ? 3 : (b.maxHp == 2 ? 2 : 1);
                if (b.btype == BrickType::Bulb) {
                    c = 0xFFF0B0;
                    if (!shadow) {
                        float fl = 0.85f + 0.15f * std::sin(time * 7.f + b.id);
                        glowAt(p, b.r * 3.2f, SDL_Color{255, 230, 140, Uint8(alpha * 0.55f * fl)});
                    }
                    rect(p.x - b.r * 0.45f, p.y + b.r * 0.75f, b.r * 0.9f, b.r * 0.5f,
                         shadow ? SDL_Color{0, 0, 0, Uint8(alpha * 0.3f)} : SDL_Color{150, 150, 160, alpha});
                }
                t = &sphere(int(b.r), c, style);
            } else {
                int style = b.maxHp >= 3 ? 2 : (b.maxHp == 2 ? 1 : 0);
                switch (b.btype) {
                    case BrickType::Metal: style = 3; break;
                    case BrickType::Explosive: style = 4; break;
                    case BrickType::Multi: style = 5; break;
                    case BrickType::GravSwitch: style = 6; break;
                    case BrickType::Conveyor: style = 7; break;
                    case BrickType::Skull: style = 8; break;
                    case BrickType::Magnet: style = 9; break;
                    case BrickType::Nest: style = 10; break;
                    case BrickType::BigBall: style = 12; break;
                    case BrickType::Switch: style = 3; break;
                    default: break;
                }
                t = &brick(int(2 * b.hw), int(2 * b.hh), c, style);
            }
            blit(*t, p.x, p.y, t->w * scale, t->h * scale, 0, alpha, shadow);
            if (shadow) break;
            if (b.btype == BrickType::Conveyor) {
                // moving chevrons
                float s = b.surfaceSpeed;
                float ph = std::fmod(std::fabs(s) * time, 28.f);
                SDL_Rect clip{int(p.x - b.hw + 3), int(p.y - b.hh), int(2 * b.hw - 6), int(2 * b.hh)};
                SDL_RenderSetClipRect(R, &clip);
                float dir = s > 0 ? 1.f : -1.f;
                for (float x = -28; x < 2 * b.hw + 28; x += 28) {
                    float cx = p.x - b.hw + x + dir * ph;
                    Vec2 a(cx - dir * 5, p.y - b.hh * 0.55f), m(cx + dir * 5, p.y), e(cx - dir * 5, p.y + b.hh * 0.55f);
                    line(a, m, 3, SDL_Color{230, 200, 60, 220});
                    line(m, e, 3, SDL_Color{230, 200, 60, 220});
                }
                SDL_RenderSetClipRect(R, nullptr);
            }
            if (b.btype == BrickType::Switch) {
                // lamp shows whether this switch's energy walls are on
                bool on = false;
                for (const auto& o : g.world.bodies)
                    if (o.btype == BrickType::Barrier && o.group == b.group) on = o.solid;
                SDL_Color gc = col(groupColor(b.group));
                disc(p, b.hw * 0.48f, SDL_Color{20, 20, 24, 255}, 20);
                disc(p, b.hw * 0.36f, on ? gc : SDL_Color{Uint8(gc.r / 4), Uint8(gc.g / 4), Uint8(gc.b / 4), 255}, 20);
                if (on) glowAt(p, b.hw * 1.3f, SDL_Color{gc.r, gc.g, gc.b, 120});
            }
            if (b.btype == BrickType::GravSwitch) {
                Vec2 gd = g.world.gravity.len() > 1 ? g.world.gravity.norm() : Vec2(0, 1);
                Vec2 n = gd.perp();
                float s = b.hw * 0.7f;
                Vec2 tip = p + gd * s, tail = p - gd * s;
                line(tail, p + gd * (s * 0.2f), 5, SDL_Color{255, 255, 255, 230});
                quad(tip, p + gd * (s * 0.1f) + n * (s * 0.55f), p + gd * (s * 0.3f), p + gd * (s * 0.1f) - n * (s * 0.55f),
                     SDL_Color{255, 255, 255, 230});
            }
            // cracks on damaged multi-hit bricks
            if (b.maxHp > 1 && b.hp < b.maxHp && b.btype == BrickType::Normal) {
                int cracks = (b.maxHp - b.hp) * 2;
                float ext = b.shape == Shape::Circle ? b.r * 0.85f : std::min(b.hw, b.hh * 3.f);
                for (int k = 0; k < cracks; k++) {
                    Vec2 q = p;
                    float a = hash2(b.id, k) * 2 * PI_F;
                    for (int s = 0; s < 4; s++) {
                        a += (hash2(b.id * 7 + k, s) - 0.5f) * 1.2f;
                        Vec2 nq = q + Vec2(std::cos(a), std::sin(a)) * (ext * 0.28f);
                        if (b.shape == Shape::Rect) {
                            nq.x = clampf(nq.x, p.x - b.hw + 3, p.x + b.hw - 3);
                            nq.y = clampf(nq.y, p.y - b.hh + 3, p.y + b.hh - 3);
                        }
                        line(q, nq, 2, SDL_Color{20, 10, 10, 170});
                        q = nq;
                    }
                }
            }
            if (b.flash > 0) {
                Uint8 fa = Uint8(std::min(1.f, b.flash / 0.15f) * (b.btype == BrickType::Bumper ? 200 : 120));
                blit(*t, p.x, p.y, t->w * scale, t->h * scale, 0, fa, false, true);
                if (b.btype == BrickType::Bumper) glowAt(p, b.r * 2.5f, SDL_Color{255, 220, 120, fa});
            }
            break;
        }
    }
}

void Renderer::drawWorld(Game& g) {
    drawFields(g);
    World& w = g.world;
    // springs of pendulum bricks
    for (const auto& b : w.bodies) {
        if (!b.spring) continue;
        Vec2 a = b.anchor + off, e = b.pos - Vec2(0, b.hh) + off;
        Vec2 d = e - a;
        Vec2 n = d.norm().perp();
        Vec2 prev = a;
        const int N = 16;
        for (int i = 1; i <= N; i++) {
            float t = float(i) / N;
            float z = (i == N) ? 0.f : ((i % 2) ? 7.f : -7.f);
            Vec2 q = a + d * t + n * z;
            line(prev, q, 2.5f, SDL_Color{60, 60, 70, 230});
            prev = q;
        }
        rect(a.x - 12, a.y, 24, 6, col(0x50545C));
    }
    // rubber bands
    for (const auto& band : w.bands) {
        const Body* a = nullptr;
        const Body* c = nullptr;
        for (const auto& b : w.bodies) {
            if (b.id == band.a) a = &b;
            if (b.id == band.b) c = &b;
        }
        if (!a || !c) continue;
        float L = (c->pos - a->pos).len();
        float stretch = std::max(0.f, L - band.rest) / band.rest;
        float th = clampf(4.5f - 3.f * stretch, 1.5f, 4.5f);
        line(a->pos + off + Vec2(5, 5), c->pos + off + Vec2(5, 5), th, SDL_Color{0, 0, 0, 50});
        line(a->pos + off, c->pos + off, th, SDL_Color{Uint8(190 + std::min(60.f, 60 * stretch)), 60, 45, 235});
    }
    // shadows
    for (const auto& b : w.bodies) drawBody(g, b, 1, 255, true);
    for (const auto& gh : g.ghosts) {
        float t = gh.t / gh.dur;
        Body bb = gh.body;
        if (gh.mode == 0) {
            drawBody(g, bb, 1 + 0.4f * t, Uint8(255 * (1 - t)), true);
        } else {   // sinking into a hole: the shadow follows it in
            bb.pos = gh.body.pos + (gh.target - gh.body.pos) * std::min(1.f, t * 1.6f);
            drawBody(g, bb, 1 - 0.85f * t, Uint8(255 * (1 - t * t)), true);
        }
    }
    // bodies: bricks first, then pills, players, balls
    const Kind order[5] = {Kind::Brick, Kind::Pill, Kind::Enemy, Kind::Player, Kind::Ball};
    for (Kind k : order)
        for (const auto& b : w.bodies)
            if (b.kind == k) drawBody(g, b, 1, 255, false);
    for (const auto& gh : g.ghosts) {
        float t = gh.t / gh.dur;
        Body bb = gh.body;
        if (gh.mode == 1) {
            bb.pos = gh.body.pos + (gh.target - gh.body.pos) * std::min(1.f, t * 1.6f);
            drawBody(g, bb, 1 - 0.85f * t, Uint8(255 * (1 - t * t)), false);
        } else {
            drawBody(g, bb, 1 + 0.4f * t, Uint8(255 * (1 - t)), false);
        }
    }
    // particles
    for (const auto& p : g.particles) {
        float a = p.life / p.maxLife;
        float s = p.size * (0.4f + 0.6f * a);
        rect(p.pos.x + off.x - s * 0.5f, p.pos.y + off.y - s * 0.5f, s, s, SDL_Color{p.r, p.g, p.b, Uint8(255 * a)});
    }
    // laser bolts
    for (const auto& sh : g.shots) {
        Vec2 p = sh.pos + off;
        line(p + Vec2(2, 2), p + Vec2(2, 20), 6, SDL_Color{0, 0, 0, 70});
        line(p, p + Vec2(0, 18), 6, SDL_Color{240, 50, 30, 255});
        line(p + Vec2(0, 2), p + Vec2(0, 16), 2, SDL_Color{255, 255, 220, 255});
    }
}

// Light bulb levels: the scene is multiplied by a light map that gets darker as bulbs break.
// Balls, bats, enemies and the remaining bulbs carry their own light.
void Renderer::drawDarkness(Game& g) {
    if (g.darkness < 0.01f) return;
    if (!lightMap) {
        lightMap = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
        if (!lightMap) return;
        SDL_SetTextureBlendMode(lightMap, SDL_BLENDMODE_MOD);
    }
    SDL_Texture* prev = SDL_GetRenderTarget(R);
    SDL_SetRenderTarget(R, lightMap);
    Uint8 amb = Uint8(255 * (1 - g.darkness));
    SDL_SetRenderDrawColor(R, amb, amb, amb, 255);
    SDL_RenderClear(R);
    for (const auto& b : g.world.bodies) {
        Vec2 p = b.pos + off;
        if (b.kind == Kind::Ball) glowAt(p, 150, SDL_Color{255, 240, 210, 255});
        else if (b.kind == Kind::Player) glowAt(p, 120, SDL_Color{200, 215, 255, 220});
        else if (b.kind == Kind::Enemy) glowAt(p, 60, SDL_Color{150, 255, 150, 200});
        else if (b.btype == BrickType::Bulb) glowAt(p, 170, SDL_Color{255, 235, 170, 255});
    }
    for (const auto& sh : g.shots) glowAt(sh.pos + off, 40, SDL_Color{255, 240, 150, 200});
    SDL_SetRenderTarget(R, prev);
    SDL_RenderCopy(R, lightMap, nullptr, nullptr);
}

void Renderer::drawTexts(Game& g) {
    for (const auto& t : g.texts) {
        float a = 1 - std::max(0.f, (t.t / t.dur - 0.6f) / 0.4f);
        text(t.text, t.big ? FONT_MEDIUM : FONT_SMALL, t.pos.x, t.pos.y - 14, t.color, ALIGN_CENTER, true, Uint8(255 * a));
    }
}

void Renderer::drawFrame() {
    SDL_Color frameC = col(0x2C303A), hi = col(0x8A92A4), lo = col(0x14161C);
    rect(0, 40, 10, 680, frameC);
    rect(1270, 40, 10, 680, frameC);
    rect(0, 710, 1280, 10, frameC);
    rect(9, 49, 1, 662, lo);
    rect(1270, 49, 1, 662, hi);
    rect(9, 710, 1262, 1, hi);
    rect(0, 0, 1280, 50, col(0x1A1D26));
    rect(0, 0, 1280, 22, col(0x232733));
    rect(0, 48, 1280, 2, hi);
}

void Renderer::drawHud(Game& g) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "SCORE  %07d", g.score);
    text(buf, FONT_SMALL, 18, 11, 0xFFFFFF);
    std::snprintf(buf, sizeof buf, "HI  %07d", std::max(g.score, g.settings.hiscore));
    text(buf, FONT_SMALL, 230, 11, 0xB8C0D0);
    std::snprintf(buf, sizeof buf, "LEVEL %d/%d  %s", g.level + 1, levelCount(), g.info.name.c_str());
    text(buf, FONT_SMALL, 610, 11, 0xFFE070, ALIGN_CENTER);

    // power-ups
    float x = 840;
    struct PU { float t; int type; };
    const PU pus[6] = {{g.tEnlarge, PILL_ENLARGE}, {g.tShrink, PILL_SHRINK}, {g.tSlow, PILL_SLOW},
                       {g.tMega, PILL_MEGA},       {g.tMagnet, PILL_MAGNET},   {g.tGun, PILL_GUN}};
    for (const auto& pu : pus) {
        if (pu.t <= 0) continue;
        Tex& t = capsule(34, 9, pillColor(pu.type), 0);
        blit(t, x, 20, float(t.w), float(t.h));
        text(pillLabel(pu.type), FONT_SMALL, x, 6, 0xFFFFFF, ALIGN_CENTER);
        rect(x - 17, 36, 34 * std::min(1.f, pu.t / 20.f), 4, col(pillColor(pu.type)));
        x += 46;
    }

    // gravity indicator
    Vec2 gc(1110, 25);
    float gl = g.world.gravity.len();
    disc(gc, 16, col(0x0C0E14));
    ring(gc, 16, 2, col(0x8A92A4));
    if (gl > 1) {
        Vec2 d = g.world.gravity / gl;
        Vec2 n = d.perp();
        line(gc - d * 9, gc + d * 4, 3, col(0xFFE070));
        quad(gc + d * 12, gc + d * 3 + n * 6, gc + d * 5, gc + d * 3 - n * 6, col(0xFFE070));
    } else {
        text("0", FONT_SMALL, gc.x, gc.y - 14, 0xFFE070, ALIGN_CENTER, false);
    }

    // lives
    Tex& lt = sphere(9, 0xD8DCE6, 0);
    int shown = std::min(g.lives, 5);
    for (int i = 0; i < shown; i++) blit(lt, 1255.f - i * 22, 25, float(lt.w), float(lt.h));
    if (g.lives > 5) {
        std::snprintf(buf, sizeof buf, "%d", g.lives);
        text(buf, FONT_SMALL, 1255.f - 5 * 22, 11, 0xFFFFFF, ALIGN_RIGHT);
    }
}

void Renderer::panel(float x, float y, float w, float h) {
    rect(x + 10, y + 10, w, h, SDL_Color{0, 0, 0, 90});
    rect(x, y, w, h, SDL_Color{16, 20, 36, 225});
    rect(x, y, w, 2, col(0x8A92A4));
    rect(x, y + h - 2, w, 2, col(0x3A4050));
    rect(x, y, 2, h, col(0x8A92A4));
    rect(x + w - 2, y, 2, h, col(0x3A4050));
}

void Renderer::drawMenu(const std::vector<std::string>& items, int sel, float y) {
    for (size_t i = 0; i < items.size(); i++) {
        bool s = int(i) == sel;
        float yy = y + i * 44;
        if (s) {
            float pulse = 0.5f + 0.5f * std::sin(time * 6);
            int w = textWidth(items[i], FONT_MEDIUM);
            rect(640 - w / 2.f - 30, yy + 2, w + 60.f, 38, SDL_Color{255, 200, 60, Uint8(40 + 30 * pulse)});
            text(">", FONT_MEDIUM, 640 - w / 2.f - 22, yy, 0xFFE070, ALIGN_LEFT);
            text("<", FONT_MEDIUM, 640 + w / 2.f + 22, yy, 0xFFE070, ALIGN_RIGHT);
        }
        text(items[i], FONT_MEDIUM, 640, yy, s ? 0xFFE070 : 0xE8ECF4, ALIGN_CENTER);
    }
}

void Renderer::drawOverlay(Game& g) {
    char buf[128];
    bool blinkOn = std::fmod(time, 1.0f) < 0.65f;
    switch (g.state) {
        case State::Title: {
            rect(0, 0, 1280, 720, SDL_Color{0, 0, 10, 120});
            float bob = std::sin(time * 1.6f) * 6;
            if (logo) {
                float s = 1.0f;
                SDL_FRect sh{640 - logoW * s / 2 + 10, 40 + bob + 10, logoW * s, logoH * s};
                SDL_SetTextureColorMod(logo, 0, 0, 0);
                SDL_SetTextureAlphaMod(logo, 110);
                SDL_RenderCopyF(R, logo, nullptr, &sh);
                SDL_SetTextureColorMod(logo, 255, 255, 255);
                SDL_SetTextureAlphaMod(logo, 255);
                SDL_FRect d{640 - logoW * s / 2, 40 + bob, logoW * s, logoH * s};
                SDL_RenderCopyF(R, logo, nullptr, &d);
            }
            std::vector<std::string> items;
            for (int i = 0; i < g.titleItemCount(); i++) items.push_back(g.titleItem(i));
            panel(390, 318, 500, 290);
            drawMenu(items, g.menuIndex, 330);
            std::snprintf(buf, sizeof buf, "HI-SCORE  %07d", g.settings.hiscore);
            text(buf, FONT_SMALL, 640, 618, 0xFFE070, ALIGN_CENTER);
            text(helpLine, FONT_SMALL, 640, 652, 0xD0D8E8, ALIGN_CENTER);
            text("v1.4.0  by rsn8887", FONT_SMALL, 640, 682,
                 0xB8C0D8, ALIGN_CENTER);
            break;
        }
        case State::Intro: {
            panel(290, 220, 700, 270);
            std::snprintf(buf, sizeof buf, "LEVEL %d", g.level + 1);
            text(buf, FONT_LARGE, 640, 236, 0xFFE070, ALIGN_CENTER);
            text(g.info.name, FONT_MEDIUM, 640, 304, 0xFFFFFF, ALIGN_CENTER);
            text(g.info.hint, FONT_SMALL, 640, 356, 0xC8D0E0, ALIGN_CENTER);
            std::snprintf(buf, sizeof buf, "Lives: %d", g.lives);
            text(buf, FONT_SMALL, 640, 392, 0xB8C0D0, ALIGN_CENTER);
            if (blinkOn) text("Click, Space or (A) to start", FONT_SMALL, 640, 436, 0xFFFFFF, ALIGN_CENTER);
            break;
        }
        case State::Paused: {
            panel(440, 230, 400, 230);
            text("PAUSED", FONT_LARGE, 640, 244, 0xFFE070, ALIGN_CENTER);
            std::vector<std::string> items;
            for (int i = 0; i < g.pauseItemCount(); i++) items.push_back(g.pauseItem(i));
            drawMenu(items, g.menuIndex, 318);
            break;
        }
        case State::GameOver: {
            rect(0, 50, 1280, 670, SDL_Color{0, 0, 0, 100});
            panel(290, 220, 700, 260);
            text("GAME OVER", FONT_LARGE, 640, 236, 0xFF5A4A, ALIGN_CENTER);
            std::snprintf(buf, sizeof buf, "Score %d", g.score);
            text(buf, FONT_MEDIUM, 640, 306, 0xFFFFFF, ALIGN_CENTER);
            std::snprintf(buf, sizeof buf, "Click / Space / (A): continue on level %d (score resets)", g.level + 1);
            text(buf, FONT_SMALL, 640, 372, 0xFFE070, ALIGN_CENTER);
            text("Esc / (B): back to title", FONT_SMALL, 640, 410, 0xC8D0E0, ALIGN_CENTER);
            break;
        }
        case State::Victory: {
            panel(290, 190, 700, 330);
            text("CONGRATULATIONS!", FONT_LARGE, 640, 204, 0xFFE070, ALIGN_CENTER);
            if (g.completeRun())
                std::snprintf(buf, sizeof buf, "You beat all %d levels of mayhem!", levelCount());
            else if (g.runFirst / LEVELS_PER_SET == g.runLast / LEVELS_PER_SET)
                std::snprintf(buf, sizeof buf, "Level set %d complete! (levels %d-%d)", g.runFirst / LEVELS_PER_SET + 1,
                              g.runFirst + 1, g.runLast + 1);
            else
                std::snprintf(buf, sizeof buf, "Levels %d-%d complete!", g.runFirst + 1, g.runLast + 1);
            text(buf, FONT_MEDIUM, 640, 270, 0xFFFFFF, ALIGN_CENTER);
            std::snprintf(buf, sizeof buf, "Final score %d", g.score);
            text(buf, FONT_MEDIUM, 640, 314, 0xFFFFFF, ALIGN_CENTER);
            if (!g.completeRun()) text("Now try the next level set!", FONT_MEDIUM, 640, 358, 0xFFE070, ALIGN_CENTER);
            if (g.score >= g.settings.hiscore) text("NEW HI-SCORE!", FONT_MEDIUM, 640, 402, 0xFF80C0, ALIGN_CENTER);
            if (blinkOn) text("Click, Space or (A) to continue", FONT_SMALL, 640, 466, 0xC8D0E0, ALIGN_CENTER);
            break;
        }
        default:
            break;
    }
}
