// Breakout Mayhem - hand-coded physics
#include "physics.h"
#include <cmath>

namespace {

struct Contact {
    Vec2 n;       // unit normal pointing from A to B
    float depth = 0;
    Vec2 p;       // contact point
};

bool circleCircle(Vec2 pa, float ra, Vec2 pb, float rb, Contact& c) {
    Vec2 d = pb - pa;
    float rs = ra + rb;
    float d2 = d.len2();
    if (d2 >= rs * rs) return false;
    float dist = std::sqrt(d2);
    c.n = dist > 1e-5f ? d / dist : Vec2(0, -1);
    c.depth = rs - dist;
    c.p = pa + c.n * ra;
    return true;
}

// normal from circle to rect
bool circleRect(Vec2 pc, float r, Vec2 pr, float hw, float hh, Contact& c) {
    Vec2 d = pc - pr;
    Vec2 cl(clampf(d.x, -hw, hw), clampf(d.y, -hh, hh));
    bool inside = std::fabs(d.x) < hw && std::fabs(d.y) < hh;
    if (!inside) {
        Vec2 diff = d - cl;
        float d2 = diff.len2();
        if (d2 >= r * r) return false;
        float dist = std::sqrt(d2);
        Vec2 nOut = dist > 1e-5f ? diff / dist : Vec2(0, -1);
        c.n = -nOut;
        c.depth = r - dist;
        c.p = pr + cl;
    } else {
        float ox = hw - std::fabs(d.x), oy = hh - std::fabs(d.y);
        Vec2 nOut;
        if (ox < oy) { nOut = Vec2(d.x < 0 ? -1.f : 1.f, 0); c.depth = ox + r; }
        else         { nOut = Vec2(0, d.y < 0 ? -1.f : 1.f); c.depth = oy + r; }
        c.n = -nOut;
        c.p = pc;
    }
    return true;
}

bool rectRect(Vec2 pa, float hwa, float hha, Vec2 pb, float hwb, float hhb, Contact& c) {
    float dx = pb.x - pa.x, dy = pb.y - pa.y;
    float ox = hwa + hwb - std::fabs(dx);
    if (ox <= 0) return false;
    float oy = hha + hhb - std::fabs(dy);
    if (oy <= 0) return false;
    if (ox < oy) { c.n = Vec2(dx < 0 ? -1.f : 1.f, 0); c.depth = ox; }
    else         { c.n = Vec2(0, dy < 0 ? -1.f : 1.f); c.depth = oy; }
    c.p = (pa + pb) * 0.5f;
    return true;
}

Vec2 closestOnSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float l2 = ab.len2();
    float t = l2 > 0 ? clampf(dot(p - a, ab) / l2, 0.f, 1.f) : 0.f;
    return a + ab * t;
}

bool circleCapsule(Vec2 pc, float r, const Body& cap, Contact& c) {
    Vec2 a, b;
    capsuleEnds(cap, a, b);
    Vec2 q = closestOnSegment(pc, a, b);
    return circleCircle(pc, r, q, cap.r, c);
}

// rect (A) vs capsule (B): sample along the capsule's spine, good enough for bats vs windmills
bool rectCapsule(Vec2 pr, float hw, float hh, const Body& cap, Contact& c) {
    Vec2 a, b;
    capsuleEnds(cap, a, b);
    float best = 1e9f;
    Vec2 bestS, bestCl;
    bool insideHit = false;
    const int N = 12;
    for (int i = 0; i <= N; i++) {
        Vec2 s = a + (b - a) * (float(i) / N);
        Vec2 d = s - pr;
        Vec2 cl(clampf(d.x, -hw, hw), clampf(d.y, -hh, hh));
        float dist = (d - cl).len();
        if (dist < best) { best = dist; bestS = s; bestCl = pr + cl; insideHit = dist < 1e-4f; }
    }
    if (best >= cap.r) return false;
    if (insideHit) {
        Vec2 d = bestS - pr;
        float ox = hw - std::fabs(d.x), oy = hh - std::fabs(d.y);
        if (ox < oy) { c.n = Vec2(d.x < 0 ? -1.f : 1.f, 0); c.depth = ox + cap.r; }
        else         { c.n = Vec2(0, d.y < 0 ? -1.f : 1.f); c.depth = oy + cap.r; }
    } else {
        c.n = (bestS - bestCl) / best;
        c.depth = cap.r - best;
    }
    c.p = bestCl;
    return true;
}

