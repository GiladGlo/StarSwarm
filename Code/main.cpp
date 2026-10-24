// =============================================================================
//  STAR SWARM  -  a Galaga-inspired arcade shooter in C++ / raylib
// -----------------------------------------------------------------------------
//  Controls:  LEFT/RIGHT or A/D  move     SPACE  fire     P  pause     ESC  quit
//  All graphics are procedural pixel-art; all sounds are synthesized at startup.
// =============================================================================
#include "raylib.h"
#include "raymath.h"
#include "sprites.h"

#include <vector>
#include <string>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cctype>
#include <algorithm>

static const int   W = 600;
static const int   H = 800;
static const float  SHIELD_RADIUS = 32.0f;       // force-field bubble radius
static const size_t MAX_TORPEDOES = 5;          // projectiles allowed on screen at once
static const char* HISCORE_FILE = "starswarm_highscore.txt";

// ----------------------------------------------------------------------------
//  Utility
// ----------------------------------------------------------------------------
static float frand(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 10000) / 10000.0f; }

static Vector2 bezier(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
    float u = 1 - t;
    return { u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
             u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y };
}

// Build a polyline from a chain of cubic Bezier control points (3n+1 points).
static std::vector<Vector2> buildPath(const std::vector<Vector2>& ctrl) {
    std::vector<Vector2> out;
    for (size_t i = 0; i + 3 < ctrl.size(); i += 3)
        for (int s = (i == 0 ? 0 : 1); s <= 24; ++s)
            out.push_back(bezier(ctrl[i], ctrl[i+1], ctrl[i+2], ctrl[i+3], s / 24.0f));
    return out;
}

// ----------------------------------------------------------------------------
//  Rendered ship textures (generated at startup by sprites.h)
// ----------------------------------------------------------------------------
struct Gfx {
    Texture2D player{}, bee[2]{}, fly[2]{}, boss[2]{}, bossDmg[2]{}, glow{}, nebula{};
    static constexpr int BOSS_N = 384;
    Texture2D meteor[7]{};                             // [6] = volatile (explosive) rock
    Texture2D dart{}, shielded[2]{}, splitter[2]{};    // later-wave enemy types
    Texture2D courier[2]{};                            // [1] = armour blown off
    Texture2D planet[10]{};                            // one backdrop body per boss cycle
    bool planetLoaded[10]{};
    Texture2D bossTex[10]{};
    std::vector<unsigned char> bossMask[10];          // alpha masks for pixel-accurate hit tests
    bool bossLoaded[10]{};
    void ensureBoss(int i) {
        if (bossLoaded[i]) return;
        bossTex[i] = spr::bossShip(i, BOSS_N, &bossMask[i]);
        bossLoaded[i] = true;
    }
    void load() {
        const int N = 256;
        player = spr::playerShip(N);
        for (int f = 0; f < 2; ++f) {
            bee[f] = spr::bee(N, f);
            fly[f] = spr::butterfly(N, f);
            boss[f] = spr::boss(N, f, false);
            bossDmg[f] = spr::boss(N, f, true);
        }
        for (int i = 0; i < 7; ++i) meteor[i] = spr::meteor(192, i);
        dart = spr::dart(N);
        courier[0] = spr::courier(N, false); courier[1] = spr::courier(N, true);
        for (int f = 0; f < 2; ++f) { shielded[f] = spr::shieldedBug(N, f); splitter[f] = spr::splitter(N, f); }
        glow = spr::glow(64);
        nebula = spr::nebula(300, 400);
    }
    void unload() {
        for (Texture2D* t : { &player, &bee[0], &bee[1], &fly[0], &fly[1], &boss[0], &boss[1], &bossDmg[0], &bossDmg[1], &glow, &nebula })
            UnloadTexture(*t);
        for (int i = 0; i < 10; ++i) if (bossLoaded[i]) UnloadTexture(bossTex[i]);
        for (auto& m : meteor) UnloadTexture(m);
        for (Texture2D* t : { &dart, &shielded[0], &shielded[1], &splitter[0], &splitter[1], &courier[0], &courier[1] }) UnloadTexture(*t);
        for (int i = 0; i < 10; ++i) if (planetLoaded[i]) UnloadTexture(planet[i]);
    }
};
static Gfx gfx;

// ----------------------------------------------------------------------------
//  Visual theme per 6-stage boss cycle: sky gradient, nebula tint and a distant body
// ----------------------------------------------------------------------------
struct Theme { Color top, bottom, neb; int style; Vector3 c1, c2; bool ring; float px, py, size; };
static const Theme THEMES[10] = {
    { { 2, 2, 10, 255 },  { 8, 4, 22, 255 },   { 255, 255, 255, 170 }, 0, spr::rgb(150, 80, 60),  spr::rgb(90, 50, 40),   false, 0.78f, 0.30f, 150 },  // Vanguard: red rock world
    { { 10, 5, 2, 255 },  { 26, 12, 4, 255 },  { 255, 190, 120, 160 }, 1, spr::rgb(220, 150, 70), spr::rgb(150, 80, 40),  true,  0.22f, 0.25f, 260 },  // Talon: ringed amber giant
    { { 4, 3, 2, 255 },   { 12, 10, 6, 255 },  { 200, 180, 140, 120 }, 2, spr::rgb(60, 50, 40),   spr::rgb(255, 210, 120), false, 0.72f, 0.22f, 190 },  // Eclipse: eclipsed sun
    { { 2, 8, 3, 255 },   { 6, 20, 8, 255 },   { 140, 255, 150, 150 }, 0, spr::rgb(90, 170, 80),  spr::rgb(40, 90, 50),   false, 0.25f, 0.35f, 170 },  // Hydra: toxic green moon
    { { 2, 6, 14, 255 },  { 6, 14, 30, 255 },  { 150, 210, 255, 170 }, 3, spr::rgb(150, 200, 240), spr::rgb(90, 140, 200), true,  0.75f, 0.28f, 230 },  // Bastion: ringed ice world
    { { 1, 8, 9, 255 },   { 3, 18, 20, 255 },  { 120, 255, 230, 150 }, 1, spr::rgb(60, 170, 170), spr::rgb(30, 90, 110),  false, 0.20f, 0.30f, 200 },  // Mantis: teal ocean world
    { { 6, 2, 12, 255 },  { 16, 6, 30, 255 },  { 210, 150, 255, 170 }, 1, spr::rgb(150, 90, 210), spr::rgb(80, 40, 130),  true,  0.76f, 0.30f, 270 },  // Leviathan: violet giant
    { { 10, 1, 1, 255 },  { 26, 4, 3, 255 },   { 255, 120, 90, 160 },  0, spr::rgb(200, 60, 30),  spr::rgb(255, 150, 40),  false, 0.24f, 0.26f, 180 },  // Cataclysm: burning world
    { { 10, 9, 6, 255 },  { 24, 20, 14, 255 }, { 255, 240, 200, 140 }, 4, spr::rgb(255, 230, 160), spr::rgb(255, 200, 120), false, 0.80f, 0.18f, 110 },  // Seraph: radiant star
    { { 4, 0, 4, 255 },   { 14, 0, 8, 255 },   { 255, 90, 120, 150 },  5, spr::rgb(0, 0, 0),      spr::rgb(255, 150, 60),  true,  0.50f, 0.30f, 260 },  // Oblivion: black hole
};
static const Texture2D& planetTex(int i) {
    if (!gfx.planetLoaded[i]) { const Theme& t = THEMES[i]; gfx.planet[i] = spr::planet(256, t.style, t.c1, t.c2, t.ring); gfx.planetLoaded[i] = true; }
    return gfx.planet[i];
}

// ----------------------------------------------------------------------------
//  Wave formations (one character per slot; '.' = empty)
//  B boss  F butterfly  b bee   -- later waves swap some for darts / shielded / splitters
// ----------------------------------------------------------------------------
struct Formation { const char* name; std::vector<const char*> rows; };
// wave number (0 = stage 1; 4 waves per cycle) at which each new type first appears: stage 2, stage 8, stage 14
static const int NEW_TYPE_WAVE[3] = { 1, 5, 9 };
static const std::vector<Formation> FORMATIONS = {
    { "CLASSIC",  { "...BBBB...", ".FFFFFFFF.", ".FFFFFFFF.", "bbbbbbbbbb", "bbbbbbbbbb" } },
    { "CHEVRON",  { "BF.......FB", "bFF.....FFb", ".bFF...FFb.", "..bFF.FFb..", "...bBBBb...", "....bbb...." } },
    { "DIAMOND",  { ".....B.....", "....FBF....", "...FFbFF...", "..bbFbFbb..", ".bbbFBFbbb.", "..bbbbbbb..", "...bb.bb..." } },
    { "TWIN PILLARS", { "BBb.....bBB", "FFb.....bFF", "FFb.....bFF", "bbb.....bbb", "bbb.....bbb", "bbb.....bbb" } },
    { "CHECKERBOARD", { ".B.B.B.B.B.", "F.F.F.F.F.F", ".F.F.F.F.F.", "b.b.b.b.b.b", ".b.b.b.b.b.", "b.b.b.b.b.b" } },
    { "RING",     { "...BBBBB...", "..F.....F..", ".F..bbb..F.", ".F.b.B.b.F.", ".F..bbb..F.", "..b.....b..", "...bbbbb..." } },
    { "ARROWHEAD", { ".....B.....", "....BFB....", "...FFbFF...", "..FbbbbbF..", ".bbb...bbb.", "bbb.....bbb" } },
    { "WINGS",    { "B.........B", "FF...B...FF", "bFF.FFF.FFb", "bbFFbbbFFbb", ".bbb...bbb." } },
};

// Draw a ship texture centred at pos, `size` logical units wide, rotated, with a soft drop shadow.
static void drawShip(const Texture2D& t, Vector2 pos, float size, float angleDeg, bool shadow = true, Color tint = WHITE) {
    Rectangle src{ 0, 0, (float)t.width, (float)t.height };
    if (shadow)
        DrawTexturePro(t, src, { pos.x + size * 0.12f, pos.y + size * 0.18f, size * 0.95f, size * 0.95f },
                       { size * 0.475f, size * 0.475f }, angleDeg, Color{ 0, 0, 0, 110 });
    DrawTexturePro(t, src, { pos.x, pos.y, size, size }, { size / 2, size / 2 }, angleDeg, tint);
}

static void drawGlow(Vector2 pos, float radius, Color c);
// ---- Player weapon classes (picked up from Courier packages) ----------------
enum WClass { W_PHOTON, W_SPREAD, W_PULSE, W_HOMING, W_ION, W_COUNT };
static const char* WNAME[W_COUNT] = { "PHOTON TORPEDO", "SPREAD CANNON", "PULSE LASER", "HOMING SWARM", "ION BURST" };
static const Color WCOL[W_COUNT] = { { 255, 110, 50, 255 }, { 255, 160, 40, 255 }, { 70, 220, 255, 255 }, { 110, 255, 120, 255 }, { 190, 110, 255, 255 } };
static const Color UPG_COL = { 255, 215, 60, 255 };
// Weapon icons for packages and the HUD indicator. k: 0 photon 1 spread 2 pulse 3 homing 4 ion 5 upgrade
static void weaponIcon(int k, Vector2 c, float s, Color col) {
    float w = s * 0.12f;
    switch (k) {
        case 0: drawGlow(c, s * 0.5f, col); DrawCircleV(c, s * 0.22f, WHITE); break;
        case 1: for (int i = -1; i <= 1; i++) { float a = (-90 + i * 28) * DEG2RAD;
                    DrawLineEx({ c.x, c.y + s * 0.38f }, { c.x + cosf(a) * s * 0.55f, c.y + s * 0.38f + sinf(a) * s * 0.75f }, w, WHITE); } break;
        case 2: DrawRectangleRounded({ c.x - w * 0.9f, c.y - s * 0.42f, w * 1.8f, s * 0.84f }, 1, 6, WHITE); break;
        case 3: { DrawRing({ c.x + s * 0.1f, c.y + s * 0.1f }, s * 0.26f, s * 0.26f + w, 180, 330, 16, WHITE);
                  Vector2 t{ c.x + s * 0.1f + s * 0.32f * cosf(330 * DEG2RAD), c.y + s * 0.1f + s * 0.32f * sinf(330 * DEG2RAD) };
                  Vector2 a{ t.x + s * 0.16f, t.y - s * 0.06f }, b{ t.x - s * 0.12f, t.y - s * 0.12f }, d{ t.x - s * 0.02f, t.y + s * 0.14f };
                  DrawTriangle(a, b, d, WHITE); DrawTriangle(a, d, b, WHITE); break; }
        case 4: { Vector2 z[6] = { { c.x + s * 0.08f, c.y - s * 0.45f }, { c.x - s * 0.18f, c.y + s * 0.02f }, { c.x + s * 0.04f, c.y + s * 0.02f },
                                   { c.x - s * 0.1f, c.y + s * 0.45f }, { c.x + s * 0.2f, c.y - s * 0.08f }, { c.x - s * 0.02f, c.y - s * 0.08f } };
                  for (int i = 0; i < 5; i++) { DrawLineEx(z[i], z[i + 1], w, WHITE); }
                  break; }
        default: for (int i = 0; i < 2; i++) { float y = c.y + s * 0.12f - i * s * 0.32f;
                    DrawLineEx({ c.x - s * 0.32f, y + s * 0.2f }, { c.x, y - s * 0.08f }, w * 1.2f, WHITE);
                    DrawLineEx({ c.x, y - s * 0.08f }, { c.x + s * 0.32f, y + s * 0.2f }, w * 1.2f, WHITE); } break;
    }
}
// Falling package: spinning hexagonal pod in the content colour, icon in the middle, orbiting beacons
static void drawPackage(int content, Vector2 c, float s, float t) {
    Color col = content >= 0 ? WCOL[content] : UPG_COL;
    float pl = 0.6f + 0.4f * sinf(t * 6);
    BeginBlendMode(BLEND_ADDITIVE); drawGlow(c, s * 1.3f, Color{ col.r, col.g, col.b, (unsigned char)(90 + 70 * pl) }); EndBlendMode();
    DrawPoly(c, 6, s * 0.62f, t * 40, Color{ 30, 30, 45, 255 });
    DrawPolyLinesEx(c, 6, s * 0.62f, t * 40, s * 0.06f, col);
    DrawPolyLinesEx(c, 6, s * 0.5f, t * 40, s * 0.02f, Color{ col.r, col.g, col.b, 150 });
    DrawCircleV(c, s * 0.36f, Color{ (unsigned char)(col.r / 3), (unsigned char)(col.g / 3), (unsigned char)(col.b / 3), 255 });
    weaponIcon(content >= 0 ? content : 5, c, s * 0.5f, col);
    for (int i = 0; i < 3; i++) {
        float a = (t * 120 + i * 120) * DEG2RAD; Vector2 b{ c.x + cosf(a) * s * 0.75f, c.y + sinf(a) * s * 0.75f };
        BeginBlendMode(BLEND_ADDITIVE); drawGlow(b, s * 0.12f, col); EndBlendMode(); DrawCircleV(b, s * 0.035f, WHITE);
    }
}
static void drawGlow(Vector2 pos, float radius, Color c) {
    DrawTexturePro(gfx.glow, { 0, 0, (float)gfx.glow.width, (float)gfx.glow.height },
                   { pos.x, pos.y, radius * 2, radius * 2 }, { radius, radius }, 0, c);
}

// ----------------------------------------------------------------------------
//  Synthesized sound effects
// ----------------------------------------------------------------------------
enum SfxKind { SFX_SHOOT, SFX_HIT, SFX_BOOM, SFX_DEATH, SFX_STAGE, SFX_DIVE, SFX_LIFE, SFX_IMPACT, SFX_SHIELD_UP, SFX_SHIELD_BREAK, SFX_ALARM, SFX_LASER, SFX_BIGBOOM, SFX_COUNT };

static Sound makeSound(SfxKind k) {
    const int rate = 22050;
    float dur = 0.15f;
    switch (k) {
        case SFX_SHOOT: dur = 0.32f; break;  case SFX_ALARM: dur = 2.0f; break;  case SFX_LASER: dur = 1.3f; break;  case SFX_BIGBOOM: dur = 2.2f; break;  case SFX_SHIELD_UP: dur = 0.45f; break;  case SFX_SHIELD_BREAK: dur = 0.6f; break;  case SFX_IMPACT: dur = 0.18f; break;  case SFX_HIT:   dur = 0.10f; break;
        case SFX_BOOM:  dur = 0.35f; break;  case SFX_DEATH: dur = 1.20f; break;
        case SFX_STAGE: dur = 0.90f; break;  case SFX_DIVE:  dur = 0.45f; break;
        case SFX_LIFE:  dur = 0.60f; break;
        default: break;
    }
    int frames = (int)(rate * dur);
    short* data = (short*)malloc(sizeof(short) * frames);
    float phase = 0, noise = 0;
    for (int i = 0; i < frames; ++i) {
        float t = (float)i / rate, p = t / dur, env = 1 - p, f = 440, s = 0;
        switch (k) {
            case SFX_SHOOT: f = 160 + 900 * expf(-p * 6) + 40 * sinf(t * 90); env = (1 - p) * (1 - p); break;
            case SFX_IMPACT: f = 700 * expf(-p * 4); env = (1 - p); break;
            case SFX_ALARM: f = fmodf(t, 0.5f) < 0.25f ? 620 : 470; env = 0.55f * (fmodf(t, 0.5f) < 0.45f ? 1.f : 0.f); break;
            case SFX_LASER: f = 90 + 25 * sinf(t * 40); env = std::min(1.0f, t * 8) * (1 - p * p); break;
            case SFX_SHIELD_UP: f = 300 + 900 * p + 80 * sinf(t * 70); env = 0.5f * sinf(p * PI); break;
            case SFX_SHIELD_BREAK: f = 1800 * expf(-p * 3) + 150; env = (1 - p) * (1 - p); break;
            case SFX_HIT:   f = 300 + 900 * p;   break;
            case SFX_DIVE:  f = 900 - 500 * p + 60 * sinf(t * 60); env = 0.6f * (1 - p); break;
            case SFX_STAGE: { static const float n[] = { 523, 659, 784, 1047, 784, 1047 };
                              f = n[std::min(5, (int)(p * 6))]; env = 0.7f; break; }
            case SFX_LIFE:  { static const float n[] = { 784, 988, 1175, 1568 };
                              f = n[(int)(p * 12) % 4]; env = 0.6f * (1 - p); break; }
            default: break;
        }
        if (k == SFX_SHIELD_UP || k == SFX_SHIELD_BREAK) {
            // shimmering energy: detuned sines (+ glassy noise on break)
            phase += f / rate;
            if (i % 2 == 0) noise = frand(-1, 1);
            s = (0.5f * sinf(phase * 2 * PI) + 0.35f * sinf(phase * 2 * PI * 1.51f) + 0.15f * sinf(phase * 2 * PI * 3.02f)) * env;
            if (k == SFX_SHIELD_BREAK) s += noise * env * env * 0.9f * (fmodf(t * 37, 1.0f) < 0.5f ? 1.f : 0.4f);
            s *= 1.3f;
        } else if (k == SFX_SHOOT || k == SFX_IMPACT) {
            // photon "whoomp": sine sweep + soft noise burst
            phase += f / rate;
            if (i % 2 == 0) noise = frand(-1, 1);
            s = (0.75f * sinf(phase * 2 * PI) + 0.25f * (fmodf(phase * 2, 1.0f) < 0.5f ? 1.f : -1.f)) * env
              + noise * env * env * (k == SFX_IMPACT ? 0.8f : 0.3f);
            s *= 1.4f;
        } else if (k == SFX_LASER) {
            phase += f / rate; if (i % 3 == 0) noise = frand(-1, 1);
            s = (0.6f * (fmodf(phase, 1.0f) * 2 - 1) + 0.4f * noise) * env;
        } else if (k == SFX_BIGBOOM) {
            if (i % 8 == 0) noise = frand(-1, 1);
            float e2 = expf(-p * 2.5f) * (0.7f + 0.3f * sinf(t * 23));
            s = (0.8f * noise + 0.4f * sinf(t * (60 - 30 * p) * 2 * PI)) * e2;
        } else if (k == SFX_BOOM || k == SFX_DEATH) {
            if (i % (k == SFX_DEATH ? 6 : 3) == 0) noise = frand(-1, 1);
            s = noise * env * env;
            if (k == SFX_DEATH) s = 0.6f * s + 0.4f * (sinf(t * (300 - 220 * p) * 2 * PI) > 0 ? 1.f : -1.f) * env * env;
        } else {
            phase += f / rate;
            s = (fmodf(phase, 1.0f) < 0.5f ? 1.f : -1.f) * env;
        }
        data[i] = (short)(s * 0.25f * 32767);
    }
    Wave w{ (unsigned)frames, (unsigned)rate, 16, 1, data };
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);
    return snd;
}

// ----------------------------------------------------------------------------
//  Game objects
// ----------------------------------------------------------------------------
enum EType  { BEE, BUTTERFLY, BOSS, DART, SHIELDED, SPLITTER, MINI, COURIER };
enum EState { WAITING, ENTERING, HOMING, FORMATION, DIVING, DEAD };

struct Enemy {
    EType  type;
    EState state = WAITING;
    int    col, row, hp;
    float  sx = 0, sy = 0;      // formation slot (grid units from the formation centre)
    Vector2 pos{ 0, -100 }, prev{ 0, -100 };
    float  angle = 0, delay = 0, speed = 260, shootTimer = 0;
    std::vector<Vector2> path;
    size_t seg = 0;
    int    shotsLeft = 0;
    bool   minion = false;      // launched by the dreadnought; never returns to formation
    float  grow = 1;            // 0->1 as a launched fighter emerges from its hatch
    int    maxHp = 1;
    int    cargo = -1;          // COURIER: weapon class it carries (W_SPREAD..W_ION) or -1 = upgrade
    float  fireT = 0;           // COURIER: time to its next shot
    bool   armourOff = false;   // COURIER: outer plates already blown off
};

// Enemy projectile kinds (boss weapons get their own look)
enum BKind { BK_ENEMY = 0, BK_ORB = 1, BK_MISSILE = 2, BK_METEOR = 3, BK_BOLT = 4, BK_SPIRAL = 5, BK_CRYSTAL = 6,
             BK_C_SPREAD = 7, BK_C_PULSE = 8, BK_C_HOMING = 9, BK_C_ION = 10, BK_C_PHOTON = 11 };   // 7+: Courier shots

enum PBKind { PB_PHOTON = 0, PB_SPREAD = 1, PB_HOMING = 3, PB_ION = 4 };   // player projectile kinds
struct Package   { Vector2 pos; int content; float t; };   // content: W_SPREAD..W_ION, or -1 = upgrade
struct Lightning { Vector2 a, b; float life; unsigned seed; };
struct Bullet   { Vector2 pos, vel; int kind = 0; float age = 0; float aux = 0; };   // aux: hue (spiral) / rock variant (meteor)
enum PKind { SPARK, FLASH, SMOKE, DEBRIS, TRAIL, EMBER, HULL };   // HULL: debris with no drag (flies off screen)
struct Particle { Vector2 pos, vel; float life, maxLife, size; Color col; PKind kind = SPARK; float rot = 0, spin = 0; };
struct Ring     { Vector2 pos; float life, maxLife, radius; Color col; };
struct Warning  { float x, t; };                         // Meteor Storm entry marker
struct Popup    { Vector2 pos; float life; std::string text; };
struct Star     { Vector2 pos; float speed, phase; Color col; };


