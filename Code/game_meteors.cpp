// Star Swarm: meteor fields and mining outposts.
#include "game.h"

void Game::startMeteorField() {
    fieldHit = false; outpostsField = 0;
    meteorField = true; fieldT = 0; spawnT = 0; outpostsSpawned = 0;
    gfx.ensureBoss(std::clamp((stage + 1) / CYCLE - 1, 0, BOSS_COUNT - 1));   // build the coming boss now
    fieldDur = 30.0f + 2.0f * cycleIndex();
    targetDensity = std::min(0.45f, 0.30f + 0.017f * cycleIndex());
    bannerTimer = 3.0f;
    playSfx(SFX_STAGE);
}

float Game::density() const {
    float a = 0;
    for (const auto& m : meteors)
        if (m.pos.y + m.r() > 0 && m.pos.y - m.r() < H) a += PI * m.r() * m.r();
    return a / (W * H);
}

void Game::spawnMeteor() {
    float pick = frand(0, 1);
    bool hot = frand(0, 1) < (getenv("SS_HOT") ? 0.3f : VOLATILE_CHANCE);
    MSize sz = hot ? M_LARGE : (pick < 0.55f ? M_LARGE : (pick < 0.9f ? M_MEDIUM : M_SMALL));
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

void Game::spawnOutpost(float x, float y) {
    float fieldAvg = (45.0f + 65.0f + 87.5f) / 3 * (1 + 0.04f * cycleIndex());
    Meteor m{ { x, y }, { frand(-10, 10), 0.7f * fieldAvg }, frand(0, 360), frand(5, 8) * (GetRandomValue(0, 1) ? 1 : -1), 0, M_LARGE, 0 };
    m.outpost = true;
    m.hp = m.hpMax = OUTPOST_HP_BASE + std::min(6, cycleIndex());
    m.gunT = 1.5f;
    addMeteor(m);
}

void Game::hitMeteor(size_t idx, Vector2 hitDir) {
    if (meteors[idx].outpost) {
        if (--meteors[idx].hp > 0) { damageOutpost(meteors[idx], hitDir); return; }
        destroyOutpost(meteors[idx]);
        detonateMeteor(idx);                 // the station's fuel goes up: volatile-style blast, no fragments
        return;
    }
    if (meteors[idx].volatileRock()) detonateMeteor(idx); else breakMeteor(idx, hitDir);
}

Vector2 Game::outpostLocal(const Meteor& m, Vector2 u) const {
    float S = m.r() * 2 / 0.8f * 1.08f, a = m.rot * DEG2RAD; Vector2 q{ u.x * S * 0.5f, u.y * S * 0.5f };
    return { m.pos.x + q.x * cosf(a) - q.y * sinf(a), m.pos.y + q.x * sinf(a) + q.y * cosf(a) };
}

void Game::damageOutpost(Meteor& m, Vector2 hitDir) {
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

void Game::updateOutpost(Meteor& m, float dt) {
    Vector2 g = outpostLocal(m, OUTPOST_GUN);
    float want = atan2f(player.y - g.y, player.x - g.x) * RAD2DEG;
    float d = fmodf(want - m.gunAim + 540, 360) - 180;
    m.gunAim += std::clamp(d, -120 * dt, 120 * dt);                       // barrel tracks the player
    bool canFire = mode == PLAYING && playerAlive && m.pos.y > 30 && m.pos.y < player.y - 140;
    m.gunT -= dt;
    if (!canFire) m.gunT = std::max(m.gunT, 0.45f);                       // hold the shot (keeps the warning honest)
    else if (m.gunT <= 0) {
        float a = m.gunAim * DEG2RAD, sp = 150.0f + 6.0f * cycleIndex();
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

void Game::destroyOutpost(Meteor& m) {
    ++outpostsSector; if (++outpostsField >= 3) unlockMedal(MD_RAIDER);
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

void Game::detonateMeteor(size_t idx) {
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

    chainT = 0.8f;                                   // rocks caught within one chain reaction count together
    std::vector<int> caught;
    for (const auto& o : meteors)
        if (Vector2Distance(o.pos, m.pos) < BLAST_RADIUS + o.r() * 0.5f) caught.push_back(o.id);
    for (int id : caught) {
        auto it = std::find_if(meteors.begin(), meteors.end(), [id](const Meteor& o) { return o.id == id; });
        if (it == meteors.end()) continue;
        if (it->volatileRock()) { if (it->fuse <= 0) { it->fuse = 0.15f; ++chainCount; } continue; }   // chain reaction
        ++chainCount;
        if (it->outpost) { hitMeteor((size_t)(it - meteors.begin()), Vector2Subtract(it->pos, m.pos)); continue; }   // armour soaks one hit
        breakMeteor((size_t)(it - meteors.begin()), Vector2Subtract(it->pos, m.pos));
    }
    if (chainCount >= 5) unlockMedal(MD_CHAIN);
}

void Game::breakMeteor(size_t idx, Vector2 hitDir) {
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

void Game::updateMeteors(float dt) {
    if (chainT > 0 && (chainT -= dt) <= 0) chainCount = 0;
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
        if (!fieldHit) unlockMedal(MD_UNTOUCHABLE);
        playSfx(SFX_LIFE);
    }
}

void Game::drawOutpost(const Meteor& m, float S) {
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

void Game::drawMeteors() {
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