// capsule (A) vs capsule (B): closest points between the spines (sampled), e.g. bat vs windmill
bool capsuleCapsule(const Body& A, const Body& B, Contact& c) {
    Vec2 a0, a1, b0, b1;
    capsuleEnds(A, a0, a1);
    capsuleEnds(B, b0, b1);
    float best = 1e9f;
    Vec2 pa, pb;
    const int N = 16;
    for (int i = 0; i <= N; i++) {
        Vec2 s = a0 + (a1 - a0) * (float(i) / N);
        Vec2 q = closestOnSegment(s, b0, b1);
        float d = (q - s).len();
        if (d < best) { best = d; pa = s; pb = q; }
    }
    for (int i = 0; i <= N; i++) {
        Vec2 s = b0 + (b1 - b0) * (float(i) / N);
        Vec2 q = closestOnSegment(s, a0, a1);
        float d = (q - s).len();
        if (d < best) { best = d; pa = q; pb = s; }
    }
    float rs = A.r + B.r;
    if (best >= rs) return false;
    c.n = best > 1e-4f ? (pb - pa) / best : Vec2(0, -1);
    c.depth = rs - best;
    c.p = pa + c.n * A.r;
    return true;
}

bool contact(const Body& A, const Body& B, Contact& c) {
    Shape sa = A.shape, sb = B.shape;
    bool ok = false;
    if (sa == Shape::Circle && sb == Shape::Circle) return circleCircle(A.pos, A.r, B.pos, B.r, c);
    if (sa == Shape::Circle && sb == Shape::Rect) return circleRect(A.pos, A.r, B.pos, B.hw, B.hh, c);
    if (sa == Shape::Rect && sb == Shape::Circle) {
        ok = circleRect(B.pos, B.r, A.pos, A.hw, A.hh, c);
        if (ok) c.n = -c.n;
        return ok;
    }
    if (sa == Shape::Rect && sb == Shape::Rect) return rectRect(A.pos, A.hw, A.hh, B.pos, B.hw, B.hh, c);
    if (sa == Shape::Circle && sb == Shape::Capsule) return circleCapsule(A.pos, A.r, B, c);
    if (sa == Shape::Capsule && sb == Shape::Circle) {
        ok = circleCapsule(B.pos, B.r, A, c);
        if (ok) c.n = -c.n;
        return ok;
    }
    if (sa == Shape::Rect && sb == Shape::Capsule) return rectCapsule(A.pos, A.hw, A.hh, B, c);
    if (sa == Shape::Capsule && sb == Shape::Rect) {
        ok = rectCapsule(B.pos, B.hw, B.hh, A, c);
        if (ok) c.n = -c.n;
        return ok;
    }
    if (sa == Shape::Capsule && sb == Shape::Capsule) return capsuleCapsule(A, B, c);
    return false;
}

void extents(const Body& b, float& ex, float& ey) {
    ex = ey = b.r;
    switch (b.shape) {
        case Shape::Circle: ex = ey = b.r; break;
        case Shape::Rect: ex = b.hw; ey = b.hh; break;
        case Shape::Capsule:
            ex = std::fabs(std::cos(b.angle)) * b.hw + b.r;
            ey = std::fabs(std::sin(b.angle)) * b.hw + b.r;
            break;
    }
}

bool isPuck(const Body& b) { return b.kind == Kind::Brick && b.btype == BrickType::Puck; }
bool sinkable(const Body& b) { return b.kind == Kind::Ball || b.kind == Kind::Pill || isPuck(b); }

