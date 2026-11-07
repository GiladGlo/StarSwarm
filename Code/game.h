#pragma once
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
#include "gamemusic.h"
#include "rumble.h"
#include <thread>
#include <atomic>
#include <functional>

#include <vector>
#include <string>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <ctime>
#include <cctype>
#include <algorithm>
#include <iterator>

// Android build: touch controls (drag to move + fire, tap for prompts, on-screen pause, Back = Esc) and phone storage.
#ifdef __ANDROID__
static constexpr bool TOUCH_UI = true;
#else
static constexpr bool TOUCH_UI = false;
#endif
// Balance-testing bot (bot.h): only in builds configured with -DSTARSWARM_BOT=ON. BOT_CAUSE notes what hit the
// ship (for the bot's statistics) and compiles to nothing otherwise.
#ifdef STARSWARM_BOT
#define BOT_CAUSE(c) (botLastHit = (c))
#else
#define BOT_CAUSE(c) ((void)0)
#endif
// Pick the touch or keyboard wording for an on-screen hint
static constexpr const char* hint(const char* keys, const char* touch) { return TOUCH_UI ? touch : keys; }

static constexpr int W = 600;                   // logical playfield size (scaled to the window)
static constexpr int H = 800;
static const float  SHIELD_RADIUS = 32.0f;       // force-field bubble radius
// Player movement area: the ship (wingtips +-23, nose -25, plume below) always stays fully on screen.
static const float  PLAYER_MARGIN_X = 34.0f;      // keep the whole ship and its shield bubble inside the sides
static const float  PLAYER_Y_LOW    = H - 70.0f;    // lowest position (the original fixed row)
static const float  PLAYER_Y_HIGH   = H - 490.0f;   // how far up the ship may fly (lower ~60% of the screen)
static const size_t MAX_TORPEDOES = 5;          // projectiles allowed on screen at once
// Everything the game remembers (high score, mute settings, medals, lifetime counters) lives in one small binary
// save file in the per-user data folder (below); it is created on first run. Older saves are migrated once, then removed.
static constexpr const char* SAVE_NAME = "starswarm.sav";
static constexpr const char* LEGACY_SAVE_FILE = SAVE_NAME;     // v1.2 dev builds kept it in the current folder (next to the exe)
// Per-user data folder: %LOCALAPPDATA%\StarSwarm on Windows (with the startup cache), ~/.local/share/starswarm on Linux.
// STARSWARM_DATA_DIR overrides it (tests). Falls back to the current folder if the variables are missing.
inline std::string userDataDir() {
    if (const char* o = getenv("STARSWARM_DATA_DIR")) return o;
#ifdef _WIN32
    if (const char* b = getenv("LOCALAPPDATA")) return std::string(b) + "\\StarSwarm";
#else
    if (const char* x = getenv("XDG_DATA_HOME")) return std::string(x) + "/starswarm";
    if (const char* h = getenv("HOME")) return std::string(h) + "/.local/share/starswarm";
#endif
    return ".";
}
inline std::string savePath() {
#ifdef _WIN32
    return userDataDir() + "\\" + SAVE_NAME;
#else
    return userDataDir() + "/" + SAVE_NAME;
#endif
}
static constexpr const char* LEGACY_HISCORE_FILE  = "starswarm_highscore.txt";
static constexpr const char* LEGACY_MEDALS_FILE   = "starswarm_medals.txt";
static constexpr const char* LEGACY_SETTINGS_FILE = "starswarm_settings.txt";

// ----------------------------------------------------------------------------
//  Utility
// ----------------------------------------------------------------------------
inline float frand(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 10000) / 10000.0f; }

inline Vector2 bezier(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
    float u = 1 - t;
    return { u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
             u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y };
}

