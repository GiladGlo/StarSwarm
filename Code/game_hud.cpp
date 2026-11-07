// Star Swarm: medals, overlays, the title roster and the HUD.
#include "game.h"

const Game::MedalDef* Game::medalDefs() {
    static const MedalDef M[MD_COUNT] = {
        { "UNTOUCHABLE",     "Clear a meteor field without taking a hit",      { 120, 200, 255, 255 }, 0 },
        { "FLAWLESS SECTOR", "Clear a whole sector without losing a ship",     { 255, 215, 60, 255 }, 1 },
        { "PURIST",          "Defeat a boss with the Photon Torpedo at Lv0",   { 255, 110, 50, 255 }, 2 },
        { "OUTPOST RAIDER",  "Destroy all 3 mining outposts in one field",     { 255, 160, 40, 255 }, 3 },
        { "CHAIN REACTION",  "Destroy 5 rocks with one volatile blast",        { 255, 80, 60, 255 }, 4 },
        { "SHARPSHOOTER",    "Finish a wave with 80% accuracy or better",      { 110, 255, 120, 255 }, 5 },
        { "COURIER HUNTER",  "Destroy 25 Couriers",                            { 255, 215, 60, 255 }, 6 },
        { "MAX POWER",       "Reach Lv3 with any weapon",                      { 190, 110, 255, 255 }, 7 },
        { "ARSENAL",         "Use all five weapons in one game",               { 70, 220, 255, 255 }, 8 },
        { "WINGMAN",         "Call in the wingman drone",                      { 110, 255, 120, 255 }, 9 },
        { "FLAK DANCER",     "Shoot down 20 outpost flak shells",              { 255, 90, 50, 255 }, 10 },
        { "BLITZ",           "Destroy a boss in under 30 seconds",             { 255, 120, 200, 255 }, 11 },
        { "SECTOR 2",        "Make your first hyperspace jump",                { 120, 150, 255, 255 }, 12 },
        { "HALFWAY THERE",   "Defeat the 5th capital ship",                    { 170, 150, 255, 255 }, 13 },
        { "NO CONTINUES",    "Reach sector 6 without losing a ship",           { 80, 230, 200, 255 }, 14 },
        { "SAVIOR",          "Defeat all ten capital ships",                   { 255, 230, 140, 255 }, 15 } };
    return M;
}

void Game::unlockMedal(int id) {
    if (debugRun || medal[id]) return;              // debug runs never earn medals
    medal[id] = true; saveMedals();
    medalToasts.push_back(id); sectorMedals.push_back(id);
    playSfx(SFX_LIFE);
}

void Game::bossDefeated(const Boss& b) {
    slowT = SLOW_TIME; flashT = FLASH_TIME; rumbleFor(1.0f, 0.9f);
    if (weapon == W_PHOTON && wlevel == 0) unlockMedal(MD_PURIST);
    if (bossFightT < 30) unlockMedal(MD_BLITZ);
    if (shipsLostSector == 0) unlockMedal(MD_FLAWLESS);
    if (b.index == 4) unlockMedal(MD_HALFWAY);
    if (b.index == BOSS_COUNT - 1) unlockMedal(MD_SAVIOR);
    panelBoss = b.D().name; panelBossT = bossFightT;
}

void Game::openSectorPanel() {
    int shots = shotsFired - sectorShots0, hit = hits - sectorHits0;
    panelSector = (stage - 1) / CYCLE + 1;
    panelAcc = shots > 0 ? std::min(100, (int)lroundf(100.0f * hit / shots)) : 0;
    panelShips = shipsLostSector; panelOutposts = outpostsSector; panelTime = sectorT;
    panelBonus = 1000 * panelSector + (panelShips == 0 ? 2000 : 0) + (panelAcc >= 70 ? 1000 : 0) + 500 * panelOutposts;
    addScore(panelBonus);
    sectorPanel = true; sectorPanelT = 0;
    playSfx(SFX_STAGE);
}

void Game::rumbleFor(float strength, float secs) {
    if (!padOn()) return;
    rumble::set(0, strength, strength * 0.7f);
    rumbleT = std::max(rumbleT, secs);
}

