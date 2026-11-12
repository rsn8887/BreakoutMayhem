// Breakout Mayhem - hand-coded physics
// Force-based physics: freely movable bat driven by forces, inertia, friction, gravity, (in)elastic collisions,
// suction holes and force fields.
#pragma once
#include <vector>
#include <cstdint>
#include "vec.h"

enum class Kind : uint8_t { Player, Ball, Brick, Pill, Enemy };
enum class Shape : uint8_t { Circle, Rect, Capsule };

enum class BrickType : uint8_t {
    Normal,     // breakable
    Metal,      // indestructible
    Bumper,     // indestructible, kicks the ball away
    Explosive,  // blows up neighbours, knocks everything back
    Multi,      // spawns an extra ball
    Puck,       // pushable by the ball, must be sunk into a hole
    GravSwitch, // rotates gravity by 90 degrees when hit
    Rail,       // static or spinning capsule (windmills, pinball rails)
    Conveyor,   // indestructible with a moving surface
    Skull,      // indestructible, deadly to bats
    BigBall,    // releases a large, heavy extra ball
    Magnet,     // attracts balls
    Nest,       // releases an enemy
    Bulb,       // light bulb: the room gets darker as bulbs break
    Switch,     // indestructible, toggles the barriers of its group
    Barrier,    // energy wall that switches turn on and off
};

enum class Path : uint8_t { None, Osc, Circle };

struct Body {
    int id = 0;
    Kind kind = Kind::Brick;
    Shape shape = Shape::Rect;
    BrickType btype = BrickType::Normal;
    bool alive = true;

    Vec2 pos, vel;
    float r = 10;          // circle / capsule radius
    float hw = 10, hh = 10; // rect half extents, capsule half length in hw
    float angle = 0, angVel = 0;

    float invMass = 0;     // 0 = infinite mass (static or kinematic)
    float damping = 0;     // velocity damping rate (1/s), i.e. friction
    float gravScale = 1;
    float maxSpeed = 1700;
    bool kinematic = false;

    // kinematic path
    Path path = Path::None;
    Vec2 base, amp;
    float freq = 0, phase = 0;

    // spring (pendulum bricks)
    bool spring = false;
    Vec2 anchor;
    float springK = 0, springRest = 0, springDamp = 0;

    float surfaceSpeed = 0; // conveyor
    float kick = 0;         // bumper extra speed
    bool solid = true;      // false: switched-off barrier, nothing collides with it
    bool deadly = false;    // touching it with a bat costs a life
    int group = -1;         // switch / barrier group

    // gameplay
    int hp = 1, maxHp = 1;
    int score = 100;
    int color = 0;
    bool counts = false;    // must be destroyed to finish the level
    float hitCooldown = 0;
    float flash = 0;

    // player
    int player = -1;
    Vec2 input;             // desired velocity from controls
    float thrust = 12;      // acceleration per unit of desired velocity
    float magnet = 0;       // attracts balls (magnet bats, magnet bricks)
    float magnetR = 260;    // magnet range
    float stun = 0;         // seconds a bat stays stunned after an enemy hit

    // ball
    int combo = 1;
    bool mega = false;
    int portalLock = -1;    // portal index the body currently sits in

    // pill
    int pill = 0;
    float life = 0;

    float mass() const { return invMass > 0 ? 1.0f / invMass : 1e9f; }
    bool dynamic() const { return invMass > 0; }
};

enum class FieldType : uint8_t { Wind, Goo, Well, Repulsor };

struct Field {
    FieldType type = FieldType::Wind;
    Vec2 pos;             // center (Well/Repulsor) or top-left (Wind/Goo)
    Vec2 size;            // rect size (Wind/Goo)
    float radius = 0;     // radius of influence (Well/Repulsor)
    Vec2 accel;           // Wind acceleration
    float strength = 0;   // Goo damping, Well G, Repulsor acceleration
    bool affectsPlayers = false;
};

struct Hole {
    Vec2 pos;
    float r = 24;
    float suctionR = 60;
    float suction = 3000; // acceleration toward the center
};

// rubber band between two bodies: pulls only when stretched beyond its rest length
struct Band {
    int a = 0, b = 0;     // body ids
    float rest = 100;
    float k = 1.0f;       // stiffness (force per pixel)
    float damp = 0.03f;   // damping of the stretching speed
};

struct Portal {
    Vec2 a, b;
    float r = 34;
};

enum class EvType : uint8_t { Hit, Wall, Swallow, Teleport, Collect, Touch };

struct PhysEvent {
    explicit PhysEvent(EvType t) : type(t) {}
    EvType type;
    int a = 0, b = 0;     // body ids (Hit/Collect: a,b; Wall/Swallow/Teleport: a)
    int idx = 0;          // hole / portal index
    float speed = 0;
    Vec2 pos;
};

struct World {
    std::vector<Body> bodies;
    std::vector<Field> fields;
    std::vector<Hole> holes;
    std::vector<Portal> portals;
    std::vector<Band> bands;
    std::vector<PhysEvent> events;

    Vec2 gravity{0, 1450};
    float gravityRot = 0;   // rad/s, rotating gravity
    float wallRestitution = 0.9f;
    float time = 0;
    float left = 10, top = 50, right = 1270, bottom = 710;
    int nextId = 1;

    void clear();
    int add(const Body& b);
    Body* find(int id);
    void step(float dt);

private:
    std::vector<size_t> dynIdx;
    std::vector<Vec2> bandAcc;
    std::vector<size_t> magnets;
    void integrate(float dt);
    void walls();
    void collisions(bool firstPass);
    void holesAndPortals();
};

// shape helpers used by the game and renderer
void capsuleEnds(const Body& b, Vec2& p0, Vec2& p1);
bool pointInBody(const Body& b, Vec2 p, float pad);