// Capital-ship bosses: one every 5th stage, ten in total (see spr::bossDesigns)
static const int BOSS_COUNT = 10;
static const int CYCLE = 6;                       // per boss: 4 waves, a meteor field, then the boss
static const int FINAL_STAGE = BOSS_COUNT * CYCLE;
enum StageKind { SK_WAVE, SK_METEORS, SK_BOSS };
static StageKind stageKind(int s) { int m = s % CYCLE; return m == 0 ? SK_BOSS : (m == CYCLE - 1 ? SK_METEORS : SK_WAVE); }
enum BossState  { B_ENTER, B_FIGHT, B_DYING };
enum BossAttack { A_NONE, A_SPREAD, A_BURST, A_SPIRAL, A_LASER, A_SPAWN, A_RING, A_HOMING, A_TWINLASER, A_RAIN, A_SWEEP };
static const float BOSS_HP[BOSS_COUNT] = { 40, 60, 85, 110, 140, 170, 200, 235, 270, 320 };
static const std::vector<BossAttack> BOSS_ATTACKS[BOSS_COUNT] = {
    { A_SPREAD, A_BURST, A_SPAWN },
    { A_SPREAD, A_BURST, A_SPAWN, A_SPIRAL },
    { A_SPREAD, A_BURST, A_SPIRAL, A_LASER },
    { A_SPREAD, A_SPAWN, A_HOMING, A_BURST, A_SPIRAL },
    { A_RING, A_BURST, A_LASER, A_SPREAD, A_HOMING },
    { A_TWINLASER, A_BURST, A_SPIRAL, A_SPAWN, A_RING },
    { A_RAIN, A_LASER, A_HOMING, A_SPREAD, A_BURST, A_RING },
    { A_SWEEP, A_TWINLASER, A_RING, A_SPIRAL, A_RAIN, A_SPAWN, A_HOMING },
    { A_SPREAD, A_BURST, A_SPIRAL, A_LASER, A_SPAWN, A_RING, A_HOMING, A_TWINLASER, A_RAIN, A_SWEEP },
    { A_SPREAD, A_BURST, A_SPIRAL, A_LASER, A_SPAWN, A_RING, A_HOMING, A_TWINLASER, A_RAIN, A_SWEEP },
};

struct Boss {
    bool active = false;
    int  index = 0;                           // 0..9
    BossState state = B_ENTER;
    BossAttack attack = A_NONE;
    Vector2 pos{ 0, -160 };
    float hp = 0, maxHp = 1, t = 0, moveT = 0, attackTimer = 2, attackT = 0, flash = 0, dyingT = 0, emitT = 0, fireT = 0;
    float vx = 0, ignite = 0;                 // horizontal speed (Scythe Beam lean) and beam-ignition flash
    int   step = 0, lastAttack = A_NONE;
    const spr::BossDesign& D() const { return spr::bossDesigns()[index]; }
    float size() const { return D().size; }
    float aggr() const { return 1.0f + 0.1f * index; }          // later bosses fire more and faster
    int phase() const { return hp > maxHp * 0.5f ? 0 : (hp > maxHp * 0.25f ? 1 : 2); }
    Vector2 W2(Vector2 u) const { return { pos.x + u.x * size() / 2, pos.y + u.y * size() / 2 }; }   // unit -> world
    Vector2 core()   const { return W2(D().coreP); }
    Vector2 muzzle() const { return W2(D().muzzle); }
    Vector2 turret(int i) const { const auto& t = D().turrets; return W2({ t[i % t.size()].x, t[i % t.size()].y + 0.2f }); }
    int turrets() const { return (int)D().turrets.size(); }
    bool hitTest(Vector2 p) const {           // pixel-accurate test against the rendered sprite's alpha
        const auto& m = gfx.bossMask[index];
        if (m.empty()) return false;
        float u = (p.x - pos.x) / size() + 0.5f, v = (p.y - pos.y) / size() + 0.5f;
        if (u < 0 || v < 0 || u >= 1 || v >= 1) return false;
        int N = Gfx::BOSS_N;
        return m[(int)(v * N) * N + (int)(u * N)] > 110;
    }
    std::vector<Vector2> beams() const {      // current laser origins
        std::vector<Vector2> o;
        if (attack == A_LASER || attack == A_SWEEP) o.push_back(muzzle());
        if (attack == A_TWINLASER) {
            int lo = 0, hi = 0;
            for (int i = 0; i < turrets(); ++i) { if (D().turrets[i].x < D().turrets[lo].x) lo = i; if (D().turrets[i].x > D().turrets[hi].x) hi = i; }
            o.push_back(turret(lo)); o.push_back(turret(hi));
        }
        return o;
    }
};

// Meteor field: 3 sizes; every torpedo hit splits a rock into two of the next size down
enum MSize { M_SMALL, M_MEDIUM, M_LARGE };
static const float METEOR_R[3]   = { 15, 27, 46 };     // collision radius
static const int   METEOR_PTS[3] = { 100, 50, 20 };
static const float MAX_DENSITY   = 0.50f;              // never cover more than half the screen
static const int   VOLATILE_VARIANT = 6;
static const float VOLATILE_CHANCE  = 0.05f;           // 5% of spawned rocks are explosive (always large)
static const float BLAST_RADIUS     = 150.0f;          // area-of-effect radius of an exploding rock
struct Meteor {
    Vector2 pos, vel; float rot, spin, flash = 0; MSize size; int variant;
    float fuse = 0;   // >0: caught in a blast, detonates when it runs out
    int   id = 0;
    float r() const { return METEOR_R[size]; }
    bool  volatileRock() const { return variant == VOLATILE_VARIANT; }
};

enum GameMode { TITLE, PLAYING, GAMEOVER, VICTORY };

struct Game {
    GameMode mode = TITLE;
    std::vector<Enemy>    enemies;
    std::vector<Bullet>   pBullets, eBullets;
    std::vector<Particle> parts;
    std::vector<Ring>     rings;
    std::vector<Warning>  warnings;
    std::vector<Package>  packages;
    std::vector<Lightning> zaps;
    int   weapon = W_PHOTON, wlevel = 0;        // current weapon class and upgrade level (0..3)
    float heat = 0, laserTick = 0, laserTop = 0;
    bool  overheated = false, laserOn = false;
    bool  autoFire = false;                     // selftest: hold the trigger
    std::vector<Meteor>   meteors;
    bool  meteorField = false;
    int   nextMeteorId = 1;
    float fieldT = 0, fieldDur = 30, spawnT = 0, targetDensity = 0.35f;
    Boss                  boss;
    std::vector<Popup>    popups;
    std::vector<Star>     stars;
    Sound sfx[SFX_COUNT];

    Vector2 player{ W / 2.0f, H - 70.0f };
    bool  playerAlive = true;
    float respawnTimer = 0, invuln = 0, fireCooldown = 0;
    bool  shield = false;                      // one-hit force field
    float shieldGrow = 1, shieldGrace = 0, shieldHitFlash = 0;
    int   lives = 3, score = 0, hiScore = 0, stage = 0, nextLifeAt = 20000;
    int   shotsFired = 0, hits = 0;
    float time = 0, stageTimer = 0, bannerTimer = 0, diveTimer = 0, shake = 0;
    bool  paused = false;
    bool  escMenu = false;                      // ESC pause menu open
    int   escSel = 0;                           // highlighted option (0 resume, 1 end game, 2 exit)
    bool  quitRequested = false;                // 'Exit to desktop' chosen
    bool  debugRun = false;                  // started from the Ctrl+L stage select: high score is not recorded
    bool  stageDialog = false;               // Ctrl+L debug stage-select dialog
    std::string dlgText = "1";
    float fireworkT = 0, clearT = 0, cycleT = 0;   // cycleT: time within the current boss cycle (backdrop drift)

    // --- formation geometry -------------------------------------------------
    Vector2 slotPos(const Enemy& e) const {
        float breathe = 1.0f + 0.07f * sinf(time * 1.6f);
        float sway    = 26.0f * sinf(time * 0.7f);
        float spacing = 44.0f * breathe;
        return { W / 2.0f + sway + e.sx * spacing, 110.0f + e.sy * 40.0f * (0.5f + 0.5f * breathe) };
    }
    std::string formationName;
    float difficulty() const { return 1.0f + std::min(1.2f, 0.12f * (stage - 1)); }

    // --- setup --------------------------------------------------------------
    void initStars() {
        stars.clear();
        for (int i = 0; i < 220; ++i) {
            float layer = frand(0, 1);
            const Color cols[] = { WHITE, SKYBLUE, PINK, YELLOW, LIME };
            Color c = cols[GetRandomValue(0, 4)];
            stars.push_back({ { frand(0, W), frand(0, H) }, 30 + layer * 120, frand(0, 6.28f), c });
        }
    }

    void newGame(int startStage = 1) {
        lives = 3; score = 0; stage = std::clamp(startStage, 1, FINAL_STAGE) - 1; nextLifeAt = 20000; shotsFired = hits = 0;
        playerAlive = true; player = { W / 2.0f, H - 70.0f }; invuln = 0;
        pBullets.clear(); eBullets.clear(); parts.clear(); rings.clear(); popups.clear(); meteors.clear(); meteorField = false;
        packages.clear(); zaps.clear(); weapon = W_PHOTON; wlevel = 0; heat = 0; overheated = false;
        mode = PLAYING; shield = false; boss.active = false;
        nextStage();
    }

    void raiseShield() {
        if (shield) return;
        shield = true; shieldGrow = 0; shieldHitFlash = 0;
        PlaySound(sfx[SFX_SHIELD_UP]);
    }