void Game::tickRealTime(float dt) {
    slowT = std::max(0.0f, slowT - dt); flashT = std::max(0.0f, flashT - dt);
    if (rumbleT > 0 && (rumbleT -= dt) <= 0) rumble::set(0, 0, 0);
}

void Game::updateMedalsScreen() {
    if (IsKeyPressed(KEY_TAB) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) { medalsOpen = false; return; }
    int cx = medalSel % 4, cy = medalSel / 4;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_LEFT))   cx = (cx + 3) % 4;
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) cx = (cx + 1) % 4;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_UP))       cy = (cy + 3) % 4;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN))   cy = (cy + 1) % 4;
    medalSel = cy * 4 + cx;
    Vector2 m = mouseLogical(), md = GetMouseDelta();
    for (int i = 0; i < MD_COUNT; ++i) { Vector2 c = medalCell(i);
        if ((md.x != 0 || md.y != 0) && CheckCollisionPointRec(m, { c.x - 60, c.y - 52, 120, 106 })) medalSel = i; }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(m, MEDAL_PANEL)) medalsOpen = false;   // click outside closes
}

void Game::drawMedalGlyph(int g, Vector2 c, float s, Color col) {
    switch (g % 8) {
        case 0: DrawPoly(c, 5, s * 0.5f, -90, col); DrawPoly(c, 5, s * 0.5f, -54, col); break;                    // star
        case 1: DrawTriangle({ c.x - s * 0.45f, c.y - s * 0.35f }, { c.x, c.y + s * 0.55f }, { c.x + s * 0.45f, c.y - s * 0.35f }, col);
                DrawRectangleV({ c.x - s * 0.45f, c.y - s * 0.5f }, { s * 0.9f, s * 0.2f }, col); break;        // shield
        case 2: DrawRing(c, s * 0.3f, s * 0.42f, 0, 360, 24, col);                                                  // crosshair
                DrawLineEx({ c.x - s * 0.6f, c.y }, { c.x + s * 0.6f, c.y }, s * 0.1f, col); DrawLineEx({ c.x, c.y - s * 0.6f }, { c.x, c.y + s * 0.6f }, s * 0.1f, col); break;
        case 3: DrawPoly(c, 7, s * 0.45f, 12, col); break;                                                        // rock
        case 4: weaponIcon(4, c, s * 1.2f, col); break;                                                           // lightning
        case 5: weaponIcon(5, c, s * 1.2f, col); break;                                                           // chevrons
        case 6: DrawPoly(c, 4, s * 0.5f, 45, col); DrawPoly(c, 4, s * 0.25f, 45, Color{ 30, 30, 40, 255 }); break; // diamond
        default: { Vector2 p[5] = { { c.x - s * 0.5f, c.y + s * 0.3f }, { c.x - s * 0.5f, c.y - s * 0.3f }, { c.x - s * 0.15f, c.y },
                                    { c.x + s * 0.15f, c.y - s * 0.45f }, { c.x + s * 0.5f, c.y - s * 0.3f } };     // crown
                   DrawRectangleV({ c.x - s * 0.5f, c.y }, { s, s * 0.3f }, col); for (int i = 0; i < 4; ++i) DrawLineEx(p[i], p[i + 1], s * 0.14f, col); break; }
    }
}

void Game::drawMedal(int id, Vector2 c, float r, bool unlocked, float glowA) {
    const MedalDef& m = medalDefs()[id];
    Color rib1 = unlocked ? m.col : Color{ 60, 62, 75, 255 };
    Color rib2 = unlocked ? Color{ (unsigned char)(m.col.r / 2), (unsigned char)(m.col.g / 2), (unsigned char)(m.col.b / 2), 255 } : Color{ 45, 46, 56, 255 };
    DrawTriangle({ c.x - r * 0.7f, c.y - r * 1.6f }, { c.x - r * 0.15f, c.y - r * 0.6f }, { c.x - r * 0.05f, c.y - r * 1.6f }, rib1);
    DrawTriangle({ c.x + r * 0.05f, c.y - r * 1.6f }, { c.x + r * 0.15f, c.y - r * 0.6f }, { c.x + r * 0.7f, c.y - r * 1.6f }, rib2);
    if (unlocked && glowA > 0) { BeginBlendMode(BLEND_ADDITIVE); drawGlow(c, r * 2.2f, Color{ 255, 220, 120, (unsigned char)(120 * glowA) }); EndBlendMode(); }
    DrawCircleV(c, r, unlocked ? Color{ 150, 110, 30, 255 } : Color{ 50, 52, 62, 255 });
    DrawCircleV(c, r * 0.86f, unlocked ? Color{ 255, 205, 70, 255 } : Color{ 70, 72, 84, 255 });
    DrawCircleV(c, r * 0.68f, unlocked ? Color{ 40, 32, 18, 255 } : Color{ 38, 39, 48, 255 });
    if (unlocked) { drawMedalGlyph(m.glyph, c, r * 0.95f, m.col); DrawCircleV({ c.x - r * 0.45f, c.y - r * 0.5f }, r * 0.12f, Color{ 255, 250, 220, 200 }); }
}

