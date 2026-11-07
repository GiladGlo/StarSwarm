// Star Swarm: enemy dives, explosions, scoring and the player getting hit.
#include "game.h"

bool Game::lastStand() const {
    if (stageKind(stage) != SK_WAVE) return false;
    int alive = 0, couriers = 0;
    for (auto& e : enemies) {
        if (e.state == DEAD) continue;
        if (e.state == WAITING || e.state == ENTERING) return false;   // the wave is still arriving
        ++alive; if (e.type == COURIER) ++couriers;
    }
    return alive == 1 || (alive > 0 && couriers == alive);
}

void Game::startStrike(Enemy& e) {
    Vector2 p = e.pos;
    float tx = Clamp(player.x + frand(-25, 25), 40, W - 40), side = tx < W / 2 ? 1.f : -1.f;
    e.path = buildPath({ p, { p.x, p.y - 25 }, { (p.x + tx) / 2, std::max(p.y + 80, H * 0.35f) }, { tx, player.y - 60 },
                         { tx + side * 50, H + 60.0f } });
    e.seg = 0; e.state = DIVING;
    e.speed = (e.type == COURIER ? 230 : 300) * std::min(difficulty(), 1.5f);
    e.shotsLeft = 1 + GetRandomValue(0, 1);
    e.shootTimer = frand(0.3f, 0.6f);
    playSfx(SFX_DIVE);
}

void Game::startDive(Enemy& e) {
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

void Game::explode(Vector2 p, Color a, Color b, int n, float power) {
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

void Game::torpedoImpact(Vector2 p) {
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

void Game::addScore(int pts) {
    score += pts;
    if (score > hiScore && !debugRun) hiScore = score;
    if (score >= nextLifeAt) {
        ++lives; nextLifeAt += 50000; playSfx(SFX_LIFE);
        popups.push_back({ { player.x, player.y - 40 }, 1.5f, "1UP!" });
    }
}

void Game::breakShield(Vector2 at) {
    noteHitTaken(); rumbleFor(0.6f, 0.25f);
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

void Game::killPlayer() {
    noteHitTaken(); ++shipsLostSector; ++shipsLostGame; rumbleFor(1.0f, 0.5f);
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

void Game::followPath(Enemy& e, float dist) {
    while (dist > 0 && e.seg + 1 < e.path.size()) {
        Vector2 to = e.path[e.seg + 1];
        float d = Vector2Distance(e.pos, to);
        if (dist >= d) { e.pos = to; ++e.seg; dist -= d; }
        else { e.pos = Vector2Add(e.pos, Vector2Scale(Vector2Normalize(Vector2Subtract(to, e.pos)), dist)); dist = 0; }
    }
}

void Game::updateEnemies(float dt) {
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
            bool last = lastStand();
            std::vector<Enemy*> pool;
            for (auto& e : enemies) if (e.state == FORMATION && (e.type != COURIER || last)) pool.push_back(&e);   // couriers only dive in a last stand
            if (!pool.empty() && last) startStrike(*pool[GetRandomValue(0, (int)pool.size() - 1)]);
            else if (!pool.empty()) {
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