// Build a polyline from a chain of cubic Bezier control points (3n+1 points).
inline std::vector<Vector2> buildPath(const std::vector<Vector2>& ctrl) {
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
    // (filled in by the startup loader: generated on first run, then from the cache)
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
inline Gfx gfx;

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
inline const Texture2D& planetTex(int i) {
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
inline void drawShip(const Texture2D& t, Vector2 pos, float size, float angleDeg, bool shadow = true, Color tint = WHITE);
// Animated monster: cross-fades the two nearest of its looping frames (ph = 0..1), shadow drawn once.
inline void drawMonster(const Texture2D* frames, int n, float ph, Vector2 pos, float size, float angleDeg, Color tint = WHITE) {
    float x = (ph - floorf(ph)) * n; int i0 = (int)x % n, i1 = (i0 + 1) % n; float a = x - floorf(x);
    drawShip(frames[i0], pos, size, angleDeg, true, tint);
    if (a > 0.02f) drawShip(frames[i1], pos, size, angleDeg, false, Color{ tint.r, tint.g, tint.b, (unsigned char)(tint.a * a) });
}
inline void drawShip(const Texture2D& t, Vector2 pos, float size, float angleDeg, bool shadow, Color tint) {
    Rectangle src{ 0, 0, (float)t.width, (float)t.height };
    if (shadow)
        DrawTexturePro(t, src, { pos.x + size * 0.12f, pos.y + size * 0.18f, size * 0.95f, size * 0.95f },
                       { size * 0.475f, size * 0.475f }, angleDeg, Color{ 0, 0, 0, 110 });
    DrawTexturePro(t, src, { pos.x, pos.y, size, size }, { size / 2, size / 2 }, angleDeg, tint);
}

inline void drawGlow(Vector2 pos, float radius, Color c);
// ---- Player weapon classes (picked up from Courier packages) ----------------
enum WClass { W_PHOTON, W_SPREAD, W_PULSE, W_HOMING, W_ION, W_COUNT };
static const char* WNAME[W_COUNT] = { "PHOTON TORPEDO", "SPREAD CANNON", "PULSE LASER", "HOMING SWARM", "ION BURST" };
static const Color WCOL[W_COUNT] = { { 255, 110, 50, 255 }, { 255, 160, 40, 255 }, { 70, 220, 255, 255 }, { 110, 255, 120, 255 }, { 190, 110, 255, 255 } };
static const Color UPG_COL = { 255, 215, 60, 255 };
// Weapon icons for packages and the HUD indicator. k: 0 photon 1 spread 2 pulse 3 homing 4 ion 5 upgrade
inline void weaponIcon(int k, Vector2 c, float s, Color col) {
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
inline void drawPackage(int content, Vector2 c, float s, float t) {
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
inline void drawGlow(Vector2 pos, float radius, Color c) {
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

// Sound from synthesised samples (from the startup cache or freshly rendered by sfxsynth.h)
inline Sound makeSound(SfxKind k, std::vector<float> v) {
    const int rate = 44100;
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
inline StageKind stageKind(int s) { int m = s % CYCLE; return m == 0 ? SK_BOSS : (m == CYCLE - 1 ? SK_METEORS : SK_WAVE); }
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
    Vector2 turret(int i) const { const auto& tu = D().turrets; return W2({ tu[i % tu.size()].x, tu[i % tu.size()].y + 0.2f }); }
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
#ifdef STARSWARM_BOT
    bool  botOn = false;                        // the autopilot is flying (bot.h)
    float botMv = 0, botMvy = 0;                // its stick input this frame (-1..1)
    int   botLastHit = 0;                       // what last hit the ship (bot::Cause)
    int   botLastKind = 0;                      // and, for a shot, its kind (BKind)
#endif
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
    void loadSfx(const std::vector<std::vector<float>>& pcm);
    void unloadSfx() { for (int i = 0; i < SFX_COUNT; ++i) { for (auto& a : sfxVoice[i]) if (a.stream.buffer) UnloadSoundAlias(a); UnloadSound(sfx[i]); } }
    void playSfx(SfxKind k);   // round-robin over the voices of this sound
    void stopLaserLoop() { if (IsSoundPlaying(sfx[SFX_LASER_LOOP])) StopSound(sfx[SFX_LASER_LOOP]); }

    // ---- Title screen audio: looping dark-ambient music (titlemusic.h) + swarm / launch effects ----
    Music titleMusic{}; bool titleMusicOk = false;
    std::vector<unsigned char> titleMusicWav;          // in-memory WAV the music stream decodes from (must stay alive)
    Sound titleFx[sfx::TFX_COUNT]{};
    float musicVol = 0;                                 // 0..1 fade level (music volume = gain * musicVol)
    float titleFxPrevT = -1;                            // titleT on the previous frame (to catch swarm-cycle events)
    static std::vector<unsigned char> makeWav(const std::vector<float>& v, int rate);
    void loadTitleAudio(const std::vector<float>& music, const std::vector<std::vector<float>>& fx);
    // ---- In-game background music (gamemusic.h): one score per stage, rotation restarts after every boss ----
    Music gameMusic[sfx::GT_COUNT]{}; bool gameMusicOk[sfx::GT_COUNT]{};
    std::vector<unsigned char> gameMusicWav[sfx::GT_COUNT];
    float gameMusicVol[sfx::GT_COUNT]{};               // crossfade level per score (0..1)
    bool  gameMusicPaused = false;
    void loadGameMusic(std::vector<std::vector<float>>& pcm);
    void unloadGameMusic() { for (int k = 0; k < sfx::GT_COUNT; ++k) if (gameMusicOk[k]) { StopMusicStream(gameMusic[k]); UnloadMusicStream(gameMusic[k]); } }
    // Which score should be playing now (-1 = none): stage's slot in the boss cycle while playing;
    // silence on the title / game over / victory screens and once the boss starts dying (fades out into the jump).
    int wantedGameTrack() const;
    void updateGameMusic(float dt);

    void unloadTitleAudio();
    // Called every frame: music fades in on the title screen (restarting with the title cycle) and fades out
    // over ~1 s once a game starts; swarm effects fire at the cycle's stream-in, lock-in and peel-off moments.
    void updateTitleAudio(float dt);

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
    Vector2 slotPos(const Enemy& e) const;
    std::string formationName;
    float difficulty() const { return 1.0f + std::min(1.2f, 0.12f * (stage - 1)); }

    // --- setup --------------------------------------------------------------
    void initStars();

    void newGame(int startStage = 1);

    // Next weapon class for a Courier to carry: drawn from a shuffled bag of the four classes,
    // skipping the one you hold; the bag refills (reshuffled) once it runs out.
    // Also skips the class offered last time, so the first pick of a fresh bag can't repeat the previous pick.
    int nextBagWeapon();

    // What a Courier / mining outpost carries: 50/50 new weapon vs upgrade while on the un-upgraded basic weapon,
    // 80% upgrades otherwise; new weapons come from the shuffle bag.
    int rollCargo();

    void raiseShield();

    void nextStage();

    static constexpr int ENTRY_KINDS = 6;
    std::vector<Vector2> entryPath(int kind, bool mirror);


    // ========================================================= METEOR FIELD ==
    int cycleIndex() const { return std::clamp((stage - 1) / CYCLE, 0, BOSS_COUNT - 1); }

    void startMeteorField();

    float density() const;   // fraction of the playfield covered by rock

    void spawnMeteor();

    // Mining outpost: drifts with the field at ~70% of the average rock speed (rocks spawn at 35-105 px/s by size)
    void spawnOutpost(float x, float y);
    void addMeteor(Meteor m) { m.id = nextMeteorId++; meteors.push_back(m); }

    // Any hit on a rock: volatile rocks detonate, ordinary rocks split.
    // A mining outpost soaks up hits first; only the last one blows the station off its rock.
    void hitMeteor(size_t idx, Vector2 hitDir);

    static constexpr int OUTPOST_HP_BASE = 6;             // 6 hits early, up to 12 in later cycles
    float outpostHealth(const Meteor& m) const { return m.hpMax > 0 ? (float)m.hp / m.hpMax : 1.0f; }
    Vector2 outpostLocal(const Meteor& m, Vector2 u) const;   // unit coords on the station -> world
    static constexpr Vector2 OUTPOST_GUN{ 0.36f, 0.42f };
    // Non-lethal outpost hit: flash, sparks and metal chips at the impact point; a bigger burst at each damage stage
    void damageOutpost(Meteor& m, Vector2 hitDir);
    // Point-defence turret + damage smoke/sparks, run each frame for every outpost rock
    void updateOutpost(Meteor& m, float dt);

    // Mining outpost blown off its rock: explosion, metal debris, 500 points and a package (Courier odds).
    // The rock itself then breaks up like any large asteroid.
    void destroyOutpost(Meteor& m);

    // Volatile rock explodes: destroyed outright (no fragments), and every rock within
    // BLAST_RADIUS takes a hit exactly like a torpedo; other volatile rocks chain-react.
    void detonateMeteor(size_t idx);

    // Torpedo (or shield) hit: split into two smaller rocks, or crumble if already small
    void breakMeteor(size_t idx, Vector2 hitDir);

    void updateMeteors(float dt);

    // Outpost rock: stabilised (fixed rock frame, slow 2D turn) with the station overlay, spinning radar dish,
    // blinking beacons, glowing drill and ore sparks streaming into the borehole.
    void drawOutpost(const Meteor& m, float S);

    void drawMeteors();

    // ============================================================= BOSSES ==
    void startBoss();

    void bossShoot(Vector2 from, float angleDeg, float speed, int kind = 1);

    // Muzzle flash: bright pop + small shock ring at a gun when it fires
    void muzzleFlash(Vector2 at, Color c, float size, bool ring = true);
    Color coreColor(unsigned char a = 255) const { return bossColor(boss.D().core, a); }

    // Scythe Beam: the bottom of the beam trails behind the boss's motion
    float beamLean(Vector2 o) const;
    float beamXAt(Vector2 o, float y) const { return o.x + tanf(beamLean(o)) * (y - o.y); }

    void spawnMinion(Vector2 at, EType type);

    bool laserFiring() const { return boss.active && boss.step == 1 && (boss.attack == A_LASER || boss.attack == A_TWINLASER || boss.attack == A_SWEEP); }

    void updateBoss(float dt);

    void damageBoss(Vector2 at, float amount = 1.0f);   // amount: spread pellet 0.5, homing 0.75, laser tick 2, ion direct hit 3

    Color bossColor(Vector3 c, unsigned char a = 255) const;
    Color spiralColor(float aux) const;

    // Heavy beam: charge-up targeting line, then a wavering, energised beam with an impact splash
    void drawBeam(Vector2 m, bool firing, float charge);

    void drawBoss();

    // The player's ship with its live effects: warp-in, engine plumes with shock diamonds, banked sprite,
    // navigation strobes, canopy glint and gun-muzzle flashes.
    void drawPlayerShip();

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

    void startHyper();
    void updateHyper(float dt);
    void drawHyper();

    // Last stand: in a wave, a lone survivor (or Couriers left on their own: they never dive otherwise, and sit out
    // of reach of the short-range Spread Cannon) stops looping and attacks the player head-on until it's shot down.
    bool lastStand() const;
    // Head-on attack dive: a short wind-up, then straight at where the player is, out past the bottom.
    void startStrike(Enemy& e);

    void startDive(Enemy& e);

    // --- effects ------------------------------------------------------------
    void explode(Vector2 p, Color a, Color b, int n, float power);

    // Small photon-torpedo detonation at the point of impact
    void torpedoImpact(Vector2 p);

    void addScore(int pts);

    // Force field shatters: shards, flash, shockwave; brief grace period so one volley can't chain-kill
    void breakShield(Vector2 at);

    void killPlayer();


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

    void startFromDialog();

    void updateStageDialog();

    void drawButton(Rectangle r, bool hot, Color base);

    void drawStageDialogShapes();

    void drawStageDialogText();

    // --- ESC pause menu -------------------------------------------------------
    static constexpr int ESC_N = 3;
    Rectangle escBox() const { return { 110, 255, W - 220.0f, 300 }; }
    Rectangle escBtn(int i) const { return { 150, 330 + i * 64.0f, W - 300.0f, 50 }; }
    void openEscMenu() { escMenu = true; escSel = 0; paused = false; touchDrag = false; while (GetCharPressed()) {} }
    // Back to the title screen with a clean playfield: nothing from the last game (enemies, rocks, boss,
    // shots, explosions, packages, popups, hyperspace, drone, laser hum) is left on screen.
    void returnToTitle();

    void escChoose(int i);
    void updateEscMenu();
    void drawEscMenuShapes();
    void drawEscMenuText();

    // --- update -------------------------------------------------------------
    void update(float dt);

    void updatePlayer(float dt);

    void followPath(Enemy& e, float dist);

    void updateEnemies(float dt);

    static constexpr float SPREAD_LIFE = 0.70f;   // Spread Cannon pellet range (620 px/s -> ~430 px): fly up to reach the top rows / boss
    void updateBullets(float dt);

    void updateParticles(float dt);


    // ============================================================ ARSENAL ==
    float wCooldown() const;
    int countPB(int kind) const { int n = 0; for (auto& b : pBullets) if (b.kind == kind) ++n; return n; }

    // Fire one volley of the current (non-laser) weapon; each class has its own on-screen limit
    // ======================================================== WINGMAN DRONE ==
    void activateDrone();
    void destroyDrone();
    // The drone flies on an arc around the ship (left -> over the top -> right, never below or on it, 42..90 units out).
    // Its preferred spot drifts along the left/top arc; when enemy shots threaten it, it moves to the safest spot.
    // Motion is acceleration-limited with a top speed 75% above the ship's. One enemy hit destroys it.
    void updateDrone(float dt);
    void drawDrone();

    static constexpr float WARP_TIME = 0.8f;
    // Banking, side-thruster puffs, exhaust particles and timers for the player's ship.
    // Velocity comes from the position change, so it works however the ship was moved.
    void updateShipFx(float dt);

    void firePlayerWeapon();

    // Damage one enemy by one hit. Returns true if it was destroyed.
    bool hitEnemy(Enemy& e, Vector2 at);

    // Courier destroyed: the hull bursts into dozens of pieces that leave the screen in about a second
    void courierBurst(Vector2 at);

    // The Courier shoots the basic form of what it carries (or of the player's weapon, if it carries an upgrade)
    void courierFire(const Enemy& e);

    // Ion Burst detonation: damages everything nearby; Lv3 chains lightning to up to 3 more enemies
    void ionBlast(Vector2 at, bool bossDirect);

    // Pulse Laser: continuous beam, damage in ticks; stops at the first thing it hits (Lv3 pierces everything)
    void updateLaser(float dt);

    void collectPackage(const Package& pk);

    void updatePackages(float dt);

    // Weapon status: compact indicator in the bottom HUD strip, after the spare-ship icons (never over the player)
    void drawWeaponIndicator();

    // Jagged lightning segment (Ion Burst chain)
    void drawZap(Vector2 a, Vector2 b, unsigned seed, Color c, float w, unsigned char alpha);

    std::vector<Vector2> pendingSplits;
    int enemyPoints(EType t, bool diving) const;
    Color enemyColor(EType t) const;
    // A destroyed splitter bursts into two small fast minis that peel away and dive
    void releaseSplits();

    // Physical enemy projectiles that player weapons can destroy; energy shots are unaffected.
    static bool shootable(int kind) { return kind == BK_MISSILE || kind == BK_C_HOMING || kind == BK_METEOR || kind == BK_FLAK; }
    static float shootableR(int kind) { return kind == BK_METEOR ? 11.f : kind == BK_FLAK ? 9.f : 7.f; }
    void destroyProjectile(size_t i);

    void collisions();


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

    static Vector2 bez3(Vector2 a, Vector2 b, Vector2 c, Vector2 d, float t);
    // Rasterise the lines and place one monster on every grid point inside the letters.
    std::vector<SwarmMon> buildSwarm(std::vector<std::pair<const char*, float>> lines, float yTop);
    void buildTitleParticles();
    static float swEase(float a, float b, float x) { float t = Clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    void drawTitleParticles();   // the swarm itself (logical coordinates, inside the camera)
    void drawTitleText();   // the small "DEVELOPED BY" / "DEDICATED TO" labels (screen space)

    // --- view / scaling -------------------------------------------------------
    // The game logic runs in a fixed 600x800 "logical" playfield. Everything is
    // drawn through a camera zoomed to fit the real window, so graphics are
    // rendered at the window's native resolution (no blurry upscaling).
    Font  font{};
    bool  customFont = false;
    float scale = 1;
    Vector2 origin{ 0, 0 };

    // A system font file. On Android raylib's file layer only reads the APK's assets and the app's own folder,
    // so /system/fonts files are read here and handed over from memory.
    static Font loadFontFile(const char* f, int size);
    void loadFont();

    // Loading screen: starfield, logo, status line and progress bar (drawn every frame while assets build)
    void drawLoading(float p, const char* status, bool firstRun, float dt);
    float loadT = 0;

    // Tall screens (phones): the 600x800 playfield keeps its size and the spare space above and below it (the
    // "bands") shows the background, with the top HUD moved up into the top band and the status bar down into the
    // bottom band, so the playfield is clear of both. A 3:4 window has no bands and looks exactly as before.
    static constexpr float TOP_HUD_H = 96;            // score / high score / buttons / boss bar block (y 0..~90)
    static constexpr float BOTTOM_HUD_H = 46;         // status bar (42) + a small gap
    float bandTop = 0, bandBot = 0;                   // spare space above / below the playfield (logical units)
    float hudTopDy = 0, hudBotDy = 0;                 // how far the top HUD moves up (<= 0) and the status bar down
    Camera2D viewCam{};                               // the playfield camera of the current frame (with shake)
    float hudDy = 0;                                  // vertical offset applied by text() while drawing a band HUD

    void computeView();
    Rectangle hudTopRect(Rectangle r) const { r.y += hudTopDy; return r; }   // a top-HUD button where it is drawn
    void beginPlayfieldClip() { BeginScissorMode((int)origin.x, (int)origin.y, (int)ceilf(W * scale), (int)ceilf(H * scale)); }
    void beginScreenClip() { BeginScissorMode(0, 0, GetScreenWidth(), GetScreenHeight()); }
    // Draw shapes (inside the playfield camera) or text (outside it) moved by dy into a band, unclipped.
    template <class F> void bandShapes(float dy, F f, bool unclipped = false) {
        if (dy == 0 && !unclipped) { f(); return; }
        EndMode2D(); EndScissorMode(); beginScreenClip();
        Camera2D c = viewCam; c.offset.y += dy * scale;
        BeginMode2D(c); f(); EndMode2D();
        EndScissorMode(); beginPlayfieldClip(); BeginMode2D(viewCam);
    }
    template <class F> void bandText(float dy, F f) {
        if (dy == 0) { f(); return; }
        EndScissorMode(); beginScreenClip();
        hudDy = dy; f(); hudDy = 0;
        EndScissorMode(); beginPlayfieldClip();
    }

    // Text drawn in screen space at the real pixel size, so it stays sharp at any scale.
    void text(const char* t, float x, float y, float size, Color c, bool centered = false, float dx = 0);
    void drawCentered(const char* t, float y, float size, Color c) { text(t, 0, y, size, c, true); }

    // The background continued into the bands above and below the playfield (tall screens): the sky's end colours,
    // the same scrolling nebula (a tileable texture) and the starfield repeated one playfield-height up and down
    // (stars wrap at H), so everything lines up seamlessly with the playfield drawn on top of it.
    void drawBandBackground(const Theme& th);

    // --- draw ---------------------------------------------------------------
    void draw();

    // Shimmering hexagonal force field around the player
    void drawShield();

    // ======================================================= MEDALS / SECTOR STATS / GAMEPAD ==
    enum MedalId { MD_UNTOUCHABLE, MD_FLAWLESS, MD_PURIST, MD_RAIDER, MD_CHAIN, MD_SHARP, MD_COURIER, MD_MAXPOWER,
                   MD_ARSENAL, MD_WINGMAN, MD_FLAK, MD_BLITZ, MD_SECTOR2, MD_HALFWAY, MD_NOCONT, MD_SAVIOR, MD_COUNT };
    struct MedalDef { const char* name; const char* desc; Color col; int glyph; };
    static const MedalDef* medalDefs();
    bool medal[MD_COUNT]{};
    int  courierKillsTotal = 0, flakShotTotal = 0;      // lifetime counters (saved with the medals)
    std::vector<int> medalToasts;                       // unlocked medals waiting to be announced
    float toastT = 0;
    std::vector<int> sectorMedals;                      // medals unlocked during the current sector
    bool medalsOpen = false; int medalSel = 0;          // title-screen medals screen
    static constexpr Rectangle MEDAL_BTN{ 60, 754, 34, 34 };
    static constexpr Rectangle MEDAL_PANEL{ 30, 90, 540, 620 };

    void saveMedals() { writeSave(); }
    int medalCount() const { int n = 0; for (bool m : medal) n += m; return n; }
    void unlockMedal(int id);

    // ---- per-sector / per-stage tracking for medals and the SECTOR CLEARED panel ----
    float sectorT = 0, bossFightT = 0;
    int   sectorShots0 = 0, sectorHits0 = 0, stageShots0 = 0, stageHits0 = 0;
    int   shipsLostSector = 0, shipsLostGame = 0, outpostsSector = 0, outpostsField = 0, weaponsUsed = 1;
    int   chainCount = 0; float chainT = 0;             // rocks caught by one volatile chain reaction
    bool  fieldHit = false;                             // took a hit during this meteor field
    void noteHitTaken() { if (stageKind(stage) == SK_METEORS) fieldHit = true; }
    void noteWeapon() { weaponsUsed |= 1 << weapon; if (weaponsUsed == (1 << W_COUNT) - 1) unlockMedal(MD_ARSENAL); }

    // ---- boss kill: slow motion + flash, then the SECTOR CLEARED panel before the jump ----
    float slowT = 0, flashT = 0;
    static constexpr float SLOW_TIME = 1.6f, FLASH_TIME = 0.35f, PANEL_TIME = 6.0f;
    float timeScale() const { if (slowT <= 0) return 1.0f; float u = 1 - slowT / SLOW_TIME; return 0.2f + 0.8f * u * u; }
    bool  sectorPanel = false; float sectorPanelT = 0;
    int   panelSector = 1, panelAcc = 0, panelShips = 0, panelOutposts = 0, panelBonus = 0; float panelTime = 0, panelBossT = 0;
    std::string panelBoss;
    void bossDefeated(const Boss& b);
    void openSectorPanel();

    // ---- gamepad (first controller) ----
    float rumbleT = 0;
    bool padOn() const { return IsGamepadAvailable(0); }
    bool padPressed(int b) const { return padOn() && IsGamepadButtonPressed(0, b); }
    bool padDown(int b) const { return padOn() && IsGamepadButtonDown(0, b); }
    bool padConfirm() const { return padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || padPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT); }
    void rumbleFor(float strength, float secs);
    // real-time bookkeeping (not slowed by the boss-kill slow motion)
    void tickRealTime(float dt);

    // ---- title-screen medals screen ----
    Vector2 medalCell(int i) const { return { MEDAL_PANEL.x + 70 + (i % 4) * 133.0f, MEDAL_PANEL.y + 118 + (i / 4) * 120.0f }; }
    void updateMedalsScreen();

    // ---- drawing ----
    void drawMedalGlyph(int g, Vector2 c, float s, Color col);
    // Gold medal on a two-tone ribbon (locked: grey coin; the "?" is drawn with the text)
    void drawMedal(int id, Vector2 c, float r, bool unlocked, float glowA = 0);
    void drawMedalButton();
    float toastY() const { float in = std::min(1.0f, toastT / 0.35f), out = std::min(1.0f, std::max(0.0f, (3.2f - toastT) / 0.35f));
                           float u = std::min(in, out); return 96 - 110 * (1 - u) * (1 - u); }
    // Overlays drawn last, on top of everything: boss-kill flash, medal toast, SECTOR CLEARED panel, medals screen
    void drawOverlays();
    // "PRESS SPACE TO START", with an A button when a controller is connected
    void drawStartPrompt();

    // ---- Title-screen enemy roster: 4x2 grid (7 monsters + rotating BOSS) and a featured Courier row ----
    struct RosterEntry { int mon; const char* name; const char* pts; Color col; };
    static const RosterEntry* roster();
    static constexpr float ROSTER_Y = 336, ROSTER_DY = 46, ROSTER_ICON = 40, ROSTER_BOSS_PER = 1.6f;
    static constexpr Rectangle ROSTER_FEAT{ 30, 506, 540, 62 };
    static Vector2 rosterCell(int k) { return { 62.0f + (k % 2) * 285, ROSTER_Y + (k / 2) * ROSTER_DY }; }   // icon centre
    static Color rosterBossCol(int b) { const Vector3 c = spr::bossDesigns()[b].core;
        return Color{ (unsigned char)(c.x * 255), (unsigned char)(c.y * 255), (unsigned char)(c.z * 255), 255 }; }
    // which boss is showing, the next one, and the cross-fade amount between them
    void rosterBossState(int& bi, int& bn, float& xf) const;
    int rosterDropLit() const { return (int)(time * 2.5f) % 5; }
    Color rosterPodCol() const { int i = (int)fmodf(time * 0.8f, 6.0f); return i == 0 ? UPG_COL : WCOL[i < 5 ? i : 4]; }

    // ---- Title-screen mute button (bottom-left, icon only): click or M toggles title music + effects ----
    bool titleMuted = false;                            // saved in the save file; gameplay sound is never affected
    static constexpr Rectangle MUTE_BTN{ 18, 754, 34, 34 };
    void toggleTitleMute();
    // Speaker icon button: waves when sound is on, grey speaker + red X when muted (scales with the rect)
    void drawSpeakerButton(Rectangle r, bool muted, bool hover);
    void drawMuteButtonShapes() { drawSpeakerButton(MUTE_BTN, titleMuted, CheckCollisionPointRec(mouseLogical(), MUTE_BTN)); }

    // ---- In-game music mute button (top-right corner, icon only): click or M; mutes only the background music ----
    bool gameMusicMuted = false;                       // saved in the save file
    static constexpr Rectangle GAME_MUTE_BTN{ W - 36.0f, 8, 28, 28 };
    bool gameMuteVisible() const { return mode == PLAYING; }
    void toggleGameMusicMute() { gameMusicMuted = !gameMusicMuted; saveSettings(); }
    void drawGameMuteButton() { drawSpeakerButton(GAME_MUTE_BTN, gameMusicMuted, CheckCollisionPointRec(mouseLogical(), hudTopRect(GAME_MUTE_BTN))); }

    // ---- Touch controls (Android) ----
    static constexpr Rectangle GAME_PAUSE_BTN{ W - 72.0f, 8, 28, 28 };   // on-screen pause, left of the music button
    static constexpr Rectangle GAME_PAUSE_HIT{ W - 82.0f, 0, 42, 46 };   // finger-sized hit areas around the small icons
    static constexpr Rectangle GAME_MUTE_HIT{ W - 40.0f, 0, 40, 46 };
    static constexpr float TOUCH_SENS = 1.25f;                           // ship moves a bit further than the finger
    bool touchDrag = false;                                              // a finger is steering (and firing)
    Vector2 touchLast{ 0, 0 };
    static Rectangle grow(Rectangle r, float d) { return { r.x - d, r.y - d, r.width + 2 * d, r.height + 2 * d }; }
    bool onHudButton(Vector2 p) const;
    // a tap that isn't on one of the small HUD buttons ("TAP TO START" / "TAP TO CONTINUE")
    bool touchTap() const { return TOUCH_UI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !onHudButton(mouseLogical()); }
    void drawGamePauseButton();

    void drawRosterShapes();
    void drawRosterText();

    // HUD graphics (drawn inside the camera, in logical coordinates)
    void drawHudShapes();
    // bottom status bar: panel, spare ships, weapon, campaign bar, shield recharge
    void drawStatusBarShapes();
    // top HUD: music / pause buttons, boss health bar, meteor-field progress
    void drawTopHudShapes();

    // Campaign bar (bottom right): progress through all 60 stages, one crest per boss (gold once defeated).
    // ---- bottom status bar: fixed zones so indicators never overlap
    //  [ spare ships | weapon + drone | shield | stage / campaign bar ]
    static constexpr float SB_TOP = H - 42.0f;
    static constexpr float SB_LIVES_X = 8, SB_WEAPON_X = 134, SB_SHIELD_CX = 337, SB_SEP[3] = { 127, 266, 410 };
    static constexpr float CB_X0 = 420, CB_X1 = 590, CB_Y = H - 14.0f;
    void textCenteredAt(const char* t, float cx, float y, float size, Color c);
    void drawStatusPanel();
    void drawCrest(Vector2 c, float r, Color fill, Color line);
    void drawCampaignBar();

    // HUD text (drawn in screen space at native resolution)
    void drawHudText();
    // top HUD text: score, high score, boss name, meteor-field label, debug info
    void drawTopHudText();
    // status bar text: drone timer, stage, spare-ship count, shield
    void drawStatusBarText();
    // VICTORY / GAME OVER texts (playfield centre)
    void drawEndScreenText();

    // --- persistence: one binary save file -------------------------------------
    // Layout (32 bytes, little-endian): "SSAV" | u16 version (1) | u16 size (32) | i32 high score | u32 medal bits |
    // i32 lifetime Courier kills | i32 lifetime flak shot down | u8 flags (bit0 title muted, bit1 game music muted) |
    // 3 reserved | u32 CRC-32 of the first 28 bytes. A missing, damaged or tampered file falls back to defaults.
    static constexpr int SAVE_SIZE = 32, SAVE_VERSION = 1;
    static uint32_t crc32(const unsigned char* p, size_t n);
    static void put32(unsigned char* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (8 * i)); }
    static uint32_t get32(const unsigned char* p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i); return v; }
    void writeSave();
    bool readSave(const std::string& path);
    // Read the old per-topic text files (pre-1.2 versions). Returns true if any existed.
    bool readLegacyFiles();
    // Loads the whole save from the user data folder; creates it if it's missing or invalid, migrating (once) a
    // save file left next to the exe by an earlier version, or the even older per-topic text files.
    void loadHiScore();
    void saveSettings() { writeSave(); }
    void saveHiScore() { if (debugRun) return; writeSave(); }
};