// which pairs interact at all
bool interacts(const Body& A, const Body& B) {
    if (!A.dynamic() && !B.dynamic()) return false;
    Kind ka = A.kind, kb = B.kind;
    if (ka == Kind::Pill || kb == Kind::Pill) {
        const Body& o = (ka == Kind::Pill) ? B : A;
        return o.kind == Kind::Brick || o.kind == Kind::Player; // player = collect
    }
    if ((ka == Kind::Player && isPuck(B)) || (kb == Kind::Player && isPuck(A))) return false;
    return true;
}

struct Material { float e, fric; };

Material material(const Body& A, const Body& B) {
    auto has = [&](Kind k) { return A.kind == k || B.kind == k; };
    const Body* brick = A.kind == Kind::Brick ? &A : (B.kind == Kind::Brick ? &B : nullptr);
    if (has(Kind::Pill)) return {0.5f, 0.1f};
    if (A.kind == Kind::Ball && B.kind == Kind::Ball) return {1.0f, 0.0f};
    if (has(Kind::Ball) && has(Kind::Player)) return {0.97f, 0.5f};
    if (A.kind == Kind::Player && B.kind == Kind::Player) return {0.3f, 0.0f};
    if (has(Kind::Player) && brick) {
        if (brick->btype == BrickType::Bumper) return {0.6f, 0.0f};
        return {0.1f, 0.0f};
    }
    if (has(Kind::Ball) && brick) {
        switch (brick->btype) {
            case BrickType::Puck: return {0.95f, 0.05f};
            case BrickType::Conveyor: return {0.85f, 0.35f};
            case BrickType::Rail: return {0.92f, 0.12f};
            default: return {1.0f, 0.0f};
        }
    }
    // brick vs brick (pucks, pendulums)
    if (A.kind == Kind::Brick && B.kind == Kind::Brick) {
        if (isPuck(A) && isPuck(B)) return {0.95f, 0.02f};
        return {0.8f, 0.05f};
    }
    return {0.5f, 0.0f};
}

Vec2 pointVel(const Body& b, Vec2 p, Vec2 outN) {
    Vec2 v = b.vel;
    if (b.angVel != 0) {
        Vec2 rel = p - b.pos;
        v += Vec2(-b.angVel * rel.y, b.angVel * rel.x);
    }
    if (b.surfaceSpeed != 0) v += Vec2(-outN.y, outN.x) * b.surfaceSpeed;
    return v;
}

bool megaSmash(const Body& ball, const Body& brick) {
    if (ball.kind != Kind::Ball || !ball.mega || brick.kind != Kind::Brick) return false;
    return brick.btype == BrickType::Normal || brick.btype == BrickType::Explosive ||
           brick.btype == BrickType::Multi;
}

} // namespace

void capsuleEnds(const Body& b, Vec2& p0, Vec2& p1) {
    Vec2 d(std::cos(b.angle) * b.hw, std::sin(b.angle) * b.hw);
    p0 = b.pos - d;
    p1 = b.pos + d;
}

bool pointInBody(const Body& b, Vec2 p, float pad) {
    switch (b.shape) {
        case Shape::Circle: return (p - b.pos).len() < b.r + pad;
        case Shape::Rect: return std::fabs(p.x - b.pos.x) < b.hw + pad && std::fabs(p.y - b.pos.y) < b.hh + pad;
        case Shape::Capsule: {
            Vec2 a, c;
            capsuleEnds(b, a, c);
            return (p - closestOnSegment(p, a, c)).len() < b.r + pad;
        }
    }
    return false;
}

void World::clear() {
    bodies.clear();
    fields.clear();
    holes.clear();
    portals.clear();
    events.clear();
    gravity = Vec2(0, 1450);
    gravityRot = 0;
    wallRestitution = 1.0f;
    time = 0;
}

