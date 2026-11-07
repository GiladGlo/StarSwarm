// Star Swarm: the per-frame update: player, enemies, bullets, particles.
#include "game.h"

void Game::update(float dt) {
    time += dt;
    if (escMenu) { updateEscMenu(); return; }
    if (!medalToasts.empty() && (toastT += dt) > 3.2f) { medalToasts.erase(medalToasts.begin()); toastT = 0; }
    for (auto& s : stars) { s.pos.y += s.speed * dt * (mode == PLAYING ? 1.0f : 0.5f);
                            if (s.pos.y > H) { s.pos.y = 0; s.pos.x = frand(0, W); } }
    updateParticles(dt);

    if (mode == TITLE) {
        bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (stageDialog) { updateStageDialog(); return; }
        titleT += dt;
        if (medalsOpen) { updateMedalsScreen(); return; }
        if (IsKeyPressed(KEY_M) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_UP) || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseLogical(), MUTE_BTN))) toggleTitleMute();
        if (IsKeyPressed(KEY_TAB) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_LEFT) || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseLogical(), MEDAL_BTN))) { medalsOpen = true; medalSel = 0; return; }
        if (ctrl && IsKeyPressed(KEY_L)) { stageDialog = true; dlgText = "1"; while (GetCharPressed()) {} return; }
        if (IsKeyPressed(KEY_SPACE) || padConfirm() || (IsKeyPressed(KEY_ENTER) && !alt) || touchTap()) { debugRun = false; newGame(); }
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
        if (stageTimer > 3.0f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || padConfirm() || touchTap())) returnToTitle();
        return;
    }
    if (mode == GAMEOVER) {
        updateEnemies(dt);
        updateBoss(dt);
        updateMeteors(dt);
        if (stageTimer > 2.0f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || padConfirm() || touchTap())) returnToTitle();
        stageTimer += dt;
        return;
    }

    if (IsKeyPressed(KEY_M) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_UP) || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseLogical(), hudTopRect(TOUCH_UI ? GAME_MUTE_HIT : GAME_MUTE_BTN)))) toggleGameMusicMute();
    if (IsKeyPressed(KEY_P) || padPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT)) paused = !paused;
    if (TOUCH_UI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseLogical(), hudTopRect(GAME_PAUSE_HIT))) { openEscMenu(); return; }
    if (paused) return;
    if (sectorPanel) {                                // SECTOR CLEARED: 6 s, or Space / A after a short beat
        sectorPanelT += dt;
        if (sectorPanelT >= PANEL_TIME || (sectorPanelT > 0.6f && (IsKeyPressed(KEY_SPACE) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || touchTap()))) { sectorPanel = false; startHyper(); }
        return;
    }
    sectorT += dt;

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
            if (stageKind(stage) == SK_BOSS && playerAlive && hyperT < 0) openSectorPanel(); else nextStage();
        }
    } else clearT = 0;
}

void Game::updatePlayer(float dt) {
    if (!playerAlive) {
        touchDrag = false;   // re-anchor the drag on the respawned ship
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
    if (padOn()) {                                   // controller: left stick (analog) or D-pad
        float ax = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        if (fabsf(ax) > 0.2f) mv += ax;
        if (fabsf(ay) > 0.2f) mvy += ay;
        if (padDown(GAMEPAD_BUTTON_LEFT_FACE_LEFT)) mv -= 1;
        if (padDown(GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) mv += 1;
        if (padDown(GAMEPAD_BUTTON_LEFT_FACE_UP)) mvy -= 1;
        if (padDown(GAMEPAD_BUTTON_LEFT_FACE_DOWN)) mvy += 1;
        mv = Clamp(mv, -1, 1); mvy = Clamp(mvy, -1, 1);
    }
#ifdef STARSWARM_BOT
    if (botOn) { mv = botMv; mvy = botMvy; }
#endif
    player.x = Clamp(player.x + mv * 340 * dt, PLAYER_MARGIN_X, W - PLAYER_MARGIN_X);
    player.y = Clamp(player.y + mvy * 300 * dt, PLAYER_Y_HIGH, PLAYER_Y_LOW);
    if (TOUCH_UI) {   // relative drag: the ship follows the finger's movement from wherever it touched; holding fires
        Vector2 tp = mouseLogical();
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) touchDrag = false;
        else if (!touchDrag) { if (!onHudButton(tp)) { touchDrag = true; touchLast = tp; } }
        else {
            player.x = Clamp(player.x + (tp.x - touchLast.x) * TOUCH_SENS, PLAYER_MARGIN_X, W - PLAYER_MARGIN_X);
            player.y = Clamp(player.y + (tp.y - touchLast.y) * TOUCH_SENS, PLAYER_Y_HIGH, PLAYER_Y_LOW);
            touchLast = tp;
        }
    }

    // debug runs: 1-5 pick a weapon, +/- change its level
    if (debugRun) {
        for (int k = 0; k < W_COUNT; ++k) if (IsKeyPressed((KeyboardKey)(KEY_ONE + k))) { weapon = k; heat = 0; overheated = false; }
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) wlevel = std::min(3, wlevel + 1);
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) wlevel = std::max(0, wlevel - 1);
    }
    fireCooldown -= dt;
    bool trigger = IsKeyDown(KEY_SPACE) || autoFire || touchDrag   // hold to keep firing (A or RT on a controller; finger down on touch)
                || padDown(GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || padDown(GAMEPAD_BUTTON_RIGHT_TRIGGER_2)
                || (padOn() && GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.3f);
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

void Game::updateBullets(float dt) {
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

void Game::updateParticles(float dt) {
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
