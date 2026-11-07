// bot.h - autoplay bot for balance testing (only in builds configured with -DSTARSWARM_BOT=ON; never in releases).
//
//   starswarm --bot [runs=20] [seed=1] [startStage=1]
//
// Plays whole games at full simulation speed (fixed 1/60 s steps, uncapped frame rate, no audio, draws only every
// 30th frame) and writes three files to the current folder:
//   bot_stages.csv   one row per stage played: duration, "tail" (time from 2 enemies left to wave clear),
//                    time a remaining enemy spent level with or below the ship, deaths and shield hits by cause
//   bot_runs.csv     one row per game: stage reached, score, simulated minutes, how it ended
//   bot_summary.txt  averages and distributions over all runs
// The autopilot sees the game state directly: each frame it predicts every hazard (enemy shots, rocks, diving
// enemies, boss beams) for the next 0.9 s along the ship's reachable path, then picks the horizontal position with
// the best mix of safety and a lined-up shot (leading moving targets), and catches weapon packages. It stays on the
// ship's normal bottom row and keeps the trigger held, like a player with a thumb on the screen.
#pragma once
#ifdef STARSWARM_BOT
#include <cstdio>
#include <algorithm>
#include <vector>

namespace bot {

enum Cause { C_NONE, C_BULLET, C_ROCK, C_DIVER, C_BEAM, C_COUNT };
static const char* CAUSE_NAME[C_COUNT] = { "?", "shot", "rock", "diver", "beam" };

struct StageRec {
    int run = 0, stage = 0, kind = 0;
    float dur = 0, tail = -1, tailBelow = 0;
    float tailCourierSpread = 0;   // tail time with only formation Couriers left while holding the Spread Cannon
    int deaths = 0, shieldHits = 0, deathBy[C_COUNT]{}, shieldBy[C_COUNT]{};
    bool cleared = false;
};
struct RunRec { int run = 0, seed = 0, stageReached = 0, score = 0; float simMin = 0; const char* end = ""; };

// ---- autopilot: sets g.botMv / g.botMvy for this frame ----
inline void think(Game& g, float dt) {
    g.botMv = 0; g.botMvy = 0;
    if (g.mode != PLAYING || !g.playerAlive || g.hyperT >= 0 || g.sectorPanel) return;
    const float SPEED = 340, Y = g.player.y, pr = g.shield ? SHIELD_RADIUS - 4 : 13;
    // Hold the bottom row, except with the short-range Spread Cannon (pellets burn out after ~430 px): when no enemy
    // is within reach, climb until the lowest one is, like a player who knows the trick; drop back down otherwise.
    float wantY = PLAYER_Y_LOW;
    if (g.weapon == W_SPREAD) {
        float lowest = -1e9f; bool inRange = false;
        for (auto& e : g.enemies) {
            if (e.state == DEAD || e.state == WAITING || e.pos.y < 0) continue;
            lowest = std::max(lowest, e.pos.y);
            if (Y - e.pos.y < 380) inRange = true;
        }
        if (!inRange && lowest > 0) wantY = std::max(PLAYER_Y_HIGH, lowest + 330);
    }
    g.botMvy = std::clamp((wantY - Y) / (300 * dt), -1.0f, 1.0f);

    struct Hz { Vector2 p, v; float r; };
    std::vector<Hz> hz;
    for (auto& b : g.eBullets) {
        float r = b.kind == BK_ENEMY ? 3.0f : b.kind == BK_METEOR ? 8.0f : b.kind == BK_BOLT ? 4.0f : 5.5f;
        hz.push_back({ b.pos, b.vel, r });
    }
    for (auto& m : g.meteors) hz.push_back({ m.pos, m.vel, m.r() * 0.82f });
    for (auto& e : g.enemies)
        if (e.state == DIVING || (e.state == ENTERING && e.pos.y > Y - 200))
            hz.push_back({ e.pos, Vector2Scale(Vector2Subtract(e.pos, e.prev), 1.0f / std::max(dt, 1e-4f)), 14 });
    std::vector<float> beamX;                                           // boss beams charging or firing
    if (g.boss.active && (g.boss.attack == A_LASER || g.boss.attack == A_TWINLASER || g.boss.attack == A_SWEEP))
        for (Vector2 o : g.boss.beams()) beamX.push_back(g.beamXAt(o, Y));

    struct Tg { float x, w; };                                          // aim points (x at shot arrival), weight
    std::vector<Tg> tg;
    auto lead = [&](Vector2 p, Vector2 v) { float t = std::max(0.0f, (Y - p.y) / 640.0f); return p.x + v.x * t; };
    for (auto& e : g.enemies) {
        if (e.state == DEAD || e.state == WAITING || e.pos.y > Y - 30 || e.pos.y < -20) continue;
        Vector2 v = Vector2Scale(Vector2Subtract(e.pos, e.prev), 1.0f / std::max(dt, 1e-4f));
        float w = 1.0f + e.pos.y / 400.0f + (e.state == DIVING ? 1.0f : 0) + (e.type == COURIER ? 1.5f : 0);
        tg.push_back({ lead(e.pos, v), w });
    }
    if (g.boss.active && g.boss.state == B_FIGHT) tg.push_back({ g.boss.core().x, 2.0f });
    for (auto& m : g.meteors) if (m.pos.y < Y - 60 && m.pos.y > 0) tg.push_back({ lead(m.pos, m.vel), m.outpost ? 1.5f : 0.6f });
    for (auto& p : g.packages) if (p.pos.y < Y + 10) tg.push_back({ p.pos.x, 4.0f });   // catch weapon packages

    float bestX = g.player.x, bestCost = 1e30f;
    for (float cx = PLAYER_MARGIN_X; cx <= W - PLAYER_MARGIN_X; cx += 6) {
        float cost = 0;
        for (int k = 0; k <= 18; ++k) {
            float t = k * 0.05f, w = 1.0f / (1.0f + 3.0f * t);
            float dx = cx - g.player.x, sx = g.player.x + std::clamp(dx, -SPEED * t, SPEED * t);
            for (auto& h : hz) {
                Vector2 q = Vector2Add(h.p, Vector2Scale(h.v, t));
                float d = Vector2Distance({ sx, Y }, q) - h.r - pr;
                cost += (d < 0 ? 1000.0f : 30.0f * expf(-d / 12.0f)) * w;
            }
            for (float bx : beamX) if (fabsf(sx - bx) < 13 + pr * 0.7f + 18) cost += 1000.0f * w;
        }
        float aim = 0;
        for (auto& t : tg) { float u = (cx - t.x) / 16.0f; aim = std::max(aim, t.w * expf(-u * u)); }
        cost -= 40.0f * aim;
        cost += 0.02f * fabsf(cx - g.player.x) + 0.01f * fabsf(cx - W / 2.0f);
        if (cost < bestCost) { bestCost = cost; bestX = cx; }
    }
    g.botMv = std::clamp((bestX - g.player.x) / (SPEED * dt), -1.0f, 1.0f);
}

// A wave still unfinished 60 s after only 2 enemies were left: append the survivors' state to bot_stuck.txt
// and save a screenshot of the next frame (bot_stuck_<run>_<stage>.png).
inline void dumpStuck(const Game& g, int run) {
    static const char* ST[] = { "WAITING", "ENTERING", "HOMING", "FORMATION", "DIVING", "DEAD" };
    static const char* TY[] = { "BEE", "BUTTERFLY", "BOSS", "DART", "SHIELDED", "SPLITTER", "MINI", "COURIER" };
    if (FILE* f = fopen("bot_stuck.txt", "a")) {
        fprintf(f, "run %d stage %d: player (%.0f, %.0f), %s\n", run, g.stage, g.player.x, g.player.y, g.playerAlive ? "alive" : "respawning");
        for (auto& e : g.enemies) if (e.state != DEAD)
            fprintf(f, "  %-9s %-9s pos (%.1f, %.1f) prev (%.1f, %.1f) hp %d/%d slot (%.1f, %.1f) path seg %zu/%zu minion %d grow %.2f\n",
                    TY[e.type], ST[e.state], e.pos.x, e.pos.y, e.prev.x, e.prev.y, e.hp, e.maxHp, e.sx, e.sy, e.seg, e.path.size(), e.minion, e.grow);
        fclose(f);
    }
    TakeScreenshot(TextFormat("bot_stuck_%d_%d.png", run, g.stage));
}

static const char* ATTACK_NAME[] = { "none", "spread", "burst", "spiral", "laser", "spawn", "ring", "homing", "twinlaser", "rain", "sweep" };
static const int ATTACKS = 11;

// ---- statistics: polled every frame ----
struct Stats {
    std::vector<StageRec> stages; std::vector<RunRec> runs;
    int bossDeaths[BOSS_COUNT][ATTACKS]{}, bossHits[BOSS_COUNT][ATTACKS]{};   // deaths / shield hits per boss, by its attack at the time
    int bossShotDeaths[BOSS_COUNT][16]{};                                      // deaths per boss by the kind of shot that hit (BKind)
    StageRec cur; bool open = false;
    int lastLives = 0; bool lastAlive = true, lastShield = false;
    void beginStage(const Game& g, int run) { cur = StageRec{}; cur.run = run; cur.stage = g.stage; cur.kind = stageKind(g.stage); open = true; }
    // cur.dur is refreshed every frame while the stage runs: the game resets stageTimer as soon as the next stage begins
    void endStage(bool cleared) { if (!open) return; cur.cleared = cleared; stages.push_back(cur); open = false; }
    void poll(const Game& g, int run, float dt) {
        if (g.mode == PLAYING) {
            if (!open || g.stage != cur.stage) { endStage(true); beginStage(g, run); }
            if (g.hyperT < 0 && !g.sectorPanel) cur.dur = g.stageTimer;
            if (cur.kind == SK_WAVE) {
                int alive = 0, pending = 0, couriers = 0; bool below = false;
                for (auto& e : g.enemies) {
                    if (e.state == DEAD) continue;
                    ++alive; if (e.state == WAITING || e.state == ENTERING) ++pending;
                    if (e.type == COURIER && e.state == FORMATION) ++couriers;
                    if (e.pos.y > g.player.y - 20) below = true;
                }
                if (cur.tail < 0 && alive > 0 && alive <= 2 && pending == 0) cur.tail = 0;
                if (cur.tail >= 0 && alive > 0) {
                    cur.tail += dt; if (below) cur.tailBelow += dt;
                    if (couriers == alive && g.weapon == W_SPREAD) cur.tailCourierSpread += dt;
                }
                if (cur.tail >= 60 && cur.tail - dt < 60) dumpStuck(g, run);   // a wave that won't end: record why
            }
        }
        // a boss's shots outlive its attack, so the attack is the one active, or the last one used, when the hit lands
        int atk = std::clamp((int)(g.boss.attack != A_NONE ? g.boss.attack : g.boss.lastAttack), 0, ATTACKS - 1);
        bool inBoss = g.mode == PLAYING && cur.kind == SK_BOSS && g.boss.active;
        if (lastAlive && !g.playerAlive) {
            ++cur.deaths; ++cur.deathBy[g.botLastHit];
            if (inBoss) { ++bossDeaths[g.boss.index][atk]; if (g.botLastHit == C_BULLET) ++bossShotDeaths[g.boss.index][std::clamp(g.botLastKind, 0, 15)]; }
        }
        else if (lastShield && !g.shield && g.playerAlive) { ++cur.shieldHits; ++cur.shieldBy[g.botLastHit]; if (inBoss) ++bossHits[g.boss.index][atk]; }
        lastAlive = g.playerAlive; lastShield = g.shield;
    }
};

static float pct(std::vector<float> v, float p) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)(p * (v.size() - 1) + 0.5f))]; }

