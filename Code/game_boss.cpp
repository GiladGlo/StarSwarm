// Star Swarm: the capital-ship bosses: attacks, damage and drawing.
#include "game.h"

void Game::startBoss() {
    bossFightT = 0;
    boss = Boss{};
    boss.active = true; boss.state = B_ENTER;
    boss.index = std::clamp(stage / CYCLE - 1, 0, BOSS_COUNT - 1);
    gfx.ensureBoss(boss.index);
    boss.maxHp = boss.hp = BOSS_HP[boss.index];
    boss.pos = { W / 2.0f, -boss.size() * 0.6f };
    bannerTimer = 3.5f;
    playSfx(SFX_ALARM);
}

void Game::bossShoot(Vector2 from, float angleDeg, float speed, int kind) {
    float a = angleDeg * DEG2RAD;
    eBullets.push_back({ from, { cosf(a) * speed, sinf(a) * speed }, kind });
}

void Game::muzzleFlash(Vector2 at, Color c, float size, bool ring) {
    parts.push_back({ at, { 0, 0 }, 0.12f, 0.12f, size, Color{ 255, 250, 235, 255 }, FLASH });
    parts.push_back({ at, { 0, 0 }, 0.2f, 0.2f, size * 1.4f, c, FLASH });
    if (ring) rings.push_back({ at, 0.22f, 0.22f, size * 1.3f, c });
}

float Game::beamLean(Vector2 o) const {
    if (boss.attack != A_SWEEP) return 0;
    float off = -std::clamp(boss.vx * 0.35f, -90.0f, 90.0f);
    return atan2f(off, H - o.y);
}

void Game::spawnMinion(Vector2 at, EType type) {
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

void Game::updateBoss(float dt) {
    Boss& b = boss;
    if (!b.active) return;
    b.t += dt;
    if (b.state == B_FIGHT) bossFightT += dt;
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
            bossDefeated(b);
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
                float spread = 80.0f + 6.0f * b.index, base = 90 + (b.step % 2 ? spread / (2 * (n - 1)) : 0);
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
                b.fireT = 0.110f / std::sqrt(ag);              // softened twice (~25% fewer orbs along each arm): the deadliest boss attack in bot testing
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
                        BOT_CAUSE(4);
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

void Game::damageBoss(Vector2 at, float amount) {
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

Color Game::bossColor(Vector3 c, unsigned char a) const {
    return { (unsigned char)(c.x * 255), (unsigned char)(c.y * 255), (unsigned char)(c.z * 255), a };
}

Color Game::spiralColor(float aux) const {
    Vector3 hsv = ColorToHSV(coreColor());
    return ColorFromHSV(fmodf(hsv.x + aux * 140.0f + 360.0f, 360.0f), 0.65f, 1.0f);
}

void Game::drawBeam(Vector2 m, bool firing, float charge) {
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

void Game::drawBoss() {
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
        Vector2 core{ p.x + D.coreP.x * S / 2, p.y + D.coreP.y * S / 2 };
        float sp = (b.index % 2 ? -1.f : 1.f) * time * 360;
        for (int k = 0; k < 3; ++k) {
            DrawRing(core, 22, 26, sp + k * 120, sp + k * 120 + 70, 12, coreColor(200));
            DrawRing(core, 32, 34, -sp * 0.6f + k * 120, -sp * 0.6f + k * 120 + 40, 10, coreColor(140));
        }
    }
    if (b.attack == A_LASER || b.attack == A_TWINLASER || b.attack == A_SWEEP) {
        float charge = std::min(1.0f, b.attackT / std::max(0.7f, 1.1f - 0.03f * b.index));
        for (Vector2 o : b.beams()) drawBeam(o, b.step == 1, charge);
    }
    EndBlendMode();
}
