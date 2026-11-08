// Breakout Mayhem - small 2D vector helper
#pragma once
#include <cmath>
#include <algorithm>

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    float len2() const { return x * x + y * y; }
    float len() const { return std::sqrt(x * x + y * y); }
    Vec2 norm() const { float l = len(); return l > 1e-6f ? Vec2(x / l, y / l) : Vec2(0, 0); }
    Vec2 perp() const { return {-y, x}; }
    Vec2 rotated(float a) const { float c = std::cos(a), s = std::sin(a); return {x * c - y * s, x * s + y * c}; }
};

inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
constexpr float PI_F = 3.14159265358979f;