int World::add(const Body& b) {
    Body nb = b;
    nb.id = nextId++;
    if (nb.kinematic && nb.path != Path::None) nb.base = nb.base.len2() > 0 ? nb.base : nb.pos;
    bodies.push_back(nb);
    return nb.id;
}

Body* World::find(int id) {
    for (auto& b : bodies)
        if (b.id == id) return &b;
    return nullptr;
}

void World::step(float dt) {
    time += dt;
    if (gravityRot != 0) gravity = gravity.rotated(gravityRot * dt);
    integrate(dt);
    walls();
    collisions(true);
    collisions(false);
    collisions(false);
    walls();
    holesAndPortals();
}

void World::integrate(float dt) {
    for (auto& b : bodies) {
        if (!b.alive) continue;
        if (b.hitCooldown > 0) b.hitCooldown -= dt;
        if (b.flash > 0) b.flash -= dt;
        if (b.kinematic) {
            float w = 2 * PI_F * b.freq;
            if (b.path == Path::Osc) {
                float s = std::sin(w * time + b.phase), co = std::cos(w * time + b.phase);
                b.pos = b.base + b.amp * s;
                b.vel = b.amp * (w * co);
            } else if (b.path == Path::Circle) {
                float a = w * time + b.phase;
                b.pos = b.base + Vec2(std::cos(a) * b.amp.x, std::sin(a) * b.amp.y);
                b.vel = Vec2(-std::sin(a) * b.amp.x * w, std::cos(a) * b.amp.y * w);
            }
            b.angle += b.angVel * dt;
            continue;
        }
        if (!b.dynamic()) continue;

        bool isPlayer = b.kind == Kind::Player;
        Vec2 acc = gravity * b.gravScale - b.vel * b.damping;
        if (isPlayer) acc += b.input * b.thrust;

        for (const auto& f : fields) {
            if (isPlayer && !f.affectsPlayers) continue;
            switch (f.type) {
                case FieldType::Wind:
                    if (b.pos.x > f.pos.x && b.pos.x < f.pos.x + f.size.x && b.pos.y > f.pos.y &&
                        b.pos.y < f.pos.y + f.size.y)
                        acc += f.accel;
                    break;
                case FieldType::Goo:
                    if (b.pos.x > f.pos.x && b.pos.x < f.pos.x + f.size.x && b.pos.y > f.pos.y &&
                        b.pos.y < f.pos.y + f.size.y)
                        acc -= b.vel * f.strength;
                    break;
                case FieldType::Well: {
                    Vec2 d = f.pos - b.pos;
                    float r2 = d.len2();
                    if (r2 < f.radius * f.radius) acc += d.norm() * (f.strength / (r2 + 1500.f));
                    break;
                }
                case FieldType::Repulsor: {
                    Vec2 d = b.pos - f.pos;
                    float dist = d.len();
                    if (dist < f.radius && dist > 1) acc += d / dist * (f.strength * (1 - dist / f.radius));
                    break;
                }
            }
        }
        if (sinkable(b)) {
            for (const auto& h : holes) {
                Vec2 d = h.pos - b.pos;
                float dist = d.len();
                if (dist < h.suctionR && dist > 1.5f) {
                    acc += d / dist * h.suction;
                    acc -= b.vel * 2.0f;
                }
            }
        }
        if (b.kind == Kind::Ball) {
            for (const auto& p : bodies) {
                if (p.kind != Kind::Player || p.magnet <= 0 || !p.alive) continue;
                Vec2 d = p.pos - b.pos;
                float dist = d.len();
                if (dist < 260 && dist > 1) acc += d / dist * (p.magnet * (1 - dist / 260));
            }
        }
        if (b.spring) {
            Vec2 d = b.pos - b.anchor;
            float L = d.len();
            if (L > 1e-3f) acc -= d / L * (b.springK * (L - b.springRest));
            acc -= b.vel * b.springDamp;
        }

        b.vel += acc * dt;
        float sp = b.vel.len();
        if (sp > b.maxSpeed) b.vel *= b.maxSpeed / sp;
        if (!std::isfinite(b.vel.x) || !std::isfinite(b.vel.y)) b.vel = Vec2(0, 0);
        b.pos += b.vel * dt;
    }
}