inline void writeReports(const Stats& s) {
    if (FILE* f = fopen("bot_stages.csv", "w")) {
        fprintf(f, "run,stage,kind,cleared,duration_s,tail_s,tail_below_ship_s,tail_courier_spread_s,deaths,shield_hits");
        for (int c = 1; c < C_COUNT; ++c) fprintf(f, ",deaths_%s", CAUSE_NAME[c]);
        for (int c = 1; c < C_COUNT; ++c) fprintf(f, ",shield_%s", CAUSE_NAME[c]);
        fprintf(f, "\n");
        for (auto& r : s.stages) {
            fprintf(f, "%d,%d,%s,%d,%.1f,%.1f,%.1f,%.1f,%d,%d", r.run, r.stage, r.kind == SK_WAVE ? "wave" : r.kind == SK_METEORS ? "meteors" : "boss",
                    r.cleared, r.dur, r.tail, r.tailBelow, r.tailCourierSpread, r.deaths, r.shieldHits);
            for (int c = 1; c < C_COUNT; ++c) fprintf(f, ",%d", r.deathBy[c]);
            for (int c = 1; c < C_COUNT; ++c) fprintf(f, ",%d", r.shieldBy[c]);
            fprintf(f, "\n");
        }
        fclose(f);
    }
    if (FILE* f = fopen("bot_runs.csv", "w")) {
        fprintf(f, "run,seed,stage_reached,score,sim_minutes,end\n");
        for (auto& r : s.runs) fprintf(f, "%d,%d,%d,%d,%.1f,%s\n", r.run, r.seed, r.stageReached, r.score, r.simMin, r.end);
        fclose(f);
    }
    if (FILE* f = fopen("bot_summary.txt", "w")) {
        std::vector<float> reached, tails, below, waveDur, cs; int waves = 0, long20 = 0, long10 = 0, belowAny = 0, csAny = 0;
        int deaths[3]{}, shield[3]{}, played[3]{}, dBy[C_COUNT]{}, sBy[C_COUNT]{};
        for (auto& r : s.runs) reached.push_back((float)r.stageReached);
        for (auto& r : s.stages) {
            ++played[r.kind]; deaths[r.kind] += r.deaths; shield[r.kind] += r.shieldHits;
            for (int c = 0; c < C_COUNT; ++c) { dBy[c] += r.deathBy[c]; sBy[c] += r.shieldBy[c]; }
            if (r.kind == SK_WAVE && r.cleared) {
                ++waves; waveDur.push_back(r.dur);
                float t = std::max(0.0f, r.tail); tails.push_back(t); below.push_back(r.tailBelow);
                if (t > 10) ++long10; if (t > 20) ++long20; if (r.tailBelow > 0.5f) ++belowAny;
                if (r.tailCourierSpread > 0.5f) { ++csAny; cs.push_back(r.tailCourierSpread); }
            }
        }
        float avgR = 0; for (float x : reached) avgR += x; if (!reached.empty()) avgR /= reached.size();
        fprintf(f, "Star Swarm bot report: %d runs\n\n", (int)s.runs.size());
        fprintf(f, "Stage reached:  mean %.1f   median %.0f   min %.0f   max %.0f\n", avgR, pct(reached, 0.5f), pct(reached, 0), pct(reached, 1));
        int wins = 0; for (auto& r : s.runs) if (!strcmp(r.end, "victory")) ++wins;
        fprintf(f, "Victories:      %d\n\n", wins);
        fprintf(f, "Cleared waves:  %d\n", waves);
        fprintf(f, "  wave duration (s):        median %.1f   p90 %.1f   max %.1f\n", pct(waveDur, 0.5f), pct(waveDur, 0.9f), pct(waveDur, 1));
        fprintf(f, "  tail, 2 left -> clear (s): median %.1f   p90 %.1f   max %.1f\n", pct(tails, 0.5f), pct(tails, 0.9f), pct(tails, 1));
        fprintf(f, "  tails over 10 s: %d (%.0f%%)   over 20 s: %d (%.0f%%)\n", long10, waves ? 100.0f * long10 / waves : 0, long20, waves ? 100.0f * long20 / waves : 0);
        fprintf(f, "  a remaining enemy level with / below the ship: in %d waves (%.0f%%), p90 %.1f s, max %.1f s\n\n",
                belowAny, waves ? 100.0f * belowAny / waves : 0, pct(below, 0.9f), pct(below, 1));
        fprintf(f, "  only formation Couriers left while holding the Spread Cannon (out of reach from the bottom row):\n"
                   "    in %d waves (%.0f%%), median %.1f s, max %.1f s\n\n", csAny, waves ? 100.0f * csAny / waves : 0, pct(cs, 0.5f), pct(cs, 1));
        const char* kn[3] = { "waves", "meteor fields", "bosses" };
        for (int k = 0; k < 3; ++k)
            fprintf(f, "%-14s played %4d   deaths %4d (%.2f per stage)   shield hits %4d (%.2f per stage)\n", kn[k], played[k], deaths[k],
                    played[k] ? (float)deaths[k] / played[k] : 0, shield[k], played[k] ? (float)shield[k] / played[k] : 0);
        fprintf(f, "\nDeaths by cause:      "); for (int c = 1; c < C_COUNT; ++c) fprintf(f, "%s %d   ", CAUSE_NAME[c], dBy[c]);
        fprintf(f, "\nShield hits by cause: "); for (int c = 1; c < C_COUNT; ++c) fprintf(f, "%s %d   ", CAUSE_NAME[c], sBy[c]);
        fprintf(f, "\n\nBoss fights: deaths (shield hits) by the boss's attack at the time\n");
        for (int b = 0; b < BOSS_COUNT; ++b) {
            int fights = 0; for (auto& r : s.stages) if (r.kind == SK_BOSS && r.stage == (b + 1) * CYCLE) ++fights;
            fprintf(f, "  %2d %-10s %3d fights:", b + 1, spr::bossDesigns()[b].name, fights);
            for (int a = 0; a < ATTACKS; ++a) if (s.bossDeaths[b][a] || s.bossHits[b][a]) fprintf(f, "  %s %d (%d)", ATTACK_NAME[a], s.bossDeaths[b][a], s.bossHits[b][a]);
            fprintf(f, "\n");
        }
        static const char* KIND[16] = { "fighter shot", "orb", "missile", "meteor", "bolt", "spiral", "crystal", "c-spread", "c-pulse",
                                        "c-homing", "c-ion", "c-photon", "flak", "13", "14", "15" };
        fprintf(f, "\nBoss fights: deaths by the kind of shot that hit\n");
        for (int b = 0; b < BOSS_COUNT; ++b) {
            fprintf(f, "  %2d %-10s", b + 1, spr::bossDesigns()[b].name);
            for (int k = 0; k < 16; ++k) if (s.bossShotDeaths[b][k]) fprintf(f, "  %s %d", KIND[k], s.bossShotDeaths[b][k]);
            fprintf(f, "\n");
        }
        fclose(f);
    }
}

