// Star Swarm: new game, stage flow, waves and entry paths.
#include "game.h"

Vector2 Game::slotPos(const Enemy& e) const {
    float breathe = 1.0f + 0.07f * sinf(time * 1.6f);
    float sway    = 26.0f * sinf(time * 0.7f);
    float spacing = 44.0f * breathe;
    return { W / 2.0f + sway + e.sx * spacing, 110.0f + e.sy * 40.0f * (0.5f + 0.5f * breathe) };
}

void Game::initStars() {
    stars.clear();
    for (int i = 0; i < 220; ++i) {
        float layer = frand(0, 1);
        const Color cols[] = { WHITE, SKYBLUE, PINK, YELLOW, LIME };
        Color c = cols[GetRandomValue(0, 4)];
        stars.push_back({ { frand(0, W), frand(0, H) }, 30 + layer * 120, frand(0, 6.28f), c });
    }
}

void Game::newGame(int startStage) {
    if (mode == TITLE) { for (int k = 0; k < sfx::TFX_LAUNCH; ++k) StopSound(titleFx[k]); if (!titleMuted) PlaySound(titleFx[sfx::TFX_LAUNCH]); }
    lives = 3; score = 0; stage = std::clamp(startStage, 1, FINAL_STAGE) - 1; nextLifeAt = 20000; shotsFired = hits = 0;
    playerAlive = true; player = { W / 2.0f, PLAYER_Y_LOW }; invuln = 0; roll = 0; prevPX = player.x; warpT = WARP_TIME;
    pBullets.clear(); eBullets.clear(); parts.clear(); rings.clear(); popups.clear(); meteors.clear(); meteorField = false;
    hyperT = -1; droneOn = false;
    packages.clear(); zaps.clear(); weapon = W_PHOTON; wlevel = 0; heat = 0; overheated = false; weaponBag.clear(); lastOffered = -1;
    mode = PLAYING; shield = false; boss.active = false;
    shipsLostGame = 0; weaponsUsed = 1 << W_PHOTON; sectorPanel = false; slowT = flashT = 0; chainCount = 0;
    medalToasts.clear(); toastT = 0; sectorMedals.clear(); medalsOpen = false;
    nextStage();
}

int Game::nextBagWeapon() {
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < weaponBag.size(); ++i)
            if (weaponBag[i] != weapon && weaponBag[i] != lastOffered) {
                int w = weaponBag[i]; weaponBag.erase(weaponBag.begin() + i); return lastOffered = w; }
        weaponBag = { W_SPREAD, W_PULSE, W_HOMING, W_ION };
        for (int i = (int)weaponBag.size() - 1; i > 0; --i) std::swap(weaponBag[i], weaponBag[GetRandomValue(0, i)]);
    }
    return lastOffered = (weapon == W_SPREAD ? W_HOMING : W_SPREAD);
}

int Game::rollCargo() {
    float pWeapon = (weapon == W_PHOTON && wlevel == 0) ? 0.5f : 0.2f;
    return frand(0, 1) < pWeapon ? nextBagWeapon() : -1;
}

void Game::raiseShield() {
    if (shield) return;
    shield = true; shieldGrow = 0; shieldHitFlash = 0; shieldRegen = 0;
    playSfx(SFX_SHIELD_UP);
}

void Game::nextStage() {
    if (stage >= 1 && stageKind(stage) == SK_WAVE && mode == PLAYING) {      // wave just cleared: accuracy medal
        int sh = shotsFired - stageShots0, ht = hits - stageHits0;
        if (sh >= 10 && ht * 100 >= sh * 80) unlockMedal(MD_SHARP);
    }
    ++stage;
    stageShots0 = shotsFired; stageHits0 = hits;
    if ((stage - 1) % CYCLE == 0) { sectorT = 0; sectorShots0 = shotsFired; sectorHits0 = hits; shipsLostSector = 0; outpostsSector = 0; sectorMedals.clear(); }
    if (stage == 5 * CYCLE + 1 && shipsLostGame == 0) unlockMedal(MD_NOCONT);
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

std::vector<Vector2> Game::entryPath(int kind, bool mirror) {
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
