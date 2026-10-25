// =============================================================================
//  STAR SWARM  -  a Galaga-inspired arcade shooter in C++ / raylib
// -----------------------------------------------------------------------------
//  Controls:  LEFT/RIGHT or A/D  move     SPACE  fire     P  pause     ESC  quit
//  All graphics are procedural pixel-art; all sounds are synthesized at startup.
// =============================================================================
#include "raylib.h"
#include "raymath.h"
#include "sprites.h"
#include "meteor3d.h"
#include "monsters.h"
#include "sfxsynth.h"
#include "titlemusic.h"
#include <thread>
#include <atomic>
#include <functional>

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
// Player movement area: the ship (wingtips +-23, nose -25, plume below) always stays fully on screen.
static const float  PLAYER_MARGIN_X = 34.0f;      // keep the whole ship and its shield bubble inside the sides
static const float  PLAYER_Y_LOW    = 800 - 70.0f;   // lowest position (the original fixed row)
static const float  PLAYER_Y_HIGH   = 800 - 490.0f;  // how far up the ship may fly (lower ~60% of the screen)
static const size_t MAX_TORPEDOES = 5;          // projectiles allowed on screen at once
static const char* HISCORE_FILE = "starswarm_highscore.txt";
static const char* SETTINGS_FILE = "starswarm_settings.txt";   // title_muted=0/1

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
    Texture2D playerRoll[5]{};                         // banked frames: -30 -15 0 +15 +30 degrees
    Texture2D player{}, glow{}, nebula{};
    // Space-monster enemies: MON_FRAMES-frame looping animations, rendered in parallel at startup
    enum MonKind { MON_URCHIN, MON_MANTA, MON_JELLY, MON_JELLY_HIT, MON_IMP, MON_BRUTE, MON_LEVIATHAN, MON_SPAWN, MON_COUNT };
    static constexpr int MON_FRAMES = 8, MON_N = 256;
    Texture2D mon[MON_COUNT][MON_FRAMES]{};
    static constexpr int BOSS_N = 512;
    Texture2D meteor[7]{};                             // 3D tumble atlases (m3d); [6] = volatile rock
    Texture2D courier[2]{};                            // [1] = armour blown off
    Texture2D drone{};                                 // wingman drone
    Texture2D outpost{};                               // mining-outpost overlay
    Texture2D planet[10]{};                            // one backdrop body per boss cycle
    bool planetLoaded[10]{};
    Texture2D bossTex[10]{};
    Texture2D bossIcon[10]{};                          // small boss portraits for the title-screen roster
    std::vector<unsigned char> bossMask[10];          // alpha masks for pixel-accurate hit tests
    bool bossLoaded[10]{};
    void ensureBoss(int i) {
        if (bossLoaded[i]) return;
        bossTex[i] = spr::bossShip(i, BOSS_N, &bossMask[i]);
        bossLoaded[i] = true;
    }
    void load() {
        const int N = 256;
        player = spr::playerShip(320);
        for (int i = 0; i < 5; ++i) playerRoll[i] = i == 2 ? player : spr::playerShip(320, -30.0f + 15 * i);
        loadMonsters();
        for (int i = 0; i < 7; ++i) {   // 64-frame 3D tumble atlases (GPU-rendered); old sprite = fallback
            Texture2D flat = spr::meteor(192, i);
            meteor[i] = m3d::atlas(i, &flat);
            UnloadTexture(flat);
        }
        courier[0] = spr::courier(N, false); courier[1] = spr::courier(N, true);
        drone = spr::wingDrone(N);
        outpost = spr::miningOutpost(384);
        glow = spr::glow(64);
        nebula = spr::nebula(300, 400);
    }
    // CPU-render every monster frame on all cores (render() returns images via g_imgOut), then upload on this thread.
    void loadMonsters() {
        struct Job { int k, f; };
        std::vector<Job> jobs;
        for (int k = 0; k < MON_COUNT; ++k) for (int f = 0; f < MON_FRAMES; ++f) jobs.push_back({ k, f });
        for (int b = 0; b < 10; ++b) jobs.push_back({ MON_COUNT + b, 0 });   // title-roster boss portraits
        std::vector<Image> imgs(jobs.size());
        std::atomic<int> next{ 0 };
        auto worker = [&]() {
            for (int j; (j = next++) < (int)jobs.size();) {
                Image out{}; spr::g_imgOut = &out;
                float ph = (float)jobs[j].f / MON_FRAMES;
                if (jobs[j].k >= MON_COUNT) { spr::bossShip(jobs[j].k - MON_COUNT, 192, nullptr); spr::g_imgOut = nullptr; imgs[j] = out; continue; }
                switch (jobs[j].k) {
                    case MON_URCHIN:    spr::novaUrchin(MON_N, ph); break;
                    case MON_MANTA:     spr::voidManta(MON_N, ph); break;
                    case MON_JELLY:     spr::nebulaJelly(MON_N, ph, false); break;
                    case MON_JELLY_HIT: spr::nebulaJelly(MON_N, ph, true); break;
                    case MON_IMP:       spr::cometImp(MON_N, ph); break;
                    case MON_BRUTE:     spr::asteroidBrute(MON_N, ph); break;
                    case MON_LEVIATHAN: spr::cosmicLeviathan(MON_N, ph); break;
                    default:            spr::leviathanSpawn(MON_N, ph); break;
                }
                spr::g_imgOut = nullptr;
                imgs[j] = out;
            }
        };
        int nt = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        std::vector<std::thread> pool;
        for (int i = 0; i < nt; ++i) pool.emplace_back(worker);
        for (auto& t : pool) t.join();
        for (size_t j = 0; j < jobs.size(); ++j) {
            Texture2D& t = jobs[j].k >= MON_COUNT ? bossIcon[jobs[j].k - MON_COUNT] : mon[jobs[j].k][jobs[j].f];
            t = LoadTextureFromImage(imgs[j]);
            UnloadImage(imgs[j]);
            GenTextureMipmaps(&t);
            SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        }
    }
    void unload() {
        for (int i : { 0, 1, 3, 4 }) UnloadTexture(playerRoll[i]);
        for (auto& row : mon) for (auto& t : row) UnloadTexture(t);
        for (Texture2D* t : { &player, &glow, &nebula })
            UnloadTexture(*t);
        for (int i = 0; i < 10; ++i) if (bossLoaded[i]) UnloadTexture(bossTex[i]);
        for (int i = 0; i < 10; ++i) UnloadTexture(bossIcon[i]);
        for (auto& m : meteor) UnloadTexture(m);
        for (Texture2D* t : { &courier[0], &courier[1], &drone, &outpost }) UnloadTexture(*t);
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
static void drawShip(const Texture2D& t, Vector2 pos, float size, float angleDeg, bool shadow = true, Color tint = WHITE);
// Animated monster: cross-fades the two nearest of its looping frames (ph = 0..1), shadow drawn once.
static void drawMonster(const Texture2D* frames, int n, float ph, Vector2 pos, float size, float angleDeg, Color tint = WHITE) {
    float x = (ph - floorf(ph)) * n; int i0 = (int)x % n, i1 = (i0 + 1) % n; float a = x - floorf(x);
    drawShip(frames[i0], pos, size, angleDeg, true, tint);
    if (a > 0.02f) drawShip(frames[i1], pos, size, angleDeg, false, Color{ tint.r, tint.g, tint.b, (unsigned char)(tint.a * a) });
}
static void drawShip(const Texture2D& t, Vector2 pos, float size, float angleDeg, bool shadow, Color tint) {
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
enum SfxKind { SFX_SHOOT, SFX_HIT, SFX_BOOM, SFX_DEATH, SFX_STAGE, SFX_DIVE, SFX_LIFE, SFX_IMPACT, SFX_SHIELD_UP, SFX_SHIELD_BREAK, SFX_ALARM, SFX_LASER, SFX_BIGBOOM,
               SFX_FIRE_SPREAD, SFX_FIRE_HOMING, SFX_FIRE_ION, SFX_LASER_LOOP, SFX_OVERHEAT, SFX_ION_BLAST, SFX_FLAK, SFX_COUNT };
// Each game sound is rendered at startup by the procedural synth in sfxsynth.h (bass-heavy, 44.1 kHz)
static const sfx::Id SFX_SYNTH[SFX_COUNT] = { sfx::FIRE_PHOTON, sfx::HIT, sfx::BOOM, sfx::DEATH, sfx::STAGE, sfx::DIVE, sfx::LIFE, sfx::IMPACT,
    sfx::SHIELD_UP, sfx::SHIELD_BREAK, sfx::ALARM, sfx::BOSS_LASER, sfx::BIGBOOM,
    sfx::FIRE_SPREAD, sfx::FIRE_HOMING, sfx::FIRE_ION, sfx::LASER_LOOP, sfx::OVERHEAT, sfx::ION_BLAST, sfx::FLAK };
static const int SFX_VOICES = 3;   // overlapping copies per sound (rapid fire / chained explosions)

static Sound makeSound(SfxKind k) {
    const int rate = 44100;
    std::vector<float> v = sfx::render(SFX_SYNTH[k], (float)rate);
    if (k == SFX_LASER_LOOP) { std::vector<float> l; for (int r = 0; r < 4; ++r) l.insert(l.end(), v.begin(), v.end()); v.swap(l); }   // 4 s seamless loop
    short* data = (short*)malloc(sizeof(short) * v.size());
    for (size_t i = 0; i < v.size(); ++i) data[i] = (short)(Clamp(v[i], -1.0f, 1.0f) * 32767);
    Wave w{ (unsigned)v.size(), (unsigned)rate, 16, 1, data };
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);
    SetSoundVolume(snd, sfx::gain(SFX_SYNTH[k]));
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
             BK_C_SPREAD = 7, BK_C_PULSE = 8, BK_C_HOMING = 9, BK_C_ION = 10, BK_C_PHOTON = 11,
             BK_FLAK = 12 };   // 12: mining-outpost flak shell (shootable)   // 7+: Courier shots

enum PBKind { PB_PHOTON = 0, PB_SPREAD = 1, PB_HOMING = 3, PB_ION = 4, PB_DRONE = 5 };   // PB_DRONE: wingman's basic homing missile   // player projectile kinds
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
    float vx = 0, ignite = 0, dmgT = 0;                 // horizontal speed (Scythe Beam lean) and beam-ignition flash
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
    bool  outpost = false;   // carries a mining outpost: stabilised (slow 2D turn instead of a tumble)
    int   hp = 0, hpMax = 0; // outpost armour: hits left before the station blows
    float gunT = 0, gunAim = 90, smokeT = 0;   // point-defence turret: time to next shot, barrel angle (deg)
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
    std::vector<int> weaponBag;                 // shuffle bag of weapon drops: each class once before any repeats
    int   lastOffered = -1;                     // last weapon class a Courier carried (never offered twice in a row)
    int   weapon = W_PHOTON, wlevel = 0;        // current weapon class and upgrade level (0..3)
    float heat = 0, laserTick = 0, laserTop = 0;
    bool  overheated = false, laserOn = false;
    bool  autoFire = false;                     // selftest: hold the trigger
    std::vector<Meteor>   meteors;
    bool  meteorField = false;
    int   nextMeteorId = 1;
    float fieldT = 0, fieldDur = 30, spawnT = 0, targetDensity = 0.35f;
    int   outpostsSpawned = 0;                  // mining outposts released this field (one at ~30%, ~60%, ~90%)
    Boss                  boss;
    std::vector<Popup>    popups;
    std::vector<Star>     stars;
    Sound sfx[SFX_COUNT];
    Sound sfxVoice[SFX_COUNT][SFX_VOICES - 1]{};    // aliases sharing the sample data, so a sound can overlap itself
    int   sfxNext[SFX_COUNT]{};
    void loadSfx() {
        for (int i = 0; i < SFX_COUNT; ++i) {
            sfx[i] = makeSound((SfxKind)i);
            if (sfx[i].stream.buffer)   // no audio device: keep the empty sounds (playing them is a no-op)
                for (int v = 0; v < SFX_VOICES - 1; ++v) { sfxVoice[i][v] = LoadSoundAlias(sfx[i]); SetSoundVolume(sfxVoice[i][v], sfx::gain(SFX_SYNTH[i])); }
        }
    }
    void unloadSfx() { for (int i = 0; i < SFX_COUNT; ++i) { for (auto& a : sfxVoice[i]) if (a.stream.buffer) UnloadSoundAlias(a); UnloadSound(sfx[i]); } }
    void playSfx(SfxKind k) {   // round-robin over the voices of this sound
        int v = sfxNext[k]; sfxNext[k] = (v + 1) % SFX_VOICES;
        PlaySound(v == 0 ? sfx[k] : sfxVoice[k][v - 1]);
    }
    void stopLaserLoop() { if (IsSoundPlaying(sfx[SFX_LASER_LOOP])) StopSound(sfx[SFX_LASER_LOOP]); }

    // ---- Title screen audio: looping dark-ambient music (titlemusic.h) + swarm / launch effects ----
    Music titleMusic{}; bool titleMusicOk = false;
    std::vector<unsigned char> titleMusicWav;          // in-memory WAV the music stream decodes from (must stay alive)
    Sound titleFx[sfx::TFX_COUNT]{};
    float musicVol = 0;                                 // 0..1 fade level (music volume = gain * musicVol)
    float titleFxPrevT = -1;                            // titleT on the previous frame (to catch swarm-cycle events)
    static std::vector<unsigned char> makeWav(const std::vector<float>& v, int rate) {
        std::vector<unsigned char> b(44 + v.size() * 2);
        auto w32 = [&](size_t o, uint32_t x) { memcpy(&b[o], &x, 4); }; auto w16 = [&](size_t o, uint16_t x) { memcpy(&b[o], &x, 2); };
        memcpy(&b[0], "RIFF", 4); w32(4, (uint32_t)(36 + v.size() * 2)); memcpy(&b[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
        w32(24, rate); w32(28, rate * 2); w16(32, 2); w16(34, 16); memcpy(&b[36], "data", 4); w32(40, (uint32_t)(v.size() * 2));
        for (size_t i = 0; i < v.size(); ++i) { int16_t x = (int16_t)(Clamp(v[i], -1.0f, 1.0f) * 32767); memcpy(&b[44 + i * 2], &x, 2); }
        return b;
    }
    void loadTitleAudio(const std::vector<float>& music) {
        const int rate = 44100;
        titleMusicWav = makeWav(music, rate);
        titleMusic = LoadMusicStreamFromMemory(".wav", titleMusicWav.data(), (int)titleMusicWav.size());
        titleMusicOk = titleMusic.stream.buffer != nullptr;
        if (titleMusicOk) titleMusic.looping = true;
        for (int k = 0; k < sfx::TFX_COUNT; ++k) {
            std::vector<float> v = sfx::renderTitleFx((sfx::TitleFx)k, (float)rate);
            short* data = (short*)malloc(sizeof(short) * v.size());
            for (size_t i = 0; i < v.size(); ++i) data[i] = (short)(Clamp(v[i], -1.0f, 1.0f) * 32767);
            Wave w{ (unsigned)v.size(), (unsigned)rate, 16, 1, data };
            titleFx[k] = LoadSoundFromWave(w); UnloadWave(w);
            SetSoundVolume(titleFx[k], sfx::titleFxGain((sfx::TitleFx)k));
        }
    }
    void unloadTitleAudio() {
        if (titleMusicOk) { StopMusicStream(titleMusic); UnloadMusicStream(titleMusic); }
        for (auto& s : titleFx) UnloadSound(s);
    }
    // Called every frame: music fades in on the title screen (restarting with the title cycle) and fades out
    // over ~1 s once a game starts; swarm effects fire at the cycle's stream-in, lock-in and peel-off moments.
    void updateTitleAudio(float dt) {
        bool onTitle = mode == TITLE;
        if (titleMusicOk) {
            if (onTitle && !IsMusicStreamPlaying(titleMusic)) { SeekMusicStream(titleMusic, fmodf(titleT, sfx::TITLE_LOOP)); PlayMusicStream(titleMusic); musicVol = 0; }
            musicVol = onTitle ? std::min(1.0f, musicVol + dt / 1.5f) : std::max(0.0f, musicVol - dt / 1.0f);
            if (IsMusicStreamPlaying(titleMusic)) {
                SetMusicVolume(titleMusic, titleMuted ? 0.0f : sfx::TITLE_MUSIC_GAIN * musicVol);   // muted: keeps playing silently, so unmuting resumes in place
                UpdateMusicStream(titleMusic);
                if (!onTitle && musicVol <= 0) StopMusicStream(titleMusic);
            }
        }
        if (!onTitle || stageDialog || titleMuted) { titleFxPrevT = onTitle ? titleT : -1; return; }
        auto crossed = [&](float at) {   // did fmod(titleT, cycle) pass `at` since last frame?
            if (titleFxPrevT < 0) return at == 0.0f;
            float a = titleFxPrevT, b = titleT;
            return floorf((b - at) / SW_CYCLE) > floorf((a - at) / SW_CYCLE);
        };
        if (crossed(0.0f))      PlaySound(titleFx[sfx::TFX_SWARM_IN]);
        if (crossed(SW_IN))     PlaySound(titleFx[sfx::TFX_SWARM_LOCK]);
        if (crossed(SW_OUT_AT)) PlaySound(titleFx[sfx::TFX_SWARM_OUT]);
        titleFxPrevT = titleT;
    }

    Vector2 player{ W / 2.0f, PLAYER_Y_LOW };
    bool  playerAlive = true;
    float respawnTimer = 0, invuln = 0, fireCooldown = 0;
    float roll = 0, prevPX = W / 2.0f, prevVX = 0, muzzleT = 0, warpT = 0, exhaustT = 0;   // player ship FX
    bool  shield = false;                      // one-hit force field
    float shieldGrow = 1, shieldGrace = 0, shieldHitFlash = 0;
    float shieldRegen = 0;                      // >0: seconds until a broken shield comes back by itself
    static constexpr float SHIELD_REGEN_TIME = 15.0f;
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
    // Wingman drone (earned by catching an upgrade at Lv3): 30 s escort with basic homing missiles
    bool  droneOn = false;
    float droneT = 0, droneFireT = 0, droneWarp = 0, droneBank = 0;
    Vector2 droneOff{ -50, 0 }, droneVel{ 0, 0 }, droneTarget{ -50, 0 };
    static constexpr float DRONE_TIME = 30.0f, DRONE_SPEED = 340 * 1.75f;
    float hyperT = -1;                          // >=0: hyperspace jump in progress (seconds since it began)
    bool  hyperSwitched = false;                // next stage already started during the jump
    Vector2 hyperStart{};
    struct HStar { float lx, ly, s; };
    std::vector<HStar> hstars;
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
        if (mode == TITLE) { for (int k = 0; k < sfx::TFX_LAUNCH; ++k) StopSound(titleFx[k]); if (!titleMuted) PlaySound(titleFx[sfx::TFX_LAUNCH]); }
        lives = 3; score = 0; stage = std::clamp(startStage, 1, FINAL_STAGE) - 1; nextLifeAt = 20000; shotsFired = hits = 0;
        playerAlive = true; player = { W / 2.0f, PLAYER_Y_LOW }; invuln = 0; roll = 0; prevPX = player.x; warpT = WARP_TIME;
        pBullets.clear(); eBullets.clear(); parts.clear(); rings.clear(); popups.clear(); meteors.clear(); meteorField = false;
        hyperT = -1; droneOn = false;
        packages.clear(); zaps.clear(); weapon = W_PHOTON; wlevel = 0; heat = 0; overheated = false; weaponBag.clear(); lastOffered = -1;
        mode = PLAYING; shield = false; boss.active = false;
        nextStage();
    }

    // Next weapon class for a Courier to carry: drawn from a shuffled bag of the four classes,
    // skipping the one you hold; the bag refills (reshuffled) once it runs out.
    // Also skips the class offered last time, so the first pick of a fresh bag can't repeat the previous pick.
    int nextBagWeapon() {
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i = 0; i < weaponBag.size(); ++i)
                if (weaponBag[i] != weapon && weaponBag[i] != lastOffered) {
                    int w = weaponBag[i]; weaponBag.erase(weaponBag.begin() + i); return lastOffered = w; }
            weaponBag = { W_SPREAD, W_PULSE, W_HOMING, W_ION };
            for (int i = (int)weaponBag.size() - 1; i > 0; --i) std::swap(weaponBag[i], weaponBag[GetRandomValue(0, i)]);
        }
        return lastOffered = (weapon == W_SPREAD ? W_HOMING : W_SPREAD);
    }

    // What a Courier / mining outpost carries: 50/50 new weapon vs upgrade while on the un-upgraded basic weapon,
    // 80% upgrades otherwise; new weapons come from the shuffle bag.
    int rollCargo() {
        float pWeapon = (weapon == W_PHOTON && wlevel == 0) ? 0.5f : 0.2f;
        return frand(0, 1) < pWeapon ? nextBagWeapon() : -1;
    }

    void raiseShield() {
        if (shield) return;
        shield = true; shieldGrow = 0; shieldHitFlash = 0; shieldRegen = 0;
        playSfx(SFX_SHIELD_UP);
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
        playSfx(SFX_STAGE);

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
                e.cargo = rollCargo();
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
        meteorField = true; fieldT = 0; spawnT = 0; outpostsSpawned = 0;
        gfx.ensureBoss(std::clamp((stage + 1) / CYCLE - 1, 0, BOSS_COUNT - 1));   // build the coming boss now
        fieldDur = 30.0f + 2.0f * cycleIndex();
        targetDensity = std::min(0.45f, 0.30f + 0.017f * cycleIndex());
        bannerTimer = 3.0f;
        playSfx(SFX_STAGE);
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

    // Mining outpost: drifts with the field at ~70% of the average rock speed (rocks spawn at 35-105 px/s by size)
    void spawnOutpost(float x, float y) {
        float fieldAvg = (45.0f + 65.0f + 87.5f) / 3 * (1 + 0.04f * cycleIndex());
        Meteor m{ { x, y }, { frand(-10, 10), 0.7f * fieldAvg }, frand(0, 360), frand(5, 8) * (GetRandomValue(0, 1) ? 1 : -1), 0, M_LARGE, 0 };
        m.outpost = true;
        m.hp = m.hpMax = OUTPOST_HP_BASE + std::min(6, cycleIndex());
        m.gunT = 1.5f;
        addMeteor(m);
    }
    void addMeteor(Meteor m) { m.id = nextMeteorId++; meteors.push_back(m); }

    // Any hit on a rock: volatile rocks detonate, ordinary rocks split.
    // A mining outpost soaks up hits first; only the last one blows the station off its rock.
    void hitMeteor(size_t idx, Vector2 hitDir) {
        if (meteors[idx].outpost) {
            if (--meteors[idx].hp > 0) { damageOutpost(meteors[idx], hitDir); return; }
            destroyOutpost(meteors[idx]);
            detonateMeteor(idx);                 // the station's fuel goes up: volatile-style blast, no fragments
            return;
        }
        if (meteors[idx].volatileRock()) detonateMeteor(idx); else breakMeteor(idx, hitDir);
    }

    static constexpr int OUTPOST_HP_BASE = 6;             // 6 hits early, up to 12 in later cycles
    float outpostHealth(const Meteor& m) const { return m.hpMax > 0 ? (float)m.hp / m.hpMax : 1.0f; }
    Vector2 outpostLocal(const Meteor& m, Vector2 u) const {   // unit coords on the station -> world
        float S = m.r() * 2 / 0.8f * 1.08f, a = m.rot * DEG2RAD; Vector2 q{ u.x * S * 0.5f, u.y * S * 0.5f };
        return { m.pos.x + q.x * cosf(a) - q.y * sinf(a), m.pos.y + q.x * sinf(a) + q.y * cosf(a) };
    }
    static constexpr Vector2 OUTPOST_GUN{ 0.36f, 0.42f };
    // Non-lethal outpost hit: flash, sparks and metal chips at the impact point; a bigger burst at each damage stage
    void damageOutpost(Meteor& m, Vector2 hitDir) {
        m.flash = 0.12f;
        Vector2 n = Vector2Length(hitDir) > 0.1f ? Vector2Normalize(hitDir) : Vector2{ 0, -1 };
        Vector2 at = Vector2Subtract(m.pos, Vector2Scale(n, m.r() * 0.7f));
        for (int i = 0; i < 10; ++i) { float a = atan2f(-n.y, -n.x) + frand(-1.1f, 1.1f), sp = frand(90, 260), life = frand(0.15f, 0.4f);
            parts.push_back({ at, { cosf(a) * sp, sinf(a) * sp }, life, life, frand(1.2f, 2.2f),
                              GetRandomValue(0, 1) ? Color{ 255, 230, 160, 255 } : Color{ 255, 150, 60, 255 }, SPARK }); }
        for (int i = 0; i < 3; ++i) { float a = frand(0, 2 * PI), sp = frand(40, 140), life = frand(0.4f, 0.8f);
            parts.push_back({ at, { cosf(a) * sp, sinf(a) * sp }, life, life, frand(1.5f, 2.5f), Color{ 200, 205, 215, 255 }, DEBRIS, frand(0, 360), frand(-600, 600) }); }
        float f = outpostHealth(m), fPrev = (float)(m.hp + 1) / m.hpMax;
        if ((fPrev > 0.66f && f <= 0.66f) || (fPrev > 0.33f && f <= 0.33f)) {   // damage stage reached: a module pops
            explode(at, Color{ 255, 200, 120, 255 }, Color{ 240, 140, 40, 255 }, 14, 160);
            shake = std::max(shake, 0.15f);
        }
        playSfx(SFX_IMPACT);
    }
    // Point-defence turret + damage smoke/sparks, run each frame for every outpost rock
    void updateOutpost(Meteor& m, float dt) {
        Vector2 g = outpostLocal(m, OUTPOST_GUN);
        float want = atan2f(player.y - g.y, player.x - g.x) * RAD2DEG;
        float d = fmodf(want - m.gunAim + 540, 360) - 180;
        m.gunAim += std::clamp(d, -120 * dt, 120 * dt);                       // barrel tracks the player
        bool canFire = mode == PLAYING && playerAlive && m.pos.y > 30 && m.pos.y < player.y - 140;
        m.gunT -= dt;
        if (!canFire) m.gunT = std::max(m.gunT, 0.45f);                       // hold the shot (keeps the warning honest)
        else if (m.gunT <= 0) {
            float a = m.gunAim * DEG2RAD, sp = 150 + 6 * cycleIndex();
            Vector2 mz{ g.x + cosf(a) * 15, g.y + sinf(a) * 15 };
            eBullets.push_back({ mz, { cosf(a) * sp, sinf(a) * sp }, BK_FLAK });
            playSfx(SFX_FLAK);
            for (int i = 0; i < 6; ++i) { float b = a + frand(-0.5f, 0.5f), v = frand(60, 160), life = frand(0.1f, 0.25f);
                parts.push_back({ mz, { cosf(b) * v, sinf(b) * v }, life, life, frand(1.5f, 2.5f), Color{ 255, 120, 60, 255 }, SPARK }); }
            m.gunT = std::max(1.8f, 2.4f - 0.08f * cycleIndex());
        }
        float f = outpostHealth(m);
        if (f <= 0.66f && (m.smokeT -= dt) <= 0) {                           // damaged: smoke, then sparks and fire
            m.smokeT = f <= 0.33f ? 0.05f : 0.12f;
            Vector2 p = outpostLocal(m, { frand(-0.3f, 0.5f), frand(-0.45f, 0.1f) });
            float life = frand(0.7f, 1.3f);
            parts.push_back({ p, { frand(-12, 12), frand(-50, -25) }, life, life, frand(6, 10), Color{ 150, 145, 150, 255 }, SMOKE });
            if (f <= 0.33f && GetRandomValue(0, 2) == 0) { float l2 = frand(0.15f, 0.35f), a = frand(0, 2 * PI);
                parts.push_back({ p, { cosf(a) * 120, sinf(a) * 120 }, l2, l2, frand(1.2f, 2), Color{ 255, 210, 120, 255 }, SPARK }); }
        }
    }

    // Mining outpost blown off its rock: explosion, metal debris, 500 points and a package (Courier odds).
    // The rock itself then breaks up like any large asteroid.
    void destroyOutpost(Meteor& m) {
        m.outpost = false;
        explode(m.pos, Color{ 255, 200, 120, 255 }, Color{ 240, 140, 40, 255 }, 40, 300);
        rings.push_back({ m.pos, 0.45f, 0.45f, 130, Color{ 255, 180, 90, 255 } });
        for (int i = 0; i < 30; ++i) { float a = frand(0, 2 * PI), sp = frand(60, 280), life = frand(0.4f, 1.0f);
            parts.push_back({ m.pos, { cosf(a) * sp, sinf(a) * sp }, life, life, frand(2, 4),
                              GetRandomValue(0, 2) ? Color{ 200, 205, 215, 255 } : Color{ 240, 140, 40, 255 }, DEBRIS, frand(0, 360), frand(-700, 700) }); }
        shake = std::max(shake, 0.3f);
        playSfx(SFX_BOOM);
        addScore(500);
        popups.push_back({ { m.pos.x - 30, m.pos.y - 14 }, 1.2f, "OUTPOST +500" });
        packages.push_back({ { m.pos.x, m.pos.y + 6 }, rollCargo(), 0 });
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
        playSfx(SFX_BOOM); playSfx(SFX_IMPACT);
        popups.push_back({ { m.pos.x - 22, m.pos.y - 10 }, 1.0f, "BOOM!" });

        std::vector<int> caught;
        for (const auto& o : meteors)
            if (Vector2Distance(o.pos, m.pos) < BLAST_RADIUS + o.r() * 0.5f) caught.push_back(o.id);
        for (int id : caught) {
            auto it = std::find_if(meteors.begin(), meteors.end(), [id](const Meteor& o) { return o.id == id; });
            if (it == meteors.end()) continue;
            if (it->volatileRock()) { if (it->fuse <= 0) it->fuse = 0.15f; continue; }   // chain reaction
            if (it->outpost) { hitMeteor((size_t)(it - meteors.begin()), Vector2Subtract(it->pos, m.pos)); continue; }   // armour soaks one hit
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
        playSfx(m.size == M_SMALL ? SFX_BOOM : SFX_HIT);
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
            if (outpostsSpawned < 3 && fieldT >= fieldDur * 0.3f * (outpostsSpawned + 1)) {   // mining outposts at ~30/60/90%
                ++outpostsSpawned;
                spawnOutpost(frand(90, W - 90.0f), -METEOR_R[M_LARGE] - 20);
            }
        }
        for (auto& m : meteors) {
            m.pos = Vector2Add(m.pos, Vector2Scale(m.vel, dt));
            m.vel.x *= 1 - 0.3f * dt;                      // fragments settle back into the drift
            if (fieldT > fieldDur) m.vel.y += 40 * dt;      // field over: the stragglers speed past
            m.rot += m.spin * dt;
            m.flash = std::max(0.0f, m.flash - dt);
            if (m.outpost) updateOutpost(m, dt);
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
            playSfx(SFX_LIFE);
        }
    }

    // Outpost rock: stabilised (fixed rock frame, slow 2D turn) with the station overlay, spinning radar dish,
    // blinking beacons, glowing drill and ore sparks streaming into the borehole.
    void drawOutpost(const Meteor& m, float S) {
        const Texture2D& rock = gfx.meteor[m.variant];
        m3d::draw(rock, 0, { m.pos.x + S * 0.12f, m.pos.y + S * 0.18f }, S * 0.95f, m.rot, Color{ 0, 0, 0, 110 }, false);
        m3d::draw(rock, 0, m.pos, S, m.rot, WHITE, false);
        const Texture2D& st = gfx.outpost;
        DrawTexturePro(st, { 0, 0, (float)st.width, (float)st.height }, { m.pos.x, m.pos.y, S, S }, { S / 2, S / 2 }, m.rot, WHITE);
        float a = m.rot * DEG2RAD;
        auto L = [&](Vector2 u) { Vector2 q{ u.x * S * 0.5f, u.y * S * 0.5f }; return Vector2{ m.pos.x + q.x * cosf(a) - q.y * sinf(a), m.pos.y + q.x * sinf(a) + q.y * cosf(a) }; };
        float hf = outpostHealth(m);
        Vector2 dc = L({ -0.48f, -0.32f }); float da = hf > 0.66f ? time * 3 + m.id : m.id + 0.7f;   // dish jams once damaged
        float rx = 2 + 8 * fabsf(cosf(da));
        DrawEllipse((int)dc.x, (int)dc.y, rx, 8, Color{ 220, 225, 235, 255 });
        DrawEllipseLines((int)dc.x, (int)dc.y, rx, 8, Color{ 90, 95, 110, 255 });
        BeginBlendMode(BLEND_ADDITIVE);
        float t = time + m.id * 0.37f;
        bool lightsOn = hf > 0.33f || fmodf(time * 7.3f + m.id, 1.0f) < 0.55f;   // badly damaged: power flickers
        if (lightsOn) {
        if (fmodf(t, 1.2f) < 0.1f) drawGlow(L({ 0.64f, -0.52f }), 8, Color{ 255, 60, 60, 255 });
        if (fmodf(t + 0.6f, 1.2f) < 0.1f) drawGlow(L({ -0.42f, -0.1f }), 7, Color{ 80, 255, 120, 255 });
        if (fmodf(t + 0.3f, 0.9f) < 0.08f) drawGlow(L({ -0.22f, 0.04f }), 9, Color{ 255, 255, 255, 255 });
        drawGlow(L({ -0.22f, 0.38f }), 11 + 2 * sinf(t * 12), Color{ 255, 140, 40, 140 });
        for (int k = 0; k < 4; ++k) { float u = fmodf(t * 1.5f + k * 0.25f, 1.f);
            drawGlow(Vector2Lerp(L({ -0.22f, 0.62f }), L({ -0.22f, 0.38f }), u), 3, Color{ 255, 200, 120, (unsigned char)(200 * (1 - u)) }); }
        }
        if (hf <= 0.66f) drawGlow(L({ 0.12f, -0.22f }), (hf <= 0.33f ? 16 : 9) + 3 * sinf(time * 23), Color{ 255, 110, 30, (unsigned char)(hf <= 0.33f ? 230 : 130) });   // hull breach fire
        // point-defence turret: charge glow at the muzzle just before it fires
        Vector2 g = L(OUTPOST_GUN); float ga = m.gunAim * DEG2RAD;
        Vector2 mz{ g.x + cosf(ga) * 15, g.y + sinf(ga) * 15 };
        if (m.gunT < 0.45f) { float c = 1 - m.gunT / 0.45f; drawGlow(mz, 6 + 12 * c, Color{ 255, 80, 50, (unsigned char)(120 + 135 * c) }); }
        EndBlendMode();
        Vector2 mzb{ g.x + cosf(ga) * 14, g.y + sinf(ga) * 14 };
        DrawLineEx(g, mzb, 4.2f, Color{ 55, 57, 66, 255 });                       // twin-barrel housing
        DrawLineEx(g, mzb, 1.6f, Color{ 160, 165, 180, 255 });
        DrawCircleV(g, 8.0f, Color{ 45, 47, 56, 255 }); DrawCircleV(g, 6.5f, Color{ 150, 60, 50, 255 });   // red-striped dome
        DrawCircleV(g, 4.2f, Color{ 120, 125, 140, 255 });
        DrawCircleV({ g.x - 1.6f, g.y - 1.6f }, 1.8f, Color{ 215, 220, 235, 255 });
        if (m.hp < m.hpMax) {                                  // armour pips under the rock once it has been hit
            float pw = 5, gap = 2, tw = m.hpMax * (pw + gap) - gap, x0 = m.pos.x - tw / 2, y0 = m.pos.y + m.r() + 8;
            DrawRectangleRounded({ x0 - 3, y0 - 2, tw + 6, 7 }, 0.5f, 4, Color{ 0, 0, 0, 150 });
            for (int i = 0; i < m.hpMax; ++i) {
                Color c = i < m.hp ? (hf > 0.66f ? Color{ 120, 230, 140, 255 } : hf > 0.33f ? Color{ 255, 200, 70, 255 } : Color{ 255, 90, 60, 255 }) : Color{ 60, 60, 70, 255 };
                DrawRectangle((int)(x0 + i * (pw + gap)), (int)y0, (int)pw, 3, c);
            }
        }
        BeginBlendMode(BLEND_ADDITIVE);
        if (m.flash > 0) drawGlow(m.pos, S * 0.5f, Color{ 255, 220, 180, (unsigned char)(160 * m.flash / 0.12f) });
        EndBlendMode();
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
            S *= 1.08f;
            if (m.outpost) continue;                    // drawn last, on top of the field
            float tilt = (float)((m.id * 53) % 40 - 20);   // fixed per-rock orientation; the tumble is in the atlas
            const Texture2D& tex = gfx.meteor[m.variant];
            m3d::draw(tex, m.rot, { m.pos.x + S * 0.12f, m.pos.y + S * 0.18f }, S * 0.95f, tilt, Color{ 0, 0, 0, 110 }, false);   // shadow
            m3d::draw(tex, m.rot, m.pos, S, tilt);
            if (m.volatileRock()) {
                float pl = 0.5f + 0.5f * sinf(time * 3.5f + m.id);
                bool armed = m.fuse > 0;
                BeginBlendMode(BLEND_ADDITIVE);
                Color tint = armed && fmodf(time, 0.06f) < 0.03f ? Color{ 255, 255, 255, 230 } : Color{ 255, 140, 90, (unsigned char)(18 + 22 * pl) };
                m3d::draw(tex, m.rot, m.pos, S, tilt, tint, false);
                EndBlendMode();
            }
            if (m.flash > 0) {
                BeginBlendMode(BLEND_ADDITIVE);
                m3d::draw(tex, m.rot, m.pos, S, tilt, Color{ 255, 220, 180, (unsigned char)(200 * m.flash / 0.12f) }, false);
                EndBlendMode();
            }
        }
        for (const auto& m : meteors) if (m.outpost) drawOutpost(m, m.r() * 2 / 0.8f * 1.08f);
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
        playSfx(SFX_ALARM);
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
        if (b.state == B_FIGHT && b.phase() > 0 && (b.dmgT -= dt) <= 0) {
            int ph = b.phase();
            b.dmgT = ph == 1 ? frand(0.25f, 0.45f) : frand(0.08f, 0.18f);
            for (int tries = 0; tries < 12; ++tries) {
                Vector2 q = b.W2({ frand(-0.8f, 0.8f), frand(-0.6f, 0.6f) });
                if (!b.hitTest(q)) continue;
                for (int i = 0; i < 5 + 3 * ph; ++i) {
                    float ang = frand(0, 2 * PI), sp = frand(60, 200), life = frand(0.2f, 0.5f);
                    parts.push_back({ q, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1.2f, 2.4f),
                                      GetRandomValue(0, 1) ? Color{ 255, 220, 140, 255 } : Color{ 255, 140, 60, 255 }, SPARK });
                }
                float life = frand(0.8f, 1.5f);
                parts.push_back({ q, { frand(-15, 15), frand(-60, -30) }, life, life, frand(8, 14) * (0.7f + 0.3f * ph),
                                  Color{ 60, 55, 60, 255 }, SMOKE });
                if (ph == 2) { float l2 = frand(0.4f, 0.8f);
                    parts.push_back({ q, { frand(-10, 10), frand(-50, -20) }, l2, l2, frand(5, 9), Color{ 255, 120, 40, 255 }, EMBER }); }
                break;
            }
        }
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
                playSfx(SFX_BOOM);
            }
            b.pos.y += 12 * dt;
            if (b.dyingT > 2.6f + 0.15f * b.index) {   // final detonation
                for (int i = 0; i < 4; ++i) explode({ b.pos.x + frand(-60, 60), b.pos.y + frand(-20, 20) }, ORANGE, YELLOW, 60, 420);
                explode(b.core(), Color{ 255, 80, 220, 255 }, WHITE, 80, 520);
                rings.push_back({ b.core(), 0.9f, 0.9f, 260, Color{ 255, 120, 220, 255 } });
                rings.push_back({ b.core(), 0.6f, 0.6f, 160, Color{ 255, 230, 180, 255 } });
                shake = 1.0f;
                int bonus = 5000 * (b.index + 1);
                addScore(bonus);
                popups.push_back({ { W / 2.0f - 130, b.pos.y }, 2.5f, TextFormat("%s DESTROYED  +%d", b.D().name, bonus) });
                playSfx(SFX_BIGBOOM);
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
                    playSfx(SFX_DIVE);
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
                    b.fireT = 0.094f / std::sqrt(ag);              // ~10% fewer orbs along each arm
                }
                if (b.attackT > 2.4f) b.attack = A_NONE;
                break;
            }
            case A_RING: {                 // expanding rings in every direction
                int rings_ = 2 + b.phase();
                if (b.fireT <= 0 && b.step < rings_) {
                    int n = (int)lroundf((16 + 2 * b.index) * 0.9f);   // ~10% fewer crystals per ring
                    float off = b.step * 180.0f / n;
                    for (int i = 0; i < n; ++i) bossShoot(b.core(), off + i * 360.0f / n, 160 * diff, BK_CRYSTAL);
                    rings.push_back({ b.core(), 1.6f, 1.6f, 160 * diff * 1.6f, coreColor(150) });   // shockwave travels with the ring
                    muzzleFlash(b.core(), coreColor(), 40);
                    playSfx(SFX_DIVE);
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
                    playSfx(SFX_DIVE);
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
                    b.step = 1; b.ignite = 0.3f; playSfx(SFX_LASER); shake = std::max(shake, 0.45f);
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

    void damageBoss(Vector2 at, float amount = 1.0f) {   // amount: spread pellet 0.5, homing 0.75, laser tick 2, ion direct hit 3
        Boss& b = boss;
        b.hp -= amount; b.flash = 0.07f;
        addScore(10);
        if (b.hp <= 0) {
            b.state = B_DYING; b.dyingT = 0; b.emitT = 0; b.attack = A_NONE; b.step = 0;
            for (auto& e : enemies) if (e.state != DEAD && e.minion) { e.state = DEAD; explode(e.pos, ORANGE, WHITE, 20, 180); }
            for (auto& eb : eBullets) explode(eb.pos, PURPLE, WHITE, 3, 60);
            eBullets.clear();
            playSfx(SFX_DEATH);
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
            float fl = 0.8f + 0.12f * sinf(time * 45 + e.x * 10) + 0.08f * sinf(time * 71 + e.x * 23);
            drawGlow(n, 22 * fl, Color{ eng.r, eng.g, eng.b, 160 });
            DrawTriangle({ n.x + 9, n.y }, { n.x, n.y - 36 * fl }, { n.x - 9, n.y }, Color{ eng.r, eng.g, eng.b, 110 });
            DrawTriangle({ n.x + 4.5f, n.y }, { n.x, n.y - 22 * fl }, { n.x - 4.5f, n.y }, Color{ 255, 255, 255, 150 });   // hot inner core
            drawGlow({ n.x, n.y - 30 * fl }, 10, Color{ eng.r, eng.g, eng.b, 60 });
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
        // --- living reactor: double-beat heartbeat that quickens as the boss is damaged
        Vector2 cc{ p.x + D.coreP.x * S / 2, p.y + D.coreP.y * S / 2 };
        float rc = D.coreR * S / 2;
        int ph = b.phase();
        float period = 1.3f / (1 + 0.45f * ph), tb = fmodf(time + b.index * 0.37f, period);
        auto bump = [](float x, float w) { return expf(-(x / w) * (x / w)); };
        float hb = bump(tb, 0.07f) + 0.6f * bump(tb - 0.22f, 0.07f);
        Color cc1 = bossColor(D.core);
        drawGlow(cc, 30 * (1 + 0.35f * hb) * (1 + 0.3f * ph), Color{ cc1.r, cc1.g, cc1.b, (unsigned char)std::min(255.0f, 120 + 110 * hb) });
        drawGlow(cc, rc * (0.85f + 0.3f * hb), Color{ 255, 255, 255, (unsigned char)(50 + 110 * hb) });
        for (int k = 0; k < 2; ++k) {                                   // heartbeat shockwaves
            float u = (tb - k * 0.22f) / 0.65f;
            if (u > 0 && u < 1)
                DrawRing(cc, rc * (1.15f + 2.0f * u), rc * (1.15f + 2.0f * u) + 2.2f, 0, 360, 48,
                         Color{ cc1.r, cc1.g, cc1.b, (unsigned char)((1 - u) * (k ? 70 : 120)) });
        }
        if (b.attack != A_SPIRAL && b.state != B_DYING) {               // containment field: counter-rotating arcs
            float sp = time * (70 + 40 * ph);
            for (int k = 0; k < 3; ++k) {
                DrawRing(cc, rc * 1.32f, rc * 1.32f + 1.8f, sp + k * 120, sp + k * 120 + 55, 12, Color{ cc1.r, cc1.g, cc1.b, 150 });
                DrawRing(cc, rc * 1.62f, rc * 1.62f + 1.2f, -sp * 0.7f + k * 120 + 40, -sp * 0.7f + k * 120 + 80, 10, Color{ cc1.r, cc1.g, cc1.b, 90 });
            }
        }
        EndBlendMode();
        {                                                               // plasma crackle arcing off the core
            unsigned slot = (unsigned)(time / 0.07f);
            int bolts = 1 + ph;
            for (int k = 0; k < bolts; ++k) {
                unsigned sd = slot * 2654435761u + k * 40503u + b.index * 977u;
                if (((sd >> 13) % 100) > 55u + 15u * ph) continue;      // flickers on and off
                float a = ((sd >> 4) % 360) * DEG2RAD;
                Vector2 a0 = Vector2Add(cc, { cosf(a) * rc * 0.9f, sinf(a) * rc * 0.9f });
                Vector2 a1 = Vector2Add(cc, { cosf(a + 0.5f) * rc * (1.9f + 0.4f * ph), sinf(a + 0.5f) * rc * (1.9f + 0.4f * ph) });
                drawZap(a0, a1, sd, cc1, 1.1f, 200);
            }
        }
        BeginBlendMode(BLEND_ADDITIVE);
        // navigation strobes (port red / starboard green) and a white beacon on the keel
        {
            Vector2 tip = spr::bossNavTip(b.index);
            float bt = fmodf(time + b.index * 0.21f, 1.4f);
            float on = bump(bt - 0.05f, 0.05f) + bump(bt - 0.25f, 0.05f);
            if (on > 0.02f && !D.organic)
                for (int sg : { -1, 1 }) {
                    Vector2 lp{ p.x + sg * tip.x * S / 2, p.y + tip.y * S / 2 };
                    Color lc = sg < 0 ? Color{ 255, 60, 60, 255 } : Color{ 80, 255, 120, 255 };
                    drawGlow(lp, 16 * on, Color{ lc.r, lc.g, lc.b, (unsigned char)(220 * std::min(1.0f, on)) });
                }
            float bw = fmodf(time + 0.7f, 2.1f);
            float bon = bump(bw, 0.04f);
            if (bon > 0.02f && !D.organic) drawGlow({ p.x, p.y + (D.muzzle.y - 0.3f) * S / 2 }, 22 * bon, Color{ 255, 255, 255, (unsigned char)(200 * bon) });
        }
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

    // The player's ship with its live effects: warp-in, engine plumes with shock diamonds, banked sprite,
    // navigation strobes, canopy glint and gun-muzzle flashes.
    void drawPlayerShip() {
        const float S = 50;
        float w = warpT > 0 ? 1 - warpT / WARP_TIME : 1;         // warp-in progress 0..1
        Vector2 pos = player;
        if (warpT > 0) {
            BeginBlendMode(BLEND_ADDITIVE);
            for (int k = 0; k < 14; ++k) {
                float a = k * 0.4488f, r0 = (1 - w) * 110 + 8, len = 22 * (1 - w) + 4;
                Vector2 d{ cosf(a), sinf(a) };
                DrawLineEx({ pos.x + d.x * r0, pos.y + d.y * r0 * 0.6f }, { pos.x + d.x * (r0 + len), pos.y + d.y * (r0 + len) * 0.6f },
                           1.6f, Color{ 140, 200, 255, (unsigned char)(220 * w) });
            }
            drawGlow(pos, 14 + 40 * w, Color{ 140, 200, 255, (unsigned char)(160 * w) });
            if (w > 0.55f) drawGlow(pos, S * 0.9f * (1 - (w - 0.55f) / 0.45f), Color{ 255, 255, 255, 200 });
            EndBlendMode();
        }
        float fade = warpT > 0 ? std::max(0.0f, (w - 0.6f) / 0.4f) : 1;
        if (fade <= 0) return;
        unsigned char fa = (unsigned char)(255 * fade);
        float cr = cosf(roll * DEG2RAD);
        BeginBlendMode(BLEND_ADDITIVE);
        for (float sx : { -1.f, 1.f }) {                          // engine plumes
            Vector2 n{ pos.x + sx * 6 * cr, pos.y + 20 };
            float fl = 0.85f + 0.1f * sinf(time * 50 + sx * 3) + 0.05f * sinf(time * 83);
            float L = S * 0.42f * fl;
            DrawTriangle({ n.x - 2.5f, n.y }, { n.x, n.y + L }, { n.x + 2.5f, n.y }, Color{ 60, 130, 255, (unsigned char)(130 * fade) });
            DrawTriangle({ n.x - 1.4f, n.y }, { n.x, n.y + L * 0.6f }, { n.x + 1.4f, n.y }, Color{ 220, 240, 255, (unsigned char)(210 * fade) });
            for (int k = 1; k <= 3; ++k)                           // shock diamonds
                drawGlow({ n.x, n.y + L * 0.17f * k }, S * (0.05f - k * 0.008f) * (1 + 0.15f * sinf(time * 40 + k)), Color{ 200, 230, 255, (unsigned char)(200 * fade) });
            drawGlow({ n.x, n.y + 2 }, S * 0.13f * fl, Color{ 90, 160, 255, (unsigned char)(170 * fade) });
        }
        EndBlendMode();
        // banked sprite: cross-fade the two nearest roll frames
        float fi = (roll + 30) / 15.0f; int i0 = std::clamp((int)fi, 0, 3); float a = Clamp(fi - i0, 0, 1);
        drawShip(gfx.playerRoll[i0], pos, S, 0, true, Color{ 255, 255, 255, fa });
        if (a > 0.02f) drawShip(gfx.playerRoll[i0 + 1], pos, S, 0, false, Color{ 255, 255, 255, (unsigned char)(fa * a) });
        BeginBlendMode(BLEND_ADDITIVE);
        auto bump = [](float x, float wd) { return expf(-(x / wd) * (x / wd)); };
        float bt = fmodf(time, 1.2f), on = bump(bt - 0.05f, 0.04f) + bump(bt - 0.22f, 0.04f);
        if (on > 0.02f) {                                         // nav strobes: red port, green starboard
            drawGlow({ pos.x - 20.5f * cr, pos.y + 1.5f }, S * 0.07f * on, Color{ 255, 60, 60, (unsigned char)(230 * fade) });
            drawGlow({ pos.x + 20.5f * cr, pos.y + 1.5f }, S * 0.07f * on, Color{ 80, 255, 120, (unsigned char)(230 * fade) });
        }
        float won = bump(fmodf(time + 0.6f, 1.8f), 0.03f);       // white tail strobe
        if (won > 0.02f) drawGlow({ pos.x, pos.y + 15.5f }, S * 0.09f * won, Color{ 255, 255, 255, (unsigned char)(230 * fade) });
        float gt = fmodf(time, 3.0f);                             // canopy glint
        if (gt < 0.5f) { float u = gt / 0.5f;
            drawGlow({ pos.x - 0.5f, pos.y + (-0.46f + 0.36f * u) * S / 2 }, S * 0.05f, Color{ 255, 255, 255, (unsigned char)(200 * sinf(u * PI) * fade) }); }
        if (muzzleT > 0) {                                        // gun-muzzle flashes
            float k = muzzleT / 0.07f;
            for (float sx : { -1.f, 1.f }) { Vector2 m{ pos.x + sx * 12.5f * cr, pos.y - 10 };
                drawGlow(m, S * 0.09f * k, Color{ 255, 170, 90, 220 }); drawGlow(m, S * 0.035f * k, Color{ 255, 240, 220, 255 }); }
        }
        EndBlendMode();
    }

    // ================================================================ HYPERSPACE JUMP ==
    // After each boss (~4 s): a window tears open ahead, the stars stretch into hyperspace streaks, the ship lines up and
    // flies into the window's tunnel, the window snaps shut with a flash, and the next stage begins (ship warps in).
    // The window ring, its tunnel, the streaks and the ship's dive share one perspective camera and one travel axis.
    static constexpr float HYPER_SWITCH = 3.3f, HYPER_END = 4.0f, HYP_TILT = 32 * DEG2RAD, HYP_CAM = 500;
    static constexpr Vector3 HYP_D = { 0, 0.12f, -0.9928f };       // travel axis: into the screen, slightly up
    static constexpr Vector2 HYP_PC = { W / 2.0f, 300 };
    static float hs(float a, float b, float x) { float t = Clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    static Vector2 hproj(Vector3 p, float* k = nullptr) { float s = HYP_CAM / (HYP_CAM - p.z); if (k) *k = s; return { HYP_PC.x + p.x * s, HYP_PC.y - p.y * s }; }
    static Vector3 hring(float R, float th, float d = 0) {
        return { R * cosf(th) + HYP_D.x * d, R * sinf(th) * sinf(HYP_TILT) + HYP_D.y * d, R * sinf(th) * cosf(HYP_TILT) + HYP_D.z * d }; }
    float hyperOpen() const { return hyperT < 0 ? 0 : hs(0.4f, 1.3f, hyperT) * (1 - hs(2.85f, 3.25f, hyperT)); }
    float hyperStreak() const { return hyperT < 0 ? 0 : hs(1.0f, 2.2f, hyperT) * (1 - hs(3.2f, 3.8f, hyperT)); }

    void startHyper() {
        hyperT = 0; hyperSwitched = false; hyperStart = player;
        if (droneOn) {                                   // the drone does not make the jump: it warps away
            Vector2 d = Vector2Add(player, droneOff);
            parts.push_back({ d, { 0, 0 }, 0.35f, 0.35f, 30, Color{ 140, 255, 170, 255 }, FLASH });
            droneOn = false;
        }
        eBullets.clear(); pBullets.clear(); warnings.clear();
        hstars.clear();
        for (int i = 0; i < 420; ++i) hstars.push_back({ frand(-900, 900), frand(-1100, 1100), frand(-250, 4000) });
        playSfx(SFX_SHIELD_UP);
    }
    void updateHyper(float dt) {
        float prev = hyperT; hyperT += dt;
        float st = hyperStreak(), speed = 2600 * st * st + 1;
        for (auto& q : hstars) { q.s -= speed * dt; if (q.s < -260) { q.s = frand(3000, 4000); q.lx = frand(-900, 900); q.ly = frand(-1100, 1100); } }
        if (prev < 2.0f && hyperT >= 2.0f) playSfx(SFX_DIVE);
        if (prev < 2.9f && hyperT >= 2.9f) { playSfx(SFX_BIGBOOM); shake = 0.3f; }
        if (!hyperSwitched && hyperT >= HYPER_SWITCH) {          // out the other side: next stage, ship warps in
            hyperSwitched = true;
            player = { W / 2.0f, PLAYER_Y_LOW }; prevPX = player.x; roll = 0; warpT = WARP_TIME; invuln = 1.2f;
            nextStage();
        }
        if (hyperT >= HYPER_END) hyperT = -1;
    }
    void drawHyper() {
        float t = hyperT, open = hyperOpen(), st = hyperStreak(), dive = hs(2.0f, 2.8f, t), R = 120 * open;
        // streaks: stars rushing toward the viewer along -D, trailing back toward the vanishing point
        if (st > 0.01f) {
            float len = 40 + 900 * st * st;
            for (auto& q : hstars) {
                Vector3 a{ q.lx + HYP_D.x * q.s, q.ly + HYP_D.y * q.s, HYP_D.z * q.s }, b{ q.lx + HYP_D.x * (q.s + len), q.ly + HYP_D.y * (q.s + len), HYP_D.z * (q.s + len) };
                float k; Vector2 pa = hproj(a, &k), pb = hproj(b);
                float al = Clamp(1 - q.s / 4000.f, 0, 1) * st;
                DrawLineEx(pa, pb, Clamp(k, 0.8f, 3.f), Color{ 200, 220, 255, (unsigned char)(230 * al) });
            }
        }
        const int NS = 160;
        auto rim = [&](bool front) {
            BeginBlendMode(BLEND_ADDITIVE);
            for (int i = 0; i < NS; ++i) { float th = i * 2 * PI / NS; if ((sinf(th) > 0) != front) continue;
                float k; Vector2 p = hproj(hring(R, th), &k); float nf = 0.7f + 0.3f * sinf(th);
                drawGlow(p, (13 + 5 * sinf(th)) * k * open, Color{ 110, 170, 255, (unsigned char)(90 * open * nf) });
                drawGlow(p, 4.5f * k * open, Color{ 220, 240, 255, (unsigned char)(190 * open * nf) }); }
            for (int j = 0; j < 3; ++j) { float a0 = t * 3.2f + j * 2.094f;
                for (float a = a0; a < a0 + 0.9f; a += 0.05f) { if ((sinf(a) > 0) != front) continue;
                    float k; Vector2 p = hproj(hring(R * 1.07f, a), &k); drawGlow(p, 6 * k, Color{ 200, 230, 255, (unsigned char)(170 * open) }); } }
            EndBlendMode(); };
        if (open > 0.001f) {
            BeginBlendMode(BLEND_ADDITIVE); drawGlow(HYP_PC, 240 * open, Color{ 60, 100, 255, (unsigned char)(70 * open) }); EndBlendMode();
            rim(false);
            Vector2 c = hproj({ 0, 0, 0 }), prevP = hproj(hring(R, 0));
            for (int i = 1; i <= NS; ++i) { Vector2 q = hproj(hring(R, i * 2 * PI / NS));
                DrawTriangle(c, q, prevP, Color{ 8, 12, 45, 255 }); DrawTriangle(c, prevP, q, Color{ 8, 12, 45, 255 }); prevP = q; }
            BeginBlendMode(BLEND_ADDITIVE);
            float L = 6 * R;
            for (int ring = 0; ring < 9; ++ring) { float u = fmodf(ring / 9.f + t * 0.7f, 1.f), d = u * u * L;
                Vector2 pv = hproj(hring(R, 0, d));
                for (int i = 1; i <= 64; ++i) { Vector2 q = hproj(hring(R, i * 2 * PI / 64, d)); DrawLineEx(pv, q, 1.3f, Color{ 110, 160, 255, (unsigned char)(150 * (1 - u) * open) }); pv = q; } }
            for (int i = 0; i < 64; ++i) { float th = i * 2.3999f, u = fmodf(i * 0.137f + t * 1.2f, 1.f), u2 = std::min(1.f, u + 0.15f);
                DrawLineEx(hproj(hring(R, th, u * u * L)), hproj(hring(R, th, u2 * u2 * L)), 1.1f + 1.4f * (1 - u),
                           Color{ 150, 200, 255, (unsigned char)(190 * (1 - u) * open) }); }
            Vector2 vp = hproj({ HYP_D.x * 1e5f, HYP_D.y * 1e5f, HYP_D.z * 1e5f });
            drawGlow(vp, 36 * open, Color{ 200, 225, 255, (unsigned char)(200 * open) }); drawGlow(vp, 12 * open, Color{ 255, 255, 255, (unsigned char)(255 * open) });
            EndBlendMode();
        }
        if (t > 0.3f && t < 0.9f) {                                  // the tear before it opens
            float u = hs(0.3f, 0.6f, t) * (1 - hs(0.7f, 0.9f, t));
            BeginBlendMode(BLEND_ADDITIVE);
            DrawRectangleV({ HYP_PC.x - 130 * u, HYP_PC.y - 1.5f }, { 260 * u, 3 }, Color{ 230, 240, 255, (unsigned char)(255 * u) });
            drawGlow(HYP_PC, 60 * u, Color{ 140, 190, 255, (unsigned char)(200 * u) });
            EndBlendMode();
        }
        if (!hyperSwitched) {                                        // the ship: line up, reach the mouth, dive down the tunnel
            Vector2 pos = Vector2Lerp(hyperStart, { W / 2.0f, 640 }, hs(0, 0.7f, t));
            float sc = 1, alpha = 1;
            if (dive > 0) {
                float ap = hs(0, 0.45f, dive);
                if (ap < 1) pos = Vector2Lerp(pos, hproj({ 0, 0, 0 }), ap);
                else { float d = hs(0.35f, 1.0f, dive); d = d * d * 700; float k; pos = hproj({ HYP_D.x * d, HYP_D.y * d, HYP_D.z * d }, &k); sc = k; }
                alpha = 1 - hs(0.8f, 1.0f, dive);
            }
            if (alpha > 0.01f) {
                float S = 50 * sc, charge = hs(1.0f, 2.0f, t);
                BeginBlendMode(BLEND_ADDITIVE);
                for (float sx : { -1.f, 1.f }) {
                    Vector2 n{ pos.x + sx * 6 * sc, pos.y + 20 * sc };
                    float L2 = (22 + 60 * charge + 50 * dive) * sc * (0.9f + 0.1f * sinf(time * 50 + sx));
                    DrawTriangle({ n.x - 2.5f * sc, n.y }, { n.x, n.y + L2 }, { n.x + 2.5f * sc, n.y }, Color{ 60, 130, 255, (unsigned char)(170 * alpha) });
                    DrawTriangle({ n.x - 1.4f * sc, n.y }, { n.x, n.y + L2 * 0.6f }, { n.x + 1.4f * sc, n.y }, Color{ 230, 245, 255, (unsigned char)(220 * alpha) });
                    drawGlow({ n.x, n.y + 3 }, (10 + 8 * charge) * sc, Color{ 90, 160, 255, (unsigned char)(190 * alpha) });
                }
                EndBlendMode();
                drawShip(gfx.player, pos, S, 0, dive <= 0, Color{ 255, 255, 255, (unsigned char)(255 * alpha) });
            }
        }
        if (open > 0.001f) rim(true);
        float cl = (t - 2.85f) / 0.6f;                               // collapse: flash + shockwave
        if (cl > 0 && cl < 1) {
            BeginBlendMode(BLEND_ADDITIVE);
            drawGlow(HYP_PC, 60 + 260 * cl, Color{ 200, 230, 255, (unsigned char)(200 * (1 - cl)) });
            for (int i = 0; i < 96; ++i) drawGlow(hproj(hring(40 + 320 * cl, i * 2 * PI / 96)), 5, Color{ 170, 210, 255, (unsigned char)(150 * (1 - cl)) });
            EndBlendMode();
        }
        float wf = hs(2.9f, 3.05f, t) * (1 - hs(3.05f, 3.6f, t));
        if (wf > 0) DrawRectangle(0, 0, W, H, Color{ 230, 240, 255, (unsigned char)(140 * wf) });
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
        playSfx(SFX_DIVE);
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
        playSfx(SFX_IMPACT);
    }

    void addScore(int pts) {
        score += pts;
        if (score > hiScore && !debugRun) hiScore = score;
        if (score >= nextLifeAt) {
            ++lives; nextLifeAt += 50000; playSfx(SFX_LIFE);
            popups.push_back({ { player.x, player.y - 40 }, 1.5f, "1UP!" });
        }
    }

    // Force field shatters: shards, flash, shockwave; brief grace period so one volley can't chain-kill
    void breakShield(Vector2 at) {
        shield = false; shieldGrace = 0.6f; shake = 0.25f; shieldRegen = SHIELD_REGEN_TIME;
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
        playSfx(SFX_SHIELD_BREAK);
    }

    void killPlayer() {
        shield = false;
        if (droneOn) destroyDrone();
        // losing a life costs one upgrade level; with none left you drop back to the basic Photon Torpedo
        if (wlevel > 0) --wlevel; else weapon = W_PHOTON;
        heat = 0; overheated = false; laserOn = false;
        playerAlive = false; respawnTimer = 2.5f; shake = 0.6f; --lives;
        explode(player, WHITE, RED, 80, 320);
        explode(player, YELLOW, ORANGE, 40, 200);
        playSfx(SFX_DEATH);
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
    // Back to the title screen with a clean playfield: nothing from the last game (enemies, rocks, boss,
    // shots, explosions, packages, popups, hyperspace, drone, laser hum) is left on screen.
    void returnToTitle() {
        enemies.clear(); pBullets.clear(); eBullets.clear(); parts.clear(); rings.clear(); warnings.clear();
        packages.clear(); zaps.clear(); meteors.clear(); popups.clear(); pendingSplits.clear();
        boss = Boss{}; meteorField = false; shake = 0; hyperT = -1;
        droneOn = false; laserOn = false; heat = 0; overheated = false; stopLaserLoop();
        playerAlive = false; invuln = 0; shield = false; bannerTimer = 0; stageTimer = 0;
        mode = TITLE; titleT = 0; paused = false; titleFxPrevT = -1;
        if (titleMusicOk) StopMusicStream(titleMusic);   // restarts from the top (in sync with the title cycle), fading in
    }

    void escChoose(int i) {
        escMenu = false;
        if (i == 1) {   // end current game -> back to the title screen
            if (!debugRun && score > hiScore) hiScore = score;
            saveHiScore();
            returnToTitle();
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
            if (IsKeyPressed(KEY_M) || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseLogical(), MUTE_BTN))) toggleTitleMute();
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
                playSfx(SFX_BOOM);
            }
            if (stageTimer > 3.0f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))) returnToTitle();
            return;
        }
        if (mode == GAMEOVER) {
            updateEnemies(dt);
            updateBoss(dt);
            updateMeteors(dt);
            if (stageTimer > 2.0f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))) returnToTitle();
            stageTimer += dt;
            return;
        }

        if (IsKeyPressed(KEY_P)) paused = !paused;
        if (paused) return;

        stageTimer += dt; cycleT += dt;
        bannerTimer -= dt;
        shake = std::max(0.0f, shake - dt);
        if (hyperT >= 0) { updateHyper(dt); if (!hyperSwitched) return; }

        updatePlayer(dt);
        updateDrone(dt);
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
            if (clearT > 1.6f) {
                clearT = 0;
                if (stageKind(stage) == SK_BOSS && playerAlive && hyperT < 0) startHyper(); else nextStage();
            }
        } else clearT = 0;
    }

    void updatePlayer(float dt) {
        if (!playerAlive) {
            respawnTimer -= dt;
            bool divers = std::any_of(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.state == DIVING; });
            if (respawnTimer <= 0 && lives <= 0) { mode = GAMEOVER; stageTimer = 0; saveHiScore(); }
            else if (respawnTimer <= 0 && !divers) {
                playerAlive = true; invuln = 2.0f; player = { W / 2.0f, PLAYER_Y_LOW }; prevPX = player.x; roll = 0; warpT = WARP_TIME;
                raiseShield();
            }
            return;
        }
        invuln = std::max(0.0f, invuln - dt);
        updateShipFx(dt);
        if (!shield && shieldRegen > 0 && (shieldRegen -= dt) <= 0) raiseShield();   // auto-regeneration
        shieldGrace = std::max(0.0f, shieldGrace - dt);
        shieldHitFlash = std::max(0.0f, shieldHitFlash - dt);
        shieldGrow = std::min(1.0f, shieldGrow + dt * 2.2f);
        float mv = 0, mvy = 0;
        if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) mv -= 1;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) mv += 1;
        if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) mvy -= 1;
        if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) mvy += 1;
        player.x = Clamp(player.x + mv * 340 * dt, PLAYER_MARGIN_X, W - PLAYER_MARGIN_X);
        player.y = Clamp(player.y + mvy * 300 * dt, PLAYER_Y_HIGH, PLAYER_Y_LOW);

        // debug runs: 1-5 pick a weapon, +/- change its level
        if (debugRun) {
            for (int k = 0; k < W_COUNT; ++k) if (IsKeyPressed((KeyboardKey)(KEY_ONE + k))) { weapon = k; heat = 0; overheated = false; }
            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) wlevel = std::min(3, wlevel + 1);
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) wlevel = std::max(0, wlevel - 1);
        }
        fireCooldown -= dt;
        bool trigger = IsKeyDown(KEY_SPACE) || autoFire;   // hold to keep firing
        laserOn = false;
        if (weapon == W_PULSE) {
            static const float rate[4] = { 0.4f, 0.34f, 0.29f, 0.24f };   // heat per second of firing (~2.5 s at Lv0)
            if (trigger && !overheated) {
                laserOn = true;
                heat += rate[wlevel] * dt;
                if (heat >= 1) { heat = 1; overheated = true; playSfx(SFX_OVERHEAT); }
                updateLaser(dt);
            } else {
                heat = std::max(0.0f, heat - dt * (overheated ? 0.8f : 1.2f));
                if (overheated && heat <= 0.25f) overheated = false;
                if (overheated && GetRandomValue(0, 3) == 0) { float life = frand(0.4f, 0.7f);
                    parts.push_back({ { player.x, player.y - 26 }, { frand(-10, 10), frand(-40, -20) }, life, life, 6, Color{ 120, 120, 130, 255 }, SMOKE }); }
            }
        }
        if (laserOn) { if (!IsSoundPlaying(sfx[SFX_LASER_LOOP])) PlaySound(sfx[SFX_LASER_LOOP]); }   // beam hum while firing
        else stopLaserLoop();
        if (weapon != W_PULSE && trigger && fireCooldown <= 0) { size_t nb = pBullets.size(); firePlayerWeapon(); if (pBullets.size() > nb && weapon != W_PULSE) muzzleT = 0.07f; }
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

    static constexpr float SPREAD_LIFE = 0.70f;   // Spread Cannon pellet range (620 px/s -> ~430 px): fly up to reach the top rows / boss
    void updateBullets(float dt) {
        for (auto& b : pBullets) {
            b.age += dt;
            if (b.kind == PB_HOMING || b.kind == PB_DRONE) {   // steer toward the nearest target
                static const float turnR[4] = { 3.0f, 4.2f, 4.2f, 5.0f };
                Vector2 tgt{ -1, -1 }; float best = 1e9f;
                if (stageKind(stage) == SK_METEORS && !boss.active) {
                    // meteor field: lock onto the closest rock ahead of the ship (35 deg cone, 350 range); keep it until
                    // it's destroyed, then re-acquire by the same rule; with no target, fly straight up
                    const Meteor* lock = nullptr;
                    if (b.aux > 0) for (auto& m : meteors) if (m.id == (int)b.aux - 1) { lock = &m; break; }
                    if (!lock) {
                        b.aux = 0;
                        for (auto& m : meteors) {
                            Vector2 d = Vector2Subtract(m.pos, player);
                            float dist = Vector2Length(d);
                            if (d.y >= 0 || dist > 350 + m.r() || fabsf(atan2f(d.x, -d.y)) > 35 * DEG2RAD) continue;
                            if (dist < best) { best = dist; lock = &m; }
                        }
                        if (lock) b.aux = (float)(lock->id + 1);
                    }
                    if (lock) tgt = lock->pos;
                    else tgt = { b.pos.x, b.pos.y - 1000 };
                } else
                for (auto& e : enemies) {
                    if (e.state == DEAD || e.state == WAITING) continue;
                    float d = Vector2Distance(e.pos, b.pos); if (e.pos.y < b.pos.y + 40 && d < best) { best = d; tgt = e.pos; } }
                if (boss.active && boss.state == B_FIGHT) { float d = Vector2Distance(boss.core(), b.pos); if (d < best) { best = d; tgt = boss.core(); } }
                if (stageKind(stage) != SK_METEORS || boss.active)
                    for (auto& m : meteors) { float d = Vector2Distance(m.pos, b.pos); if (m.pos.y < b.pos.y + 40 && d < best) { best = d; tgt = m.pos; } }
                float cur = atan2f(b.vel.y, b.vel.x);
                if (tgt.x >= 0 || tgt.y < -100) {
                    float want = atan2f(tgt.y - b.pos.y, tgt.x - b.pos.x);
                    float d = fmodf(want - cur + 3 * PI, 2 * PI) - PI, turn = (b.kind == PB_DRONE ? 3.5f : turnR[wlevel]) * dt;
                    cur += std::clamp(d, -turn, turn);
                }
                float sp = std::min(b.kind == PB_DRONE ? 560.0f : 480.0f, Vector2Length(b.vel) + 500 * dt);
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
                if (b.age > SPREAD_LIFE) b.pos.y = -100;   // short range: pellets burn out after ~430 px
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
    // ======================================================== WINGMAN DRONE ==
    void activateDrone() {
        droneT = DRONE_TIME;
        if (droneOn) return;                             // already flying: just refresh the timer
        droneOn = true; droneWarp = 0.6f; droneFireT = 0.8f; droneBank = 0;
        droneOff = droneTarget = { -50, 0 }; droneVel = { 0, 0 };
        playSfx(SFX_SHIELD_UP);
    }
    void destroyDrone() {
        Vector2 d = Vector2Add(player, droneOff);
        explode(d, Color{ 140, 255, 170, 255 }, Color{ 220, 230, 245, 255 }, 26, 240);
        for (int i = 0; i < 10; ++i) { float a = frand(0, 2 * PI), sp = frand(80, 220), life = frand(0.4f, 0.8f);
            parts.push_back({ d, { cosf(a) * sp, sinf(a) * sp }, life, life, frand(2, 3.5f), Color{ 200, 210, 225, 255 }, DEBRIS, frand(0, 360), frand(-800, 800) }); }
        playSfx(SFX_BOOM);
        droneOn = false;
    }
    // The drone flies on an arc around the ship (left -> over the top -> right, never below or on it, 42..90 units out).
    // Its preferred spot drifts along the left/top arc; when enemy shots threaten it, it moves to the safest spot.
    // Motion is acceleration-limited with a top speed 75% above the ship's. One enemy hit destroys it.
    void updateDrone(float dt) {
        if (!droneOn || !playerAlive) return;
        droneWarp = std::max(0.0f, droneWarp - dt);
        if ((droneT -= dt) <= 0) {                       // time's up: warp away
            parts.push_back({ Vector2Add(player, droneOff), { 0, 0 }, 0.35f, 0.35f, 30, Color{ 140, 255, 170, 255 }, FLASH });
            droneOn = false; return;
        }
        auto dangerAt = [&](Vector2 c) {
            Vector2 w = Vector2Add(player, c); float dg = 0;
            for (const auto& b : eBullets) {
                Vector2 v = Vector2Subtract(b.pos, w);
                if (v.x * v.x + v.y * v.y > 260 * 260) continue;
                for (float tt = 0; tt <= 0.5f; tt += 0.1f) {
                    float d = Vector2Distance(Vector2Add(b.pos, Vector2Scale(b.vel, tt)), w);
                    if (d < 26) dg += (1 - d / 26) * (1 - tt);
                }
            }
            return dg; };
        auto onScreen = [&](Vector2 c) { float x = player.x + c.x, y = player.y + c.y; return x > 16 && x < W - 16 && y > 60; };
        float ha = (205 + 40 * sinf(time * 0.7f)) * DEG2RAD, hr = 58 + 12 * sinf(time * 1.1f);
        Vector2 home{ cosf(ha) * hr, sinf(ha) * hr * 0.75f };
        if (!onScreen(home)) home.x = -home.x;           // near the left edge the idle spot mirrors to the right
        Vector2 best = home; float bestS = dangerAt(home) * 100;
        for (int ri = 0; ri < 4; ++ri) for (int ai = 0; ai <= 12; ++ai) {
            float r = 42 + ri * 16.0f, a = (180 + ai * 15) * DEG2RAD;
            Vector2 c{ cosf(a) * r, sinf(a) * r * 0.75f };
            if (!onScreen(c)) continue;
            float sc = dangerAt(c) * 100 + Vector2Distance(c, home) / 50 + Vector2Distance(c, droneOff) / 90;
            if (sc < bestS - 0.6f) { bestS = sc; best = c; }   // hysteresis: only change plan for a clearly better spot
        }
        droneTarget = best;
        Vector2 want = Vector2Subtract(droneTarget, droneOff); float dist = Vector2Length(want);
        Vector2 desired = dist > 1 ? Vector2Scale(want, std::min(DRONE_SPEED, dist * 6) / dist) : Vector2{ 0, 0 };
        Vector2 dv = Vector2Subtract(desired, droneVel); float dl = Vector2Length(dv), acc = 3000 * dt;
        if (dl > acc) dv = Vector2Scale(dv, acc / dl);
        droneVel = Vector2Add(droneVel, dv);
        droneOff = Vector2Add(droneOff, Vector2Scale(droneVel, dt));
        if (droneOff.y > 6) droneOff.y = 6;                                   // never below the ship
        float L = Vector2Length(droneOff);
        if (L < 40) droneOff = Vector2Scale(Vector2Normalize(droneOff), 40);  // never on top of it
        if (L > 95) droneOff = Vector2Scale(Vector2Normalize(droneOff), 95);
        droneOff.x = Clamp(droneOff.x, 16 - player.x, W - 16 - player.x);    // stay on screen
        droneBank += (Clamp(droneVel.x / DRONE_SPEED, -1, 1) * 16 - droneBank) * std::min(1.0f, dt * 8);
        Vector2 dpos = Vector2Add(player, droneOff);
        // fire: one basic homing missile at a time
        bool targets = boss.active || !meteors.empty() ||
                       std::any_of(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.state != DEAD && e.state != WAITING; });
        if ((droneFireT -= dt) <= 0 && targets && droneWarp <= 0) {
            droneFireT = 0.42f;
            pBullets.push_back({ { dpos.x, dpos.y - 10 }, { frand(-40, 40), -300 }, PB_DRONE });
        }
        // one hit destroys it
        for (size_t i = 0; i < eBullets.size(); ++i)
            if (Vector2Distance(eBullets[i].pos, dpos) < 10) { eBullets.erase(eBullets.begin() + i); destroyDrone(); return; }
    }
    void drawDrone() {
        if (!droneOn || !playerAlive) return;
        Vector2 d = Vector2Add(player, droneOff);
        float w = droneWarp > 0 ? 1 - droneWarp / 0.6f : 1;
        if (droneWarp > 0) {                              // green warp-in streaks
            BeginBlendMode(BLEND_ADDITIVE);
            for (int k = 0; k < 10; ++k) { float a = k * 0.628f, r0 = (1 - w) * 60 + 5;
                DrawLineEx({ d.x + cosf(a) * r0, d.y + sinf(a) * r0 * 0.6f }, { d.x + cosf(a) * (r0 + 10), d.y + sinf(a) * (r0 + 10) * 0.6f }, 1.2f, Color{ 140, 255, 170, (unsigned char)(220 * w) }); }
            drawGlow(d, 20 * w, Color{ 140, 255, 170, (unsigned char)(150 * w) });
            EndBlendMode();
        }
        float fa = droneWarp > 0 ? std::max(0.0f, (w - 0.6f) / 0.4f) : 1;
        if (droneT < 5 && fmodf(time, 0.25f) < 0.1f) fa *= 0.25f;          // blinks in its last 5 s
        if (fa <= 0) return;
        BeginBlendMode(BLEND_ADDITIVE);
        Vector2 n{ d.x, d.y + 15 };
        float fl = 0.85f + 0.15f * sinf(time * 47);
        DrawTriangle({ n.x - 2, n.y }, { n.x, n.y + (12 + 6 * Clamp(-droneVel.y / DRONE_SPEED, 0, 1)) * fl }, { n.x + 2, n.y }, Color{ 60, 130, 255, (unsigned char)(170 * fa) });
        drawGlow({ n.x, n.y + 2 }, 6 * fl, Color{ 90, 160, 255, (unsigned char)(170 * fa) });
        if (fmodf(time, 1.0f) < 0.08f) drawGlow({ d.x - 12, d.y - 2 }, 5, Color{ 110, 255, 120, (unsigned char)(230 * fa) });
        EndBlendMode();
        drawShip(gfx.drone, d, 30, droneBank, true, Color{ 255, 255, 255, (unsigned char)(255 * fa) });
    }

    static constexpr float WARP_TIME = 0.8f;
    // Banking, side-thruster puffs, exhaust particles and timers for the player's ship.
    // Velocity comes from the position change, so it works however the ship was moved.
    void updateShipFx(float dt) {
        warpT = std::max(0.0f, warpT - dt);
        muzzleT = std::max(0.0f, muzzleT - dt);
        float vx = dt > 0 ? (player.x - prevPX) / dt : 0;
        prevPX = player.x;
        float target = Clamp(vx / 340.0f, -1, 1) * 30;
        roll += (target - roll) * std::min(1.0f, dt * 9);
        float dv = vx - prevVX; prevVX = vx;
        if (fabsf(dv) > 150 && warpT <= 0) {           // sudden change of direction: side thrusters fire against it
            float side = dv > 0 ? -1.f : 1.f;          // puff leaves on the side opposite to the new push
            Vector2 o{ player.x + side * 17, player.y + 2 };
            for (int k = 0; k < 6; ++k) { float life = frand(0.25f, 0.4f);
                parts.push_back({ o, { side * frand(90, 200), frand(-40, 40) }, life, life, frand(2.5f, 4.5f), Color{ 230, 240, 255, 255 }, TRAIL }); }
        }
        if ((exhaustT -= dt) <= 0 && warpT <= 0) {     // exhaust particles stream behind the engines
            exhaustT = 1 / 45.0f;
            float cr = cosf(roll * DEG2RAD);
            for (float sx : { -1.f, 1.f }) { float life = frand(0.25f, 0.4f);
                parts.push_back({ { player.x + sx * 6 * cr + frand(-1, 1), player.y + 22 }, { frand(-10, 10) - vx * 0.25f, frand(150, 240) },
                                  life, life, frand(1.6f, 2.8f), Color{ 120, 190, 255, 255 }, TRAIL }); }
        }
    }

    void firePlayerWeapon() {
        Vector2 m{ player.x, player.y - 26 };
        switch (weapon) {
            case W_PHOTON: {
                static const int volleys[4] = { 3, 4, 4, 5 };   // torpedoes (or pairs/triples) on screen per level
                int per = wlevel >= 3 ? 3 : (wlevel >= 2 ? 2 : 1);
                if (countPB(PB_PHOTON) + per > volleys[wlevel] * per) return;
                for (int i = 0; i < per; ++i) { float off = (i - (per - 1) / 2.0f) * 16;
                    pBullets.push_back({ { m.x + off, m.y }, { off * 0.6f, -640 }, PB_PHOTON }); }
                break; }
            case W_SPREAD: {
                static const int pel[4] = { 3, 5, 5, 5 }, vol[4] = { 2, 2, 3, 3 };
                static const float spr[4] = { 30, 42, 46, 62 };
                int n = pel[wlevel];
                if (countPB(PB_SPREAD) + n > vol[wlevel] * n) return;
                for (int i = 0; i < n; ++i) { float a = (-90 - spr[wlevel] / 2 + spr[wlevel] * i / (n - 1)) * DEG2RAD;
                    pBullets.push_back({ m, { cosf(a) * 620, sinf(a) * 620 }, PB_SPREAD }); }
                break; }
            case W_HOMING: {
                static const int per[4] = { 1, 2, 3, 4 }, cap[4] = { 4, 4, 6, 8 };
                int n = per[wlevel];
                if (countPB(PB_HOMING) + n > cap[wlevel]) return;
                for (int i = 0; i < n; ++i) { float a = (-90 + (i - (n - 1) / 2.0f) * 38) * DEG2RAD;
                    pBullets.push_back({ { m.x + cosf(a) * 10, m.y + 6 }, { cosf(a) * 260, sinf(a) * 260 }, PB_HOMING }); }
                break; }
            case W_ION: {
                static const int cap[4] = { 2, 3, 3, 4 };
                if (countPB(PB_ION) >= cap[wlevel]) return;
                pBullets.push_back({ m, { 0, -560 }, PB_ION });
                break; }
            default: return;
        }
        fireCooldown = wCooldown(); ++shotsFired;
        Color c = WCOL[weapon];
        parts.push_back({ m, { 0, 0 }, 0.12f, 0.12f, 22, c, FLASH });
        playSfx(weapon == W_SPREAD ? SFX_FIRE_SPREAD : weapon == W_HOMING ? SFX_FIRE_HOMING : weapon == W_ION ? SFX_FIRE_ION : SFX_SHOOT);
    }

    // Damage one enemy by one hit. Returns true if it was destroyed.
    bool hitEnemy(Enemy& e, Vector2 at) {
        (void)at;
        if (--e.hp > 0) {
            playSfx(SFX_HIT);
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
            playSfx(SFX_BOOM);
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
        playSfx(SFX_BOOM); playSfx(SFX_IMPACT);
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
        playSfx(SFX_DIVE);
    }

    // Ion Burst detonation: damages everything nearby; Lv3 chains lightning to up to 3 more enemies
    void ionBlast(Vector2 at, bool bossDirect) {
        static const float R[4] = { 70, 80, 80, 85 };
        float r = R[wlevel];
        Color c = WCOL[W_ION];
        parts.push_back({ at, { 0, 0 }, 0.25f, 0.25f, r * 1.1f, Color{ 230, 200, 255, 255 }, FLASH });
        rings.push_back({ at, 0.3f, 0.3f, r, c });
        explode(at, c, WHITE, 16, 200);
        playSfx(SFX_ION_BLAST);
        for (auto& e : enemies)
            if (e.state != DEAD && e.state != WAITING && Vector2Distance(e.pos, at) < r + 14) hitEnemy(e, at);
        for (size_t j = 0; j < eBullets.size();)              // the blast also clears missiles and rocks
            if (shootable(eBullets[j].kind) && Vector2Distance(eBullets[j].pos, at) < r + shootableR(eBullets[j].kind)) destroyProjectile(j); else ++j;
        std::vector<int> mids;
        for (int j = 0; j < (int)meteors.size(); ++j) if (Vector2Distance(meteors[j].pos, at) < r + meteors[j].r() * 0.6f) mids.push_back(meteors[j].id);
        for (int id : mids) {
            auto it = std::find_if(meteors.begin(), meteors.end(), [id](const Meteor& m) { return m.id == id; });
            if (it != meteors.end()) hitMeteor((size_t)(it - meteors.begin()), Vector2Subtract(it->pos, at));
        }
        if (!bossDirect && boss.active && boss.state == B_FIGHT && Vector2Distance(boss.core(), at) < r + boss.size() * 0.3f) damageBoss(at);
        if (wlevel >= 2) {   // chain lightning (Lv2+)
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
        for (size_t j = 0; j < eBullets.size();) {            // the beam burns through missiles and rocks in its path
            const Bullet& b = eBullets[j];
            if (shootable(b.kind) && b.pos.y < y0 && b.pos.y > laserTop - shootableR(b.kind) && fabsf(b.pos.x - x) < w / 2 + shootableR(b.kind))
                destroyProjectile(j);
            else ++j;
        }
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
            if (bossY > 0) { damageBoss({ x, bossY }, 2.0f); any = true; }
            if (any) ++hits;
        } else if (firstE) { hitEnemy(*firstE, { x, stop }); ++hits; }
        else if (firstM >= 0) { hitMeteor((size_t)firstM, { 0, -1 }); ++hits; }
        else if (firstBoss) { damageBoss({ x, stop }, 2.0f); ++hits; }
    }

    void collectPackage(const Package& pk) {
        Color c = pk.content >= 0 ? WCOL[pk.content] : UPG_COL;
        std::string label;
        if (pk.content < 0 || pk.content == weapon) {
            if (wlevel < 3) { ++wlevel; label = TextFormat("UPGRADE +1   LV%d", wlevel); }
            else { addScore(1000); label = droneOn ? "MAX LEVEL +1000  DRONE 30s" : "MAX LEVEL +1000  WINGMAN DRONE"; activateDrone(); }
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
        playSfx(SFX_SHIELD_UP); playSfx(SFX_LIFE);
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
        Vector2 o{ SB_WEAPON_X, H - 33.0f };
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
        if (droneOn && playerAlive) {                    // wingman drone timer
            float dx = o.x + 70, f = droneT / DRONE_TIME;
            Color dc{ 140, 255, 170, 255 };
            DrawRectangleRounded({ dx, o.y, 56, 24 }, 0.35f, 6, Color{ 10, 28, 16, 200 });
            DrawRectangleRoundedLinesEx({ dx, o.y, 56, 24 }, 0.35f, 6, 1.0f, Color{ dc.r, dc.g, dc.b, 170 });
            DrawRectangle((int)dx + 6, (int)o.y + 17, 44, 3, Color{ 30, 50, 36, 255 });
            DrawRectangle((int)dx + 6, (int)o.y + 17, (int)(44 * f), 3, droneT < 5 && fmodf(time, 0.3f) < 0.15f ? Color{ 255, 220, 120, 255 } : dc);
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
        if (!pendingSplits.empty()) playSfx(SFX_HIT);
        pendingSplits.clear();
    }

    // Physical enemy projectiles that player weapons can destroy; energy shots are unaffected.
    static bool shootable(int kind) { return kind == BK_MISSILE || kind == BK_C_HOMING || kind == BK_METEOR || kind == BK_FLAK; }
    static float shootableR(int kind) { return kind == BK_METEOR ? 11.f : kind == BK_FLAK ? 9.f : 7.f; }
    void destroyProjectile(size_t i) {
        Bullet b = eBullets[i];
        eBullets.erase(eBullets.begin() + i);
        if (b.kind == BK_METEOR) {
            explode(b.pos, Color{ 255, 150, 60, 255 }, Color{ 150, 130, 115, 255 }, 12, 180);
            for (int q = 0; q < 6; ++q) { float ang = frand(0, 2 * PI), sp = frand(60, 160), life = frand(0.4f, 0.8f);
                parts.push_back({ b.pos, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(2, 3.5f), Color{ 130, 115, 100, 255 }, DEBRIS, frand(0, 360), frand(-600, 600) }); }
        } else explode(b.pos, Color{ 255, 170, 70, 255 }, WHITE, 14, 200);
        addScore(25);
        playSfx(SFX_IMPACT);
    }

    void collisions() {
        // Player projectiles vs enemies, rocks and the boss
        for (size_t i = 0; i < pBullets.size();) {
            bool used = false;
            Bullet pb = pBullets[i];
            float pr = pb.kind == PB_ION ? 9.f : pb.kind == PB_SPREAD ? 5.f : (pb.kind == PB_HOMING || pb.kind == PB_DRONE) ? 6.f : 7.f;
            auto impact = [&](Vector2 at) {
                if (pb.kind == PB_PHOTON) torpedoImpact(at);
                else if (pb.kind != PB_ION) {
                    Color c = pb.kind == PB_SPREAD ? WCOL[W_SPREAD] : WCOL[W_HOMING];
                    parts.push_back({ at, { 0, 0 }, 0.15f, 0.15f, 18, c, FLASH });
                    for (int q = 0; q < 6; ++q) { float ang = frand(0, 2 * PI), sp = frand(100, 240), life = frand(0.12f, 0.3f);
                        parts.push_back({ at, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(1, 1.8f), c, SPARK }); }
                    playSfx(SFX_IMPACT);
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
                for (size_t j = 0; j < eBullets.size(); ++j)
                    if (shootable(eBullets[j].kind) && CheckCollisionCircles(pb.pos, pr, eBullets[j].pos, shootableR(eBullets[j].kind))) {
                        used = true; ++hits;
                        impact(pb.pos);
                        if (pb.kind == PB_ION) ionBlast(pb.pos, false); else destroyProjectile(j);
                        break;
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
                damageBoss(pb.pos, pb.kind == PB_ION ? 3.0f : (pb.kind == PB_HOMING || pb.kind == PB_DRONE) ? 0.75f : pb.kind == PB_SPREAD ? 0.5f : 1.0f);
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


    // ======================================================== TITLE SWARM ==
    // Three rotating titles spelled by swarms of tiny animated monsters. Per title (7 s): the swarm streams in
    // from both sides along curved entry paths (outer letters first), holds as a gently breathing formation,
    // then peels off left to right and dives away; the next swarm follows.
    struct SwarmMon { Vector2 tgt, in0, in1, in2, out1, out2, out3; float dIn, dOut, ph; int kind; };
    struct SwarmTitle { const char* label; std::vector<SwarmMon> mons; };
    static constexpr float SW_CYCLE = 7.0f, SW_IN = 1.8f, SW_OUT_AT = 5.0f, SW_OUT = 1.3f;
    std::vector<SwarmTitle> swarmTitles;
    float titleT = 0;
    Font  swarmFont{}; bool swarmFontOwn = false;   // a heavy sans font gives the swarm thick, readable letters

    static Vector2 bez3(Vector2 a, Vector2 b, Vector2 c, Vector2 d, float t) {
        float u = 1 - t;
        return { u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                 u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y };
    }
    // Rasterise the lines and place one monster on every grid point inside the letters.
    std::vector<SwarmMon> buildSwarm(std::vector<std::pair<const char*, float>> lines, float yTop) {
        std::vector<SwarmMon> out; float y = yTop; int lineNo = 0;
        for (auto& [str, sz] : lines) {
            float sp = sz * 0.08f;
            Image im = ImageTextEx(swarmFont, str, sz, sp, WHITE);
            int iw = im.width, ih = im.height;
            Color* px = LoadImageColors(im);
            float step = sz > 88 ? 8.5f : 7.0f, x0 = W / 2.0f - iw / 2.0f;
            for (float yy = step / 2; yy < ih; yy += step)
                for (float xx = step / 2; xx < iw; xx += step) {
                    // keep a grid point when enough of its cell is ink (not just the centre pixel), so thin strokes
                    // such as the bar of an "e" or the dot of an "i" always get monsters
                    int cov = 0, tot = 0;
                    for (int cy = (int)(yy - step / 2); cy < (int)(yy + step / 2); ++cy)
                        for (int cx = (int)(xx - step / 2); cx < (int)(xx + step / 2); ++cx) {
                            if (cx < 0 || cy < 0 || cx >= iw || cy >= ih) continue;
                            ++tot; if (px[cy * iw + cx].a > 140) ++cov;
                        }
                    if (tot == 0 || cov < 0.3f * tot) continue;
                    SwarmMon m;
                    m.tgt = { x0 + xx, y + yy };
                    float side = m.tgt.x < W / 2 ? -1.f : 1.f;
                    m.in0 = { W / 2 + side * (W / 2 + 60), frand(-60, 120) };
                    m.in1 = { W / 2 - side * frand(60, 200), frand(330, 480) };
                    m.in2 = { m.tgt.x + side * frand(40, 120), m.tgt.y + frand(80, 160) };
                    float s2 = frand(0, 1) < 0.5f ? -1.f : 1.f;
                    m.out1 = { m.tgt.x + s2 * frand(30, 70), m.tgt.y - frand(20, 50) };
                    m.out2 = { m.tgt.x + s2 * frand(100, 180), m.tgt.y + frand(180, 300) };
                    m.out3 = { m.tgt.x + s2 * frand(20, 90), H + 60.0f };
                    m.dIn  = fabsf(m.tgt.x - W / 2) / (W / 2) * 0.9f + frand(0, 0.15f);   // outer letters fill first
                    m.dOut = (m.tgt.x / W) * 0.7f + frand(0, 0.15f);                       // peel off left to right
                    m.kind = lineNo == 0 ? (yy < ih * 0.45f ? Gfx::MON_JELLY : Gfx::MON_MANTA) : Gfx::MON_URCHIN;
                    m.ph = frand(0, 1);
                    out.push_back(m);
                }
            UnloadImageColors(px); UnloadImage(im);
            y += ih + 4; ++lineNo;
        }
        return out;
    }
    void buildTitleParticles() {
        swarmTitles.clear();
        swarmFont = font;
        for (const char* f : { "C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                               "/System/Library/Fonts/Supplemental/Arial Bold.ttf" })
            if (FileExists(f)) { Font ft = LoadFontEx(f, 112, nullptr, 0); if (ft.texture.id) { swarmFont = ft; swarmFontOwn = true; break; } }
        swarmTitles.push_back({ nullptr, buildSwarm({ { "STAR", 92 }, { "SWARM", 92 } }, 70) });
        swarmTitles.push_back({ "DEVELOPED BY", buildSwarm({ { "GILAD", 86 }, { "GLOBEN", 86 } }, 90) });
        swarmTitles.push_back({ "DEDICATED TO", buildSwarm({ { "DANIEL", 86 }, { "GLOBEN", 86 } }, 90) });
    }
    static float swEase(float a, float b, float x) { float t = Clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    void drawTitleParticles() {                    // the swarm itself (logical coordinates, inside the camera)
        if (swarmTitles.empty()) return;
        int ti = (int)(titleT / SW_CYCLE) % (int)swarmTitles.size();
        float t = fmodf(titleT, SW_CYCLE);
        const SwarmTitle& T = swarmTitles[ti];
        float breathe = 1 + 0.015f * sinf(time * 2.2f), sway = 6 * sinf(time * 1.1f);
        static std::vector<std::pair<Vector2, const SwarmMon*>> vis;
        vis.clear();
        for (const auto& m : T.mons) {
            if (t < m.dIn) continue;
            Vector2 home{ W / 2 + (m.tgt.x - W / 2) * breathe + sway, m.tgt.y };
            float uIn = swEase(0, SW_IN, t - m.dIn), uOut = Clamp((t - SW_OUT_AT - m.dOut) / SW_OUT, 0, 1);
            Vector2 p = uOut > 0 ? bez3(home, m.out1, m.out2, m.out3, uOut * uOut) : (uIn < 1 ? bez3(m.in0, m.in1, m.in2, home, uIn) : home);
            vis.push_back({ p, &m });
        }
        BeginBlendMode(BLEND_ADDITIVE);
        for (auto& [p, m] : vis) {
            Color gc = m->kind == Gfx::MON_JELLY ? Color{ 80, 230, 200, 85 } : m->kind == Gfx::MON_MANTA ? Color{ 120, 150, 255, 85 } : Color{ 255, 200, 80, 85 };
            drawGlow(p, 8, gc);
        }
        EndBlendMode();
        for (auto& [p, m] : vis) {
            int f = (int)(fmodf(time * 0.9f + m->ph, 1.f) * Gfx::MON_FRAMES) % Gfx::MON_FRAMES;
            const Texture2D& tx = gfx.mon[m->kind][f];
            float S = m->tgt.y > 0 && T.label ? (m->kind == Gfx::MON_URCHIN ? 10.5f : 11.5f) : (m->kind == Gfx::MON_URCHIN ? 12 : 13);
            DrawTexturePro(tx, { 0, 0, (float)tx.width, (float)tx.height }, { p.x, p.y, S, S }, { S / 2, S / 2 }, 0, WHITE);
        }
    }
    void drawTitleText() {                         // the small "DEVELOPED BY" / "DEDICATED TO" labels (screen space)
        if (swarmTitles.empty()) return;
        int ti = (int)(titleT / SW_CYCLE) % (int)swarmTitles.size();
        float t = fmodf(titleT, SW_CYCLE);
        const char* label = swarmTitles[ti].label;
        if (!label) return;
        float a = swEase(1.2f, 2.0f, t) * (1 - swEase(4.8f, 5.4f, t));
        drawCentered(label, 62, 20, Color{ 170, 200, 255, (unsigned char)(255 * a) });
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
        float starFade = 1 - hyperStreak();
        for (auto& s : stars) {
            float tw = 0.5f + 0.5f * sinf(time * 3 + s.phase);
            Color c = s.col; c.a = (unsigned char)(std::min(255.0f, 60 + 195 * tw * (s.speed / 150.0f)) * starFade);
            float sz = s.speed > 100 ? 1.4f : 0.9f;
            DrawRectangleV({ s.pos.x - sz / 2, s.pos.y - sz / 2 }, { sz, sz * (mode == PLAYING ? 2.2f : 1.0f) }, c);
            if (s.speed > 130) drawGlow(s.pos, 3.5f, Color{ c.r, c.g, c.b, (unsigned char)(c.a / 3) });
        }
        EndBlendMode();

        if (hyperT >= 0) drawHyper();

        // smoke under ships
        for (auto& p : parts) if (p.kind == SMOKE) {
            float t = p.life / p.maxLife;
            drawGlow(p.pos, p.size * (1.6f - 0.6f * t), Color{ p.col.r, p.col.g, p.col.b, (unsigned char)(110 * t) });
        }

        drawMeteors();
        drawBoss();

        for (auto& e : enemies) {
            if (e.state == DEAD || e.state == WAITING) continue;
            // each monster loops its own animation, offset per formation slot so a wave never moves in lockstep;
            // divers animate faster
            float off = spr::hash2(e.sx * 3.1f + 7, e.sy * 5.7f + 3);
            auto ph = [&](float loopsPerSec) { return time * loopsPerSec * (e.state == DIVING ? 1.8f : 1.0f) + off; };
            const int MF = Gfx::MON_FRAMES;
            float g = 0.3f + 0.7f * e.grow;
            if (e.grow < 1) {                  // engine burst while emerging from the hatch
                BeginBlendMode(BLEND_ADDITIVE);
                drawGlow(e.pos, 30 * (1 - e.grow) + 8, Color{ 120, 200, 255, (unsigned char)(200 * (1 - e.grow)) });
                EndBlendMode();
            }
            switch (e.type) {
                case BEE:       drawMonster(gfx.mon[Gfx::MON_URCHIN], MF, ph(0.55f), e.pos, 42 * g, e.angle); break;
                case BUTTERFLY: drawMonster(gfx.mon[Gfx::MON_MANTA], MF, ph(0.9f), e.pos, 46 * g, e.angle); break;
                case BOSS:      drawMonster(gfx.mon[e.hp < 2 ? Gfx::MON_JELLY_HIT : Gfx::MON_JELLY], MF, ph(0.45f), e.pos, 56 * g, e.angle); break;
                case DART: {
                    if (e.state == DIVING) {        // comet trail while striking
                        BeginBlendMode(BLEND_ADDITIVE);
                        Vector2 v = Vector2Normalize(Vector2Subtract(e.pos, e.prev));
                        for (int k = 1; k <= 5; ++k) drawGlow(Vector2Subtract(e.pos, Vector2Scale(v, 8.0f * k)), 11 - k * 1.6f,
                                                              k < 3 ? Color{ 255, 120, 200, (unsigned char)(190 - k * 30) } : Color{ 120, 150, 255, (unsigned char)(170 - k * 28) });
                        EndBlendMode();
                    }
                    drawMonster(gfx.mon[Gfx::MON_IMP], MF, ph(1.3f), e.pos, 40 * g, e.angle); break;
                }
                case SHIELDED:
                    drawMonster(gfx.mon[Gfx::MON_BRUTE], MF, ph(0.35f), e.pos, 44 * g, e.angle);
                    if (e.hp > 1) {                 // projected energy shield
                        float pl = 0.6f + 0.4f * sinf(time * 5 + e.sx);
                        BeginBlendMode(BLEND_ADDITIVE);
                        drawGlow(e.pos, 30, Color{ 60, 220, 255, (unsigned char)(40 + 30 * pl) });
                        DrawRing(e.pos, 22.5f, 24.5f, 0, 360, 36, Color{ 110, 255, 240, (unsigned char)(120 + 80 * pl) });
                        DrawPolyLinesEx(e.pos, 6, 23, time * 40, 1.0f, Color{ 150, 255, 245, (unsigned char)(70 * pl) });
                        EndBlendMode();
                    }
                    break;
                case SPLITTER: drawMonster(gfx.mon[Gfx::MON_LEVIATHAN], MF, ph(0.5f), e.pos, 46 * g, e.angle); break;
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
                case MINI:     drawMonster(gfx.mon[Gfx::MON_SPAWN], MF, ph(0.9f), e.pos, 26 * g, e.angle); break;
            }
        }

        if ((mode == PLAYING || mode == VICTORY) && playerAlive && !(hyperT >= 0 && !hyperSwitched)) {
            bool blink = invuln > 0 && fmodf(time, 0.2f) < 0.1f;
            if (!blink) drawPlayerShip();
            drawDrone();
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
            } else if (b.kind == PB_SPREAD) {  // spread plasma bolt (shrinks and fades as it burns out)
                float k0 = Clamp((SPREAD_LIFE - b.age) / 0.15f, 0, 1), sc = 0.4f + 0.6f * k0;
                auto A = [&](int a) { return (unsigned char)(a * k0); };
                drawGlow(b.pos, 18 * sc, Color{ 255, 150, 40, A(210) });
                for (int k = 1; k <= 3; ++k) drawGlow(Vector2Subtract(b.pos, Vector2Scale(dv, k * 7.0f * sc)), (9 - k * 2.0f) * sc, Color{ 255, 140, 40, A(170 - k * 40) });
                DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 8 * sc)), Vector2Add(b.pos, Vector2Scale(dv, 6 * sc)), 5 * sc, Color{ 255, 220, 120, A(255) });
                DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 5 * sc)), Vector2Add(b.pos, Vector2Scale(dv, 5 * sc)), 2.2f * sc, Color{ 255, 255, 255, A(255) });
            } else if (b.kind == PB_HOMING || b.kind == PB_DRONE) {  // homing missile (body drawn here; trail from particles)
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
            } else if (b.kind == BK_FLAK) {    // OUTPOST FLAK: slow spiky red shell (shootable)
                float rot = b.age * 300, pl = 0.85f + 0.15f * sinf(time * 25 + b.pos.x);
                drawGlow(b.pos, 20 * pl, Color{ 255, 70, 40, 200 });
                DrawPoly(b.pos, 4, 8, rot, Color{ 255, 120, 60, 255 });
                DrawPoly(b.pos, 4, 8, rot + 45, Color{ 255, 90, 50, 255 });
                DrawCircleV(b.pos, 3.4f, Color{ 255, 240, 200, 255 });
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
                m3d::draw(gfx.meteor[v], b.age * (v % 2 ? 160 : -130), b.pos, (24 + v * 1.5f) * 1.08f, (float)(v * 25));
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

    // ---- Title-screen enemy roster: 4x2 grid (7 monsters + rotating BOSS) and a featured Courier row ----
    struct RosterEntry { int mon; const char* name; const char* pts; Color col; };
    static const RosterEntry* roster() {
        static const RosterEntry R[7] = {
            { Gfx::MON_URCHIN,    "NOVA URCHIN",      "50 / 100",  { 255, 200, 80, 255 } },
            { Gfx::MON_MANTA,     "VOID MANTA",       "80 / 160",  { 120, 150, 255, 255 } },
            { Gfx::MON_JELLY,     "NEBULA JELLY",     "150 / 400", { 80, 230, 200, 255 } },
            { Gfx::MON_IMP,       "COMET IMP",        "120 / 240", { 255, 120, 200, 255 } },
            { Gfx::MON_BRUTE,     "ASTEROID BRUTE",   "150 / 300", { 255, 140, 60, 255 } },
            { Gfx::MON_LEVIATHAN, "COSMIC LEVIATHAN", "100 / 200", { 170, 150, 255, 255 } },
            { Gfx::MON_SPAWN,     "LEVIATHAN SPAWN",  "60",        { 120, 200, 255, 255 } } };
        return R;
    }
    static constexpr float ROSTER_Y = 336, ROSTER_DY = 46, ROSTER_ICON = 40, ROSTER_BOSS_PER = 1.6f;
    static constexpr Rectangle ROSTER_FEAT{ 30, 506, 540, 62 };
    static Vector2 rosterCell(int k) { return { 62.0f + (k % 2) * 285, ROSTER_Y + (k / 2) * ROSTER_DY }; }   // icon centre
    static Color rosterBossCol(int b) { const Vector3 c = spr::bossDesigns()[b].core;
        return Color{ (unsigned char)(c.x * 255), (unsigned char)(c.y * 255), (unsigned char)(c.z * 255), 255 }; }
    // which boss is showing, the next one, and the cross-fade amount between them
    void rosterBossState(int& bi, int& bn, float& xf) const {
        float u = time / ROSTER_BOSS_PER, fu = u - floorf(u); bi = (int)u % 10; bn = (bi + 1) % 10;
        float t = std::clamp((fu - 0.8f) / 0.2f, 0.0f, 1.0f); xf = t * t * (3 - 2 * t);
    }
    int rosterDropLit() const { return (int)(time * 2.5f) % 5; }
    Color rosterPodCol() const { int i = (int)fmodf(time * 0.8f, 6.0f); return i == 0 ? UPG_COL : WCOL[i < 5 ? i : 4]; }

    // ---- Title-screen mute button (bottom-left, icon only): click or M toggles title music + effects ----
    bool titleMuted = false;                            // saved in SETTINGS_FILE; gameplay sound is never affected
    static constexpr Rectangle MUTE_BTN{ 18, 754, 34, 34 };
    void toggleTitleMute() {
        titleMuted = !titleMuted;
        if (titleMuted) for (auto& s : titleFx) StopSound(s);
        saveSettings();
    }
    void drawMuteButtonShapes() {
        const Rectangle& r = MUTE_BTN;
        bool hover = CheckCollisionPointRec(mouseLogical(), r);
        Color edge = titleMuted ? Color{ 255, 90, 80, 255 } : Color{ 120, 200, 255, 255 };
        DrawRectangleRounded(r, 0.3f, 8, hover ? Color{ 40, 44, 70, 230 } : Color{ 20, 22, 40, 200 });
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 1.5f, Color{ edge.r, edge.g, edge.b, (unsigned char)(hover ? 255 : 170) });
        Vector2 c{ r.x + 13, r.y + r.height / 2 };
        Color ic = titleMuted ? Color{ 170, 170, 185, 255 } : RAYWHITE;
        DrawRectangleRec({ c.x - 6, c.y - 4, 5, 8 }, ic);                                  // speaker body
        DrawTriangle({ c.x - 1, c.y - 4 }, { c.x - 1, c.y + 4 }, { c.x + 5, c.y + 9 }, ic);  // cone
        DrawTriangle({ c.x - 1, c.y - 4 }, { c.x + 5, c.y + 9 }, { c.x + 5, c.y - 9 }, ic);
        if (titleMuted) {                                                                   // red X
            Vector2 x0{ c.x + 13, c.y };
            DrawLineEx({ x0.x - 5, x0.y - 5 }, { x0.x + 5, x0.y + 5 }, 2.6f, Color{ 255, 90, 80, 255 });
            DrawLineEx({ x0.x - 5, x0.y + 5 }, { x0.x + 5, x0.y - 5 }, 2.6f, Color{ 255, 90, 80, 255 });
        } else {                                                                            // sound waves, gently pulsing
            for (int k = 0; k < 2; ++k) { float rr = 6.5f + k * 5.0f; unsigned char a = (unsigned char)(150 + 100 * (0.5f + 0.5f * sinf(time * 4 - k)));
                DrawRing({ c.x + 3, c.y }, rr, rr + 1.8f, -45, 45, 12, Color{ 120, 200, 255, a }); }
        }
    }

    void drawRosterShapes() {
        for (int k = 0; k < 7; ++k)
            drawMonster(gfx.mon[roster()[k].mon], Gfx::MON_FRAMES, time * 0.8f + k * 0.13f, rosterCell(k), ROSTER_ICON, 0);
        // BOSS cell: all ten bosses in turn, cross-fading, each with a glow in its core colour
        int bi, bn; float xf; rosterBossState(bi, bn, xf);
        Vector2 bc = rosterCell(7); float pl = 0.7f + 0.3f * sinf(time * 4), bs = ROSTER_ICON * 1.3f;
        for (int pass = 0; pass < 2; ++pass) {
            int b = pass ? bn : bi; float a = pass ? xf : 1 - xf; if (a <= 0.01f) continue;
            Color gc = rosterBossCol(b); gc.a = (unsigned char)(90 * a * pl);
            BeginBlendMode(BLEND_ADDITIVE); drawGlow(bc, ROSTER_ICON * 0.75f, gc); EndBlendMode();
            drawShip(gfx.bossIcon[b], bc, bs, 0, true, Color{ 255, 255, 255, (unsigned char)(255 * a) });
        }
        // featured Courier row
        const Rectangle& r = ROSTER_FEAT; float fp = 0.6f + 0.4f * sinf(time * 3);
        DrawRectangleRounded(r, 0.25f, 8, Color{ 40, 32, 10, 200 });
        DrawRectangleRoundedLinesEx(r, 0.25f, 8, 1.6f, Color{ 255, 215, 60, (unsigned char)(150 + 80 * fp) });
        Vector2 cp{ r.x + 40, r.y + 31 }; float cs = 50; Color pc = rosterPodCol();
        drawShip(gfx.courier[0], cp, cs, 0);
        float rr = cs * 0.08f; DrawCircleV({ cp.x, cp.y - 0.5f }, rr, pc);
        BeginBlendMode(BLEND_ADDITIVE); drawGlow(cp, rr * 3.4f, Color{ pc.r, pc.g, pc.b, 210 }); EndBlendMode();
        int lit = rosterDropLit();
        for (int i = 0; i < 5; ++i) {
            int content = i < 4 ? i + 1 : -1; Color col = content >= 0 ? WCOL[content] : UPG_COL;
            Vector2 c{ r.x + 320 + i * 44.0f, r.y + 31 }; bool on = i == lit; float sz = on ? 32.5f : 26.0f;
            BeginBlendMode(BLEND_ADDITIVE); drawGlow(c, sz * (on ? 1.2f : 0.8f), Color{ col.r, col.g, col.b, (unsigned char)(on ? 150 : 70) }); EndBlendMode();
            DrawPoly(c, 6, sz * 0.62f, time * 40, Color{ 30, 30, 45, 255 });
            DrawPolyLinesEx(c, 6, sz * 0.62f, time * 40, sz * 0.07f, col);
            DrawCircleV(c, sz * 0.36f, Color{ (unsigned char)(col.r / 3), (unsigned char)(col.g / 3), (unsigned char)(col.b / 3), 255 });
            weaponIcon(content >= 0 ? content : 5, c, sz * 0.5f, col);
        }
    }
    void drawRosterText() {
        drawCentered("POINTS:  FORMATION / DIVING", ROSTER_Y - 38, 13, Color{ 110, 120, 150, 255 });
        for (int k = 0; k < 8; ++k) {
            Vector2 c = rosterCell(k); float x = c.x + 30;
            if (k < 7) {
                text(roster()[k].name, x, c.y - 17, 15, roster()[k].col);
                text(roster()[k].pts, x, c.y + 2, 17, Color{ 235, 235, 245, 255 });
            } else {
                text("BOSS", x, c.y - 17, 15, Color{ 255, 120, 190, 255 });
                text("5000 - 50000", x, c.y + 2, 17, Color{ 235, 235, 245, 255 });
                int bi, bn; float xf; rosterBossState(bi, bn, xf);
                int b = xf > 0.5f ? bn : bi; float na = xf > 0 ? fabsf(xf - 0.5f) * 2 : 1;   // name dips out and back in mid-fade
                Color nc = rosterBossCol(b);
                nc = { (unsigned char)std::min(255, nc.r + 60), (unsigned char)std::min(255, nc.g + 60), (unsigned char)std::min(255, nc.b + 60), (unsigned char)(220 * na) };
                text(TextFormat("%d/10  %s", b + 1, spr::bossDesigns()[b].name), x + 48, c.y - 14, 11, nc);
            }
        }
        const Rectangle& r = ROSTER_FEAT;
        text("COURIER", r.x + 75, r.y + 8, 17, Color{ 255, 215, 60, 255 });
        text("500 PTS  .  8-12 HITS", r.x + 75, r.y + 30, 13, Color{ 235, 235, 245, 255 });
        text("drops:", r.x + 260, r.y + 24, 12, Color{ 200, 190, 150, 255 });
    }

    // HUD graphics (drawn inside the camera, in logical coordinates)
    void drawHudShapes() {
        if (mode == TITLE) {
            int f = fmodf(time, 0.5f) < 0.25f ? 0 : 1;
            if (stageDialog) { drawStageDialogShapes(); return; }
            drawTitleParticles();
            (void)f;
            drawRosterShapes();
            drawMuteButtonShapes();
            return;
        }
        if (mode == PLAYING || mode == VICTORY || mode == GAMEOVER) drawStatusPanel();
        {   int spare = std::max(0, lives - (playerAlive ? 1 : 0));
            if (spare <= 4) for (int i = 0; i < spare; ++i) drawShip(gfx.player, { SB_LIVES_X + 15 + i * 28.0f, H - 21.0f }, 26, 0, false);
            else drawShip(gfx.player, { SB_LIVES_X + 15, H - 21.0f }, 26, 0, false);   // "x N" label drawn with the HUD text
        }
        if (mode == PLAYING || mode == VICTORY) drawWeaponIndicator();
        if (mode == PLAYING || mode == VICTORY || mode == GAMEOVER) drawCampaignBar();
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
        if (mode == PLAYING && playerAlive && !shield && shieldRegen > 0) {   // shield recharge bar under the label
            float f = 1 - shieldRegen / SHIELD_REGEN_TIME, bw = 76, x = SB_SHIELD_CX - bw / 2, y = H - 13.0f;
            DrawRectangleRounded({ x, y, bw, 3.5f }, 1, 6, Color{ 40, 30, 36, 220 });
            DrawRectangleRounded({ x, y, std::max(3.5f, bw * f), 3.5f }, 1, 6, Color{ (unsigned char)(140 - 30 * f), (unsigned char)(90 + 120 * f), (unsigned char)(90 + 165 * f), 255 });
        }
        if (mode == GAMEOVER) DrawRectangle(0, H / 2 - 120, W, 240, Color{ 0, 0, 0, 170 });
        if (mode == VICTORY)  DrawRectangle(0, H / 2 - 170, W, 330, Color{ 0, 0, 0, 150 });
    }

    // Campaign bar (bottom right): progress through all 60 stages, one crest per boss (gold once defeated).
    // ---- bottom status bar: fixed zones so indicators never overlap
    //  [ spare ships | weapon + drone | shield | stage / campaign bar ]
    static constexpr float SB_TOP = H - 42.0f;
    static constexpr float SB_LIVES_X = 8, SB_WEAPON_X = 134, SB_SHIELD_CX = 337, SB_SEP[3] = { 127, 266, 410 };
    static constexpr float CB_X0 = 420, CB_X1 = 590, CB_Y = H - 14.0f;
    void textCenteredAt(const char* t, float cx, float y, float size, Color c) {
        float fs = size * scale, sp = customFont ? fs * 0.04f : fs / 10;
        text(t, cx - MeasureTextEx(font, t, fs, sp).x / scale / 2, y, size, c);
    }
    void drawStatusPanel() {
        DrawRectangle(0, (int)SB_TOP, W, H - (int)SB_TOP, Color{ 6, 8, 20, 170 });
        DrawLineEx({ 0, SB_TOP }, { (float)W, SB_TOP }, 1, Color{ 80, 100, 150, 110 });
        for (float x : SB_SEP) DrawLineEx({ x, SB_TOP + 7 }, { x, H - 6.0f }, 1, Color{ 70, 80, 110, 110 });
    }
    void drawCrest(Vector2 c, float r, Color fill, Color line) {
        Vector2 v[8] = { { c.x - r, c.y - r * 0.7f }, { c.x - r * 0.45f, c.y - r * 0.25f }, { c.x, c.y - r * 0.9f }, { c.x + r * 0.45f, c.y - r * 0.25f },
                         { c.x + r, c.y - r * 0.7f }, { c.x + r * 0.75f, c.y + r * 0.35f }, { c.x, c.y + r }, { c.x - r * 0.75f, c.y + r * 0.35f } };
        for (int i = 0; i < 8; ++i) { Vector2 a = v[i], b = v[(i + 1) % 8]; DrawTriangle(c, a, b, fill); DrawTriangle(c, b, a, fill); }
        for (int i = 0; i < 8; ++i) DrawLineEx(v[i], v[(i + 1) % 8], 1.0f, line);
    }
    void drawCampaignBar() {
        bool won = mode == VICTORY;
        int bosses = won ? BOSS_COUNT : (stage - 1) / CYCLE;                 // bosses already defeated
        float prog = won ? 1.0f : (stage - 0.5f) / FINAL_STAGE;
        float x0 = CB_X0, x1 = CB_X1, y = CB_Y, hx = x0 + (x1 - x0) * prog;
        DrawRectangleRounded({ x0, y - 3, x1 - x0, 6 }, 1, 8, Color{ 30, 32, 46, 230 });
        DrawRectangleRounded({ x0, y - 3, std::max(6.0f, hx - x0), 6 }, 1, 8, Color{ 110, 210, 255, 255 });
        DrawRectangleRoundedLinesEx({ x0, y - 3, x1 - x0, 6 }, 1, 8, 1, Color{ 80, 85, 110, 255 });
        for (int b = 1; b <= BOSS_COUNT; ++b) {
            float bx = x0 + (x1 - x0) * b * CYCLE / FINAL_STAGE;
            bool done = b <= bosses, next = b == bosses + 1 && !won;
            float pl = next ? 0.5f + 0.5f * sinf(time * 4) : 0;
            Color fill = done ? GOLD : Color{ (unsigned char)(60 + 60 * pl), (unsigned char)(55 + 45 * pl), (unsigned char)(40 + 10 * pl), 255 };
            drawCrest({ bx, y }, 4.5f, fill, done ? Color{ 255, 230, 150, 255 } : Color{ 120, 110, 80, 255 });
        }
        float pulse = 0.6f + 0.4f * sinf(time * 5) + (bannerTimer > 0 ? 0.6f * bannerTimer / 3.0f : 0);   // flares as a stage begins
        BeginBlendMode(BLEND_ADDITIVE);
        drawGlow({ hx, y }, 9 * pulse, Color{ 60, 150, 220, 220 });
        drawGlow({ hx, y }, 3.5f, Color{ 220, 245, 255, 255 });
        EndBlendMode();
    }

    // HUD text (drawn in screen space at native resolution)
    void drawHudText() {
        if (mode == TITLE && stageDialog) { drawStageDialogText(); return; }
        if (mode == TITLE) {
            drawTitleText();
            drawRosterText();
            if (fmodf(time, 1.0f) < 0.6f) drawCentered("PRESS SPACE TO START", 584, 26, RAYWHITE);
            drawCentered("ARROWS / WASD  move    SPACE  fire    P / ESC  pause", 640, 16, GRAY);
            drawCentered("F11 / ALT+ENTER  fullscreen", 664, 16, GRAY);
            drawCentered(TextFormat("HIGH SCORE  %d", hiScore), 710, 22, GOLD);
            drawCentered("CTRL+L  debug stage select", 760, 12, Color{ 90, 90, 110, 255 });
            return;
        }

        text("1UP", 20, 10, 18, RED);
        if (droneOn && playerAlive && (mode == PLAYING || mode == VICTORY)) {   // drone timer label
            text(TextFormat("DRONE %d", (int)ceilf(droneT)), SB_WEAPON_X + 70 + 6, H - 31, 10, Color{ 170, 255, 190, 255 });
        }
        if (mode == PLAYING || mode == VICTORY || mode == GAMEOVER) {   // campaign bar labels
            text(TextFormat("STAGE %d", mode == VICTORY ? FINAL_STAGE : stage), CB_X0, H - 37, 12, RAYWHITE);
            text(TextFormat("/ %d", FINAL_STAGE), CB_X1 - 32, H - 35, 9, Color{ 150, 160, 190, 255 });
        }
        if (mode == PLAYING || mode == VICTORY || mode == GAMEOVER) {   // spare-ship count when there are more than 4
            int spare = std::max(0, lives - (playerAlive ? 1 : 0));
            if (spare > 4) text(TextFormat("x %d", spare), SB_LIVES_X + 32, H - 30, 15, RAYWHITE);
        }
        if (mode == PLAYING && playerAlive)
            textCenteredAt(shield ? "SHIELD" : shieldRegen > 0 ? TextFormat("SHIELD  %d", (int)ceilf(shieldRegen)) : "SHIELD DOWN", SB_SHIELD_CX, H - 33, 15,
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
                const char* intro = wn == NEW_TYPE_WAVE[0] ? "NEW MONSTER: COMET IMP - strikes straight down"
                                  : wn == NEW_TYPE_WAVE[1] ? "NEW MONSTER: ASTEROID BRUTE - takes 2 hits"
                                  : wn == NEW_TYPE_WAVE[2] ? "NEW MONSTER: COSMIC LEVIATHAN - bursts into two spawn" : nullptr;
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
    void loadHiScore() { std::ifstream f(HISCORE_FILE); if (f) f >> hiScore; loadSettings(); }
    void loadSettings() { std::ifstream f(SETTINGS_FILE); std::string line;
        while (f && std::getline(f, line)) if (line.rfind("title_muted=", 0) == 0) titleMuted = line.size() > 12 && line[12] == '1'; }
    void saveSettings() { std::ofstream f(SETTINGS_FILE); if (f) f << "title_muted=" << (titleMuted ? 1 : 0) << "\n"; }
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
    game.loadSfx();
    game.initStars();
    game.loadHiScore();
    game.loadFont();
    game.buildTitleParticles();
    std::vector<float> titleMusicPcm;
    std::thread musicJob([&] { titleMusicPcm = sfx::renderTitleMusic(44100); });   // synthesise while the graphics render
    gfx.load();
    musicJob.join();
    game.loadTitleAudio(titleMusicPcm);
    if (selftest) {
        int st = (argc > 3 && isdigit((unsigned char)argv[3][0])) ? atoi(argv[3]) : 1;
        if (argc > 3 && std::string(argv[3]) == "boss") st = CYCLE;
        if (argc > 3 && std::string(argv[3]) == "dialog") { game.stageDialog = true; game.dlgText = "35"; }
        else if (argc > 3 && std::string(argv[3]) == "title") { /* stay on the title screen */ }
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
        game.updateTitleAudio(dt);
        if (game.quitRequested) break;
        if (game.paused || game.escMenu || game.mode != PLAYING || !game.playerAlive) game.stopLaserLoop();   // beam hum never outlives the beam
        game.draw();
        if (selftest && getenv("SS_SEQ") && frame >= shotFrame - atoi(getenv("SS_SEQ"))) TakeScreenshot(TextFormat("seq%03d.png", frame));
        if (selftest && ++frame == shotFrame) { TakeScreenshot("selftest.png"); break; }
    }

    if (game.score >= game.hiScore) game.saveHiScore();
    if (game.customFont) UnloadFont(game.font);
    gfx.unload();
    game.unloadTitleAudio();
    game.unloadSfx();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