    void nextStage() {
        ++stage;
        if ((stage - 1) % CYCLE == 0) cycleT = 0;
        if (playerAlive) raiseShield();
        enemies.clear(); eBullets.clear(); warnings.clear();
        stageTimer = 0; bannerTimer = 2.2f; diveTimer = 3.0f;
        meteors.clear(); meteorField = false;
        if (stageKind(stage) == SK_BOSS)    { startBoss(); return; }
        if (stageKind(stage) == SK_METEORS) { startMeteorField(); return; }
        PlaySound(sfx[SFX_STAGE]);

        // ---- build this wave: formation shape, new enemy types, entry paths
        int cyc = cycleIndex(), waveInCycle = (stage - 1) % CYCLE, waveNo = cyc * 4 + waveInCycle;
        const Formation& F = FORMATIONS[(waveNo == 0) ? 0 : (waveNo * 3 + 1) % FORMATIONS.size()];
        formationName = F.name;
        struct Slot { float x, y; EType t; };
        std::vector<Slot> slots;
        int nRows = (int)F.rows.size();
        for (int r = 0; r < nRows; ++r) {
            int w = (int)strlen(F.rows[r]);
            for (int c = 0; c < w; ++c) {
                char ch = F.rows[r][c];
                if (ch == '.') continue;
                EType t = ch == 'B' ? BOSS : (ch == 'F' ? BUTTERFLY : BEE);
                slots.push_back({ c - (w - 1) / 2.0f, (float)r, t });
            }
        }
        // New enemy types arrive gradually: a couple at first, then a few more every other wave.
        //   darts from stage 2, shielded from stage 8, splitters from stage 14 (see NEW_TYPE_WAVE)
        auto upgrade = [&](EType from1, EType from2, EType to, int unlock, int maxN) {
            if (waveNo < unlock) return;
            int want = std::min(maxN, 2 + (waveNo - unlock) / 2);
            std::vector<int> pool;
            for (int i = 0; i < (int)slots.size(); ++i) if (slots[i].t == from1 || slots[i].t == from2) pool.push_back(i);
            for (int i = (int)pool.size() - 1; i > 0; --i) std::swap(pool[i], pool[GetRandomValue(0, i)]);
            for (int i = 0; i < want && i < (int)pool.size(); ++i) slots[pool[i]].t = to;
        };
        upgrade(BEE, BEE, DART, NEW_TYPE_WAVE[0], 10);
        upgrade(BUTTERFLY, BUTTERFLY, SHIELDED, NEW_TYPE_WAVE[1], 8);
        upgrade(BEE, BUTTERFLY, SPLITTER, NEW_TYPE_WAVE[2], 8);
        // One Courier per wave from stage 2: takes a central non-boss slot near the top
        if (waveNo >= 1) {
            int best = -1;
            for (int i = 0; i < (int)slots.size(); ++i)
                if (slots[i].t != BOSS && (best < 0 || slots[i].y < slots[best].y - 0.1f ||
                    (fabsf(slots[i].y - slots[best].y) < 0.1f && fabsf(slots[i].x) < fabsf(slots[best].x)))) best = i;
            if (best >= 0) slots[best].t = COURIER;
        }
        // entry groups: sort by row then distance from centre, chunk into 5 groups
        std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
            return a.y != b.y ? a.y < b.y : fabsf(a.x) < fabsf(b.x); });
        int groupsN = 5, per = ((int)slots.size() + groupsN - 1) / groupsN;
        float d = difficulty();
        for (size_t i = 0; i < slots.size(); ++i) {
            int g = (int)i / per, k = (int)i % per;
            Enemy e;
            e.sx = slots[i].x; e.sy = slots[i].y;
            e.col = (int)lroundf(e.sx + 4.5f); e.row = (int)e.sy;
            e.type = slots[i].t;
            e.hp = (e.type == BOSS || e.type == SHIELDED) ? 2 : 1;
            if (e.type == COURIER) {
                e.hp = 8 + std::min(4, cyc);
                // what it carries: 50/50 while on the basic weapon, 80% upgrades once you hold a new weapon
                float pWeapon = weapon == W_PHOTON ? 0.5f : 0.2f;
                if (frand(0, 1) < pWeapon) { do { e.cargo = GetRandomValue(W_SPREAD, W_ION); } while (e.cargo == weapon); }
                else e.cargo = -1;
                e.fireT = frand(2.5f, 3.5f);
            }
            e.maxHp = e.hp;
            e.delay = 1.2f + g * 2.4f / std::min(d, 1.6f) + (k / 2) * 0.15f;
            e.speed = 330 * std::min(d, 1.5f);
            int kind = (waveNo + g * 2) % ENTRY_KINDS;              // every wave mixes different paths
            bool mirror = (g % 2 == 0) ? (k % 2 == 1) : ((g / 2) % 2 == 1);
            e.path = entryPath(kind, mirror);
            e.pos = e.prev = e.path[0];
            enemies.push_back(e);
        }
    }

    static constexpr int ENTRY_KINDS = 6;
    std::vector<Vector2> entryPath(int kind, bool mirror) {
        auto P = [&](float x, float y) { return Vector2{ mirror ? W - x * W : x * W, y * H }; };
        switch (kind) {
            case 0:  // swoop from the top, loop near the bottom
                return buildPath({ P(.45f, -.05f), P(.45f, .30f), P(.05f, .42f), P(.20f, .62f),
                                   P(.35f, .82f), P(.60f, .60f), P(.42f, .45f) });
            case 1:  // sweep in from the side, curl up
                return buildPath({ P(-.08f, .75f), P(.30f, .72f), P(.55f, .45f), P(.38f, .38f),
                                   P(.22f, .32f), P(.15f, .55f), P(.32f, .58f) });
            case 2:  // dive down the middle and turn a full loop-the-loop
                return buildPath({ P(.50f, -.05f), P(.50f, .25f), P(.48f, .45f), P(.50f, .55f),
                                   P(.52f, .70f), P(.80f, .70f), P(.80f, .50f),
                                   P(.80f, .30f), P(.50f, .30f), P(.50f, .45f),
                                   P(.50f, .55f), P(.40f, .50f), P(.38f, .40f) });
            case 3:  // rise from a bottom corner and arc over
                return buildPath({ P(-.08f, 1.02f), P(.25f, .78f), P(.08f, .45f), P(.30f, .38f),
                                   P(.50f, .32f), P(.48f, .60f), P(.36f, .52f) });
            case 4:  // spiral in around the screen
                return buildPath({ P(.50f, -.05f), P(.95f, .12f), P(.95f, .66f), P(.50f, .66f),
                                   P(.08f, .66f), P(.12f, .25f), P(.50f, .25f),
                                   P(.76f, .25f), P(.70f, .50f), P(.50f, .45f) });
            default: // figure-eight weave
                return buildPath({ P(.10f, -.05f), P(.90f, .18f), P(.10f, .34f), P(.90f, .50f),
                                   P(1.05f, .58f), P(.80f, .70f), P(.60f, .55f),
                                   P(.40f, .40f), P(.25f, .55f), P(.40f, .45f) });
        }
    }


    // ========================================================= METEOR FIELD ==
    int cycleIndex() const { return std::clamp((stage - 1) / CYCLE, 0, BOSS_COUNT - 1); }

    void startMeteorField() {
        meteorField = true; fieldT = 0; spawnT = 0;
        fieldDur = 30.0f + 2.0f * cycleIndex();
        targetDensity = std::min(0.45f, 0.30f + 0.017f * cycleIndex());
        bannerTimer = 3.0f;
        PlaySound(sfx[SFX_STAGE]);
    }

    float density() const {   // fraction of the playfield covered by rock
        float a = 0;
        for (const auto& m : meteors)
            if (m.pos.y + m.r() > 0 && m.pos.y - m.r() < H) a += PI * m.r() * m.r();
        return a / (W * H);
    }

    void spawnMeteor() {
        float roll = frand(0, 1);
        bool hot = frand(0, 1) < (getenv("SS_HOT") ? 0.3f : VOLATILE_CHANCE);
        MSize sz = hot ? M_LARGE : (roll < 0.55f ? M_LARGE : (roll < 0.9f ? M_MEDIUM : M_SMALL));
        float r = METEOR_R[sz];
        if (density() + PI * r * r / (W * H) > std::min(targetDensity + 0.05f, MAX_DENSITY)) return;
        for (int tries = 0; tries < 8; ++tries) {          // find a gap so rocks don't spawn stacked
            Vector2 p{ frand(r, W - r), -r - frand(5, 160) };
            bool ok = true;
            for (const auto& m : meteors) if (Vector2Distance(p, m.pos) < r + m.r() + 6) { ok = false; break; }
            if (!ok) continue;
            float base = (sz == M_LARGE ? frand(35, 55) : sz == M_MEDIUM ? frand(50, 80) : frand(70, 105)) * (1 + 0.04f * cycleIndex());
            addMeteor({ p, { frand(-18, 18), base }, frand(0, 360), frand(-40, 40) * (sz == M_LARGE ? 0.5f : 1.0f), 0, sz,
                        hot ? VOLATILE_VARIANT : GetRandomValue(0, 5) });
            return;
        }
    }

    void addMeteor(Meteor m) { m.id = nextMeteorId++; meteors.push_back(m); }

    // Any hit on a rock: volatile rocks detonate, ordinary rocks split
    void hitMeteor(size_t idx, Vector2 hitDir) {
        if (meteors[idx].volatileRock()) detonateMeteor(idx); else breakMeteor(idx, hitDir);
    }

    // Volatile rock explodes: destroyed outright (no fragments), and every rock within
    // BLAST_RADIUS takes a hit exactly like a torpedo; other volatile rocks chain-react.
    void detonateMeteor(size_t idx) {
        Meteor m = meteors[idx];
        meteors.erase(meteors.begin() + idx);
        addScore(200);
        explode(m.pos, Color{ 255, 70, 30, 255 }, Color{ 255, 200, 80, 255 }, 70, 420);
        explode(m.pos, Color{ 255, 40, 40, 255 }, WHITE, 30, 260);
        parts.push_back({ m.pos, { 0, 0 }, 0.45f, 0.45f, BLAST_RADIUS * 0.9f, Color{ 255, 120, 40, 255 }, FLASH });
        rings.push_back({ m.pos, 0.40f, 0.40f, BLAST_RADIUS, Color{ 255, 90, 40, 255 } });
        rings.push_back({ m.pos, 0.28f, 0.28f, BLAST_RADIUS * 0.7f, Color{ 255, 230, 160, 255 } });
        for (int i = 0; i < 18; ++i) {   // red-hot chunks
            float ang = frand(0, 2 * PI), sp = frand(120, 320), life = frand(0.5f, 1.0f);
            parts.push_back({ m.pos, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(3, 6),
                              Color{ 200, 60, 40, 255 }, DEBRIS, frand(0, 360), frand(-700, 700) });
        }
        shake = std::max(shake, 0.45f);
        PlaySound(sfx[SFX_BOOM]); PlaySound(sfx[SFX_IMPACT]);
        popups.push_back({ { m.pos.x - 22, m.pos.y - 10 }, 1.0f, "BOOM!" });

        std::vector<int> caught;
        for (const auto& o : meteors)
            if (Vector2Distance(o.pos, m.pos) < BLAST_RADIUS + o.r() * 0.5f) caught.push_back(o.id);
        for (int id : caught) {
            auto it = std::find_if(meteors.begin(), meteors.end(), [id](const Meteor& o) { return o.id == id; });
            if (it == meteors.end()) continue;
            if (it->volatileRock()) { if (it->fuse <= 0) it->fuse = 0.15f; continue; }   // chain reaction
            breakMeteor((size_t)(it - meteors.begin()), Vector2Subtract(it->pos, m.pos));
        }
    }

    // Torpedo (or shield) hit: split into two smaller rocks, or crumble if already small
    void breakMeteor(size_t idx, Vector2 hitDir) {
        Meteor m = meteors[idx];
        meteors.erase(meteors.begin() + idx);
        addScore(METEOR_PTS[m.size]);
        Color rock{ 150, 125, 100, 255 }, hot{ 255, 150, 60, 255 };
        int chips = 8 + 6 * m.size;
        for (int i = 0; i < chips; ++i) {
            float ang = frand(0, 2 * PI), sp = frand(60, 200), life = frand(0.5f, 1.1f);
            parts.push_back({ Vector2Add(m.pos, { cosf(ang) * m.r() * 0.6f, sinf(ang) * m.r() * 0.6f }), { cosf(ang) * sp, sinf(ang) * sp },
                              life, life, frand(2, 3.5f + m.size * 1.5f), rock, DEBRIS, frand(0, 360), frand(-600, 600) });
        }
        for (int i = 0; i < 3 + m.size * 2; ++i) {
            float ang = frand(0, 2 * PI), sp = frand(10, 40), life = frand(0.6f, 1.2f);
            parts.push_back({ m.pos, { cosf(ang) * sp, sinf(ang) * sp }, life, life, m.r() * frand(0.4f, 0.7f), Color{ 110, 95, 85, 255 }, SMOKE });
        }
        if (m.variant == 1 || m.variant == 4) explode(m.pos, hot, YELLOW, 10, 160);
        PlaySound(sfx[m.size == M_SMALL ? SFX_BOOM : SFX_HIT]);
        if (m.size == M_SMALL) return;
        MSize child = (MSize)(m.size - 1);
        Vector2 side = Vector2Normalize({ -hitDir.y, hitDir.x });
        if (Vector2Length(side) < 0.1f) side = { 1, 0 };
        for (int k = -1; k <= 1; k += 2) {
            float push = frand(45, 80);
            Vector2 v = Vector2Add(m.vel, Vector2Add(Vector2Scale(side, k * push), { 0, frand(5, 25) }));
            addMeteor({ Vector2Add(m.pos, Vector2Scale(side, k * METEOR_R[child] * 0.8f)), v, frand(0, 360),
                        frand(-90, 90), 0.12f, child, m.variant });
        }
    }

    void updateMeteors(float dt) {
        if (!meteorField && meteors.empty()) return;
        fieldT += dt;
        if (meteorField && fieldT < fieldDur && mode == PLAYING) {
            spawnT -= dt;
            if (spawnT <= 0) { spawnT = 0.12f; if (density() < targetDensity) spawnMeteor(); }
        }
        for (auto& m : meteors) {
            m.pos = Vector2Add(m.pos, Vector2Scale(m.vel, dt));
            m.vel.x *= 1 - 0.3f * dt;                      // fragments settle back into the drift
            if (fieldT > fieldDur) m.vel.y += 40 * dt;      // field over: the stragglers speed past
            m.rot += m.spin * dt;
            m.flash = std::max(0.0f, m.flash - dt);
            if (m.pos.x < m.r() * 0.5f && m.vel.x < 0) m.vel.x = -m.vel.x;     // stay on screen sideways
            if (m.pos.x > W - m.r() * 0.5f && m.vel.x > 0) m.vel.x = -m.vel.x;
        }
        meteors.erase(std::remove_if(meteors.begin(), meteors.end(), [](const Meteor& m) { return m.pos.y - m.r() > H + 10; }), meteors.end());
        std::vector<int> due;                                // chain-reaction detonations
        for (auto& m : meteors) if (m.fuse > 0 && (m.fuse -= dt) <= 0) due.push_back(m.id);
        for (int id : due) {
            auto it = std::find_if(meteors.begin(), meteors.end(), [id](const Meteor& o) { return o.id == id; });
            if (it != meteors.end()) detonateMeteor((size_t)(it - meteors.begin()));
        }
        if (meteorField && fieldT >= fieldDur && meteors.empty() && mode == PLAYING) {
            meteorField = false;
            int bonus = 1000 * (cycleIndex() + 1);
            addScore(bonus);
            popups.push_back({ { W / 2.0f - 100, H / 2.0f }, 2.0f, TextFormat("FIELD CLEARED  +%d", bonus) });
            PlaySound(sfx[SFX_LIFE]);
        }
    }

    void drawMeteors() {
        for (const auto& m : meteors) {
            float S = m.r() * 2 / 0.8f;
            if (m.volatileRock()) {          // soft, slowly breathing ember halo behind the rock
                float pl = 0.5f + 0.5f * sinf(time * 3.5f + m.id);
                BeginBlendMode(BLEND_ADDITIVE);
                drawGlow(m.pos, m.r() * (1.55f + 0.12f * pl), Color{ 255, 120, 70, (unsigned char)(45 + 40 * pl) });
                drawGlow(m.pos, m.r() * 1.15f, Color{ 255, 150, 90, (unsigned char)(25 + 20 * pl) });
                EndBlendMode();
            }
            drawShip(gfx.meteor[m.variant], m.pos, S, m.rot, true);
            if (m.volatileRock()) {
                float pl = 0.5f + 0.5f * sinf(time * 3.5f + m.id);
                bool armed = m.fuse > 0;
                BeginBlendMode(BLEND_ADDITIVE);
                const Texture2D& t = gfx.meteor[m.variant];
                Color tint = armed && fmodf(time, 0.06f) < 0.03f ? Color{ 255, 255, 255, 230 } : Color{ 255, 140, 90, (unsigned char)(18 + 22 * pl) };
                DrawTexturePro(t, { 0, 0, (float)t.width, (float)t.height }, { m.pos.x, m.pos.y, S, S }, { S / 2, S / 2 }, m.rot, tint);
                EndBlendMode();
            }
            if (m.flash > 0) {
                BeginBlendMode(BLEND_ADDITIVE);
                const Texture2D& t = gfx.meteor[m.variant];
                DrawTexturePro(t, { 0, 0, (float)t.width, (float)t.height }, { m.pos.x, m.pos.y, S, S }, { S / 2, S / 2 }, m.rot,
                               Color{ 255, 220, 180, (unsigned char)(200 * m.flash / 0.12f) });
                EndBlendMode();
            }
        }
    }

    // ============================================================= BOSSES ==
    void startBoss() {
        boss = Boss{};
        boss.active = true; boss.state = B_ENTER;
        boss.index = std::clamp(stage / CYCLE - 1, 0, BOSS_COUNT - 1);
        gfx.ensureBoss(boss.index);
        boss.maxHp = boss.hp = BOSS_HP[boss.index];
        boss.pos = { W / 2.0f, -boss.size() * 0.6f };
        bannerTimer = 3.5f;
        PlaySound(sfx[SFX_ALARM]);
    }

    void bossShoot(Vector2 from, float angleDeg, float speed, int kind = 1) {
        float a = angleDeg * DEG2RAD;
        eBullets.push_back({ from, { cosf(a) * speed, sinf(a) * speed }, kind });
    }

    // Muzzle flash: bright pop + small shock ring at a gun when it fires
    void muzzleFlash(Vector2 at, Color c, float size, bool ring = true) {
        parts.push_back({ at, { 0, 0 }, 0.12f, 0.12f, size, Color{ 255, 250, 235, 255 }, FLASH });
        parts.push_back({ at, { 0, 0 }, 0.2f, 0.2f, size * 1.4f, c, FLASH });
        if (ring) rings.push_back({ at, 0.22f, 0.22f, size * 1.3f, c });
    }
    Color coreColor(unsigned char a = 255) const { return bossColor(boss.D().core, a); }

    // Scythe Beam: the bottom of the beam trails behind the boss's motion
    float beamLean(Vector2 o) const {
        if (boss.attack != A_SWEEP) return 0;
        float off = -std::clamp(boss.vx * 0.35f, -90.0f, 90.0f);
        return atan2f(off, H - o.y);
    }
    float beamXAt(Vector2 o, float y) const { return o.x + tanf(beamLean(o)) * (y - o.y); }

    void spawnMinion(Vector2 at, EType type) {
        // hangar hatch opens: glow + ring, then the fighter grows out of it with an engine burst
        muzzleFlash(at, coreColor(), 26);
        for (int i = 0; i < 10; ++i) {
            float ang = frand(PI * 0.25f, PI * 0.75f), sp = frand(80, 200), life = frand(0.2f, 0.4f);
            parts.push_back({ at, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1.2f, 2.2f), Color{ 120, 200, 255, 255 }, SPARK });
        }
        Enemy e;
        e.type = type; e.hp = type == BOSS ? 2 : 1; e.col = 0; e.row = 3; e.sy = 3; e.minion = true; e.grow = 0;
        e.pos = e.prev = at;
        enemies.push_back(e);
        startDive(enemies.back());
    }

    bool laserFiring() const { return boss.active && boss.step == 1 && (boss.attack == A_LASER || boss.attack == A_TWINLASER || boss.attack == A_SWEEP); }

    void updateBoss(float dt) {
        Boss& b = boss;
        if (!b.active) return;
        b.t += dt;
        b.flash = std::max(0.0f, b.flash - dt);
        float hover = 150 + b.size() * 0.12f;

        if (b.state == B_ENTER) {
            b.pos.y += (hover - b.pos.y) * std::min(1.0f, dt * 1.4f);
            if (b.pos.y > hover - 5) { b.state = B_FIGHT; b.attackTimer = 1.2f; }
            return;
        }
        if (b.state == B_DYING) {
            b.dyingT += dt; b.emitT -= dt; shake = std::max(shake, 0.35f);
            float half = b.size() * 0.4f;
            if (b.emitT <= 0) {
                b.emitT = 0.09f;
                Vector2 p{ b.pos.x + frand(-half, half), b.pos.y + frand(-half * 0.5f, half * 0.6f) };
                Color c = bossColor(b.D().core);
                explode(p, GetRandomValue(0, 1) ? ORANGE : c, WHITE, 22, 220);
                PlaySound(sfx[SFX_BOOM]);
            }
            b.pos.y += 12 * dt;
            if (b.dyingT > 2.6f + 0.15f * b.index) {   // final detonation
                for (int i = 0; i < 4; ++i) explode({ b.pos.x + frand(-60, 60), b.pos.y + frand(-20, 20) }, ORANGE, YELLOW, 60, 420);
                explode(b.core(), Color{ 255, 80, 220, 255 }, WHITE, 80, 520);
                rings.push_back({ b.core(), 0.9f, 0.9f, 260, Color{ 255, 120, 220, 255 } });
                rings.push_back({ b.core(), 0.6f, 0.6f, 160, Color{ 255, 230, 180, 255 } });
                shake = 1.0f;
                if (weapon != W_PHOTON || wlevel > 0) {   // after every boss the arsenal resets to the basic weapon
                    weapon = W_PHOTON; wlevel = 0; heat = 0; overheated = false;
                    popups.push_back({ { W / 2.0f - 120, b.pos.y + 40 }, 2.5f, "SYSTEMS RESET - PHOTON TORPEDO" });
                }
                int bonus = 5000 * (b.index + 1);
                addScore(bonus);
                popups.push_back({ { W / 2.0f - 130, b.pos.y }, 2.5f, TextFormat("%s DESTROYED  +%d", b.D().name, bonus) });
                PlaySound(sfx[SFX_BIGBOOM]);
                b.active = false;
                if (b.index == BOSS_COUNT - 1) { mode = VICTORY; stageTimer = 0; fireworkT = 0.5f; saveHiScore(); }
            }
            return;
        }

        // ---- movement (holds still for fixed lasers; races during the sweep)
        bool still = b.step == 1 && (b.attack == A_LASER || b.attack == A_TWINLASER);
        if (!still) b.moveT += dt * (1 + 0.3f * b.phase()) * (b.attack == A_SWEEP && b.step == 1 ? 1.8f : 1.0f);
        float range = W / 2.0f - b.size() * 0.42f, oldX = b.pos.x;
        b.pos.x = W / 2.0f + sinf(b.moveT * (0.5f + 0.03f * b.index)) * range;
        if (dt > 0) b.vx += ((b.pos.x - oldX) / dt - b.vx) * std::min(1.0f, dt * 8);
        b.ignite = std::max(0.0f, b.ignite - dt);
        b.pos.y = hover + sinf(b.t * 1.3f) * 10;

        // ---- damage smoke/sparks as the hull degrades
        b.emitT -= dt;
        if (b.emitT <= 0 && b.hp < b.maxHp * 0.7f) {
            b.emitT = b.phase() == 2 ? 0.05f : 0.12f;
            float half = b.size() * 0.32f;
            Vector2 p{ b.pos.x + frand(-half, half), b.pos.y + frand(-half * 0.4f, half * 0.4f) };
            if (b.hitTest(p)) {
                float life = frand(0.8f, 1.5f);
                parts.push_back({ p, { frand(-10, 10), frand(-50, -20) }, life, life, frand(8, 14), Color{ 60, 55, 65, 255 }, SMOKE });
                float l2 = frand(0.2f, 0.4f);
                parts.push_back({ p, { frand(-80, 80), frand(-60, 60) }, l2, l2, frand(1, 2), Color{ 255, 160, 60, 255 }, SPARK });
            }
        }

        if (mode != PLAYING || !playerAlive) { if (!laserFiring()) b.attack = A_NONE; }
        float diff = difficulty() * (0.9f + 0.06f * b.index), ph = (float)b.phase(), ag = b.aggr();
        int extra = b.index / 3;

        // ---- attack scheduler
        if (b.attack == A_NONE) {
            b.attackTimer -= dt;
            if (b.attackTimer <= 0 && mode == PLAYING && playerAlive) {
                const auto& pool = BOSS_ATTACKS[b.index];
                BossAttack a = pool[GetRandomValue(0, (int)pool.size() - 1)];
                for (int tries = 0; tries < 6 && a == b.lastAttack; ++tries) a = pool[GetRandomValue(0, (int)pool.size() - 1)];
                b.attack = a; b.lastAttack = a; b.attackT = 0; b.step = 0; b.fireT = 0;
            }
            return;
        }

        b.attackT += dt; b.fireT -= dt;
        switch (b.attack) {
            case A_SPREAD: {               // fans of plasma from the core
                int volleys = 2 + b.phase() + extra / 2;
                if (b.fireT <= 0 && b.step < volleys) {
                    int n = 7 + 2 * b.phase() + extra;
                    float spread = 80 + 6 * b.index, base = 90 + (b.step % 2 ? spread / (2 * (n - 1)) : 0);
                    for (int i = 0; i < n; ++i) bossShoot(b.core(), base - spread / 2 + spread * i / (n - 1), 200 * diff, BK_ORB);
                    muzzleFlash(b.core(), coreColor(), 34);
                    PlaySound(sfx[SFX_DIVE]);
                    ++b.step; b.fireT = 0.45f / ag;
                } else if (b.step >= volleys && b.fireT <= 0) b.attack = A_NONE;
                break;
            }
            case A_BURST: {                // turrets fire aimed bursts
                int shots = 8 + 4 * b.phase() + 2 * extra;
                if (b.fireT <= 0 && b.step < shots) {
                    Vector2 w = b.turret(b.step);
                    Vector2 d = Vector2Subtract(player, w);
                    bossShoot(w, atan2f(d.y, d.x) * RAD2DEG + frand(-4, 4), 330 * diff, BK_BOLT);
                    muzzleFlash(w, Color{ 255, 210, 110, 255 }, 14, false);
                    { float life = frand(0.4f, 0.7f); parts.push_back({ w, { frand(-15, 15), frand(-30, -10) }, life, life, 5, Color{ 90, 85, 95, 255 }, SMOKE }); }
                    ++b.step; b.fireT = 0.13f / ag;
                } else if (b.step >= shots && b.fireT <= 0) b.attack = A_NONE;
                break;
            }
            case A_SPIRAL: {               // rotating multi-arm spiral
                int arms = 2 + b.phase() + extra / 2;
                if (b.fireT <= 0) {
                    float dir = b.index % 2 ? -1.f : 1.f;
                    for (int k = 0; k < arms; ++k) {
                        bossShoot(b.core(), dir * b.attackT * (160 + 10 * b.index) + k * 360.0f / arms, 170 * diff, BK_SPIRAL);
                        eBullets.back().aux = (float)k / arms + b.attackT * 0.25f;   // colour shifts along each arm
                    }
                    muzzleFlash(b.core(), coreColor(), 14, false);
                    b.fireT = 0.085f / std::sqrt(ag);
                }
                if (b.attackT > 2.4f) b.attack = A_NONE;
                break;
            }
            case A_RING: {                 // expanding rings in every direction
                int rings_ = 2 + b.phase();
                if (b.fireT <= 0 && b.step < rings_) {
                    int n = 16 + 2 * b.index;
                    float off = b.step * 180.0f / n;
                    for (int i = 0; i < n; ++i) bossShoot(b.core(), off + i * 360.0f / n, 160 * diff, BK_CRYSTAL);
                    rings.push_back({ b.core(), 1.6f, 1.6f, 160 * diff * 1.6f, coreColor(150) });   // shockwave travels with the ring
                    muzzleFlash(b.core(), coreColor(), 40);
                    PlaySound(sfx[SFX_DIVE]);
                    ++b.step; b.fireT = 0.55f / ag;
                } else if (b.step >= rings_ && b.fireT <= 0) b.attack = A_NONE;
                break;
            }
            case A_HOMING: {               // slow seeker missiles from the turrets
                int n = 2 + b.phase() + extra;
                if (b.fireT <= 0 && b.step < n) {
                    Vector2 w = b.turret(b.step);
                    bossShoot(w, 90 + frand(-50, 50), 150, BK_MISSILE);
                    muzzleFlash(w, Color{ 255, 150, 60, 255 }, 18);
                    PlaySound(sfx[SFX_DIVE]);
                    ++b.step; b.fireT = 0.35f;
                } else if (b.step >= n && b.fireT <= 0) b.attack = A_NONE;
                break;
            }
            case A_RAIN: {                 // meteor storm from the top of the screen
                if (b.fireT <= 0) {                // warn first: a red marker shows where each rock will enter
                    warnings.push_back({ frand(14, W - 14.0f), 0.5f });
                    b.fireT = 0.09f / ag;
                }
                if (b.attackT > 2.6f + 0.4f * ph) b.attack = A_NONE;
                break;
            }
            case A_LASER: case A_TWINLASER: case A_SWEEP: {   // charge, then fire heavy beams
                const float charge = std::max(0.7f, 1.1f - 0.03f * b.index), fire = b.attack == A_SWEEP ? 2.2f : 1.4f;
                if (b.step == 0) {                 // charging: energy streams into the gun
                    for (Vector2 o : b.beams())
                        for (int q = 0; q < 2; ++q) {
                            float ang = frand(0, 2 * PI), r = frand(30, 70), life = frand(0.18f, 0.3f);
                            Vector2 st{ o.x + cosf(ang) * r, o.y + sinf(ang) * r };
                            parts.push_back({ st, Vector2Scale(Vector2Subtract(o, st), 1.0f / life), life, life, frand(1, 2), coreColor(), SPARK });
                        }
                }
                if (b.step == 0 && b.attackT > charge) {
                    b.step = 1; b.ignite = 0.3f; PlaySound(sfx[SFX_LASER]); shake = std::max(shake, 0.45f);
                    for (Vector2 o : b.beams()) muzzleFlash(o, coreColor(), 50);
                }
                if (b.step == 1) {
                    for (Vector2 o : b.beams()) {
                        float bx = beamXAt(o, player.y), ex = beamXAt(o, (float)H);
                        if (playerAlive && invuln <= 0 && shieldGrace <= 0 && fabsf(player.x - bx) < 13 + (shield ? SHIELD_RADIUS * 0.7f : 9)) {
                            if (shield) breakShield({ player.x, player.y - SHIELD_RADIUS }); else killPlayer();
                        }
                        if (GetRandomValue(0, 1)) {
                            float life = frand(0.15f, 0.3f), yy = frand(o.y, (float)H);
                            parts.push_back({ { beamXAt(o, yy) + frand(-10, 10), yy }, { frand(-120, 120), frand(-40, 40) },
                                              life, life, frand(1, 2), coreColor(), SPARK });
                        }
                        for (int q = 0; q < 2; ++q) {     // impact splash where the beam hits the bottom
                            float ang = frand(PI * 1.1f, PI * 1.9f), sp = frand(120, 320), life = frand(0.2f, 0.45f);
                            float bias = b.attack == A_SWEEP ? (b.vx > 0 ? -1.f : 1.f) * 120 : 0;   // sweep throws sparks behind it
                            parts.push_back({ { ex, (float)H - 4 }, { cosf(ang) * sp + bias, sinf(ang) * sp }, life, life, frand(1.2f, 2.4f),
                                              GetRandomValue(0, 1) ? coreColor() : Color{ 255, 220, 160, 255 }, SPARK });
                        }
                        if (b.attack == A_SWEEP && GetRandomValue(0, 1))   // glowing burn line left along the bottom
                            parts.push_back({ { ex + frand(-4, 4), (float)H - 3 }, { 0, 0 }, 1.1f, 1.1f, 9, Color{ 255, 120, 40, 255 }, EMBER });
                    }
                }
                if (b.attackT > charge + fire) { b.attack = A_NONE; b.step = 0; }
                break;
            }
            case A_SPAWN: {                // launch escort fighters
                int n = 3 + b.phase() + extra;
                if (b.fireT <= 0 && b.step < n) {
                    EType t = b.index >= 6 && b.step % 4 == 3 ? BOSS : (b.step % 3 == 2 ? BUTTERFLY : BEE);
                    spawnMinion(b.turret(b.step), t);
                    ++b.step; b.fireT = 0.3f;
                } else if (b.step >= n) b.attack = A_NONE;
                break;
            }
            default: b.attack = A_NONE; break;
        }
        if (b.attack == A_NONE) b.attackTimer = (1.5f - 0.35f * ph) / std::min(diff, 1.6f) / ag;
    }

    void damageBoss(Vector2 at) {
        Boss& b = boss;
        b.hp -= 1; b.flash = 0.07f;
        addScore(10);
        if (b.hp <= 0) {
            b.state = B_DYING; b.dyingT = 0; b.emitT = 0; b.attack = A_NONE; b.step = 0;
            for (auto& e : enemies) if (e.state != DEAD && e.minion) { e.state = DEAD; explode(e.pos, ORANGE, WHITE, 20, 180); }
            for (auto& eb : eBullets) explode(eb.pos, PURPLE, WHITE, 3, 60);
            eBullets.clear();
            PlaySound(sfx[SFX_DEATH]);
        }
        (void)at;
    }

    Color bossColor(Vector3 c, unsigned char a = 255) const {
        return { (unsigned char)(c.x * 255), (unsigned char)(c.y * 255), (unsigned char)(c.z * 255), a };
    }
    Color spiralColor(float aux) const {
        Vector3 hsv = ColorToHSV(coreColor());
        return ColorFromHSV(fmodf(hsv.x + aux * 140.0f + 360.0f, 360.0f), 0.65f, 1.0f);
    }

    // Heavy beam: charge-up targeting line, then a wavering, energised beam with an impact splash
    void drawBeam(Vector2 m, bool firing, float charge) {
        Color core = coreColor();
        float lean = beamLean(m);
        Vector2 dir{ sinf(lean), cosf(lean) };          // beam axis (straight down unless sweeping)
        float L = (H - m.y) / dir.y;
        if (!firing) {
            float pulse = 0.5f + 0.5f * sinf(time * 30);
            drawGlow(m, 8 + 30 * charge, Color{ core.r, core.g, core.b, (unsigned char)(120 + 135 * charge) });
            drawGlow(m, 4 + 10 * charge, Color{ 255, 250, 240, (unsigned char)(200 * charge) });
            float wline = 1.0f + 2.5f * charge * pulse, dash = 16, gap = 8, off = fmodf(time * 320, dash + gap);
            for (float y = -dash + off; y < L; y += dash + gap) {
                float y0 = std::max(0.0f, y), y1 = std::min(L, y + dash);
                if (y1 <= y0) continue;
                DrawLineEx(Vector2Add(m, Vector2Scale(dir, y0)), Vector2Add(m, Vector2Scale(dir, y1)), wline,
                           Color{ 255, 70, 110, (unsigned char)(100 + 140 * charge) });
            }
            return;
        }
        Vector2 perp{ dir.y, -dir.x };
        const float step = 6;
        int n = (int)(L / step) + 1;
        auto layer = [&](float wMul, Color col, bool pulse) {
            for (int i = 0; i < n; ++i) {
                float y0 = i * step, y1 = std::min(L, y0 + step);
                auto W_ = [&](float y) { return 17 * wMul * (1 + 0.12f * sinf(y * 0.07f + time * 40) + 0.07f * sinf(y * 0.19f - time * 25)); };
                Vector2 c0 = Vector2Add(m, Vector2Scale(dir, y0)), c1 = Vector2Add(m, Vector2Scale(dir, y1));
                float w0 = W_(y0) / 2, w1 = W_(y1) / 2;
                Color cc = col;
                if (pulse) { float e = 0.5f + 0.5f * sinf(y0 * 0.045f - time * 30); cc.a = (unsigned char)std::min(255.0f, col.a * (0.75f + 0.35f * e)); }
                Vector2 l0 = Vector2Add(c0, Vector2Scale(perp, w0)), r0 = Vector2Subtract(c0, Vector2Scale(perp, w0));
                Vector2 l1 = Vector2Add(c1, Vector2Scale(perp, w1)), r1 = Vector2Subtract(c1, Vector2Scale(perp, w1));
                DrawTriangle(l0, r0, r1, cc); DrawTriangle(l0, r1, l1, cc);
                DrawTriangle(l0, r1, r0, cc); DrawTriangle(l0, l1, r1, cc);   // either winding
            }
        };
        layer(3.0f, Color{ core.r, core.g, core.b, 50 }, true);
        layer(1.0f, Color{ core.r, core.g, core.b, 200 }, true);
        layer(0.38f, Color{ 255, 250, 255, 255 }, false);
        // muzzle flare with rotating spikes
        drawGlow(m, 48, core);
        drawGlow(m, 20, Color{ 255, 255, 255, 230 });
        for (int k = 0; k < 2; ++k)
            DrawRectanglePro({ m.x, m.y, 90, 3 }, { 45, 1.5f }, time * 200 + k * 90, Color{ core.r, core.g, core.b, 160 });
        // impact at the bottom
        Vector2 end = Vector2Add(m, Vector2Scale(dir, L));
        drawGlow(end, 46, Color{ core.r, core.g, core.b, 220 });
        drawGlow(end, 22, Color{ 255, 240, 220, 230 });
        if (boss.ignite > 0) drawGlow(m, 160 * (boss.ignite / 0.3f), Color{ 255, 255, 255, (unsigned char)(200 * boss.ignite / 0.3f) });
    }

    void drawBoss() {
        const Boss& b = boss;
        if (!b.active) return;
        const auto& D = b.D();
        Vector2 p = b.pos;
        if (b.state == B_DYING) p = Vector2Add(p, { frand(-4, 4), frand(-3, 3) });
        float S = b.size();
        Color eng = bossColor(D.engine);
        // engine plumes (exhaust points up, away from the player)
        BeginBlendMode(BLEND_ADDITIVE);
        for (Vector2 e : D.engines) {
            Vector2 n{ p.x + e.x * S / 2, p.y + (e.y - 0.03f) * S / 2 };
            float fl = 0.85f + 0.15f * sinf(time * 45 + e.x * 10);
            drawGlow(n, 20 * fl, Color{ eng.r, eng.g, eng.b, 160 });
            DrawTriangle({ n.x + 8, n.y }, { n.x, n.y - 30 * fl }, { n.x - 8, n.y }, Color{ eng.r, eng.g, eng.b, 140 });
        }
        EndBlendMode();
        Color tint = WHITE;
        if (b.state == B_DYING && fmodf(time, 0.12f) < 0.06f) tint = Color{ 255, 190, 190, 255 };
        const Texture2D& tex = gfx.bossTex[b.index];
        drawShip(tex, p, S, 0, true, tint);

        BeginBlendMode(BLEND_ADDITIVE);
        if (b.flash > 0)
            DrawTexturePro(tex, { 0, 0, (float)tex.width, (float)tex.height }, { p.x, p.y, S, S }, { S / 2, S / 2 }, 0,
                           Color{ 255, 255, 255, (unsigned char)(150 * b.flash / 0.07f) });
        float cp = 0.7f + 0.3f * sinf(time * 6);
        drawGlow({ p.x + D.coreP.x * S / 2, p.y + D.coreP.y * S / 2 }, 30 * cp * (1 + 0.3f * b.phase()), bossColor(D.core, 150));
        if (b.attack == A_SPIRAL) {            // spinning sigil at the core while the spiral fires
            Vector2 cc{ p.x + D.coreP.x * S / 2, p.y + D.coreP.y * S / 2 };
            float sp = (b.index % 2 ? -1.f : 1.f) * time * 360;
            for (int k = 0; k < 3; ++k) {
                DrawRing(cc, 22, 26, sp + k * 120, sp + k * 120 + 70, 12, coreColor(200));
                DrawRing(cc, 32, 34, -sp * 0.6f + k * 120, -sp * 0.6f + k * 120 + 40, 10, coreColor(140));
            }
        }
        if (b.attack == A_LASER || b.attack == A_TWINLASER || b.attack == A_SWEEP) {
            float charge = std::min(1.0f, b.attackT / std::max(0.7f, 1.1f - 0.03f * b.index));
            for (Vector2 o : b.beams()) drawBeam(o, b.step == 1, charge);
        }
        EndBlendMode();
    }

    void startDive(Enemy& e) {
        float dir = e.pos.x < W / 2 ? -1.f : 1.f;
        float tx = Clamp(player.x + frand(-60, 60), 40, W - 40);
        Vector2 p = e.pos;
        e.path = buildPath({
            p, { p.x + dir * 70, p.y - 70 }, { p.x + dir * 120, p.y + 30 }, { p.x + dir * 40, p.y + 110 },
            { p.x - dir * 20, p.y + 190 }, { tx + dir * 160, H * 0.55f }, { tx, H * 0.72f },
            { tx - dir * 70, H * 0.86f }, { tx - dir * 140, H * 0.95f }, { tx - dir * 90, H + 60.0f } });
        if (e.type == DART) {                 // darts: short wind-up, then a near-straight strike
            e.path = buildPath({ p, { p.x, p.y - 30 }, { tx, H * 0.45f }, { tx + frand(-40, 40), H + 60.0f } });
        }
        e.seg = 0; e.state = DIVING;
        e.speed = (e.type == BOSS ? 280 : e.type == DART ? 520 : e.type == MINI ? 380 : 320) * difficulty();
        e.shotsLeft = 1 + (stage >= 3 ? GetRandomValue(0, 2) : GetRandomValue(0, 1));
        e.shootTimer = frand(0.3f, 0.8f);
        PlaySound(sfx[SFX_DIVE]);
    }

    // --- effects ------------------------------------------------------------
    void explode(Vector2 p, Color a, Color b, int n, float power) {
        for (int i = 0; i < n; ++i) {
            float ang = frand(0, 2 * PI), sp = frand(40, power);
            float life = frand(0.3f, 0.9f);
            parts.push_back({ p, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1.5f, 3.5f),
                              GetRandomValue(0, 1) ? a : b, SPARK });
        }
        float big = std::min(1.5f, n / 28.0f);
        parts.push_back({ p, { 0, 0 }, 0.35f, 0.35f, 46 * big, Color{ 255, 230, 180, 255 }, FLASH });
        parts.push_back({ p, { 0, 0 }, 0.22f, 0.22f, 26 * big, a, FLASH });
        for (int i = 0; i < (int)(6 * big); ++i) {
            float ang = frand(0, 2 * PI), sp = frand(10, 50), life = frand(0.7f, 1.4f);
            parts.push_back({ Vector2Add(p, { frand(-6, 6), frand(-6, 6) }), { cosf(ang) * sp, sinf(ang) * sp }, life, life,
                              frand(10, 18) * big, Color{ 70, 60, 75, 255 }, SMOKE });
        }
        for (int i = 0; i < (int)(8 * big); ++i) {
            float ang = frand(0, 2 * PI), sp = frand(60, power * 0.8f), life = frand(0.5f, 1.0f);
            parts.push_back({ p, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(2, 4), b, DEBRIS,
                              frand(0, 360), frand(-720, 720) });
        }
        if (n >= 20) rings.push_back({ p, 0.45f, 0.45f, 40 * big, a });
    }

    // Small photon-torpedo detonation at the point of impact
    void torpedoImpact(Vector2 p) {
        parts.push_back({ p, { 0, 0 }, 0.18f, 0.18f, 34, Color{ 255, 245, 220, 255 }, FLASH });
        parts.push_back({ p, { 0, 0 }, 0.38f, 0.38f, 30, Color{ 255, 110, 40, 255 }, FLASH });
        rings.push_back({ p, 0.32f, 0.32f, 30, Color{ 255, 140, 60, 255 } });
        for (int i = 0; i < 16; ++i) {
            float ang = frand(0, 2 * PI), sp = frand(120, 340), life = frand(0.15f, 0.4f);
            parts.push_back({ p, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1.2f, 2.4f),
                              GetRandomValue(0, 1) ? Color{ 255, 120, 50, 255 } : Color{ 255, 230, 150, 255 }, SPARK });
        }
        PlaySound(sfx[SFX_IMPACT]);
    }

    void addScore(int pts) {
        score += pts;
        if (score > hiScore && !debugRun) hiScore = score;
        if (score >= nextLifeAt) {
            ++lives; nextLifeAt += 50000; PlaySound(sfx[SFX_LIFE]);
            popups.push_back({ { player.x, player.y - 40 }, 1.5f, "1UP!" });
        }
    }

    // Force field shatters: shards, flash, shockwave; brief grace period so one volley can't chain-kill
    void breakShield(Vector2 at) {
        shield = false; shieldGrace = 0.6f; shake = 0.25f;
        Color cy{ 90, 210, 255, 255 }, wh{ 210, 245, 255, 255 };
        parts.push_back({ at, { 0, 0 }, 0.2f, 0.2f, 26, wh, FLASH });
        parts.push_back({ player, { 0, 0 }, 0.35f, 0.35f, SHIELD_RADIUS * 1.8f, cy, FLASH });
        rings.push_back({ player, 0.45f, 0.45f, SHIELD_RADIUS * 2.2f, cy });
        rings.push_back({ player, 0.30f, 0.30f, SHIELD_RADIUS * 1.4f, wh });
        for (int i = 0; i < 36; ++i) {            // glassy shards flying off the bubble
            float ang = frand(0, 2 * PI), sp = frand(90, 260), life = frand(0.4f, 0.9f);
            Vector2 o{ player.x + cosf(ang) * SHIELD_RADIUS, player.y + sinf(ang) * SHIELD_RADIUS };
            parts.push_back({ o, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(2.5f, 5), GetRandomValue(0, 1) ? cy : wh,
                              DEBRIS, frand(0, 360), frand(-900, 900) });
        }
        for (int i = 0; i < 24; ++i) {
            float ang = frand(0, 2 * PI), sp = frand(150, 380), life = frand(0.2f, 0.5f);
            parts.push_back({ at, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1.2f, 2.4f), cy, SPARK });
        }
        PlaySound(sfx[SFX_SHIELD_BREAK]);
    }

    void killPlayer() {
        shield = false;
        // losing a life costs one upgrade level; with none left you drop back to the basic Photon Torpedo
        if (wlevel > 0) --wlevel; else weapon = W_PHOTON;
        heat = 0; overheated = false; laserOn = false;
        playerAlive = false; respawnTimer = 2.5f; shake = 0.6f; --lives;
        explode(player, WHITE, RED, 80, 320);
        explode(player, YELLOW, ORANGE, 40, 200);
        PlaySound(sfx[SFX_DEATH]);
    }


    // ---- Ctrl+L debug stage-select dialog -------------------------------------
    int dlgValue() const { int v = dlgText.empty() ? 0 : atoi(dlgText.c_str()); return v; }
    bool dlgValid() const { int v = dlgValue(); return v >= 1 && v <= FINAL_STAGE; }
    void dlgSet(int v) { dlgText = std::to_string(std::clamp(v, 1, FINAL_STAGE)); }
    Vector2 mouseLogical() const { Vector2 m = GetMousePosition(); return { (m.x - origin.x) / scale, (m.y - origin.y) / scale }; }
    // dialog layout (logical coords)
    Rectangle dlgBox() const    { return { 70, 230, W - 140.0f, 370 }; }
    Rectangle dlgMinus5() const { return { 120, 330, 52, 46 }; }
    Rectangle dlgMinus() const  { return { 180, 330, 46, 46 }; }
    Rectangle dlgField() const  { return { 236, 326, 128, 54 }; }
    Rectangle dlgPlus() const   { return { 374, 330, 46, 46 }; }
    Rectangle dlgPlus5() const  { return { 428, 330, 52, 46 }; }
    Rectangle dlgStart() const  { return { 150, 494, 140, 46 }; }
    Rectangle dlgCancel() const { return { 310, 494, 140, 46 }; }

    void startFromDialog() {
        if (!dlgValid()) return;
        stageDialog = false; debugRun = true;
        newGame(dlgValue());
    }

    void updateStageDialog() {
        for (int c = GetCharPressed(); c; c = GetCharPressed())
            if (c >= '0' && c <= '9' && dlgText.size() < 2) { if (dlgText == "0") dlgText.clear(); dlgText.push_back((char)c); }
        if (IsKeyPressed(KEY_BACKSPACE) && !dlgText.empty()) dlgText.pop_back();
        if (IsKeyPressed(KEY_UP)    || IsKeyPressedRepeat(KEY_UP))    dlgSet(dlgValue() + 1);
        if (IsKeyPressed(KEY_DOWN)  || IsKeyPressedRepeat(KEY_DOWN))  dlgSet(dlgValue() - 1);
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) dlgSet((dlgValue() / CYCLE + 1) * CYCLE);
        if (IsKeyPressed(KEY_LEFT)  || IsKeyPressedRepeat(KEY_LEFT))  dlgSet(((dlgValue() - 1) / CYCLE) * CYCLE);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) startFromDialog();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 m = mouseLogical();
            if (CheckCollisionPointRec(m, dlgMinus5())) dlgSet(((dlgValue() - 1) / CYCLE) * CYCLE);
            if (CheckCollisionPointRec(m, dlgMinus()))  dlgSet(dlgValue() - 1);
            if (CheckCollisionPointRec(m, dlgPlus()))   dlgSet(dlgValue() + 1);
            if (CheckCollisionPointRec(m, dlgPlus5()))  dlgSet((dlgValue() / CYCLE + 1) * CYCLE);
            if (CheckCollisionPointRec(m, dlgStart()))  startFromDialog();
            if (CheckCollisionPointRec(m, dlgCancel())) stageDialog = false;
        }
    }

    void drawButton(Rectangle r, bool hot, Color base) {
        DrawRectangleRounded(r, 0.3f, 8, hot ? Color{ (unsigned char)std::min(255, base.r + 40), (unsigned char)std::min(255, base.g + 40), (unsigned char)std::min(255, base.b + 40), 255 } : base);
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 1.5f, Color{ 255, 255, 255, 90 });
    }

    void drawStageDialogShapes() {
        DrawRectangle(0, 0, W, H, Color{ 0, 0, 0, 160 });
        Rectangle box = dlgBox();
        DrawRectangleRounded(box, 0.06f, 10, Color{ 18, 20, 44, 245 });
        DrawRectangleRoundedLinesEx(box, 0.06f, 10, 2, Color{ 90, 180, 255, 200 });
        Vector2 m = mouseLogical();
        Color btn{ 40, 60, 110, 255 };
        for (Rectangle r : { dlgMinus5(), dlgMinus(), dlgPlus(), dlgPlus5() }) drawButton(r, CheckCollisionPointRec(m, r), btn);
        DrawRectangleRounded(dlgField(), 0.2f, 8, Color{ 6, 8, 20, 255 });
        DrawRectangleRoundedLinesEx(dlgField(), 0.2f, 8, 2, dlgValid() ? Color{ 110, 210, 255, 255 } : Color{ 255, 80, 80, 255 });
        drawButton(dlgStart(), CheckCollisionPointRec(m, dlgStart()), dlgValid() ? Color{ 30, 120, 70, 255 } : Color{ 60, 60, 60, 255 });
        drawButton(dlgCancel(), CheckCollisionPointRec(m, dlgCancel()), Color{ 110, 40, 50, 255 });
    }

    void drawStageDialogText() {
        Rectangle box = dlgBox();
        drawCentered("DEBUG  -  START AT STAGE", box.y + 22, 24, Color{ 110, 210, 255, 255 });
        drawCentered(TextFormat("type 1-%d, up/down +-1, left/right or << >> jump to boss stages", FINAL_STAGE), box.y + 58, 13, GRAY);
        auto label = [&](Rectangle r, const char* t, float sz, Color c) {
            float fs = sz * scale, sp = customFont ? fs * 0.04f : fs / 10;
            Vector2 d = MeasureTextEx(font, t, fs, sp);
            DrawTextEx(font, t, { origin.x + (r.x + r.width / 2) * scale - d.x / 2, origin.y + (r.y + r.height / 2) * scale - d.y / 2 }, fs, sp, c);
        };
        label(dlgMinus5(), "<<", 20, RAYWHITE); label(dlgMinus(), "-", 26, RAYWHITE);
        label(dlgPlus(), "+", 26, RAYWHITE);    label(dlgPlus5(), ">>", 20, RAYWHITE);
        bool caret = fmodf(time, 1.0f) < 0.5f;
        label(dlgField(), TextFormat("%s%s", dlgText.c_str(), caret ? "_" : " "), 34, RAYWHITE);
        int v = dlgValue();
        if (!dlgValid())
            drawCentered(TextFormat("enter a stage between 1 and %d", FINAL_STAGE), 400, 16, Color{ 255, 110, 110, 255 });
        else if (stageKind(v) == SK_BOSS) {
            const auto& D = spr::bossDesigns()[v / CYCLE - 1];
            drawCentered(TextFormat("BOSS %d / %d :  %s", v / CYCLE, BOSS_COUNT, D.name), 398, 20, Color{ 255, 140, 190, 255 });
            drawCentered(TextFormat("%s  -  %d hits to destroy", D.title, (int)BOSS_HP[v / CYCLE - 1]), 424, 15, Color{ 220, 170, 200, 255 });
        } else if (stageKind(v) == SK_METEORS) {
            drawCentered("METEOR FIELD", 398, 20, Color{ 230, 160, 80, 255 });
            drawCentered(TextFormat("boss next stage:  %s", spr::bossDesigns()[(v - 1) / CYCLE].name), 424, 15, Color{ 220, 180, 150, 255 });
        } else
            drawCentered(TextFormat("normal wave  -  meteor field at %d, boss at %d", (v / CYCLE) * CYCLE + CYCLE - 1, (v / CYCLE + 1) * CYCLE), 404, 15, Color{ 170, 200, 230, 255 });
        drawCentered("meteor fields: 5 11 17 ... 59      bosses: 6 12 18 ... 60", 456, 13, DARKGRAY);
        label(dlgStart(), "START", 20, RAYWHITE);
        label(dlgCancel(), "CANCEL", 20, RAYWHITE);
        drawCentered("ENTER start     ESC cancel", box.y + box.height - 30, 13, GRAY);
    }

    // --- ESC pause menu -------------------------------------------------------
    static constexpr int ESC_N = 3;
    Rectangle escBox() const { return { 110, 255, W - 220.0f, 300 }; }
    Rectangle escBtn(int i) const { return { 150, 330 + i * 64.0f, W - 300.0f, 50 }; }
    void openEscMenu() { escMenu = true; escSel = 0; paused = false; while (GetCharPressed()) {} }
    void escChoose(int i) {
        escMenu = false;
        if (i == 1) {   // end current game -> back to the title screen
            if (!debugRun && score > hiScore) hiScore = score;
            saveHiScore();
            enemies.clear(); pBullets.clear(); eBullets.clear(); parts.clear(); rings.clear(); warnings.clear();
            packages.clear(); zaps.clear(); meteors.clear(); popups.clear(); pendingSplits.clear();
            boss.active = false; meteorField = false; shake = 0;
            mode = TITLE; titleT = 0; paused = false;
        } else if (i == 2) {   // exit to desktop
            if (mode != TITLE && !debugRun && score > hiScore) hiScore = score;
            saveHiScore();
            quitRequested = true;
        }
    }
    void updateEscMenu() {
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsKeyPressedRepeat(KEY_UP))     escSel = (escSel + ESC_N - 1) % ESC_N;
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsKeyPressedRepeat(KEY_DOWN)) escSel = (escSel + 1) % ESC_N;
        for (int i = 0; i < ESC_N; ++i) if (IsKeyPressed((KeyboardKey)(KEY_ONE + i)) || IsKeyPressed((KeyboardKey)(KEY_KP_1 + i))) { escChoose(i); return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) { escChoose(escSel); return; }
        Vector2 m = mouseLogical();
        Vector2 md = GetMouseDelta();
        for (int i = 0; i < ESC_N; ++i) if (CheckCollisionPointRec(m, escBtn(i))) {
            if (md.x != 0 || md.y != 0) escSel = i;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { escChoose(i); return; }
        }
    }
    void drawEscMenuShapes() {
        DrawRectangle(0, 0, W, H, Color{ 0, 0, 0, 170 });
        Rectangle box = escBox();
        DrawRectangleRounded(box, 0.07f, 10, Color{ 18, 20, 44, 245 });
        DrawRectangleRoundedLinesEx(box, 0.07f, 10, 2, Color{ 90, 180, 255, 200 });
        Color base[ESC_N] = { { 30, 110, 70, 255 }, { 60, 70, 120, 255 }, { 110, 40, 50, 255 } };
        for (int i = 0; i < ESC_N; ++i) {
            drawButton(escBtn(i), i == escSel, base[i]);
            if (i == escSel) DrawRectangleRoundedLinesEx(escBtn(i), 0.3f, 8, 2.5f, Color{ 255, 230, 120, 230 });
        }
    }
    void drawEscMenuText() {
        Rectangle box = escBox();
        drawCentered("PAUSED", box.y + 22, 30, YELLOW);
        const char* lbl[ESC_N] = { "RESUME GAME", "END CURRENT GAME", "EXIT TO DESKTOP" };
        float fs = 20 * scale, sp = customFont ? fs * 0.04f : fs / 10;
        for (int i = 0; i < ESC_N; ++i) {   // number and label in fixed left-aligned columns
            Rectangle r = escBtn(i);
            float y = origin.y + (r.y + r.height / 2) * scale - MeasureTextEx(font, "0", fs, sp).y / 2;
            DrawTextEx(font, TextFormat("%d", i + 1), { origin.x + (r.x + 30) * scale, y }, fs, sp, Color{ 255, 230, 120, 255 });
            DrawTextEx(font, lbl[i], { origin.x + (r.x + 72) * scale, y }, fs, sp, RAYWHITE);
        }
        drawCentered("UP/DOWN select    ENTER confirm    ESC resume", box.y + box.height - 30, 13, GRAY);
    }

    // --- update -------------------------------------------------------------
    void update(float dt) {
        time += dt;
        if (escMenu) { updateEscMenu(); return; }
        for (auto& s : stars) { s.pos.y += s.speed * dt * (mode == PLAYING ? 1.0f : 0.5f);
                                if (s.pos.y > H) { s.pos.y = 0; s.pos.x = frand(0, W); } }
        updateParticles(dt);

        if (mode == TITLE) {
            bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
            bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            if (stageDialog) { updateStageDialog(); return; }
            titleT += dt;
            if (ctrl && IsKeyPressed(KEY_L)) { stageDialog = true; dlgText = "1"; while (GetCharPressed()) {} return; }
            if (IsKeyPressed(KEY_SPACE) || (IsKeyPressed(KEY_ENTER) && !alt)) { debugRun = false; newGame(); }
            return;
        }
        if (mode == VICTORY) {
            updateBullets(dt);
            eBullets.clear();
            stageTimer += dt; fireworkT -= dt;
            if (fireworkT <= 0) {
                fireworkT = frand(0.15f, 0.45f);
                Color cs[] = { GOLD, SKYBLUE, PINK, LIME, ORANGE, VIOLET };
                Vector2 p{ frand(60, W - 60.0f), frand(80, H * 0.6f) };
                explode(p, cs[GetRandomValue(0, 5)], WHITE, 40, 260);
                PlaySound(sfx[SFX_BOOM]);
            }
            if (stageTimer > 3.0f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))) mode = TITLE;
            return;
        }
        if (mode == GAMEOVER) {
            updateEnemies(dt);
            updateBoss(dt);
            updateMeteors(dt);
            if (stageTimer > 2.0f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))) mode = TITLE;
            stageTimer += dt;
            return;
        }

        if (IsKeyPressed(KEY_P)) paused = !paused;
        if (paused) return;

        stageTimer += dt; cycleT += dt;
        bannerTimer -= dt;
        shake = std::max(0.0f, shake - dt);

        updatePlayer(dt);
        updateEnemies(dt);
        updateBoss(dt);
        updateMeteors(dt);
        updatePackages(dt);
        updateBullets(dt);
        collisions();

        // Stage clear?
        bool anyAlive = std::any_of(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.state != DEAD; });
        // (a short pause after the last kill so explosions can play out; torpedo trails don't block it)
        if (mode == PLAYING && !anyAlive && !boss.active && !meteorField && meteors.empty() && packages.empty()) {
            clearT += dt;
            if (clearT > 1.6f) { clearT = 0; nextStage(); }
        } else clearT = 0;
    }

    void updatePlayer(float dt) {
        if (!playerAlive) {
            respawnTimer -= dt;
            bool divers = std::any_of(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.state == DIVING; });
            if (respawnTimer <= 0 && lives <= 0) { mode = GAMEOVER; stageTimer = 0; saveHiScore(); }
            else if (respawnTimer <= 0 && !divers) {
                playerAlive = true; invuln = 2.0f; player.x = W / 2.0f;
                raiseShield();
            }
            return;
        }
        invuln = std::max(0.0f, invuln - dt);
        shieldGrace = std::max(0.0f, shieldGrace - dt);
        shieldHitFlash = std::max(0.0f, shieldHitFlash - dt);
        shieldGrow = std::min(1.0f, shieldGrow + dt * 2.2f);
        float mv = 0;
        if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) mv -= 1;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) mv += 1;
        player.x = Clamp(player.x + mv * 340 * dt, 24, W - 24);

        // debug runs: 1-5 pick a weapon, +/- change its level
        if (debugRun) {
            for (int k = 0; k < W_COUNT; ++k) if (IsKeyPressed((KeyboardKey)(KEY_ONE + k))) { weapon = k; heat = 0; overheated = false; }
            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) wlevel = std::min(3, wlevel + 1);
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) wlevel = std::max(0, wlevel - 1);
        }
        fireCooldown -= dt;
        bool trigger = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_UP) || autoFire;   // hold to keep firing
        laserOn = false;
        if (weapon == W_PULSE) {
            static const float rate[4] = { 0.5f, 0.42f, 0.36f, 0.3f };   // heat per second of firing (~2 s at Lv0)
            if (trigger && !overheated) {
                laserOn = true;
                heat += rate[wlevel] * dt;
                if (heat >= 1) { heat = 1; overheated = true; PlaySound(sfx[SFX_HIT]); }
                updateLaser(dt);
            } else {
                heat = std::max(0.0f, heat - dt * (overheated ? 0.8f : 1.2f));
                if (overheated && heat <= 0.25f) overheated = false;
                if (overheated && GetRandomValue(0, 3) == 0) { float life = frand(0.4f, 0.7f);
                    parts.push_back({ { player.x, player.y - 26 }, { frand(-10, 10), frand(-40, -20) }, life, life, 6, Color{ 120, 120, 130, 255 }, SMOKE }); }
            }
        } else if (trigger && fireCooldown <= 0) firePlayerWeapon();
    }

    void followPath(Enemy& e, float dist) {
        while (dist > 0 && e.seg + 1 < e.path.size()) {
            Vector2 to = e.path[e.seg + 1];
            float d = Vector2Distance(e.pos, to);
            if (dist >= d) { e.pos = to; ++e.seg; dist -= d; }
            else { e.pos = Vector2Add(e.pos, Vector2Scale(Vector2Normalize(Vector2Subtract(to, e.pos)), dist)); dist = 0; }
        }
    }

    void updateEnemies(float dt) {
        bool settling = false;
        int  divingCount = 0;
        for (auto& e : enemies) {
            if (e.state == WAITING || e.state == ENTERING || e.state == HOMING) settling = true;
            if (e.state == DIVING) ++divingCount;
        }

        for (auto& e : enemies) {
            e.prev = e.pos;
            e.grow = std::min(1.0f, e.grow + dt * 2.5f);
            switch (e.state) {
                case WAITING:
                    e.delay -= dt;
                    if (e.delay <= 0) { e.state = ENTERING; e.seg = 0; }
                    break;
                case ENTERING:
                    followPath(e, e.speed * dt);
                    if (e.seg + 1 >= e.path.size()) e.state = HOMING;
                    break;
                case HOMING: {
                    Vector2 target = slotPos(e);
                    Vector2 d = Vector2Subtract(target, e.pos);
                    float len = Vector2Length(d), step = e.speed * 0.9f * dt;
                    if (len <= step) { e.pos = target; e.state = FORMATION; }
                    else e.pos = Vector2Add(e.pos, Vector2Scale(d, step / len));
                    break;
                }
                case FORMATION:
                    e.pos = slotPos(e);
                    if (e.type == COURIER && mode == PLAYING && playerAlive) {
                        e.fireT -= dt;
                        if (e.fireT <= 0) { courierFire(e); e.fireT = frand(1.7f, 2.6f) / std::min(difficulty(), 1.5f); }
                    }
                    break;
                case DIVING:
                    followPath(e, e.speed * dt);
                    if (mode == PLAYING && e.shotsLeft > 0 && e.pos.y > 180 && e.pos.y < H * 0.7f) {
                        e.shootTimer -= dt;
                        if (e.shootTimer <= 0) {
                            float aim = Clamp((player.x - e.pos.x) * 0.6f, -140, 140);
                            eBullets.push_back({ e.pos, { aim, 300 * difficulty() } });
                            --e.shotsLeft; e.shootTimer = frand(0.25f, 0.5f);
                        }
                    }
                    if (e.seg + 1 >= e.path.size() && e.minion) { e.state = DEAD; break; }
                    if (e.seg + 1 >= e.path.size()) {          // left the bottom: re-enter from top
                        Vector2 s = slotPos(e);
                        e.pos = e.prev = { s.x, -40 };
                        e.state = HOMING; e.speed = 260 * std::min(difficulty(), 1.5f);
                    }
                    break;
                case DEAD: break;
            }
            // Facing: point along velocity while moving, settle to 0 in formation.
            Vector2 v = Vector2Subtract(e.pos, e.prev);
            float target = e.state == FORMATION ? 0.0f
                         : (Vector2Length(v) > 0.01f ? atan2f(v.y, v.x) * RAD2DEG - 90.0f : e.angle);
            float diff = fmodf(target - e.angle + 540.0f, 360.0f) - 180.0f;
            e.angle += diff * std::min(1.0f, dt * 12.0f);
        }

        // Dive scheduler
        if (mode == PLAYING && !settling && playerAlive) {
            diveTimer -= dt;
            int maxDivers = 2 + std::min(4, stage / 2);
            if (diveTimer <= 0 && divingCount < maxDivers) {
                std::vector<Enemy*> pool;
                for (auto& e : enemies) if (e.state == FORMATION && e.type != COURIER) pool.push_back(&e);   // couriers never dive
                if (!pool.empty()) {
                    Enemy* lead = pool[GetRandomValue(0, (int)pool.size() - 1)];
                    startDive(*lead);
                    // Bosses bring escorts, like the arcade original
                    if (lead->type == BOSS) {
                        int escorts = 0;
                        for (auto& e : enemies)
                            if (escorts < 2 && e.state == FORMATION && e.type != COURIER && &e != lead && fabsf(e.sy - lead->sy - 1) < 0.1f && fabsf(e.sx - lead->sx) <= 1.01f) {
                                startDive(e); ++escorts;
                            }
                    }
                }
                diveTimer = frand(0.8f, 2.2f) / difficulty();
            }
            // Remaining few enemies become aggressive
            int alive = 0; for (auto& e : enemies) if (e.state != DEAD) ++alive;
            if (alive <= 4 && diveTimer > 0.6f) diveTimer = 0.6f;
        }
    }

    void updateBullets(float dt) {
        for (auto& b : pBullets) {
            b.age += dt;
            if (b.kind == PB_HOMING) {          // steer toward the nearest target
                static const float turnR[4] = { 3.5f, 5.0f, 5.0f, 6.0f };
                Vector2 tgt{ -1, -1 }; float best = 1e9f;
                for (auto& e : enemies) {
                    if (e.state == DEAD || e.state == WAITING) continue;
                    float d = Vector2Distance(e.pos, b.pos); if (e.pos.y < b.pos.y + 40 && d < best) { best = d; tgt = e.pos; } }
                if (boss.active && boss.state == B_FIGHT) { float d = Vector2Distance(boss.core(), b.pos); if (d < best) { best = d; tgt = boss.core(); } }
                for (auto& m : meteors) { float d = Vector2Distance(m.pos, b.pos); if (m.pos.y < b.pos.y + 40 && d < best) { best = d; tgt = m.pos; } }
                float cur = atan2f(b.vel.y, b.vel.x);
                if (tgt.x >= 0) {
                    float want = atan2f(tgt.y - b.pos.y, tgt.x - b.pos.x);
                    float d = fmodf(want - cur + 3 * PI, 2 * PI) - PI, turn = turnR[wlevel] * dt;
                    cur += std::clamp(d, -turn, turn);
                }
                float sp = std::min(560.0f, Vector2Length(b.vel) + 500 * dt);
                b.vel = { cosf(cur) * sp, sinf(cur) * sp };
                float life = frand(0.18f, 0.3f);
                parts.push_back({ b.pos, { frand(-8, 8), frand(-8, 8) }, life, life, frand(2.5f, 4), WCOL[W_HOMING], TRAIL });
                if (b.age > 3.0f) b.pos.y = -100;
            } else if (b.kind == PB_PHOTON) {
                for (int k = 0; k < 3; ++k) {   // energy wake
                    float life = frand(0.18f, 0.35f);
                    parts.push_back({ Vector2Add(b.pos, { frand(-3, 3), frand(4, 10) }), { frand(-18, 18), frand(10, 50) },
                                      life, life, frand(3, 6), GetRandomValue(0, 2) ? Color{ 255, 90, 40, 255 } : Color{ 255, 200, 120, 255 }, TRAIL });
                }
            } else if (b.kind == PB_SPREAD) {
                if (GetRandomValue(0, 1)) { float life = frand(0.12f, 0.22f);
                    parts.push_back({ b.pos, { 0, 0 }, life, life, 3.5f, WCOL[W_SPREAD], TRAIL }); }
            } else if (b.kind == PB_ION) {
                if (GetRandomValue(0, 1)) { float life = frand(0.15f, 0.3f), a = frand(0, 2 * PI);
                    parts.push_back({ b.pos, { cosf(a) * 60, sinf(a) * 60 }, life, life, frand(1, 1.8f), WCOL[W_ION], SPARK }); }
            }
            b.pos = Vector2Add(b.pos, Vector2Scale(b.vel, dt));
        }
        for (auto& b : eBullets) {
            b.age += dt;
            if (b.kind == 2) {             // homing missile: steer toward the player for a while
                if (b.age < 2.2f && playerAlive) {
                    float cur = atan2f(b.vel.y, b.vel.x), want = atan2f(player.y - b.pos.y, player.x - b.pos.x);
                    float d = fmodf(want - cur + 3 * PI, 2 * PI) - PI, turn = 2.4f * dt;
                    cur += std::clamp(d, -turn, turn);
                    float sp = std::min(260.0f, Vector2Length(b.vel) + 60 * dt);
                    b.vel = { cosf(cur) * sp, sinf(cur) * sp };
                }
                Vector2 back = Vector2Subtract(b.pos, Vector2Scale(Vector2Normalize(b.vel), 9));
                { float life = frand(0.15f, 0.3f);
                  parts.push_back({ back, { frand(-10, 10), frand(-10, 10) }, life, life, frand(2.5f, 4), Color{ 255, 150, 50, 255 }, TRAIL }); }
                if (GetRandomValue(0, 1)) { float life = frand(0.5f, 0.9f);
                  parts.push_back({ back, { frand(-8, 8), frand(-8, 8) }, life, life, frand(3, 5), Color{ 120, 115, 125, 255 }, SMOKE }); }
                if (b.age > 4.5f) {                // out of fuel: detonates instead of vanishing
                    explode(b.pos, Color{ 255, 150, 50, 255 }, YELLOW, 14, 170);
                    b.pos.y = H + 100;             // removed below
                }
            } else if (b.kind == BK_C_HOMING) {    // courier's seeker
                if (b.age < 2.0f && playerAlive) {
                    float cur = atan2f(b.vel.y, b.vel.x), want = atan2f(player.y - b.pos.y, player.x - b.pos.x);
                    float d = fmodf(want - cur + 3 * PI, 2 * PI) - PI, turn = 2.0f * dt;
                    cur += std::clamp(d, -turn, turn);
                    float sp = std::min(230.0f, Vector2Length(b.vel) + 50 * dt);
                    b.vel = { cosf(cur) * sp, sinf(cur) * sp };
                }
                if (GetRandomValue(0, 1)) { float life = frand(0.15f, 0.3f);
                    parts.push_back({ b.pos, { frand(-8, 8), frand(-8, 8) }, life, life, frand(2, 3.5f), WCOL[W_HOMING], TRAIL }); }
            } else if (b.kind == BK_C_ION && GetRandomValue(0, 2) == 0) {
                float life = frand(0.15f, 0.3f), a = frand(0, 2 * PI);
                parts.push_back({ b.pos, { cosf(a) * 50, sinf(a) * 50 }, life, life, frand(1, 1.6f), WCOL[W_ION], SPARK });
            } else if (b.kind == BK_SPIRAL) {      // ribbon trail so the spiral arms read as curves
                float life = 0.32f;
                parts.push_back({ b.pos, { 0, 0 }, life, life, 4.5f, spiralColor(b.aux), TRAIL });
            } else if (b.kind == 3 && GetRandomValue(0, 2) == 0) {
                float life = frand(0.2f, 0.35f);
                parts.push_back({ b.pos, { frand(-10, 10), frand(-40, -10) }, life, life, frand(3, 5), Color{ 255, 110, 40, 255 }, TRAIL });
            }
            b.pos = Vector2Add(b.pos, Vector2Scale(b.vel, dt));
        }
        for (auto& w : warnings) {
            w.t -= dt;
            if (w.t <= 0) eBullets.push_back({ { w.x, -14 }, { frand(-25, 25), frand(200, 270) * difficulty() * 0.9f }, BK_METEOR, 0,
                                               (float)GetRandomValue(0, 5) });
        }
        warnings.erase(std::remove_if(warnings.begin(), warnings.end(), [](const Warning& w) { return w.t <= 0; }), warnings.end());
        pBullets.erase(std::remove_if(pBullets.begin(), pBullets.end(), [](const Bullet& b) { return b.pos.y < -20 || b.pos.x < -30 || b.pos.x > W + 30 || b.pos.y > H + 30; }), pBullets.end());
        eBullets.erase(std::remove_if(eBullets.begin(), eBullets.end(), [](const Bullet& b) { return b.pos.y > H + 20 || b.pos.y < -40 || b.pos.x < -40 || b.pos.x > W + 40; }), eBullets.end());
    }

    void updateParticles(float dt) {
        for (auto& p : parts) {
            p.pos = Vector2Add(p.pos, Vector2Scale(p.vel, dt));
            if (p.kind != HULL) p.vel = Vector2Scale(p.vel, p.kind == SMOKE ? 0.95f : 0.97f);
            if (p.kind == SMOKE) p.pos.y += 12 * dt;
            p.rot += p.spin * dt; p.life -= dt;
        }
        for (auto& r : rings) r.life -= dt;
        rings.erase(std::remove_if(rings.begin(), rings.end(), [](const Ring& r) { return r.life <= 0; }), rings.end());
        parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0; }), parts.end());
        for (auto& p : popups) { p.life -= dt; p.pos.y -= 30 * dt; }
        popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup& p) { return p.life <= 0; }), popups.end());
    }


    // ============================================================ ARSENAL ==
    float wCooldown() const {
        static const float cd[W_COUNT][4] = { { 0.2f, 0.14f, 0.14f, 0.14f }, { 0.32f, 0.3f, 0.28f, 0.26f }, { 0, 0, 0, 0 },
                                              { 0.42f, 0.36f, 0.36f, 0.32f }, { 0.5f, 0.45f, 0.34f, 0.34f } };
        return cd[weapon][wlevel];
    }
    int countPB(int kind) const { int n = 0; for (auto& b : pBullets) if (b.kind == kind) ++n; return n; }

    // Fire one volley of the current (non-laser) weapon; each class has its own on-screen limit
    void firePlayerWeapon() {
        Vector2 m{ player.x, player.y - 26 };
        switch (weapon) {
            case W_PHOTON: {
                static const int volleys[4] = { 5, 6, 6, 7 };
                int per = wlevel >= 3 ? 3 : (wlevel >= 2 ? 2 : 1);
                if (countPB(PB_PHOTON) + per > volleys[wlevel] * per) return;
                for (int i = 0; i < per; ++i) { float off = (i - (per - 1) / 2.0f) * 16;
                    pBullets.push_back({ { m.x + off, m.y }, { off * 0.6f, -640 }, PB_PHOTON }); }
                break; }
            case W_SPREAD: {
                static const int pel[4] = { 3, 5, 5, 7 }, vol[4] = { 2, 2, 3, 3 };
                static const float spr[4] = { 30, 42, 46, 62 };
                int n = pel[wlevel];
                if (countPB(PB_SPREAD) + n > vol[wlevel] * n) return;
                for (int i = 0; i < n; ++i) { float a = (-90 - spr[wlevel] / 2 + spr[wlevel] * i / (n - 1)) * DEG2RAD;
                    pBullets.push_back({ m, { cosf(a) * 620, sinf(a) * 620 }, PB_SPREAD }); }
                break; }
            case W_HOMING: {
                static const int per[4] = { 2, 2, 3, 4 }, cap[4] = { 6, 6, 8, 10 };
                int n = per[wlevel];
                if (countPB(PB_HOMING) + n > cap[wlevel]) return;
                for (int i = 0; i < n; ++i) { float a = (-90 + (i - (n - 1) / 2.0f) * 38) * DEG2RAD;
                    pBullets.push_back({ { m.x + cosf(a) * 10, m.y + 6 }, { cosf(a) * 300, sinf(a) * 300 }, PB_HOMING }); }
                break; }
            case W_ION: {
                static const int cap[4] = { 2, 2, 3, 3 };
                if (countPB(PB_ION) >= cap[wlevel]) return;
                pBullets.push_back({ m, { 0, -430 }, PB_ION });
                break; }
            default: return;
        }
        fireCooldown = wCooldown(); ++shotsFired;
        Color c = WCOL[weapon];
        parts.push_back({ m, { 0, 0 }, 0.12f, 0.12f, 22, c, FLASH });
        PlaySound(sfx[SFX_SHOOT]);
    }

    // Damage one enemy by one hit. Returns true if it was destroyed.
    bool hitEnemy(Enemy& e, Vector2 at) {
        (void)at;
        if (--e.hp > 0) {
            PlaySound(sfx[SFX_HIT]);
            if (e.type == SHIELDED) {          // energy shield pops in a burst of cyan shards
                rings.push_back({ e.pos, 0.3f, 0.3f, 36, Color{ 90, 255, 240, 255 } });
                for (int q = 0; q < 16; ++q) {
                    float ang = frand(0, 2 * PI), sp = frand(80, 200), life = frand(0.3f, 0.6f);
                    parts.push_back({ Vector2Add(e.pos, { cosf(ang) * 22, sinf(ang) * 22 }), { cosf(ang) * sp, sinf(ang) * sp },
                                      life, life, frand(2, 3.5f), Color{ 120, 255, 240, 255 }, DEBRIS, frand(0, 360), frand(-800, 800) });
                }
            } else if (e.type == COURIER) {    // sparks; outer armour plates blow off at half health
                for (int q = 0; q < 5; ++q) { float ang = frand(0, 2 * PI), sp = frand(80, 220), life = frand(0.2f, 0.4f);
                    parts.push_back({ at, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1, 2), UPG_COL, SPARK }); }
                if (!e.armourOff && e.hp * 2 <= e.maxHp) {
                    e.armourOff = true;
                    for (int side : { -1, 1 }) for (int q = 0; q < 2; ++q) {
                        float life = 1.2f;
                        parts.push_back({ { e.pos.x + side * 24.0f, e.pos.y + (q ? 8.0f : -8.0f) }, { side * frand(160, 260), frand(-80, 80) },
                                          life, life, frand(6, 8), Color{ 110, 116, 135, 255 }, HULL, frand(0, 360), frand(-600, 600) });
                    }
                    explode(e.pos, ORANGE, UPG_COL, 14, 160);
                }
            } else explode(e.pos, PURPLE, WHITE, 10, 120);
            return false;
        }
        bool diving = e.state != FORMATION;
        int pts = enemyPoints(e.type, diving);
        addScore(pts);
        if ((diving && pts >= 160) || e.type == COURIER) popups.push_back({ e.pos, 1.0f, std::to_string(pts) });
        e.state = DEAD;
        if (e.type == COURIER) {
            courierBurst(e.pos);
            packages.push_back({ { e.pos.x, e.pos.y + 6 }, e.cargo, 0 });
        } else {
            explode(e.pos, enemyColor(e.type), WHITE, e.type == MINI ? 14 : 28, 220);
            PlaySound(sfx[SFX_BOOM]);
        }
        if (e.type == SPLITTER) pendingSplits.push_back(e.pos);
        return true;
    }

    // Courier destroyed: the hull bursts into dozens of pieces that leave the screen in about a second
    void courierBurst(Vector2 at) {
        parts.push_back({ at, { 0, 0 }, 0.3f, 0.3f, 90, Color{ 255, 240, 210, 255 }, FLASH });
        parts.push_back({ at, { 0, 0 }, 0.45f, 0.45f, 60, Color{ 255, 150, 60, 255 }, FLASH });
        rings.push_back({ at, 0.5f, 0.5f, 220, Color{ 255, 200, 120, 255 } });
        for (int i = 0; i < 56; ++i) {
            float ang = frand(0, 2 * PI), sp = frand(500, 1300), life = 1.4f;
            int ty = GetRandomValue(0, 2);
            Color c = ty == 0 ? Color{ 110, 116, 135, 255 } : ty == 1 ? Color{ 230, 185, 70, 255 } : Color{ 255, 190, 110, 255 };
            parts.push_back({ at, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(3, 9), c, HULL, frand(0, 360), frand(-1400, 1400) });
        }
        for (int i = 0; i < 18; ++i) { float ang = frand(0, 2 * PI), sp = frand(200, 500), life = frand(0.3f, 0.6f);
            parts.push_back({ at, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1.5f, 3), Color{ 255, 200, 120, 255 }, SPARK }); }
        shake = std::max(shake, 0.35f);
        PlaySound(sfx[SFX_BOOM]); PlaySound(sfx[SFX_IMPACT]);
    }

    // The Courier shoots the basic form of what it carries (or of the player's weapon, if it carries an upgrade)
    void courierFire(const Enemy& e) {
        int w = e.cargo >= 0 ? e.cargo : weapon;
        Vector2 from{ e.pos.x, e.pos.y + 24 };
        Vector2 d = Vector2Normalize(Vector2Subtract(player, from));
        float a = atan2f(d.y, d.x);
        switch (w) {
            case W_PHOTON: eBullets.push_back({ from, Vector2Scale(d, 300), BK_C_PHOTON }); break;
            case W_SPREAD: for (int i = -1; i <= 1; ++i) { float aa = a + i * 14 * DEG2RAD;
                               eBullets.push_back({ from, { cosf(aa) * 260, sinf(aa) * 260 }, BK_C_SPREAD }); } break;
            case W_PULSE:  eBullets.push_back({ from, { 0, 540 }, BK_C_PULSE }); break;
            case W_HOMING: eBullets.push_back({ from, Vector2Scale(d, 150), BK_C_HOMING }); break;
            default:       eBullets.push_back({ from, Vector2Scale(d, 170), BK_C_ION }); break;
        }
        Color c = WCOL[w];
        parts.push_back({ from, { 0, 0 }, 0.15f, 0.15f, 18, c, FLASH });
        PlaySound(sfx[SFX_DIVE]);
    }

    // Ion Burst detonation: damages everything nearby; Lv3 chains lightning to up to 3 more enemies
    void ionBlast(Vector2 at, bool bossDirect) {
        static const float R[4] = { 55, 75, 75, 85 };
        float r = R[wlevel];
        Color c = WCOL[W_ION];
        parts.push_back({ at, { 0, 0 }, 0.25f, 0.25f, r * 1.1f, Color{ 230, 200, 255, 255 }, FLASH });
        rings.push_back({ at, 0.3f, 0.3f, r, c });
        explode(at, c, WHITE, 16, 200);
        PlaySound(sfx[SFX_IMPACT]);
        for (auto& e : enemies)
            if (e.state != DEAD && e.state != WAITING && Vector2Distance(e.pos, at) < r + 14) hitEnemy(e, at);
        std::vector<int> mids;
        for (int j = 0; j < (int)meteors.size(); ++j) if (Vector2Distance(meteors[j].pos, at) < r + meteors[j].r() * 0.6f) mids.push_back(meteors[j].id);
        for (int id : mids) {
            auto it = std::find_if(meteors.begin(), meteors.end(), [id](const Meteor& m) { return m.id == id; });
            if (it != meteors.end()) hitMeteor((size_t)(it - meteors.begin()), Vector2Subtract(it->pos, at));
        }
        if (!bossDirect && boss.active && boss.state == B_FIGHT && Vector2Distance(boss.core(), at) < r + boss.size() * 0.3f) damageBoss(at);
        if (wlevel >= 3) {   // chain lightning
            std::vector<Enemy*> near;
            for (auto& e : enemies) if (e.state != DEAD && e.state != WAITING && Vector2Distance(e.pos, at) < 180) near.push_back(&e);
            std::sort(near.begin(), near.end(), [&](Enemy* a, Enemy* b) { return Vector2Distance(a->pos, at) < Vector2Distance(b->pos, at); });
            for (int k = 0; k < (int)near.size() && k < 3; ++k) {
                zaps.push_back({ at, near[k]->pos, 0.22f, (unsigned)GetRandomValue(1, 1 << 20) });
                hitEnemy(*near[k], near[k]->pos);
            }
        }
    }

    // Pulse Laser: continuous beam, damage in ticks; stops at the first thing it hits (Lv3 pierces everything)
    void updateLaser(float dt) {
        static const float wid[4] = { 4.5f, 6, 8, 11 }, tick[4] = { 0.16f, 0.14f, 0.12f, 0.11f };
        float x = player.x, y0 = player.y - 26, w = wid[wlevel];
        bool pierce = wlevel >= 3;
        float stop = 0;   // y where the beam ends
        Enemy* firstE = nullptr; int firstM = -1; bool firstBoss = false;
        for (auto& e : enemies) {
            if (e.state == DEAD || e.state == WAITING || e.pos.y > y0) continue;
            float er = e.type == COURIER ? 26.f : e.type == BOSS ? 21.f : 16.f;
            if (fabsf(e.pos.x - x) < er + w / 2 && e.pos.y > stop) { stop = e.pos.y; firstE = &e; firstM = -1; firstBoss = false; }
        }
        for (int j = 0; j < (int)meteors.size(); ++j) {
            const Meteor& m = meteors[j];
            if (m.pos.y < y0 && fabsf(m.pos.x - x) < m.r() * 0.9f + w / 2 && m.pos.y > stop) { stop = m.pos.y; firstM = j; firstE = nullptr; firstBoss = false; }
        }
        float bossY = -1;
        if (boss.active && boss.state == B_FIGHT)
            for (float y = y0; y > 0; y -= 6) if (boss.hitTest({ x, y })) { bossY = y; break; }
        if (bossY > stop) { stop = bossY; firstBoss = true; firstE = nullptr; firstM = -1; }
        laserTop = pierce ? 0 : stop;
        // sparks where it bites
        if (stop > 0 && GetRandomValue(0, 1)) { float life = frand(0.15f, 0.3f);
            parts.push_back({ { x + frand(-w, w), stop }, { frand(-140, 140), frand(-120, 40) }, life, life, frand(1, 2), WCOL[W_PULSE], SPARK }); }
        laserTick -= dt;
        if (laserTick > 0) return;
        laserTick = tick[wlevel]; ++shotsFired;
        if (pierce) {
            bool any = false;
            for (auto& e : enemies) {
                if (e.state == DEAD || e.state == WAITING || e.pos.y > y0) continue;
                float er = e.type == COURIER ? 26.f : e.type == BOSS ? 21.f : 16.f;
                if (fabsf(e.pos.x - x) < er + w / 2) { hitEnemy(e, { x, e.pos.y }); any = true; }
            }
            std::vector<int> mids;
            for (auto& m : meteors) if (m.pos.y < y0 && fabsf(m.pos.x - x) < m.r() * 0.9f + w / 2) mids.push_back(m.id);
            for (int id : mids) { auto it = std::find_if(meteors.begin(), meteors.end(), [id](const Meteor& m) { return m.id == id; });
                if (it != meteors.end()) { hitMeteor((size_t)(it - meteors.begin()), { 0, -1 }); any = true; } }
            if (bossY > 0) { damageBoss({ x, bossY }); any = true; }
            if (any) ++hits;
        } else if (firstE) { hitEnemy(*firstE, { x, stop }); ++hits; }
        else if (firstM >= 0) { hitMeteor((size_t)firstM, { 0, -1 }); ++hits; }
        else if (firstBoss) { damageBoss({ x, stop }); ++hits; }
    }

    void collectPackage(const Package& pk) {
        Color c = pk.content >= 0 ? WCOL[pk.content] : UPG_COL;
        std::string label;
        if (pk.content < 0 || pk.content == weapon) {
            if (wlevel < 3) { ++wlevel; label = TextFormat("UPGRADE +1   LV%d", wlevel); }
            else { addScore(1000); label = "MAX LEVEL  +1000"; }
        } else {
            weapon = pk.content; heat = 0; overheated = false;   // keeps the current upgrade level
            label = WNAME[weapon];
        }
        popups.push_back({ { player.x - 70, player.y - 80 }, 1.8f, label });
        parts.push_back({ player, { 0, 0 }, 0.35f, 0.35f, 80, c, FLASH });
        rings.push_back({ player, 0.4f, 0.4f, 80, c });
        rings.push_back({ player, 0.6f, 0.6f, 120, Color{ c.r, c.g, c.b, 140 } });
        for (int i = 0; i < 24; ++i) { float ang = frand(0, 2 * PI), sp = frand(80, 220), life = frand(0.3f, 0.6f);
            parts.push_back({ player, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1.2f, 2.2f), c, SPARK }); }
        PlaySound(sfx[SFX_SHIELD_UP]); PlaySound(sfx[SFX_LIFE]);
    }

    void updatePackages(float dt) {
        for (auto& pk : packages) {
            pk.t += dt; pk.pos.y += 120 * dt;
            pk.pos.x += sinf(pk.t * 2.2f) * 12 * dt;
            if (playerAlive && mode == PLAYING && Vector2Distance(pk.pos, player) < 34) { collectPackage(pk); pk.pos.y = H + 100; }
        }
        packages.erase(std::remove_if(packages.begin(), packages.end(), [](const Package& p) { return p.pos.y > H + 30; }), packages.end());
        for (auto& z : zaps) z.life -= dt;
        zaps.erase(std::remove_if(zaps.begin(), zaps.end(), [](const Lightning& z) { return z.life <= 0; }), zaps.end());
    }

    // Weapon status: compact indicator in the bottom HUD strip, after the spare-ship icons (never over the player)
    void drawWeaponIndicator() {
        int n = std::max(0, lives - (playerAlive ? 1 : 0));
        float x = n > 0 ? 41 + (n - 1) * 34 + 8.0f : 12;
        Vector2 o{ x, H - 34.0f };
        Color col = WCOL[weapon];
        DrawRectangleRounded({ o.x, o.y, 64, 24 }, 0.35f, 6, Color{ 10, 12, 28, 200 });
        DrawRectangleRoundedLinesEx({ o.x, o.y, 64, 24 }, 0.35f, 6, 1.0f, Color{ col.r, col.g, col.b, 170 });
        DrawRectangleRounded({ o.x + 3, o.y + 3, 18, 18 }, 0.3f, 6, Color{ (unsigned char)(col.r / 3), (unsigned char)(col.g / 3), (unsigned char)(col.b / 3), 255 });
        weaponIcon(weapon, { o.x + 12, o.y + 12 }, 12, col);
        bool laser = weapon == W_PULSE;
        for (int i = 0; i < 3; i++) {
            Vector2 c{ o.x + 30 + i * 11.0f, o.y + (laser ? 9.0f : 12.0f) };
            if (i < wlevel) DrawCircleV(c, 3.4f, col); else DrawCircleLines((int)c.x, (int)c.y, 3.4f, Color{ col.r, col.g, col.b, 150 });
        }
        if (laser) {
            Color hc = overheated ? (fmodf(time, 0.3f) < 0.15f ? Color{ 255, 90, 60, 255 } : Color{ 120, 40, 30, 255 }) : (heat > 0.8f ? Color{ 255, 150, 60, 255 } : col);
            DrawRectangle((int)o.x + 25, (int)o.y + 16, 34, 3, Color{ 30, 40, 60, 255 });
            DrawRectangle((int)o.x + 25, (int)o.y + 16, (int)(34 * heat), 3, hc);
        }
    }

    // Jagged lightning segment (Ion Burst chain)
    void drawZap(Vector2 a, Vector2 b, unsigned seed, Color c, float w, unsigned char alpha) {
        Vector2 prev = a; unsigned s = seed;
        auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; };
        Vector2 n = Vector2Normalize({ -(b.y - a.y), b.x - a.x });
        for (int i = 1; i <= 8; ++i) {
            Vector2 q = Vector2Lerp(a, b, i / 8.0f);
            if (i < 8) q = Vector2Add(q, Vector2Scale(n, (rnd() - 0.5f) * 18));
            BeginBlendMode(BLEND_ADDITIVE); DrawLineEx(prev, q, w * 3, Color{ c.r, c.g, c.b, (unsigned char)(alpha * 0.35f) }); EndBlendMode();
            DrawLineEx(prev, q, w, Color{ 240, 220, 255, alpha });
            prev = q;
        }
    }

    std::vector<Vector2> pendingSplits;
    int enemyPoints(EType t, bool diving) const {
        switch (t) {
            case BEE: return diving ? 100 : 50;          case BUTTERFLY: return diving ? 160 : 80;
            case BOSS: return diving ? 400 : 150;        case DART: return diving ? 240 : 120;
            case SHIELDED: return diving ? 300 : 150;    case SPLITTER: return diving ? 200 : 100;
            case COURIER: return 500;
            default: return 60;                          // MINI
        }
    }
    Color enemyColor(EType t) const {
        switch (t) {
            case BEE: return YELLOW; case BUTTERFLY: return RED; case BOSS: return GREEN;
            case DART: return Color{ 255, 80, 60, 255 }; case SHIELDED: return Color{ 90, 255, 230, 255 };
            case COURIER: return UPG_COL;
            default: return Color{ 220, 90, 255, 255 };
        }
    }
    // A destroyed splitter bursts into two small fast minis that peel away and dive
    void releaseSplits() {
        for (Vector2 at : pendingSplits)
            for (int side : { -1, 1 }) {
                Enemy m;
                m.type = MINI; m.hp = 1; m.minion = true; m.sx = 0; m.sy = 3; m.col = 0; m.row = 3;
                m.pos = m.prev = { at.x + side * 12.0f, at.y };
                enemies.push_back(m);
                startDive(enemies.back());
            }
        if (!pendingSplits.empty()) PlaySound(sfx[SFX_HIT]);
        pendingSplits.clear();
    }

    void collisions() {
        // Player projectiles vs enemies, rocks and the boss
        for (size_t i = 0; i < pBullets.size();) {
            bool used = false;
            Bullet pb = pBullets[i];
            float pr = pb.kind == PB_ION ? 9.f : pb.kind == PB_SPREAD ? 5.f : pb.kind == PB_HOMING ? 6.f : 7.f;
            auto impact = [&](Vector2 at) {
                if (pb.kind == PB_PHOTON) torpedoImpact(at);
                else if (pb.kind != PB_ION) {
                    Color c = pb.kind == PB_SPREAD ? WCOL[W_SPREAD] : WCOL[W_HOMING];
                    parts.push_back({ at, { 0, 0 }, 0.15f, 0.15f, 18, c, FLASH });
                    for (int q = 0; q < 6; ++q) { float ang = frand(0, 2 * PI), sp = frand(100, 240), life = frand(0.12f, 0.3f);
                        parts.push_back({ at, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1, 1.8f), c, SPARK }); }
                    PlaySound(sfx[SFX_IMPACT]);
                }
            };
            for (auto& e : enemies) {
                if (e.state == DEAD || e.state == WAITING) continue;
                float er = e.type == BOSS ? 21.f : e.type == COURIER ? 26.f : e.type == SHIELDED ? (e.hp > 1 ? 24.f : 18.f) : e.type == MINI ? 11.f : e.type == DART ? 14.f : 17.f;
                if (CheckCollisionCircles(pb.pos, pr, e.pos, er)) {
                    used = true; ++hits;
                    impact(pb.pos);
                    if (pb.kind == PB_ION) ionBlast(pb.pos, false); else hitEnemy(e, pb.pos);
                    break;
                }
            }
            if (!used)
                for (size_t j = 0; j < meteors.size(); ++j)
                    if (CheckCollisionCircles(pb.pos, pr - 1, meteors[j].pos, meteors[j].r() * 0.9f)) {
                        used = true; ++hits;
                        impact(pb.pos);
                        if (pb.kind == PB_ION) ionBlast(pb.pos, false); else hitMeteor(j, pb.vel);
                        break;
                    }
            if (!used && boss.active && boss.state == B_FIGHT && boss.hitTest(pb.pos)) {
                used = true; ++hits;
                impact(pb.pos);
                damageBoss(pb.pos);
                if (pb.kind == PB_ION && boss.active && boss.state == B_FIGHT) ionBlast(pb.pos, true);
            }
            if (used) pBullets.erase(pBullets.begin() + i); else ++i;
        }
        releaseSplits();
        if (!playerAlive || invuln > 0 || shieldGrace > 0) return;
        float hitR = shield ? SHIELD_RADIUS - 4 : 13;
        // Enemy bullets vs player (the force field absorbs the first hit)
        for (size_t i = 0; i < eBullets.size(); ++i)
            if (CheckCollisionCircles(eBullets[i].pos, eBullets[i].kind == BK_ENEMY ? 3.0f : eBullets[i].kind == BK_METEOR ? 8.0f
                                      : eBullets[i].kind == BK_BOLT ? 4.0f : 5.5f, player, hitR)) {
                Vector2 at = eBullets[i].pos;
                eBullets.erase(eBullets.begin() + i);
                if (shield) breakShield(at); else killPlayer();
                return;
            }
        // Meteors smashing into the player
        for (size_t j = 0; j < meteors.size(); ++j)
            if (CheckCollisionCircles(meteors[j].pos, meteors[j].r() * 0.82f, player, shield ? SHIELD_RADIUS - 2 : 13)) {
                Vector2 at = meteors[j].pos;
                hitMeteor(j, Vector2Subtract(at, player));
                if (shield) breakShield(at); else killPlayer();
                return;
            }
        // Divers ramming the player
        for (auto& e : enemies)
            if (e.state == DIVING && CheckCollisionCircles(e.pos, 14, player, shield ? SHIELD_RADIUS - 2 : 14)) {
                e.state = DEAD; explode(e.pos, ORANGE, WHITE, 25, 200);
                addScore(enemyPoints(e.type, true));
                if (e.type == SPLITTER) pendingSplits.push_back(e.pos);
                if (shield) breakShield(e.pos); else killPlayer();
                return;
            }
    }


    // ======================================================== TITLE CAROUSEL ==
    // Three rotating titles. Cycle per title (11 s): hold 5 s -> instability 2 s ->
    // blow apart into particles 2 s -> particles fly back and form the next title 2 s.
    struct TitleLine  { const char* text; float y, size; Color col; };
    struct TitleCard  { std::vector<TitleLine> lines; };
    struct TitleDot   { Vector2 home; Color col; };
    static constexpr float T_HOLD = 5, T_UNSTABLE = 2, T_BLOW = 2, T_FORM = 2;
    static constexpr float T_CARD = T_HOLD + T_UNSTABLE + T_BLOW + T_FORM;
    static constexpr float DOT = 2.6f;                         // particle spacing / size (logical units)
    std::vector<TitleCard> cards = {
        { { { "STAR SWARM", 168, 64, Color{ 255, 70, 90, 255 } } } },
        { { { "DEVELOPED BY", 152, 22, Color{ 150, 200, 255, 255 } }, { "GILAD GLOBEN", 184, 50, Color{ 90, 190, 255, 255 } } } },
        { { { "DEDICATED TO", 152, 22, Color{ 255, 225, 160, 255 } }, { "DANIEL GLOBEN", 184, 50, Color{ 255, 195, 70, 255 } } } },
    };
    std::vector<std::vector<TitleDot>> cardDots;               // sampled pixel targets per card
    std::vector<std::vector<int>>      dotMap;                 // particle i -> target index, per card
    std::vector<std::vector<Vector2>>  dotOut;                 // particle i -> off-screen point, per card
    std::vector<float> dotDelay, dotCurve;
    int   nDots = 0;
    float titleT = 0;

    float lineSpacing(float fs) const { return customFont ? fs * 0.04f : fs / 10; }

    void buildTitleParticles() {
        cardDots.clear();
        const float K = 4;                                     // oversample when rasterising the text
        for (auto& card : cards) {
            std::vector<TitleDot> dots;
            for (auto& L : card.lines) {
                float fs = L.size * K;
                Image img = ImageTextEx(font, L.text, fs, lineSpacing(fs), WHITE);
                float w = img.width / K, x0 = W / 2.0f - w / 2;
                Color* px = LoadImageColors(img);
                for (float y = DOT / 2; y < img.height / K; y += DOT)
                    for (float x = DOT / 2; x < w; x += DOT) {
                        int ix = (int)(x * K), iy = (int)(y * K);
                        if (ix >= img.width || iy >= img.height) continue;
                        if (px[iy * img.width + ix].a > 120) dots.push_back({ { x0 + x, L.y + y }, L.col });
                    }
                UnloadImageColors(px);
                UnloadImage(img);
            }
            cardDots.push_back(dots);
        }
        nDots = 0;
        for (auto& d : cardDots) nDots = std::max(nDots, (int)d.size());
        dotMap.assign(cards.size(), std::vector<int>(nDots));
        dotOut.assign(cards.size(), std::vector<Vector2>(nDots));
        dotDelay.resize(nDots); dotCurve.resize(nDots);
        for (size_t k = 0; k < cards.size(); ++k) {
            int cnt = (int)cardDots[k].size();
            std::vector<int> perm(nDots);
            for (int i = 0; i < nDots; ++i) perm[i] = i % cnt;
            for (int i = nDots - 1; i > 0; --i) std::swap(perm[i], perm[GetRandomValue(0, i)]);   // particles swap places between cards
            dotMap[k] = perm;
            for (int i = 0; i < nDots; ++i) {
                Vector2 h = cardDots[k][perm[i]].home;
                Vector2 c{ W / 2.0f, 190 };
                float ang = atan2f(h.y - c.y, h.x - c.x) + frand(-0.6f, 0.6f);
                float dist = frand(700, 1100);
                dotOut[k][i] = { c.x + cosf(ang) * dist, c.y + sinf(ang) * dist };
            }
        }
        for (int i = 0; i < nDots; ++i) { dotDelay[i] = frand(0, 1); dotCurve[i] = frand(-90, 90); }
    }

    // Particle phase drawing (logical coordinates, inside the camera)
    void drawTitleParticles() {
        if (cardDots.empty()) return;
        int nC = (int)cards.size();
        float T = fmodf(titleT, T_CARD * nC);
        int k = (int)(T / T_CARD);
        float u = T - k * T_CARD;
        float blow0 = T_HOLD + T_UNSTABLE, form0 = blow0 + T_BLOW;
        if (u < blow0) return;
        int nk = (k + 1) % nC;
        auto ease3in  = [](float x) { return x * x * x; };
        auto ease3out = [](float x) { float y = 1 - x; return 1 - y * y * y; };
        Vector2 c{ W / 2.0f, 190 };

        if (u < form0) {                                       // ---- blowing apart
            float e = (u - blow0) / T_BLOW;
            BeginBlendMode(BLEND_ADDITIVE);
            if (e < 0.25f) {                                   // detonation flash + shockwave
                float f = 1 - e / 0.25f;
                drawGlow(c, 260 * (0.6f + e), Color{ 255, 230, 200, (unsigned char)(160 * f) });
                DrawRing(c, 40 + e * 900, 44 + e * 900 + 6 * f, 0, 360, 64, Color{ 255, 220, 180, (unsigned char)(200 * f) });
            }
            EndBlendMode();
            for (int i = 0; i < nDots; ++i) {
                const TitleDot& d = cardDots[k][dotMap[k][i]];
                float r = Vector2Distance(d.home, c) / 300.0f;
                float del = 0.18f * std::min(1.0f, r) + 0.07f * dotDelay[i];       // blast ripples outward
                float l = std::clamp((e - del) / (1 - del), 0.0f, 1.0f);
                Vector2 p = Vector2Lerp(d.home, dotOut[k][i], ease3in(l));
                p.y += sinf(l * PI) * dotCurve[i] * 0.3f;
                float heat = std::max(0.0f, 1 - l * 3);           // white-hot at first, cooling to its colour
                Color col = ColorLerp(d.col, WHITE, heat * 0.7f);
                float sz = DOT * (1 + 0.6f * heat);
                DrawRectanglePro({ p.x, p.y, sz, sz }, { sz / 2, sz / 2 }, l * 720 + i * 37, col);
            }
            BeginBlendMode(BLEND_ADDITIVE);
            for (int i = 0; i < nDots; i += 9) {
                const TitleDot& d = cardDots[k][dotMap[k][i]];
                float r = Vector2Distance(d.home, c) / 300.0f;
                float del = 0.18f * std::min(1.0f, r) + 0.07f * dotDelay[i];
                float l = std::clamp((e - del) / (1 - del), 0.0f, 1.0f);
                Vector2 p = Vector2Lerp(d.home, dotOut[k][i], ease3in(l));
                p.y += sinf(l * PI) * dotCurve[i] * 0.3f;
                drawGlow(p, 7, Color{ d.col.r, d.col.g, d.col.b, (unsigned char)(140 * (1 - l * 0.5f)) });
            }
            EndBlendMode();
            return;
        }
        // ---- re-forming as the next card: the same particles fly back in from where they went
        float a = (u - form0) / T_FORM;
        float fadeOut = a > 0.9f ? 1 - (a - 0.9f) / 0.1f : 1;  // hand over to the crisp text at the very end
        for (int i = 0; i < nDots; ++i) {
            const TitleDot& from = cardDots[k][dotMap[k][i]];
            const TitleDot& to = cardDots[nk][dotMap[nk][i]];
            float del = 0.15f * dotDelay[i];
            float l = std::clamp((a - del) / 0.82f, 0.0f, 1.0f);
            float el = ease3out(l);
            Vector2 st = dotOut[k][i];
            Vector2 p = Vector2Lerp(st, to.home, el);
            Vector2 dir = Vector2Normalize(Vector2Subtract(to.home, st));
            p = Vector2Add(p, Vector2Scale({ -dir.y, dir.x }, sinf(el * PI) * dotCurve[i]));   // swirl in on a curve
            Color col = ColorLerp(from.col, to.col, std::min(1.0f, l * 1.5f));
            col.a = (unsigned char)(255 * fadeOut);
            float spin = (1 - el) * 540 + i * 37 * (1 - el);
            DrawRectanglePro({ p.x, p.y, DOT, DOT }, { DOT / 2, DOT / 2 }, spin, col);
        }
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < nDots; i += 9) {
            const TitleDot& to = cardDots[nk][dotMap[nk][i]];
            float l = std::clamp((a - 0.15f * dotDelay[i]) / 0.82f, 0.0f, 1.0f);
            float el = ease3out(l);
            Vector2 st = dotOut[k][i];
            Vector2 p = Vector2Lerp(st, to.home, el);
            Vector2 dir = Vector2Normalize(Vector2Subtract(to.home, st));
            p = Vector2Add(p, Vector2Scale({ -dir.y, dir.x }, sinf(el * PI) * dotCurve[i]));
            drawGlow(p, 7, Color{ to.col.r, to.col.g, to.col.b, (unsigned char)(120 * (1 - el) * fadeOut) });
        }
        if (a > 0.85f) drawGlow(c, 220, Color{ 255, 255, 255, (unsigned char)(90 * sinf((a - 0.85f) / 0.15f * PI)) });  // "snap" flash as it locks
        EndBlendMode();
    }

    // Crisp text phases: steady hold, then the 2 s instability (screen space)
    void drawTitleText() {
        int nC = (int)cards.size();
        float T = fmodf(titleT, T_CARD * nC);
        int k = (int)(T / T_CARD);
        float u = T - k * T_CARD;
        float blow0 = T_HOLD + T_UNSTABLE, form0 = blow0 + T_BLOW;
        int show = -1; float alpha = 1, inst = 0;
        if (u < T_HOLD) show = k;
        else if (u < blow0) { show = k; inst = (u - T_HOLD) / T_UNSTABLE; }
        else if (u >= form0 + T_FORM * 0.9f) { show = (k + 1) % nC; alpha = (u - form0 - T_FORM * 0.9f) / (T_FORM * 0.1f); }
        if (show < 0) return;
        const TitleCard& card = cards[show];
        if (inst <= 0) {
            for (auto& L : card.lines) { Color c = L.col; c.a = (unsigned char)(255 * alpha); drawCentered(L.text, L.y, L.size, c); }
            return;
        }
        // ---- instability: tremor, chromatic split, glitch slices, flicker, rising glow and sparks
        float k2 = inst * inst;
        if (GetRandomValue(0, 100) < (int)(30 * inst)) {
            const auto& dots = cardDots[show];
            const TitleDot& d = dots[GetRandomValue(0, (int)dots.size() - 1)];
            float ang = frand(0, 2 * PI), sp = frand(40, 160) * inst, life = frand(0.2f, 0.5f);
            parts.push_back({ d.home, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1, 2), d.col, SPARK });
        }
        bool flick = inst > 0.5f && GetRandomValue(0, 100) < (int)(25 * k2);
        float shake = 0.6f + 6 * k2;
        float split = 1.5f + 7 * k2;
        int sx = (int)origin.x, sw = (int)ceilf(W * scale);
        for (auto& L : card.lines) {
            float top = L.y, h = L.size * 1.1f;
            int bands = 7;
            for (int b = 0; b < bands; ++b) {
                float by = top + h * b / bands, bh = h / bands + 1;
                bool glitch = GetRandomValue(0, 100) < (int)(70 * k2);
                float off = frand(-shake, shake) + (glitch ? frand(-1, 1) * 26 * k2 : 0);
                float oy = frand(-shake, shake) * 0.5f;
                BeginScissorMode(sx, (int)(origin.y + by * scale), sw, (int)ceilf(bh * scale) + 1);
                Color base = L.col;
                if (flick) base = ColorLerp(base, WHITE, 0.6f);
                BeginBlendMode(BLEND_ADDITIVE);
                text(L.text, 0, L.y + oy, L.size, Color{ 255, 40, 60, (unsigned char)(150 * inst) }, true, off - split);
                text(L.text, 0, L.y + oy, L.size, Color{ 40, 200, 255, (unsigned char)(150 * inst) }, true, off + split);
                EndBlendMode();
                text(L.text, 0, L.y + oy, L.size, base, true, off);
            }
            BeginScissorMode((int)origin.x, (int)origin.y, (int)ceilf(W * scale), (int)ceilf(H * scale));
        }
    }

    // --- view / scaling -------------------------------------------------------
    // The game logic runs in a fixed 600x800 "logical" playfield. Everything is
    // drawn through a camera zoomed to fit the real window, so graphics are
    // rendered at the window's native resolution (no blurry upscaling).
    Font  font{};
    bool  customFont = false;
    float scale = 1;
    Vector2 origin{ 0, 0 };

    void loadFont() {
        const char* candidates[] = {
            "C:/Windows/Fonts/consolab.ttf", "C:/Windows/Fonts/lucon.ttf", "C:/Windows/Fonts/courbd.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
            "/System/Library/Fonts/Menlo.ttc" };
        for (const char* f : candidates) {
            if (!FileExists(f)) continue;
            font = LoadFontEx(f, 128, nullptr, 0);
            if (font.texture.id != 0) {
                GenTextureMipmaps(&font.texture);
                SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
                customFont = true;
                return;
            }
        }
        font = GetFontDefault();
    }

    void computeView() {
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        scale  = std::min(sw / W, sh / H);
        origin = { (sw - W * scale) / 2, (sh - H * scale) / 2 };
    }

    // Text drawn in screen space at the real pixel size, so it stays sharp at any scale.
    void text(const char* t, float x, float y, float size, Color c, bool centered = false, float dx = 0) {
        float fs = size * scale, sp = customFont ? fs * 0.04f : fs / 10;
        if (centered) x = W / 2.0f - MeasureTextEx(font, t, fs, sp).x / scale / 2 + dx;
        DrawTextEx(font, t, { origin.x + x * scale, origin.y + y * scale }, fs, sp, c);
    }
    void drawCentered(const char* t, float y, float size, Color c) { text(t, 0, y, size, c, true); }

    // --- draw ---------------------------------------------------------------
    void draw() {
        computeView();
        BeginDrawing();
        ClearBackground(BLACK);   // letterbox bars
        BeginScissorMode((int)origin.x, (int)origin.y, (int)ceilf(W * scale), (int)ceilf(H * scale));

        Camera2D cam{}; cam.zoom = scale; cam.offset = origin;
        if (shake > 0) cam.offset = Vector2Add(origin, { frand(-6, 6) * shake * scale, frand(-6, 6) * shake * scale });
        BeginMode2D(cam);
        const Theme& th = THEMES[mode == TITLE ? 0 : cycleIndex()];
        DrawRectangleGradientV(0, 0, W, H, th.top, th.bottom);
        if (mode != TITLE) {   // distant body for this cycle, drifting very slowly
            int ci = cycleIndex();
            const Texture2D& pt = planetTex(ci);
            float drift = fmodf(cycleT * 2.5f, H * 0.6f);
            Vector2 pp{ th.px * W, th.py * H + drift };
            if (th.style == 4 || th.style == 2 || th.style == 5) {
                BeginBlendMode(BLEND_ADDITIVE);
                Color halo = th.style == 5 ? Color{ 255, 120, 60, 50 } : Color{ 255, 220, 150, (unsigned char)(th.style == 4 ? 90 : 60) };
                drawGlow(pp, th.size * 0.9f, halo);
                EndBlendMode();
            }
            DrawTexturePro(pt, { 0, 0, (float)pt.width, (float)pt.height }, { pp.x, pp.y, th.size, th.size }, { th.size / 2, th.size / 2 }, 0,
                           Color{ 200, 200, 210, 165 });   // a little dimmer so enemies stand out against it
        }
        {   // slowly scrolling nebula (tileable texture), tinted per cycle
            float scroll = fmodf(time * 6.0f, (float)H);
            Rectangle src{ 0, -scroll * gfx.nebula.height / (float)H, (float)gfx.nebula.width, (float)gfx.nebula.height };
            DrawTexturePro(gfx.nebula, src, { 0, 0, (float)W, (float)H }, { 0, 0 }, 0, th.neb);
        }

        BeginBlendMode(BLEND_ADDITIVE);
        for (auto& s : stars) {
            float tw = 0.5f + 0.5f * sinf(time * 3 + s.phase);
            Color c = s.col; c.a = (unsigned char)std::min(255.0f, 60 + 195 * tw * (s.speed / 150.0f));
            float sz = s.speed > 100 ? 1.4f : 0.9f;
            DrawRectangleV({ s.pos.x - sz / 2, s.pos.y - sz / 2 }, { sz, sz * (mode == PLAYING ? 2.2f : 1.0f) }, c);
            if (s.speed > 130) drawGlow(s.pos, 3.5f, Color{ c.r, c.g, c.b, (unsigned char)(c.a / 3) });
        }
        EndBlendMode();

        // smoke under ships
        for (auto& p : parts) if (p.kind == SMOKE) {
            float t = p.life / p.maxLife;
            drawGlow(p.pos, p.size * (1.6f - 0.6f * t), Color{ p.col.r, p.col.g, p.col.b, (unsigned char)(110 * t) });
        }

        drawMeteors();
        drawBoss();

        int fr = fmodf(time, 0.5f) < 0.25f ? 0 : 1;
        for (auto& e : enemies) {
            if (e.state == DEAD || e.state == WAITING) continue;
            int f = e.state == FORMATION ? fr : (fmodf(time, 0.16f) < 0.08f ? 0 : 1);
            float g = 0.3f + 0.7f * e.grow;
            if (e.grow < 1) {                  // engine burst while emerging from the hatch
                BeginBlendMode(BLEND_ADDITIVE);
                drawGlow(e.pos, 30 * (1 - e.grow) + 8, Color{ 120, 200, 255, (unsigned char)(200 * (1 - e.grow)) });
                EndBlendMode();
            }
            switch (e.type) {
                case BEE:       drawShip(gfx.bee[f], e.pos, 42 * g, e.angle); break;
                case BUTTERFLY: drawShip(gfx.fly[f], e.pos, 46 * g, e.angle); break;
                case BOSS:      drawShip(e.hp < 2 ? gfx.bossDmg[f] : gfx.boss[f], e.pos, 56 * g, e.angle); break;
                case DART: {
                    if (e.state == DIVING) {        // afterburner streak while striking
                        BeginBlendMode(BLEND_ADDITIVE);
                        Vector2 v = Vector2Normalize(Vector2Subtract(e.pos, e.prev));
                        for (int k = 1; k <= 4; ++k) drawGlow(Vector2Subtract(e.pos, Vector2Scale(v, 8.0f * k)), 9 - k * 1.5f, Color{ 255, 120, 50, (unsigned char)(180 - k * 35) });
                        EndBlendMode();
                    }
                    drawShip(gfx.dart, e.pos, 40 * g, e.angle); break;
                }
                case SHIELDED:
                    drawShip(gfx.shielded[f], e.pos, 44 * g, e.angle);
                    if (e.hp > 1) {                 // projected energy shield
                        float pl = 0.6f + 0.4f * sinf(time * 5 + e.sx);
                        BeginBlendMode(BLEND_ADDITIVE);
                        drawGlow(e.pos, 30, Color{ 60, 220, 255, (unsigned char)(40 + 30 * pl) });
                        DrawRing(e.pos, 22.5f, 24.5f, 0, 360, 36, Color{ 110, 255, 240, (unsigned char)(120 + 80 * pl) });
                        DrawPolyLinesEx(e.pos, 6, 23, time * 40, 1.0f, Color{ 150, 255, 245, (unsigned char)(70 * pl) });
                        EndBlendMode();
                    }
                    break;
                case SPLITTER: drawShip(gfx.splitter[f], e.pos, 46 * g, e.angle); break;
                case COURIER: {
                    float S = 62 * g;
                    drawShip(gfx.courier[e.armourOff ? 1 : 0], e.pos, S, e.angle);
                    Color c = e.cargo >= 0 ? WCOL[e.cargo] : UPG_COL;          // cargo pod glows the colour of what it carries
                    Vector2 pc{ e.pos.x, e.pos.y - S * 0.01f };
                    float rr = S * 0.075f, pl = 0.75f + 0.25f * sinf(time * 5);
                    DrawCircleV(pc, rr * 1.05f, Color{ c.r, c.g, c.b, 235 });
                    BeginBlendMode(BLEND_ADDITIVE);
                    drawGlow(pc, rr * 3.4f * pl, Color{ c.r, c.g, c.b, 200 });
                    drawGlow(pc, rr * 0.9f, Color{ 255, 250, 235, 230 });
                    EndBlendMode();
                    if (e.hp < e.maxHp) {                                         // health bar
                        float f = (float)e.hp / e.maxHp;
                        DrawRectangle((int)(e.pos.x - 24), (int)(e.pos.y - 38), 48, 5, Color{ 30, 25, 20, 220 });
                        DrawRectangle((int)(e.pos.x - 24), (int)(e.pos.y - 38), (int)(48 * f), 5, c);
                    }
                    break;
                }
                case MINI:     drawShip(gfx.splitter[f], e.pos, 26 * g, e.angle); break;
            }
        }

        if ((mode == PLAYING || mode == VICTORY) && playerAlive) {
            bool blink = invuln > 0 && fmodf(time, 0.2f) < 0.1f;
            if (!blink) {
                float fl = 0.8f + 0.2f * sinf(time * 50);
                BeginBlendMode(BLEND_ADDITIVE);
                for (float sx : { -11.5f, 11.5f }) {
                    Vector2 n{ player.x + sx, player.y + 22 };
                    DrawTriangle({ n.x - 3.5f, n.y }, { n.x, n.y + 16 * fl }, { n.x + 3.5f, n.y }, Color{ 80, 150, 255, 200 });
                    DrawTriangle({ n.x - 2, n.y }, { n.x, n.y + 9 * fl }, { n.x + 2, n.y }, Color{ 220, 240, 255, 255 });
                    drawGlow({ n.x, n.y + 4 }, 12 * fl, Color{ 90, 160, 255, 180 });
                }
                EndBlendMode();
                drawShip(gfx.player, player, 50, 0);
            }
            if (shield) drawShield();
        }

        BeginBlendMode(BLEND_ADDITIVE);
        for (auto& p : parts) if (p.kind == TRAIL) {
            float t = p.life / p.maxLife;
            Color c = p.col; c.a = (unsigned char)(200 * t);
            drawGlow(p.pos, p.size * (0.6f + 0.4f * t), c);
        }
        for (auto& b : pBullets) {
            Vector2 dv = Vector2Length(b.vel) > 1 ? Vector2Normalize(b.vel) : Vector2{ 0, -1 };
            if (b.kind == PB_PHOTON) {         // photon torpedo
                float pulse = 1 + 0.18f * sinf(time * 38 + b.pos.x);
                float spin = time * 540 + b.pos.x * 3;
                for (int k = 1; k <= 6; ++k)    // comet tail
                    drawGlow({ b.pos.x, b.pos.y + k * 6.0f }, 10 - k * 1.2f, Color{ 255, 100, 40, (unsigned char)(150 - k * 20) });
                drawGlow(b.pos, 30 * pulse, Color{ 255, 60, 25, 130 });
                drawGlow(b.pos, 17 * pulse, Color{ 255, 120, 50, 230 });
                drawGlow(b.pos, 8, Color{ 255, 210, 150, 255 });
                for (int k = 0; k < 2; ++k) {   // rotating star-flare spikes
                    float a = spin + k * 90;
                    DrawRectanglePro({ b.pos.x, b.pos.y, 30 * pulse, 1.6f }, { 15 * pulse, 0.8f }, a, Color{ 255, 170, 110, 170 });
                    DrawRectanglePro({ b.pos.x, b.pos.y, 16, 1.0f }, { 8, 0.5f }, a + 45, Color{ 255, 230, 200, 140 });
                }
                DrawCircleV(b.pos, 3.2f, WHITE);
            } else if (b.kind == PB_SPREAD) {  // spread plasma bolt
                drawGlow(b.pos, 18, Color{ 255, 150, 40, 210 });
                for (int k = 1; k <= 3; ++k) drawGlow(Vector2Subtract(b.pos, Vector2Scale(dv, k * 7.0f)), 9 - k * 2.0f, Color{ 255, 140, 40, (unsigned char)(170 - k * 40) });
                DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 8)), Vector2Add(b.pos, Vector2Scale(dv, 6)), 5, Color{ 255, 220, 120, 255 });
                DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 5)), Vector2Add(b.pos, Vector2Scale(dv, 5)), 2.2f, WHITE);
            } else if (b.kind == PB_HOMING) {  // homing missile (body drawn here; trail from particles)
                drawGlow(Vector2Subtract(b.pos, Vector2Scale(dv, 7)), 12, Color{ 160, 255, 140, 230 });
                float ang = atan2f(dv.y, dv.x) * RAD2DEG;
                DrawRectanglePro({ b.pos.x, b.pos.y, 15, 4.5f }, { 7.5f, 2.25f }, ang, Color{ 215, 245, 215, 255 });
                DrawCircleV(Vector2Add(b.pos, Vector2Scale(dv, 7)), 2.4f, WCOL[W_HOMING]);
            } else if (b.kind == PB_ION) {     // ion orb with crackle
                drawGlow(b.pos, 32, Color{ 170, 90, 255, 180 });
                drawGlow(b.pos, 16, Color{ 210, 150, 255, 255 });
                DrawCircleV(b.pos, 5, WHITE);
            }
        }
        for (auto& b : pBullets) if (b.kind == PB_ION) {
            unsigned sd = (unsigned)(b.pos.y * 13 + b.pos.x) + (unsigned)(time * 20);
            for (int i = 0; i < 3; ++i) { float a = (sd % 628) / 100.0f + i * 2.1f;
                drawZap(b.pos, { b.pos.x + cosf(a) * 16, b.pos.y + sinf(a) * 16 }, sd + i, WCOL[W_ION], 1, 220); }
        }
        if (laserOn && playerAlive) {           // pulse laser beam
            static const float wid[4] = { 4.5f, 6, 8, 11 };
            float w = wid[wlevel], x = player.x, y0 = player.y - 26, y1 = laserTop;
            for (float y = y1; y < y0; y += 6) {
                float ww = w * (1 + 0.12f * sinf(y * 0.09f + time * 30));
                DrawRectangleV({ x - ww * 1.6f, y }, { ww * 3.2f, 6.5f }, Color{ 40, 180, 255, 45 });
                DrawRectangleV({ x - ww / 2, y }, { ww, 6.5f }, Color{ 70, 220, 255, 200 });
                DrawRectangleV({ x - ww * 0.18f, y }, { ww * 0.36f, 6.5f }, Color{ 230, 255, 255, 255 });
            }
            drawGlow({ x, y0 }, w * 2.4f, Color{ 70, 220, 255, 230 });
            if (y1 > 0) drawGlow({ x, y1 }, w * 2.8f, Color{ 120, 240, 255, 230 });
        }
        for (auto& z : zaps) {
            EndBlendMode();
            drawZap(z.a, z.b, z.seed, WCOL[W_ION], 2.2f, (unsigned char)(255 * std::min(1.0f, z.life / 0.15f)));
            BeginBlendMode(BLEND_ADDITIVE);
        }
        for (auto& b : eBullets) {
            Vector2 dv = Vector2Length(b.vel) > 1 ? Vector2Normalize(b.vel) : Vector2{ 0, 1 };
            if (b.kind == BK_ORB) {            // PLASMA FAN: big pulsing orb with a short comet tail
                float pl = 1 + 0.18f * sinf(time * 30 + b.pos.x);
                Color c = coreColor(170);
                for (int k = 1; k <= 3; ++k)
                    drawGlow(Vector2Subtract(b.pos, Vector2Scale(dv, k * 7.0f)), 16 - k * 3.0f, Color{ c.r, c.g, c.b, (unsigned char)(200 - k * 50) });
                drawGlow(b.pos, 32 * pl, Color{ c.r, c.g, c.b, 210 });
                drawGlow(b.pos, 15, Color{ c.r, c.g, c.b, 255 });
                drawGlow(b.pos, 8, Color{ 255, 240, 250, 255 });
                DrawCircleV(b.pos, 4.2f * pl, WHITE);
            } else if (b.kind == BK_BOLT) {    // HUNTER VOLLEY: long tracer bolt along its flight path
                Vector2 tail = Vector2Subtract(b.pos, Vector2Scale(dv, 18)), tip = Vector2Add(b.pos, Vector2Scale(dv, 5));
                drawGlow(b.pos, 22, Color{ 255, 170, 60, 200 });
                DrawLineEx(tail, tip, 7, Color{ 255, 170, 60, 200 });
                DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 12)), tip, 2.6f, Color{ 255, 250, 220, 255 });
            } else if (b.kind == BK_SPIRAL) {  // VORTEX SPIRAL: hue-shifting orb (ribbon trail comes from particles)
                Color c = spiralColor(b.aux);
                drawGlow(b.pos, 26, Color{ c.r, c.g, c.b, 210 });
                drawGlow(b.pos, 11, Color{ c.r, c.g, c.b, 255 });
                DrawCircleV(b.pos, 3.2f, WHITE);
            } else if (b.kind == BK_CRYSTAL) { // NOVA RING: spinning diamond crystal
                Color c = coreColor();
                float rot = b.age * 360 + b.pos.x;
                drawGlow(b.pos, 26, Color{ c.r, c.g, c.b, 190 });
                DrawPoly(b.pos, 4, 9, rot, Color{ c.r, c.g, c.b, 240 });
                DrawPoly(b.pos, 4, 4, rot, WHITE);
            } else if (b.kind == BK_MISSILE) { // SEEKER: engine flame (body drawn below, unblended)
                float fl = 0.8f + 0.2f * sinf(time * 60 + b.pos.x);
                Vector2 back = Vector2Subtract(b.pos, Vector2Scale(dv, 9));
                drawGlow(back, 22 * fl, Color{ 255, 130, 40, 230 });
                DrawLineEx(back, Vector2Subtract(back, Vector2Scale(dv, 20 * fl)), 6, Color{ 255, 190, 80, 230 });
                DrawLineEx(back, Vector2Subtract(back, Vector2Scale(dv, 10 * fl)), 2.5f, Color{ 255, 250, 220, 255 });
                if (fmodf(time + b.pos.x * 0.01f, 0.25f) < 0.12f) drawGlow(Vector2Add(b.pos, Vector2Scale(dv, 12)), 12, Color{ 255, 40, 40, 255 });
            } else if (b.kind == BK_C_SPREAD) {   // courier: basic spread bolt
                drawGlow(b.pos, 14, Color{ 255, 150, 40, 200 });
                DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 6)), Vector2Add(b.pos, Vector2Scale(dv, 4)), 4, Color{ 255, 220, 120, 255 });
            } else if (b.kind == BK_C_PULSE) {    // courier: short laser burst
                drawGlow(b.pos, 14, Color{ 70, 220, 255, 200 });
                DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 22)), b.pos, 4, Color{ 70, 220, 255, 230 });
                DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 18)), b.pos, 1.6f, WHITE);
            } else if (b.kind == BK_C_HOMING) {   // courier: slow seeker
                drawGlow(b.pos, 14, Color{ 110, 255, 120, 200 });
                DrawRectanglePro({ b.pos.x, b.pos.y, 13, 4 }, { 6.5f, 2 }, atan2f(dv.y, dv.x) * RAD2DEG, Color{ 215, 245, 215, 255 });
            } else if (b.kind == BK_C_ION) {      // courier: slow ion orb
                drawGlow(b.pos, 24, Color{ 170, 90, 255, 180 });
                drawGlow(b.pos, 10, Color{ 230, 200, 255, 255 });
                DrawCircleV(b.pos, 3.2f, WHITE);
            } else if (b.kind == BK_C_PHOTON) {   // courier: basic torpedo
                drawGlow(b.pos, 18, Color{ 255, 90, 40, 170 });
                drawGlow(b.pos, 8, Color{ 255, 210, 150, 255 });
                DrawCircleV(b.pos, 2.6f, WHITE);
            } else if (b.kind == BK_METEOR) {  // METEOR STORM: fiery entry tail (rock drawn below)
                drawGlow(b.pos, 28, Color{ 255, 100, 30, 200 });
                for (int k = 1; k <= 6; ++k)
                    drawGlow(Vector2Subtract(b.pos, Vector2Scale(dv, k * 8.0f)), 18 - k * 2.4f, Color{ 255, 150, 50, (unsigned char)(220 - k * 32) });
            } else {
                drawGlow(b.pos, 11, Color{ 255, 60, 80, 170 });
                drawGlow(b.pos, 4.5f, Color{ 255, 220, 220, 255 });
            }
        }
        for (auto& r : rings) {
            float t = 1 - r.life / r.maxLife;
            Color c = r.col; c.a = (unsigned char)(200 * (1 - t));
            DrawRing(r.pos, r.radius * t, r.radius * t + 3 * (1 - t) + 1, 0, 360, 48, c);
        }
        for (auto& p : parts) {
            float t = p.life / p.maxLife;
            if (p.kind == SPARK) {
                Color c = p.col; c.a = (unsigned char)(255 * t);
                drawGlow(p.pos, p.size * 3, c);
                DrawCircleV(p.pos, p.size * 0.45f, Color{ 255, 255, 255, (unsigned char)(255 * t) });
            } else if (p.kind == FLASH) {
                Color c = p.col; c.a = (unsigned char)(255 * t);
                drawGlow(p.pos, p.size * (1.3f - 0.5f * t), c);
            } else if (p.kind == EMBER) {
                Color c = p.col; c.a = (unsigned char)(200 * t);
                drawGlow(p.pos, p.size, c);
            }
        }
        EndBlendMode();
        for (auto& b : eBullets) {
            if (b.kind == BK_MISSILE) {
                float ang = atan2f(b.vel.y, b.vel.x) * RAD2DEG;
                Vector2 dv = Vector2Normalize(b.vel), pv{ -dv.y, dv.x };
                Vector2 tailC = Vector2Subtract(b.pos, Vector2Scale(dv, 6));
                for (int s2 : { -1, 1 }) {      // fins
                    Vector2 a1 = Vector2Add(tailC, Vector2Scale(pv, s2 * 3.0f)), a2 = Vector2Add(Vector2Subtract(tailC, Vector2Scale(dv, 6)), Vector2Scale(pv, s2 * 9.0f)),
                            a3 = Vector2Add(Vector2Add(tailC, Vector2Scale(dv, 5)), Vector2Scale(pv, s2 * 3.0f));
                    DrawTriangle(a1, a2, a3, Color{ 170, 45, 45, 255 }); DrawTriangle(a1, a3, a2, Color{ 170, 45, 45, 255 });
                }
                DrawRectanglePro({ b.pos.x, b.pos.y, 24, 7 }, { 12, 3.5f }, ang, Color{ 190, 190, 205, 255 });
                DrawRectanglePro({ b.pos.x, b.pos.y, 24, 2.5f }, { 12, 3.0f }, ang, Color{ 245, 245, 255, 255 });
                DrawCircleV(Vector2Add(b.pos, Vector2Scale(dv, 12)), 3.5f, Color{ 200, 50, 50, 255 });
            } else if (b.kind == BK_METEOR) {
                int v = std::clamp((int)b.aux, 0, 5);
                drawShip(gfx.meteor[v], b.pos, 24 + v * 1.5f, b.age * (v % 2 ? 260 : -200), false);
            }
        }
        if (playerAlive && mode == PLAYING) {   // missile lock-on reticle
            bool locked = false;
            for (auto& b : eBullets) if (b.kind == BK_MISSILE && b.age < 2.2f) { locked = true; break; }
            if (locked) {
                float r = 34 + 3 * sinf(time * 12), rot = time * 90;
                for (int k = 0; k < 4; ++k) {
                    float a0 = (rot + k * 90) * DEG2RAD;
                    Vector2 c{ player.x + cosf(a0) * r, player.y + sinf(a0) * r };
                    DrawRing(player, r - 1.5f, r + 1.5f, rot + k * 90 - 14, rot + k * 90 + 14, 8, Color{ 255, 60, 60, 220 });
                    DrawCircleV(c, 1.8f, Color{ 255, 120, 120, 255 });
                }
            }
        }
        for (auto& w : warnings) {               // meteor-storm entry markers
            bool on = fmodf(w.t, 0.12f) < 0.07f;
            Color c{ 255, 60, 50, (unsigned char)(on ? 255 : 120) };
            DrawTriangle({ w.x - 8, 4 }, { w.x, 18 }, { w.x + 8, 4 }, c);
            DrawRectangle((int)w.x - 1, 20, 2, 6, c);
        }
        for (auto& p : parts) if (p.kind == DEBRIS || p.kind == HULL) {
            float t = p.life / p.maxLife;
            Color c = p.col; c.a = (unsigned char)(255 * (p.kind == HULL ? std::min(1.0f, t * 3) : t));
            DrawRectanglePro({ p.pos.x, p.pos.y, p.size * 1.8f, p.size * 0.8f }, { p.size * 0.9f, p.size * 0.4f }, p.rot, c);
        }
        for (auto& pk : packages) drawPackage(pk.content, pk.pos, 44, pk.t);
        drawHudShapes();
        if (escMenu) drawEscMenuShapes();
        EndMode2D();

        if (escMenu) { drawEscMenuText(); EndScissorMode(); EndDrawing(); return; }
        for (auto& p : popups) text(p.text.c_str(), p.pos.x - 16, p.pos.y, 18, SKYBLUE);
        drawHudText();
        EndScissorMode();
        EndDrawing();
    }

    // Shimmering hexagonal force field around the player
    void drawShield() {
        float g = shieldGrow, ease = 1 - (1 - g) * (1 - g) * (1 - g);
        float r = SHIELD_RADIUS * (0.4f + 0.6f * ease) * (1 + 0.025f * sinf(time * 6));
        float pulse = 0.75f + 0.25f * sinf(time * 4.3f);
        unsigned char a = (unsigned char)(255 * std::min(1.0f, g * 1.5f));
        auto A = [&](float f) { return (unsigned char)(a * std::min(1.0f, f)); };
        BeginBlendMode(BLEND_ADDITIVE);
        drawGlow(player, r * 1.25f, Color{ 40, 140, 255, A(0.22f * pulse) });           // soft inner fill
        DrawRing(player, r - 1.5f, r + 1.5f, 0, 360, 64, Color{ 90, 200, 255, A(0.85f * pulse) });  // rim
        DrawRing(player, r - 5, r - 1.5f, 0, 360, 64, Color{ 60, 150, 255, A(0.25f) });
        // rotating energy arcs
        for (int k = 0; k < 3; ++k) {
            float st = time * 120 + k * 120;
            DrawRing(player, r + 0.5f, r + 3.0f, st, st + 50, 16, Color{ 200, 245, 255, A(0.7f) });
        }
        // hex lattice clipped to the bubble
        float hs = 9.0f, off = fmodf(time * 6, hs * 1.732f);
        for (int row = -5; row <= 5; ++row)
            for (int col = -5; col <= 5; ++col) {
                Vector2 c{ player.x + col * hs * 1.5f, player.y + row * hs * 1.732f + (col & 1) * hs * 0.866f - off };
                float d = Vector2Distance(c, player);
                if (d > r - hs * 0.6f) continue;
                float edge = d / r;   // lattice is brighter near the rim, like a curved surface
                DrawPolyLinesEx(c, 6, hs * 0.95f, 0, 1.0f, Color{ 120, 210, 255, A(0.08f + 0.35f * edge * edge) });
            }
        // specular highlight
        DrawRing(player, r - 6.5f, r - 3.5f, 200, 255, 24, Color{ 230, 250, 255, A(0.55f) });   // crescent glint
        if (g < 1) DrawRing(player, r, r + 10 * (1 - g), 0, 360, 64, Color{ 200, 245, 255, A(1 - g) });  // power-up flash
        EndBlendMode();
    }

    // HUD graphics (drawn inside the camera, in logical coordinates)
    void drawHudShapes() {
        if (mode == TITLE) {
            int f = fmodf(time, 0.5f) < 0.25f ? 0 : 1;
            if (stageDialog) { drawStageDialogShapes(); return; }
            drawTitleParticles();
            drawShip(gfx.boss[f], { W / 2.0f - 120, 340 }, 56, 0);
            drawShip(gfx.fly[f],  { W / 2.0f - 120, 400 }, 46, 0);
            drawShip(gfx.bee[f],  { W / 2.0f - 120, 460 }, 42, 0);
            return;
        }
        for (int i = 0; i < std::max(0, lives - (playerAlive ? 1 : 0)); ++i)
            drawShip(gfx.player, { 26.0f + i * 34, H - 22.0f }, 30, 0, false);
        if (mode == PLAYING || mode == VICTORY) drawWeaponIndicator();
        for (int i = 0; i < stage && i < 12; ++i) {
            int st = stage - i;
            float x = W - 20.0f - i * 16;
            StageKind k = stageKind(st);
            DrawLineEx({ x, H - 34.0f }, { x, H - 8.0f }, 2, RAYWHITE);
            DrawTriangle({ x, H - 34.0f }, { x, H - 24.0f }, { x + 11, H - 29.0f },
                         k == SK_BOSS ? GOLD : (k == SK_METEORS ? Color{ 190, 130, 70, 255 } : RED));
        }
        if (boss.active && boss.state != B_ENTER) {
            float bw = 340, x = W / 2.0f - bw / 2, y = 76, f = std::max(0.0f, boss.hp / boss.maxHp);
            DrawRectangleRounded({ x - 3, y - 3, bw + 6, 16 }, 0.5f, 6, Color{ 0, 0, 0, 170 });
            DrawRectangleRounded({ x, y, bw, 10 }, 0.5f, 6, Color{ 60, 20, 40, 255 });
            Color c = boss.phase() == 0 ? Color{ 230, 60, 160, 255 } : (boss.phase() == 1 ? Color{ 255, 120, 40, 255 } : Color{ 255, 40, 40, 255 });
            if (f > 0) DrawRectangleRounded({ x, y, bw * f, 10 }, 0.5f, 6, c);
            DrawRectangle((int)x, (int)y + 1, (int)(bw * f), 3, Color{ 255, 255, 255, 60 });
        }
        if (meteorField && mode == PLAYING) {
            float bw = 340, x = W / 2.0f - bw / 2, y = 76, f = std::clamp(fieldT / fieldDur, 0.0f, 1.0f);
            DrawRectangleRounded({ x - 3, y - 3, bw + 6, 12 }, 0.5f, 6, Color{ 0, 0, 0, 170 });
            DrawRectangleRounded({ x, y, bw, 6 }, 0.5f, 6, Color{ 50, 40, 30, 255 });
            if (f > 0) DrawRectangleRounded({ x, y, bw * f, 6 }, 0.5f, 6, Color{ 220, 150, 70, 255 });
        }
        if (mode == GAMEOVER) DrawRectangle(0, H / 2 - 120, W, 240, Color{ 0, 0, 0, 170 });
        if (mode == VICTORY)  DrawRectangle(0, H / 2 - 170, W, 330, Color{ 0, 0, 0, 150 });
    }

    // HUD text (drawn in screen space at native resolution)
    void drawHudText() {
        if (mode == TITLE && stageDialog) { drawStageDialogText(); return; }
        if (mode == TITLE) {
            drawTitleText();
            text("150 / 400", W / 2.0f - 60, 328, 22, GREEN);
            text(" 80 / 160", W / 2.0f - 60, 388, 22, RED);
            text(" 50 / 100", W / 2.0f - 60, 448, 22, YELLOW);
            text("formation / diving", W / 2.0f - 60, 490, 14, DARKGRAY);
            if (fmodf(time, 1.0f) < 0.6f) drawCentered("PRESS SPACE TO START", 570, 26, RAYWHITE);
            drawCentered("ARROWS / A D  move    SPACE  fire    P / ESC  pause", 640, 16, GRAY);
            drawCentered("F11 / ALT+ENTER  fullscreen", 664, 16, GRAY);
            drawCentered(TextFormat("HIGH SCORE  %d", hiScore), 710, 22, GOLD);
            drawCentered("CTRL+L  debug stage select", 760, 12, Color{ 90, 90, 110, 255 });
            return;
        }

        text("1UP", 20, 10, 18, RED);
        if (mode == PLAYING && playerAlive)
            drawCentered(shield ? "SHIELD" : "SHIELD DOWN", H - 26, 16,
                 shield ? Color{ 110, 210, 255, 255 } : Color{ 140, 90, 90, 255 });
        text(TextFormat("%d", score), 20, 30, 22, RAYWHITE);
        drawCentered("HIGH SCORE", 10, 18, RED);
        drawCentered(TextFormat("%d", hiScore), 30, 22, RAYWHITE);

        if (mode == PLAYING && bannerTimer > 0) {
            if (boss.active && boss.state == B_ENTER) {
                if (fmodf(time, 0.5f) < 0.33f) drawCentered("WARNING", H / 2 - 50, 52, Color{ 255, 50, 60, 255 });
                drawCentered(TextFormat("STAGE %d  -  BOSS %d OF %d", stage, boss.index + 1, BOSS_COUNT), H / 2 + 12, 20, Color{ 255, 150, 170, 255 });
                drawCentered(TextFormat("%s  -  %s", boss.D().name, boss.D().title), H / 2 + 40, 18, Color{ 255, 200, 210, 255 });
            } else if (meteorField) {
                drawCentered(TextFormat("STAGE %d", stage), H / 2 - 60, 40, SKYBLUE);
                drawCentered("METEOR FIELD", H / 2 - 10, 32, Color{ 230, 160, 80, 255 });
                drawCentered(TextFormat("boss ahead:  %s", spr::bossDesigns()[cycleIndex()].name), H / 2 + 30, 16, Color{ 255, 170, 200, 255 });
            } else {
                drawCentered(TextFormat("STAGE %d", stage), H / 2 - 40, 40, SKYBLUE);
                drawCentered(formationName.c_str(), H / 2 + 8, 18, Color{ 170, 210, 255, 255 });
                int wn = cycleIndex() * 4 + (stage - 1) % CYCLE;
                const char* intro = wn == NEW_TYPE_WAVE[0] ? "NEW ENEMY: DART - strikes straight down"
                                  : wn == NEW_TYPE_WAVE[1] ? "NEW ENEMY: SHIELDED - takes 2 hits"
                                  : wn == NEW_TYPE_WAVE[2] ? "NEW ENEMY: SPLITTER - bursts into two minis" : nullptr;
                if (intro) drawCentered(intro, H / 2 + 36, 15, Color{ 255, 210, 120, 255 });
            }
        }
        if (boss.active && boss.state != B_ENTER)
            drawCentered(TextFormat("%s  -  %d / %d", boss.D().name, boss.index + 1, BOSS_COUNT), 58, 14, Color{ 255, 170, 210, 255 });
        if (debugRun && mode != TITLE) text("DEBUG", W - 70, 12, 14, Color{ 255, 200, 60, 255 });
        if (meteorField && mode == PLAYING) {
            drawCentered("METEOR FIELD", 58, 14, Color{ 230, 170, 110, 255 });
            if (debugRun) text(TextFormat("density %2.0f%%", density() * 100), W - 110, 32, 13, Color{ 255, 200, 60, 255 });
        }
        if (mode == PLAYING && !playerAlive && lives > 0 && respawnTimer < 1.5f)
            drawCentered("READY", H / 2 + 30, 32, RED);
        if (paused) drawCentered("PAUSED", H / 2 - 20, 44, YELLOW);

        if (mode == VICTORY) {
            float bob = sinf(time * 2) * 5;
            drawCentered("VICTORY!", H / 2 - 150 + bob, 64, GOLD);
            drawCentered("all ten capital ships destroyed", H / 2 - 72, 20, Color{ 255, 230, 160, 255 });
            drawCentered("the galaxy is safe", H / 2 - 46, 18, Color{ 200, 220, 255, 255 });
            drawCentered(TextFormat("FINAL SCORE   %d", score), H / 2 - 6, 28, RAYWHITE);
            drawCentered(TextFormat("HIT RATIO   %.1f%%     LIVES LEFT   %d", shotsFired ? 100.0f * hits / shotsFired : 0.0f, lives), H / 2 + 34, 18, YELLOW);
            if (debugRun) drawCentered("(debug run - high score not recorded)", H / 2 + 62, 14, GRAY);
            if (stageTimer > 3.0f && fmodf(time, 1.0f) < 0.6f) drawCentered("PRESS SPACE", H / 2 + 110, 24, RAYWHITE);
        }
        if (mode == GAMEOVER) {
            drawCentered("GAME OVER", H / 2 - 100, 52, RED);
            drawCentered(TextFormat("STAGE REACHED   %d", stage), H / 2 - 30, 22, RAYWHITE);
            drawCentered(TextFormat("SHOTS FIRED   %d", shotsFired), H / 2 + 0, 22, RAYWHITE);
            drawCentered(TextFormat("HIT RATIO   %.1f%%", shotsFired ? 100.0f * hits / shotsFired : 0.0f), H / 2 + 30, 22, YELLOW);
            if (stageTimer > 2.0f && fmodf(time, 1.0f) < 0.6f) drawCentered("PRESS SPACE", H / 2 + 75, 24, RAYWHITE);
        }
    }

    // --- persistence ----------------------------------------------------------
    void loadHiScore() { std::ifstream f(HISCORE_FILE); if (f) f >> hiScore; }
    void saveHiScore() { if (debugRun) return; std::ofstream f(HISCORE_FILE); if (f) f << hiScore; }
};

