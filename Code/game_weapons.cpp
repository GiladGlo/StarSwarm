// Star Swarm: weapons, the drone, the laser, packages and collisions.
#include "game.h"

float Game::wCooldown() const {
    static const float cd[W_COUNT][4] = { { 0.2f, 0.14f, 0.14f, 0.14f }, { 0.32f, 0.3f, 0.28f, 0.26f }, { 0, 0, 0, 0 },
                                          { 0.42f, 0.36f, 0.36f, 0.32f }, { 0.5f, 0.45f, 0.34f, 0.34f } };
    return cd[weapon][wlevel];
}

void Game::activateDrone() {
    unlockMedal(MD_WINGMAN);
    droneT = DRONE_TIME;
    if (droneOn) return;                             // already flying: just refresh the timer
    droneOn = true; droneWarp = 0.6f; droneFireT = 0.8f; droneBank = 0;
    droneOff = droneTarget = { -50, 0 }; droneVel = { 0, 0 };
    playSfx(SFX_SHIELD_UP);
}

void Game::destroyDrone() {
    Vector2 d = Vector2Add(player, droneOff);
    explode(d, Color{ 140, 255, 170, 255 }, Color{ 220, 230, 245, 255 }, 26, 240);
    for (int i = 0; i < 10; ++i) { float a = frand(0, 2 * PI), sp = frand(80, 220), life = frand(0.4f, 0.8f);
        parts.push_back({ d, { cosf(a) * sp, sinf(a) * sp }, life, life, frand(2, 3.5f), Color{ 200, 210, 225, 255 }, DEBRIS, frand(0, 360), frand(-800, 800) }); }
    playSfx(SFX_BOOM);
    droneOn = false;
}