// ---- driver: plays `runs` games and writes the reports ----
inline void run(Game& game, int runs, int seed, int startStage) {
    Stats s;
    const float dt = 1.0f / 60.0f, CAP_MIN = 60.0f;                    // a game longer than 60 simulated minutes is cut off
    game.botOn = true; game.autoFire = true;
    for (int r = 1; r <= runs && !WindowShouldClose(); ++r) {
        // Every game depends only on its seed: everything that draws random numbers or keeps time is reset after
        // seeding (the background stars take a random number whenever one wraps; the clock drives the formation sway).
        SetRandomSeed((unsigned)(seed + r - 1));
        game.time = 0; game.initStars(); game.fireCooldown = 0; game.boss = Boss{};   // + leftovers newGame keeps
        game.newGame(startStage);
        s.open = false; s.lastAlive = true; s.lastShield = game.shield;
        float simT = 0; long frame = 0; const char* end = "timeout";
        // STARSWARM_BOT_TRACE=1: a per-frame fingerprint of the game state (first 20000 frames) in bot_trace_<run>.txt,
        // for checking that a seed replays identically (compare two traces).
        FILE* trace = getenv("STARSWARM_BOT_TRACE") ? fopen(TextFormat("bot_trace_%d.txt", r), "w") : nullptr;
        while (!WindowShouldClose()) {
            if (trace && frame < 20000) {
                int alive = 0; float ex = 0; for (auto& e : game.enemies) if (e.state != DEAD) { ++alive; ex += e.pos.x + e.pos.y; }
                float bx = 0; for (auto& b : game.eBullets) bx += b.pos.x + b.pos.y;
                fprintf(trace, "%ld stage %d px %.3f alive %d ex %.3f eb %zu bx %.3f pb %zu parts %zu score %d boss %.2f mode %d\n", frame, game.stage,
                        game.player.x, alive, ex, game.eBullets.size(), bx, game.pBullets.size(), game.parts.size(), game.score, game.boss.hp, (int)game.mode);
            }
            think(game, dt);
            game.update(dt * game.timeScale());
            game.tickRealTime(dt);
            s.poll(game, r, dt);
            simT += dt;
            if (game.mode == GAMEOVER) { end = "game over"; break; }
            if (game.mode == VICTORY)  { end = "victory"; break; }
            if (simT > CAP_MIN * 60) break;
            if (++frame % 30 == 0) game.draw(); else PollInputEvents();
        }
        if (trace) fclose(trace);
        s.endStage(game.mode == VICTORY);
        s.runs.push_back({ r, seed + r - 1, game.stage, game.score, simT / 60, end });
        writeReports(s);                                               // after every run, so partial results survive
    }
}

} // namespace bot
#endif