// On laptops with hybrid graphics, ask the NVIDIA (Optimus) and AMD (PowerXpress) drivers
// to run the game on the discrete, high-performance GPU. Ignored on single-GPU systems.
#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// ----------------------------------------------------------------------------
int main(int argc, char** argv) {
    // --selftest: auto-plays for a few seconds, saves a screenshot, exits (for CI/headless checks)
    bool selftest = argc > 1 && std::string(argv[1]) == "--selftest";

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(W, H, "Star Swarm");
    SetWindowMinSize(W / 2, H / 2);
    if (selftest) SetWindowSize(960, 1100);   // exercise scaling + letterboxing
    else {
        // Size the window to ~88% of the monitor height (3:4 aspect) and centre it.
        int m = GetCurrentMonitor();
        int mw = GetMonitorWidth(m), mh = GetMonitorHeight(m);
        int wh = (int)(mh * 0.88f), ww = wh * W / H;
        if (ww > mw) { ww = (int)(mw * 0.9f); wh = ww * H / W; }
        Vector2 mp = GetMonitorPosition(m);
        SetWindowSize(ww, wh);
        SetWindowPosition((int)mp.x + (mw - ww) / 2, (int)mp.y + std::max(30, (mh - wh) / 2));
    }
    SetExitKey(KEY_NULL);   // ESC handled manually (closes the debug dialog instead of quitting)
    InitAudioDevice();
    SetTargetFPS(60);
    SetRandomSeed((unsigned)std::time(nullptr));

    static Game game;
    for (int i = 0; i < SFX_COUNT; ++i) game.sfx[i] = makeSound((SfxKind)i);
    game.initStars();
    game.loadHiScore();
    game.loadFont();
    game.buildTitleParticles();
    gfx.load();
    if (selftest) {
        int st = (argc > 3 && isdigit((unsigned char)argv[3][0])) ? atoi(argv[3]) : 1;
        if (argc > 3 && std::string(argv[3]) == "boss") st = CYCLE;
        if (argc > 3 && std::string(argv[3]) == "dialog") { game.stageDialog = true; game.dlgText = "35"; }
        else { game.debugRun = st != 1; game.newGame(st); }
        if (argc > 4) { game.boss.hp = std::min(game.boss.hp, (float)atof(argv[4])); }   // optional: cap boss HP
    }

    int frame = 0;
    int shotFrame = (selftest && argc > 2) ? atoi(argv[2]) : 900;
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (game.stageDialog) game.stageDialog = false;
            else if (game.escMenu) game.escMenu = false;               // ESC again = resume
            else if (game.mode == TITLE) break;                         // title screen: quit as before
            else game.openEscMenu();
        }
        if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER)))
            ToggleBorderlessWindowed();
        float dt = std::min(GetFrameTime(), 1.0f / 30.0f);
        if (selftest) {
            dt = 1.0f / 60.0f;
            game.autoFire = !getenv("SS_NOFIRE");
            if (frame == 0 && getenv("SS_WEAPON")) { game.weapon = std::clamp(atoi(getenv("SS_WEAPON")), 0, W_COUNT - 1);
                game.wlevel = getenv("SS_LEVEL") ? std::clamp(atoi(getenv("SS_LEVEL")), 0, 3) : 0; }
            game.player.x = W / 2.0f + sinf(frame * 0.02f) * 200;
            if (getenv("SS_GOD")) game.invuln = 0.05f;   // selftest-only invulnerability
            if (getenv("SS_ESC") && frame == atoi(getenv("SS_ESC"))) { game.openEscMenu(); if (getenv("SS_ESCSEL")) game.escSel = atoi(getenv("SS_ESCSEL")); }
            if (getenv("SS_ESCDO") && frame == atoi(getenv("SS_ESCDO"))) game.escChoose(game.escSel);
        }
        if (selftest && argc > 3 && std::string(argv[3]) == "break" && frame == shotFrame - 6 && game.shield) game.breakShield({ game.player.x, game.player.y - SHIELD_RADIUS });
        game.update(dt);
        if (game.quitRequested) break;
        game.draw();
        if (selftest && ++frame == shotFrame) { TakeScreenshot("selftest.png"); break; }
    }

    if (game.score >= game.hiScore) game.saveHiScore();
    if (game.customFont) UnloadFont(game.font);
    gfx.unload();
    for (auto& s : game.sfx) UnloadSound(s);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