void Game::updateDrone(float dt) {
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

void Game::drawDrone() {
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

void Game::updateShipFx(float dt) {
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

void Game::firePlayerWeapon() {
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
    if (weapon == W_ION) rumbleFor(0.25f, 0.1f);
    Color c = WCOL[weapon];
    parts.push_back({ m, { 0, 0 }, 0.12f, 0.12f, 22, c, FLASH });
    playSfx(weapon == W_SPREAD ? SFX_FIRE_SPREAD : weapon == W_HOMING ? SFX_FIRE_HOMING : weapon == W_ION ? SFX_FIRE_ION : SFX_SHOOT);
}

bool Game::hitEnemy(Enemy& e, Vector2 at) {
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
        if (!debugRun) { ++courierKillsTotal; saveMedals(); if (courierKillsTotal >= 25) unlockMedal(MD_COURIER); }
        courierBurst(e.pos);
        packages.push_back({ { e.pos.x, e.pos.y + 6 }, e.cargo, 0 });
    } else {
        explode(e.pos, enemyColor(e.type), WHITE, e.type == MINI ? 14 : 28, 220);
        playSfx(SFX_BOOM);
    }
    if (e.type == SPLITTER) pendingSplits.push_back(e.pos);
    return true;
}

void Game::courierBurst(Vector2 at) {
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

void Game::courierFire(const Enemy& e) {
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

void Game::ionBlast(Vector2 at, bool bossDirect) {
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

void Game::updateLaser(float dt) {
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

void Game::collectPackage(const Package& pk) {
    Color c = pk.content >= 0 ? WCOL[pk.content] : UPG_COL;
    std::string label;
    if (pk.content < 0 || pk.content == weapon) {
        if (wlevel < 3) { ++wlevel; label = TextFormat("UPGRADE +1   LV%d", wlevel); if (wlevel == 3) unlockMedal(MD_MAXPOWER); }
        else { addScore(1000); label = droneOn ? "MAX LEVEL +1000  DRONE 30s" : "MAX LEVEL +1000  WINGMAN DRONE"; activateDrone(); }
    } else {
        weapon = pk.content; heat = 0; overheated = false;   // keeps the current upgrade level
        noteWeapon(); if (wlevel == 3) unlockMedal(MD_MAXPOWER);
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

void Game::updatePackages(float dt) {
    for (auto& pk : packages) {
        pk.t += dt; pk.pos.y += 120 * dt;
        pk.pos.x += sinf(pk.t * 2.2f) * 12 * dt;
        if (playerAlive && mode == PLAYING && Vector2Distance(pk.pos, player) < 34) { collectPackage(pk); pk.pos.y = H + 100; }
    }
    packages.erase(std::remove_if(packages.begin(), packages.end(), [](const Package& p) { return p.pos.y > H + 30; }), packages.end());
    for (auto& z : zaps) z.life -= dt;
    zaps.erase(std::remove_if(zaps.begin(), zaps.end(), [](const Lightning& z) { return z.life <= 0; }), zaps.end());
}

void Game::drawWeaponIndicator() {
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

void Game::drawZap(Vector2 a, Vector2 b, unsigned seed, Color c, float w, unsigned char alpha) {
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

int Game::enemyPoints(EType t, bool diving) const {
    switch (t) {
        case BEE: return diving ? 100 : 50;          case BUTTERFLY: return diving ? 160 : 80;
        case BOSS: return diving ? 400 : 150;        case DART: return diving ? 240 : 120;
        case SHIELDED: return diving ? 300 : 150;    case SPLITTER: return diving ? 200 : 100;
        case COURIER: return 500;
        default: return 60;                          // MINI
    }
}

Color Game::enemyColor(EType t) const {
    switch (t) {
        case BEE: return YELLOW; case BUTTERFLY: return RED; case BOSS: return GREEN;
        case DART: return Color{ 255, 80, 60, 255 }; case SHIELDED: return Color{ 90, 255, 230, 255 };
        case COURIER: return UPG_COL;
        default: return Color{ 220, 90, 255, 255 };
    }
}

void Game::releaseSplits() {
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

void Game::destroyProjectile(size_t i) {
    Bullet b = eBullets[i];
    eBullets.erase(eBullets.begin() + i);
    if (b.kind == BK_FLAK && !debugRun) { ++flakShotTotal; saveMedals(); if (flakShotTotal >= 20) unlockMedal(MD_FLAK); }
    if (b.kind == BK_METEOR) {
        explode(b.pos, Color{ 255, 150, 60, 255 }, Color{ 150, 130, 115, 255 }, 12, 180);
        for (int q = 0; q < 6; ++q) { float ang = frand(0, 2 * PI), sp = frand(60, 160), life = frand(0.4f, 0.8f);
            parts.push_back({ b.pos, { cosf(ang) * sp, sinf(ang) * sp }, life, life, frand(2, 3.5f), Color{ 130, 115, 100, 255 }, DEBRIS, frand(0, 360), frand(-600, 600) }); }
    } else explode(b.pos, Color{ 255, 170, 70, 255 }, WHITE, 14, 200);
    addScore(25);
    playSfx(SFX_IMPACT);
}

void Game::collisions() {
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
#ifdef STARSWARM_BOT
            botLastKind = eBullets[i].kind;
#endif
            eBullets.erase(eBullets.begin() + i);
            BOT_CAUSE(1);
            if (shield) breakShield(at); else killPlayer();
            return;
        }
    // Meteors smashing into the player
    for (size_t j = 0; j < meteors.size(); ++j)
        if (CheckCollisionCircles(meteors[j].pos, meteors[j].r() * 0.82f, player, shield ? SHIELD_RADIUS - 2 : 13)) {
            Vector2 at = meteors[j].pos;
            hitMeteor(j, Vector2Subtract(at, player));
            BOT_CAUSE(2);
            if (shield) breakShield(at); else killPlayer();
            return;
        }
    // Divers ramming the player
    for (auto& e : enemies)
        if (e.state == DIVING && CheckCollisionCircles(e.pos, 14, player, shield ? SHIELD_RADIUS - 2 : 14)) {
            e.state = DEAD; explode(e.pos, ORANGE, WHITE, 25, 200);
            addScore(enemyPoints(e.type, true));
            if (e.type == SPLITTER) pendingSplits.push_back(e.pos);
            BOT_CAUSE(3);
            if (shield) breakShield(e.pos); else killPlayer();
            return;
        }
}