void World::walls() {
    for (auto& b : bodies) {
        if (!b.alive || !b.dynamic()) continue;
        float ex, ey;
        extents(b, ex, ey);
        float e = 0.0f;
        switch (b.kind) {
            case Kind::Ball: e = wallRestitution; break;
            case Kind::Player: e = 0.0f; break;
            case Kind::Pill: e = 0.5f; break;
            case Kind::Brick: e = isPuck(b) ? 0.85f : 0.3f; break;
        }
        float hitSpeed = 0;
        Vec2 hitPos;
        if (b.pos.x - ex < left) {
            b.pos.x = left + ex;
            if (b.vel.x < 0) { hitSpeed = -b.vel.x; b.vel.x = -b.vel.x * e; hitPos = Vec2(left, b.pos.y); }
        }
        if (b.pos.x + ex > right) {
            b.pos.x = right - ex;
            if (b.vel.x > 0) { hitSpeed = b.vel.x; b.vel.x = -b.vel.x * e; hitPos = Vec2(right, b.pos.y); }
        }
        if (b.pos.y - ey < top) {
            b.pos.y = top + ey;
            if (b.vel.y < 0) { hitSpeed = std::max(hitSpeed, -b.vel.y); b.vel.y = -b.vel.y * e; hitPos = Vec2(b.pos.x, top); }
        }
        if (b.pos.y + ey > bottom) {
            b.pos.y = bottom - ey;
            if (b.vel.y > 0) { hitSpeed = std::max(hitSpeed, b.vel.y); b.vel.y = -b.vel.y * e; hitPos = Vec2(b.pos.x, bottom); }
        }
        if (hitSpeed > 80 && (b.kind == Kind::Ball || isPuck(b))) {
            PhysEvent ev{EvType::Wall};
            ev.a = b.id;
            ev.speed = hitSpeed;
            ev.pos = hitPos;
            events.push_back(ev);
        }
    }
}