void Game::drawMedalButton() {
    bool hover = CheckCollisionPointRec(mouseLogical(), MEDAL_BTN);
    DrawRectangleRounded(MEDAL_BTN, 0.3f, 8, hover ? Color{ 40, 44, 70, 230 } : Color{ 20, 22, 40, 200 });
    DrawRectangleRoundedLinesEx(MEDAL_BTN, 0.3f, 8, 1.5f, Color{ 255, 215, 60, (unsigned char)(hover ? 255 : 170) });
    drawMedal(MD_FLAWLESS, { MEDAL_BTN.x + 17, MEDAL_BTN.y + 21 }, 8, true);
}

void Game::drawOverlays() {
    if (flashT > 0) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{ 255, 255, 255, (unsigned char)(220 * flashT / FLASH_TIME) });
    Camera2D cam{ origin, { 0, 0 }, 0, scale };
    bool toast = !medalToasts.empty() && mode == PLAYING;
    float ty = toastY();
    float pa = std::min(1.0f, sectorPanelT / 0.4f); unsigned char PA = (unsigned char)(255 * pa);
    BeginMode2D(cam);
    if (toast) {
        Rectangle r{ W / 2.0f - 165, ty, 330, 56 };
        DrawRectangleRounded(r, 0.35f, 8, Color{ 30, 24, 8, 235 }); DrawRectangleRoundedLinesEx(r, 0.35f, 8, 1.8f, Color{ 255, 215, 60, 255 });
        drawMedal(medalToasts.front(), { r.x + 34, r.y + 34 }, 15, true, 0.8f + 0.2f * sinf(time * 6));
    }
    if (sectorPanel) {
        DrawRectangle(0, 0, W, H, Color{ 0, 0, 10, (unsigned char)(120 * pa) });
        Rectangle r{ W / 2.0f - 210, 190, 420, 352 };
        DrawRectangleRounded(r, 0.08f, 8, Color{ 14, 14, 32, (unsigned char)(235 * pa) });
        DrawRectangleRoundedLinesEx(r, 0.08f, 8, 2, Color{ 255, 140, 200, PA });
        int n = std::min(6, (int)sectorMedals.size());
        for (int i = 0; i < n; ++i) drawMedal(sectorMedals[i], { W / 2.0f - (n - 1) * 25.0f + i * 50.0f, r.y + 292 }, 14, true, 0.6f);
    }
    if (medalsOpen) {
        DrawRectangle(0, 0, W, H, Color{ 0, 0, 8, 215 });
        DrawRectangleRounded(MEDAL_PANEL, 0.05f, 8, Color{ 14, 14, 32, 245 });
        DrawRectangleRoundedLinesEx(MEDAL_PANEL, 0.05f, 8, 2, Color{ 255, 215, 60, 255 });
        for (int i = 0; i < MD_COUNT; ++i) { Vector2 c = medalCell(i);
            if (i == medalSel) DrawRectangleRounded({ c.x - 60, c.y - 52, 120, 106 }, 0.15f, 6, Color{ 255, 215, 60, 40 });
            drawMedal(i, c, 22, medal[i], i == medalSel ? 0.7f : 0); }
    }
    EndMode2D();
    if (toast) {
        text("MEDAL UNLOCKED", W / 2.0f - 112, ty + 9, 12, Color{ 255, 215, 60, 255 });
        text(medalDefs()[medalToasts.front()].name, W / 2.0f - 112, ty + 25, 20, RAYWHITE);
    }
    if (sectorPanel) {
        drawCentered(TextFormat("SECTOR %d CLEARED", panelSector), 206, 30, Color{ 255, 140, 200, PA });
        drawCentered(TextFormat("%s DESTROYED", panelBoss.c_str()), 244, 14, Color{ 200, 200, 220, PA });
        auto mmss = [](float s) { int t = (int)s; return TextFormat("%d:%02d", t / 60, t % 60); };
        const char* L[5] = { "Sector time", "Accuracy", "Ships lost", "Outposts destroyed", "Boss time" };
        std::string V[5] = { mmss(panelTime), TextFormat("%d%%", panelAcc), TextFormat("%d", panelShips), TextFormat("%d / 3", panelOutposts), mmss(panelBossT) };
        for (int i = 0; i < 5; ++i) { text(L[i], W / 2.0f - 170, 278 + i * 26.0f, 16, Color{ 170, 175, 200, PA });
                                      text(V[i].c_str(), W / 2.0f + 110, 278 + i * 26.0f, 16, Color{ 255, 255, 255, PA }); }
        text("SECTOR BONUS", W / 2.0f - 170, 412, 18, Color{ 255, 215, 60, PA });
        text(TextFormat("+%d", panelBonus), W / 2.0f + 90, 412, 18, Color{ 255, 215, 60, PA });
        drawCentered(sectorMedals.empty() ? "NO NEW MEDALS" : "MEDALS EARNED", 440, 11, Color{ 170, 175, 200, PA });
        drawCentered(TextFormat("%s%s  -  JUMP TO HYPERSPACE  (%d)", hint("SPACE", "TAP"), padOn() ? " / A" : "", std::max(1, (int)ceilf(PANEL_TIME - sectorPanelT))), 516, 12, Color{ 200, 200, 220, PA });
    }
    if (medalsOpen) {
        drawCentered(TextFormat("MEDALS   %d / %d", medalCount(), (int)MD_COUNT), 108, 26, Color{ 255, 215, 60, 255 });
        for (int i = 0; i < MD_COUNT; ++i) { Vector2 c = medalCell(i);
            if (!medal[i]) text("?", c.x - 5, c.y - 11, 21, Color{ 120, 122, 140, 255 });
            const char* nm = medalDefs()[i].name; float w = MeasureTextEx(font, nm, 11 * scale, 11 * scale * 0.04f).x / scale;
            text(nm, c.x - w / 2, c.y + 30, 11, medal[i] ? RAYWHITE : Color{ 110, 112, 130, 255 }); }
        const MedalDef& d = medalDefs()[medalSel];
        drawCentered(TextFormat("%s  -  %s", d.name, d.desc), 676, 12, medal[medalSel] ? Color{ 255, 230, 160, 255 } : Color{ 170, 172, 190, 255 });
        drawCentered(padOn() ? "TAB / ESC / B  close" : hint("TAB / ESC  close", "TAP OUTSIDE / BACK  close"), 694, 10, Color{ 120, 122, 140, 255 });
    }
}

