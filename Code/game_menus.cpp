// Star Swarm: the stage-select dialog and the Esc menu.
#include "game.h"

void Game::startFromDialog() {
    if (!dlgValid()) return;
    stageDialog = false; debugRun = true;
    newGame(dlgValue());
}

void Game::updateStageDialog() {
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

void Game::drawButton(Rectangle r, bool hot, Color base) {
    DrawRectangleRounded(r, 0.3f, 8, hot ? Color{ (unsigned char)std::min(255, base.r + 40), (unsigned char)std::min(255, base.g + 40), (unsigned char)std::min(255, base.b + 40), 255 } : base);
    DrawRectangleRoundedLinesEx(r, 0.3f, 8, 1.5f, Color{ 255, 255, 255, 90 });
}

void Game::drawStageDialogShapes() {
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

void Game::drawStageDialogText() {
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

void Game::returnToTitle() {
    enemies.clear(); pBullets.clear(); eBullets.clear(); parts.clear(); rings.clear(); warnings.clear();
    packages.clear(); zaps.clear(); meteors.clear(); popups.clear(); pendingSplits.clear();
    boss = Boss{}; meteorField = false; shake = 0; hyperT = -1;
    droneOn = false; laserOn = false; heat = 0; overheated = false; stopLaserLoop();
    playerAlive = false; invuln = 0; shield = false; bannerTimer = 0; stageTimer = 0;
    mode = TITLE; titleT = 0; paused = false; titleFxPrevT = -1;
    if (titleMusicOk) StopMusicStream(titleMusic);   // restarts from the top (in sync with the title cycle), fading in
}

void Game::escChoose(int i) {
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

void Game::updateEscMenu() {
    if (gameMuteVisible() && (IsKeyPressed(KEY_M) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_UP) || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseLogical(), hudTopRect(TOUCH_UI ? GAME_MUTE_HIT : GAME_MUTE_BTN))))){
        toggleGameMusicMute(); return;   // music mute stays usable while the menu is open
    }
    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) { escMenu = false; return; }   // B = resume
    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) { escChoose(escSel); return; }   // A = confirm
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsKeyPressedRepeat(KEY_UP) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_UP))       escSel = (escSel + ESC_N - 1) % ESC_N;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsKeyPressedRepeat(KEY_DOWN) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN)) escSel = (escSel + 1) % ESC_N;
    for (int i = 0; i < ESC_N; ++i) if (IsKeyPressed((KeyboardKey)(KEY_ONE + i)) || IsKeyPressed((KeyboardKey)(KEY_KP_1 + i))) { escChoose(i); return; }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) { escChoose(escSel); return; }
    Vector2 m = mouseLogical();
    Vector2 md = GetMouseDelta();
    for (int i = 0; i < ESC_N; ++i) if (CheckCollisionPointRec(m, escBtn(i))) {
        if (md.x != 0 || md.y != 0) escSel = i;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { escChoose(i); return; }
    }
}

void Game::drawEscMenuShapes() {
    DrawRectangle(0, 0, W, H, Color{ 0, 0, 0, 170 });
    if (bandTop > 0) bandShapes(0, [&] {                   // dim the HUD in the bands too
        DrawRectangleRec({ 0, -bandTop, (float)W, bandTop }, Color{ 0, 0, 0, 170 });
        DrawRectangleRec({ 0, (float)H, (float)W, bandBot }, Color{ 0, 0, 0, 170 });
    }, true);
    Rectangle box = escBox();
    DrawRectangleRounded(box, 0.07f, 10, Color{ 18, 20, 44, 245 });
    DrawRectangleRoundedLinesEx(box, 0.07f, 10, 2, Color{ 90, 180, 255, 200 });
    Color base[ESC_N] = { { 30, 110, 70, 255 }, { 60, 70, 120, 255 }, { 110, 40, 50, 255 } };
    for (int i = 0; i < ESC_N; ++i) {
        drawButton(escBtn(i), i == escSel, base[i]);
        if (i == escSel) DrawRectangleRoundedLinesEx(escBtn(i), 0.3f, 8, 2.5f, Color{ 255, 230, 120, 230 });
    }
}

void Game::drawEscMenuText() {
    Rectangle box = escBox();
    drawCentered("PAUSED", box.y + 22, 30, YELLOW);
    const char* lbl[ESC_N] = { "RESUME GAME", "END CURRENT GAME", hint("EXIT TO DESKTOP", "EXIT GAME") };
    float fs = 20 * scale, sp = customFont ? fs * 0.04f : fs / 10;
    for (int i = 0; i < ESC_N; ++i) {   // number and label in fixed left-aligned columns
        Rectangle r = escBtn(i);
        float y = origin.y + (r.y + r.height / 2) * scale - MeasureTextEx(font, "0", fs, sp).y / 2;
        DrawTextEx(font, TextFormat("%d", i + 1), { origin.x + (r.x + 30) * scale, y }, fs, sp, Color{ 255, 230, 120, 255 });
        DrawTextEx(font, lbl[i], { origin.x + (r.x + 72) * scale, y }, fs, sp, RAYWHITE);
    }
    drawCentered(hint("UP/DOWN select    ENTER confirm    ESC resume", "TAP to choose    BACK resume"), box.y + box.height - 30, 13, GRAY);
}