void World::collisions(bool firstPass) {
    // only pairs with at least one moving body can collide: loop over the few dynamic bodies
    const size_t n = bodies.size();
    dynIdx.clear();
    for (size_t i = 0; i < n; i++)
        if (bodies[i].alive && bodies[i].dynamic()) dynIdx.push_back(i);
    for (size_t d : dynIdx) {
        for (size_t k = 0; k < n; k++) {
            if (k == d || (k < d && bodies[k].dynamic())) continue; // dynamic pairs only once
            size_t i = std::min(d, k), j = std::max(d, k);
            Body& A = bodies[i];
            Body& B = bodies[j];
            if (!A.alive || !B.alive) continue;
            if (!interacts(A, B)) continue;
            float eax, eay, ebx, eby;
            extents(A, eax, eay);
            extents(B, ebx, eby);
            if (std::fabs(A.pos.x - B.pos.x) > eax + ebx || std::fabs(A.pos.y - B.pos.y) > eay + eby) continue;
            Contact c;
            if (!contact(A, B, c)) continue;

            // pills are collected by players, no physical response
            if ((A.kind == Kind::Pill && B.kind == Kind::Player) || (B.kind == Kind::Pill && A.kind == Kind::Player)) {
                if (firstPass) {
                    PhysEvent ev{EvType::Collect};
                    ev.a = A.kind == Kind::Pill ? A.id : B.id;
                    ev.b = A.kind == Kind::Pill ? B.id : A.id;
                    events.push_back(ev);
                }
                continue;
            }
            // mega ball ploughs through breakable bricks
            if (megaSmash(A, B) || megaSmash(B, A)) {
                if (firstPass) {
                    PhysEvent ev{EvType::Hit};
                    ev.a = A.id; ev.b = B.id; ev.speed = 9999; ev.pos = c.p;
                    events.push_back(ev);
                }
                continue;
            }

            float invA = A.invMass, invB = B.invMass;
            float sum = invA + invB;
            if (sum <= 0) continue;
            Material m = material(A, B);

            Vec2 va = pointVel(A, c.p, c.n);
            Vec2 vb = pointVel(B, c.p, -c.n);
            Vec2 vrel = vb - va;
            float vn = dot(vrel, c.n);
            if (vn < 0) {
                float jn = -(1 + m.e) * vn / sum;
                A.vel -= c.n * (jn * invA);
                B.vel += c.n * (jn * invB);
                // bumpers kick the other body away
                if (firstPass && vn < -20) {
                    if (A.kind == Kind::Brick && A.btype == BrickType::Bumper && B.dynamic()) B.vel += c.n * A.kick;
                    if (B.kind == Kind::Brick && B.btype == BrickType::Bumper && A.dynamic()) A.vel -= c.n * B.kick;
                }
                if (firstPass && -vn > 20) {
                    PhysEvent ev{EvType::Hit};
                    ev.a = A.id; ev.b = B.id; ev.speed = -vn; ev.pos = c.p;
                    events.push_back(ev);
                }
            }
            // friction: "rubbing" of the bat on the ball, also conveyors and windmills
            if (m.fric > 0) {
                va = pointVel(A, c.p, c.n);
                vb = pointVel(B, c.p, -c.n);
                vrel = vb - va;
                Vec2 vt = vrel - c.n * dot(vrel, c.n);
                float vtl = vt.len();
                if (vtl > 1e-3f) {
                    float jt = m.fric * vtl / sum;
                    Vec2 t = vt / vtl;
                    A.vel += t * (jt * invA);
                    B.vel -= t * (jt * invB);
                }
            }
            // positional correction, split by inverse mass
            float corr = std::max(c.depth - 0.01f, 0.0f);
            A.pos -= c.n * (corr * invA / sum);
            B.pos += c.n * (corr * invB / sum);
        }
    }
}

void World::holesAndPortals() {
    for (auto& b : bodies) {
        if (!b.alive || !sinkable(b)) continue;
        float size = b.shape == Shape::Rect ? b.hh : b.r;
        // a bat covering the hole shields whatever rests on it
        bool shielded = false;
        for (const auto& p : bodies)
            if (p.kind == Kind::Player && p.alive && pointInBody(p, b.pos, size + 2)) shielded = true;
        for (size_t h = 0; h < holes.size() && !shielded; h++) {
            const Hole& hole = holes[h];
            if (hole.r < size * 0.9f) continue;
            float dist = (b.pos - hole.pos).len();
            if (dist + 0.5f * size < hole.r) {
                b.alive = false;
                PhysEvent ev{EvType::Swallow};
                ev.a = b.id;
                ev.idx = int(h);
                ev.pos = hole.pos;
                events.push_back(ev);
                break;
            }
        }
        if (!b.alive) continue;
        if (b.portalLock >= 0) {
            const Portal& p = portals[b.portalLock / 2];
            Vec2 e = (b.portalLock % 2) ? p.b : p.a;
            if ((b.pos - e).len() > p.r + size) b.portalLock = -1;
            continue;
        }
        for (size_t i = 0; i < portals.size(); i++) {
            const Portal& p = portals[i];
            for (int side = 0; side < 2; side++) {
                Vec2 e = side ? p.b : p.a;
                if ((b.pos - e).len() < p.r * 0.6f) {
                    Vec2 out = side ? p.a : p.b;
                    PhysEvent ev{EvType::Teleport};
                    ev.a = b.id;
                    ev.idx = int(i);
                    ev.pos = e;
                    events.push_back(ev);
                    b.pos = out;
                    b.portalLock = int(i) * 2 + (1 - side);
                    goto teleported;
                }
            }
        }
    teleported:;
    }
}