void Game::drawStartPrompt() {
    if (!padOn()) { drawCentered(hint("PRESS SPACE TO START", "TAP TO START"), 584, 26, RAYWHITE); return; }
    float fs = 26 * scale, sp = customFont ? fs * 0.04f : fs / 10;
    float w1 = MeasureTextEx(font, "PRESS SPACE OR ", fs, sp).x / scale, w2 = MeasureTextEx(font, " TO START", fs, sp).x / scale, bw = 30;
    float x0 = W / 2.0f - (w1 + bw + w2) / 2;
    text("PRESS SPACE OR ", x0, 584, 26, RAYWHITE); text(" TO START", x0 + w1 + bw, 584, 26, RAYWHITE);
    BeginMode2D(Camera2D{ origin, { 0, 0 }, 0, scale });
    DrawCircleV({ x0 + w1 + bw / 2, 597 }, 14, Color{ 60, 170, 80, 255 }); DrawCircleLinesV({ x0 + w1 + bw / 2, 597 }, 14, Color{ 160, 240, 170, 255 });
    EndMode2D();
    text("A", x0 + w1 + bw / 2 - 7, 586, 20, RAYWHITE);
}

const Game::RosterEntry* Game::roster() {
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

void Game::rosterBossState(int& bi, int& bn, float& xf) const {
    float u = time / ROSTER_BOSS_PER, fu = u - floorf(u); bi = (int)u % 10; bn = (bi + 1) % 10;
    float t = std::clamp((fu - 0.8f) / 0.2f, 0.0f, 1.0f); xf = t * t * (3 - 2 * t);
}

void Game::toggleTitleMute() {
    titleMuted = !titleMuted;
    if (titleMuted) for (auto& s : titleFx) StopSound(s);
    saveSettings();
}

void Game::drawSpeakerButton(Rectangle r, bool muted, bool hover) {
    Color edge = muted ? Color{ 255, 90, 80, 255 } : Color{ 120, 200, 255, 255 };
    DrawRectangleRounded(r, 0.3f, 8, hover ? Color{ 40, 44, 70, 230 } : Color{ 20, 22, 40, 200 });
    DrawRectangleRoundedLinesEx(r, 0.3f, 8, 1.5f, Color{ edge.r, edge.g, edge.b, (unsigned char)(hover ? 255 : 170) });
    float k = r.width / 34.0f;
    Vector2 c{ r.x + 13 * k, r.y + r.height / 2 };
    Color ic = muted ? Color{ 170, 170, 185, 255 } : RAYWHITE;
    DrawRectangleRec({ c.x - 6 * k, c.y - 4 * k, 5 * k, 8 * k }, ic);                                     // speaker body
    DrawTriangle({ c.x - 1 * k, c.y - 4 * k }, { c.x - 1 * k, c.y + 4 * k }, { c.x + 5 * k, c.y + 9 * k }, ic);  // cone
    DrawTriangle({ c.x - 1 * k, c.y - 4 * k }, { c.x + 5 * k, c.y + 9 * k }, { c.x + 5 * k, c.y - 9 * k }, ic);
    if (muted) { Vector2 x0{ c.x + 13 * k, c.y };                                                          // red X
        DrawLineEx({ x0.x - 5 * k, x0.y - 5 * k }, { x0.x + 5 * k, x0.y + 5 * k }, 2.6f * k, Color{ 255, 90, 80, 255 });
        DrawLineEx({ x0.x - 5 * k, x0.y + 5 * k }, { x0.x + 5 * k, x0.y - 5 * k }, 2.6f * k, Color{ 255, 90, 80, 255 }); }
    else for (int q = 0; q < 2; ++q) {                                                                      // sound waves, gently pulsing
        float rr = (6.5f + q * 5.0f) * k; unsigned char a = (unsigned char)(150 + 100 * (0.5f + 0.5f * sinf(time * 4 - q)));
        DrawRing({ c.x + 3 * k, c.y }, rr, rr + 1.8f * k, -45, 45, 12, Color{ 120, 200, 255, a }); }
}

bool Game::onHudButton(Vector2 p) const {
    if (mode == TITLE) return CheckCollisionPointRec(p, grow(MUTE_BTN, 8)) || CheckCollisionPointRec(p, grow(MEDAL_BTN, 8));
    return gameMuteVisible() && (CheckCollisionPointRec(p, hudTopRect(GAME_MUTE_HIT)) || CheckCollisionPointRec(p, hudTopRect(GAME_PAUSE_HIT)));
}

void Game::drawGamePauseButton() {
    const Rectangle& r = GAME_PAUSE_BTN;
    DrawRectangleRounded(r, 0.3f, 8, Color{ 20, 22, 40, 200 });
    DrawRectangleRoundedLinesEx(r, 0.3f, 8, 1.5f, Color{ 120, 200, 255, 170 });
    float bw = r.width * 0.16f, bh = r.height * 0.46f, cy = r.y + (r.height - bh) / 2, cx = r.x + r.width / 2;
    DrawRectangleRec({ cx - bw * 1.6f, cy, bw, bh }, RAYWHITE);
    DrawRectangleRec({ cx + bw * 0.6f, cy, bw, bh }, RAYWHITE);
}

void Game::drawRosterShapes() {
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

void Game::drawRosterText() {
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

void Game::drawHudShapes() {
    if (mode == TITLE) {
        int f = fmodf(time, 0.5f) < 0.25f ? 0 : 1;
        if (stageDialog) { drawStageDialogShapes(); return; }
        drawTitleParticles();
        (void)f;
        drawRosterShapes();
        drawMuteButtonShapes();
        drawMedalButton();
        return;
    }
    bandShapes(hudBotDy, [&] { drawStatusBarShapes(); });
    bandShapes(hudTopDy, [&] { drawTopHudShapes(); });
    if (mode == GAMEOVER) DrawRectangle(0, H / 2 - 120, W, 240, Color{ 0, 0, 0, 170 });
    if (mode == VICTORY)  DrawRectangle(0, H / 2 - 170, W, 330, Color{ 0, 0, 0, 150 });
}

void Game::drawStatusBarShapes() {
    if (mode == PLAYING || mode == VICTORY || mode == GAMEOVER) drawStatusPanel();
    {   int spare = std::max(0, lives - (playerAlive ? 1 : 0));
        if (spare <= 4) for (int i = 0; i < spare; ++i) drawShip(gfx.player, { SB_LIVES_X + 15 + i * 28.0f, H - 21.0f }, 26, 0, false);
        else drawShip(gfx.player, { SB_LIVES_X + 15, H - 21.0f }, 26, 0, false);   // "x N" label drawn with the HUD text
    }
    if (mode == PLAYING || mode == VICTORY) drawWeaponIndicator();
    if (mode == PLAYING || mode == VICTORY || mode == GAMEOVER) drawCampaignBar();
    if (mode == PLAYING && playerAlive && !shield && shieldRegen > 0) {   // shield recharge bar under the label
        float f = 1 - shieldRegen / SHIELD_REGEN_TIME, bw = 76, x = SB_SHIELD_CX - bw / 2, y = H - 13.0f;
        DrawRectangleRounded({ x, y, bw, 3.5f }, 1, 6, Color{ 40, 30, 36, 220 });
        DrawRectangleRounded({ x, y, std::max(3.5f, bw * f), 3.5f }, 1, 6, Color{ (unsigned char)(140 - 30 * f), (unsigned char)(90 + 120 * f), (unsigned char)(90 + 165 * f), 255 });
    }
}

void Game::drawTopHudShapes() {
    if (gameMuteVisible()) { drawGameMuteButton(); if (TOUCH_UI && !escMenu) drawGamePauseButton(); }
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
}

void Game::textCenteredAt(const char* t, float cx, float y, float size, Color c) {
    float fs = size * scale, sp = customFont ? fs * 0.04f : fs / 10;
    text(t, cx - MeasureTextEx(font, t, fs, sp).x / scale / 2, y, size, c);
}

void Game::drawStatusPanel() {
    DrawRectangle(0, (int)SB_TOP, W, H - (int)SB_TOP, Color{ 6, 8, 20, 170 });
    DrawLineEx({ 0, SB_TOP }, { (float)W, SB_TOP }, 1, Color{ 80, 100, 150, 110 });
    for (float x : SB_SEP) DrawLineEx({ x, SB_TOP + 7 }, { x, H - 6.0f }, 1, Color{ 70, 80, 110, 110 });
}

void Game::drawCrest(Vector2 c, float r, Color fill, Color line) {
    Vector2 v[8] = { { c.x - r, c.y - r * 0.7f }, { c.x - r * 0.45f, c.y - r * 0.25f }, { c.x, c.y - r * 0.9f }, { c.x + r * 0.45f, c.y - r * 0.25f },
                     { c.x + r, c.y - r * 0.7f }, { c.x + r * 0.75f, c.y + r * 0.35f }, { c.x, c.y + r }, { c.x - r * 0.75f, c.y + r * 0.35f } };
    for (int i = 0; i < 8; ++i) { Vector2 a = v[i], b = v[(i + 1) % 8]; DrawTriangle(c, a, b, fill); DrawTriangle(c, b, a, fill); }
    for (int i = 0; i < 8; ++i) DrawLineEx(v[i], v[(i + 1) % 8], 1.0f, line);
}

void Game::drawCampaignBar() {
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

void Game::drawHudText() {
    if (mode == TITLE && stageDialog) { drawStageDialogText(); return; }
    if (mode == TITLE) {
        drawTitleText();
        drawRosterText();
        if (fmodf(time, 1.0f) < 0.6f) drawStartPrompt();
        drawCentered(hint("ARROWS / WASD  move    SPACE  fire    P / ESC  pause", "DRAG to move - hold to fire"), 640, 16, GRAY);
        drawCentered(hint("F11 / ALT+ENTER  fullscreen", "BACK or II  pause"), 664, 16, GRAY);
        drawCentered(TextFormat("HIGH SCORE  %d", hiScore), 710, 22, GOLD);
        if (!TOUCH_UI) drawCentered("CTRL+L  debug stage select", 760, 12, Color{ 90, 90, 110, 255 });
        return;
    }

    bandText(hudBotDy, [&] { drawStatusBarText(); });
    bandText(hudTopDy, [&] { drawTopHudText(); });

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
    if (mode == PLAYING && !playerAlive && lives > 0 && respawnTimer < 1.5f)
        drawCentered("READY", H / 2 + 30, 32, RED);
    if (paused) drawCentered("PAUSED", H / 2 - 20, 44, YELLOW);
    drawEndScreenText();
}

void Game::drawTopHudText() {
    text("1UP", 20, 10, 18, RED);
    text(TextFormat("%d", score), 20, 30, 22, RAYWHITE);
    drawCentered("HIGH SCORE", 10, 18, RED);
    drawCentered(TextFormat("%d", hiScore), 30, 22, RAYWHITE);
    if (boss.active && boss.state != B_ENTER)
        drawCentered(TextFormat("%s  -  %d / %d", boss.D().name, boss.index + 1, BOSS_COUNT), 58, 14, Color{ 255, 170, 210, 255 });
    if (debugRun && mode != TITLE) text("DEBUG", W - 112, 14, 14, Color{ 255, 200, 60, 255 });
    if (meteorField && mode == PLAYING) {
        drawCentered("METEOR FIELD", 58, 14, Color{ 230, 170, 110, 255 });
        if (debugRun) text(TextFormat("density %2.0f%%", density() * 100), W - 110, 32, 13, Color{ 255, 200, 60, 255 });
    }
}

void Game::drawStatusBarText() {
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
}

void Game::drawEndScreenText() {
    if (mode == VICTORY) {
        float bob = sinf(time * 2) * 5;
        drawCentered("VICTORY!", H / 2 - 150 + bob, 64, GOLD);
        drawCentered("all ten capital ships destroyed", H / 2 - 72, 20, Color{ 255, 230, 160, 255 });
        drawCentered("the galaxy is safe", H / 2 - 46, 18, Color{ 200, 220, 255, 255 });
        drawCentered(TextFormat("FINAL SCORE   %d", score), H / 2 - 6, 28, RAYWHITE);
        drawCentered(TextFormat("HIT RATIO   %.1f%%     LIVES LEFT   %d", shotsFired ? 100.0f * hits / shotsFired : 0.0f, lives), H / 2 + 34, 18, YELLOW);
        if (debugRun) drawCentered("(debug run - high score not recorded)", H / 2 + 62, 14, GRAY);
        if (stageTimer > 3.0f && fmodf(time, 1.0f) < 0.6f) drawCentered(hint("PRESS SPACE", "TAP TO CONTINUE"), H / 2 + 110, 24, RAYWHITE);
    }
    if (mode == GAMEOVER) {
        drawCentered("GAME OVER", H / 2 - 100, 52, RED);
        drawCentered(TextFormat("STAGE REACHED   %d", stage), H / 2 - 30, 22, RAYWHITE);
        drawCentered(TextFormat("SHOTS FIRED   %d", shotsFired), H / 2 + 0, 22, RAYWHITE);
        drawCentered(TextFormat("HIT RATIO   %.1f%%", shotsFired ? 100.0f * hits / shotsFired : 0.0f), H / 2 + 30, 22, YELLOW);
        if (stageTimer > 2.0f && fmodf(time, 1.0f) < 0.6f) drawCentered(hint("PRESS SPACE", "TAP TO CONTINUE"), H / 2 + 75, 24, RAYWHITE);
    }
}
