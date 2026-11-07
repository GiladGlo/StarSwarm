// Star Swarm: the player ship and the hyperspace jump.
#include "game.h"

void Game::drawPlayerShip() {
    const float S = 50;
    float w = warpT > 0 ? 1 - warpT / WARP_TIME : 1;         // warp-in progress 0..1
    Vector2 pos = player;
    if (warpT > 0) {
        BeginBlendMode(BLEND_ADDITIVE);
        for (int k = 0; k < 14; ++k) {
            float a = k * 0.4488f, r0 = (1 - w) * 110 + 8, len = 22 * (1 - w) + 4;
            Vector2 d{ cosf(a), sinf(a) };
            DrawLineEx({ pos.x + d.x * r0, pos.y + d.y * r0 * 0.6f }, { pos.x + d.x * (r0 + len), pos.y + d.y * (r0 + len) * 0.6f },
                       1.6f, Color{ 140, 200, 255, (unsigned char)(220 * w) });
        }
        drawGlow(pos, 14 + 40 * w, Color{ 140, 200, 255, (unsigned char)(160 * w) });
        if (w > 0.55f) drawGlow(pos, S * 0.9f * (1 - (w - 0.55f) / 0.45f), Color{ 255, 255, 255, 200 });
        EndBlendMode();
    }
    float fade = warpT > 0 ? std::max(0.0f, (w - 0.6f) / 0.4f) : 1;
    if (fade <= 0) return;
    unsigned char fa = (unsigned char)(255 * fade);
    float cr = cosf(roll * DEG2RAD);
    BeginBlendMode(BLEND_ADDITIVE);
    for (float sx : { -1.f, 1.f }) {                          // engine plumes
        Vector2 n{ pos.x + sx * 6 * cr, pos.y + 20 };
        float fl = 0.85f + 0.1f * sinf(time * 50 + sx * 3) + 0.05f * sinf(time * 83);
        float L = S * 0.42f * fl;
        DrawTriangle({ n.x - 2.5f, n.y }, { n.x, n.y + L }, { n.x + 2.5f, n.y }, Color{ 60, 130, 255, (unsigned char)(130 * fade) });
        DrawTriangle({ n.x - 1.4f, n.y }, { n.x, n.y + L * 0.6f }, { n.x + 1.4f, n.y }, Color{ 220, 240, 255, (unsigned char)(210 * fade) });
        for (int k = 1; k <= 3; ++k)                           // shock diamonds
            drawGlow({ n.x, n.y + L * 0.17f * k }, S * (0.05f - k * 0.008f) * (1 + 0.15f * sinf(time * 40 + k)), Color{ 200, 230, 255, (unsigned char)(200 * fade) });
        drawGlow({ n.x, n.y + 2 }, S * 0.13f * fl, Color{ 90, 160, 255, (unsigned char)(170 * fade) });
    }
    EndBlendMode();
    // banked sprite: cross-fade the two nearest roll frames
    float fi = (roll + 30) / 15.0f; int i0 = std::clamp((int)fi, 0, 3); float a = Clamp(fi - i0, 0, 1);
    drawShip(gfx.playerRoll[i0], pos, S, 0, true, Color{ 255, 255, 255, fa });
    if (a > 0.02f) drawShip(gfx.playerRoll[i0 + 1], pos, S, 0, false, Color{ 255, 255, 255, (unsigned char)(fa * a) });
    BeginBlendMode(BLEND_ADDITIVE);
    auto bump = [](float x, float wd) { return expf(-(x / wd) * (x / wd)); };
    float bt = fmodf(time, 1.2f), on = bump(bt - 0.05f, 0.04f) + bump(bt - 0.22f, 0.04f);
    if (on > 0.02f) {                                         // nav strobes: red port, green starboard
        drawGlow({ pos.x - 20.5f * cr, pos.y + 1.5f }, S * 0.07f * on, Color{ 255, 60, 60, (unsigned char)(230 * fade) });
        drawGlow({ pos.x + 20.5f * cr, pos.y + 1.5f }, S * 0.07f * on, Color{ 80, 255, 120, (unsigned char)(230 * fade) });
    }
    float won = bump(fmodf(time + 0.6f, 1.8f), 0.03f);       // white tail strobe
    if (won > 0.02f) drawGlow({ pos.x, pos.y + 15.5f }, S * 0.09f * won, Color{ 255, 255, 255, (unsigned char)(230 * fade) });
    float gt = fmodf(time, 3.0f);                             // canopy glint
    if (gt < 0.5f) { float u = gt / 0.5f;
        drawGlow({ pos.x - 0.5f, pos.y + (-0.46f + 0.36f * u) * S / 2 }, S * 0.05f, Color{ 255, 255, 255, (unsigned char)(200 * sinf(u * PI) * fade) }); }
    if (muzzleT > 0) {                                        // gun-muzzle flashes
        float k = muzzleT / 0.07f;
        for (float sx : { -1.f, 1.f }) { Vector2 m{ pos.x + sx * 12.5f * cr, pos.y - 10 };
            drawGlow(m, S * 0.09f * k, Color{ 255, 170, 90, 220 }); drawGlow(m, S * 0.035f * k, Color{ 255, 240, 220, 255 }); }
    }
    EndBlendMode();
}

void Game::startHyper() {
    hyperT = 0; hyperSwitched = false; hyperStart = player;
    unlockMedal(MD_SECTOR2);
    if (droneOn) {                                   // the drone does not make the jump: it warps away
        Vector2 d = Vector2Add(player, droneOff);
        parts.push_back({ d, { 0, 0 }, 0.35f, 0.35f, 30, Color{ 140, 255, 170, 255 }, FLASH });
        droneOn = false;
    }
    eBullets.clear(); pBullets.clear(); warnings.clear();
    hstars.clear();
    for (int i = 0; i < 420; ++i) hstars.push_back({ frand(-900, 900), frand(-1100, 1100), frand(-250, 4000) });
    playSfx(SFX_SHIELD_UP);
}

void Game::updateHyper(float dt) {
    float prev = hyperT; hyperT += dt;
    float st = hyperStreak(), speed = 2600 * st * st + 1;
    for (auto& q : hstars) { q.s -= speed * dt; if (q.s < -260) { q.s = frand(3000, 4000); q.lx = frand(-900, 900); q.ly = frand(-1100, 1100); } }
    if (prev < 2.0f && hyperT >= 2.0f) playSfx(SFX_DIVE);
    if (prev < 2.9f && hyperT >= 2.9f) { playSfx(SFX_BIGBOOM); shake = 0.3f; }
    if (!hyperSwitched && hyperT >= HYPER_SWITCH) {          // out the other side: next stage, ship warps in
        hyperSwitched = true;
        player = { W / 2.0f, PLAYER_Y_LOW }; prevPX = player.x; roll = 0; warpT = WARP_TIME; invuln = 1.2f;
        nextStage();
    }
    if (hyperT >= HYPER_END) hyperT = -1;
}

void Game::drawHyper() {
    float t = hyperT, open = hyperOpen(), st = hyperStreak(), dive = hs(2.0f, 2.8f, t), R = 120 * open;
    // streaks: stars rushing toward the viewer along -D, trailing back toward the vanishing point
    if (st > 0.01f) {
        float len = 40 + 900 * st * st;
        for (auto& q : hstars) {
            Vector3 a{ q.lx + HYP_D.x * q.s, q.ly + HYP_D.y * q.s, HYP_D.z * q.s }, b{ q.lx + HYP_D.x * (q.s + len), q.ly + HYP_D.y * (q.s + len), HYP_D.z * (q.s + len) };
            float k; Vector2 pa = hproj(a, &k), pb = hproj(b);
            float al = Clamp(1 - q.s / 4000.f, 0, 1) * st;
            DrawLineEx(pa, pb, Clamp(k, 0.8f, 3.f), Color{ 200, 220, 255, (unsigned char)(230 * al) });
        }
    }
    const int NS = 160;
    auto rim = [&](bool front) {
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < NS; ++i) { float th = i * 2 * PI / NS; if ((sinf(th) > 0) != front) continue;
            float k; Vector2 p = hproj(hring(R, th), &k); float nf = 0.7f + 0.3f * sinf(th);
            drawGlow(p, (13 + 5 * sinf(th)) * k * open, Color{ 110, 170, 255, (unsigned char)(90 * open * nf) });
            drawGlow(p, 4.5f * k * open, Color{ 220, 240, 255, (unsigned char)(190 * open * nf) }); }
        for (int j = 0; j < 3; ++j) { float a0 = t * 3.2f + j * 2.094f;
            for (float a = a0; a < a0 + 0.9f; a += 0.05f) { if ((sinf(a) > 0) != front) continue;
                float k; Vector2 p = hproj(hring(R * 1.07f, a), &k); drawGlow(p, 6 * k, Color{ 200, 230, 255, (unsigned char)(170 * open) }); } }
        EndBlendMode(); };
    if (open > 0.001f) {
        BeginBlendMode(BLEND_ADDITIVE); drawGlow(HYP_PC, 240 * open, Color{ 60, 100, 255, (unsigned char)(70 * open) }); EndBlendMode();
        rim(false);
        Vector2 c = hproj({ 0, 0, 0 }), prevP = hproj(hring(R, 0));
        for (int i = 1; i <= NS; ++i) { Vector2 q = hproj(hring(R, i * 2 * PI / NS));
            DrawTriangle(c, q, prevP, Color{ 8, 12, 45, 255 }); DrawTriangle(c, prevP, q, Color{ 8, 12, 45, 255 }); prevP = q; }
        BeginBlendMode(BLEND_ADDITIVE);
        float L = 6 * R;
        for (int ring = 0; ring < 9; ++ring) { float u = fmodf(ring / 9.f + t * 0.7f, 1.f), d = u * u * L;
            Vector2 pv = hproj(hring(R, 0, d));
            for (int i = 1; i <= 64; ++i) { Vector2 q = hproj(hring(R, i * 2 * PI / 64, d)); DrawLineEx(pv, q, 1.3f, Color{ 110, 160, 255, (unsigned char)(150 * (1 - u) * open) }); pv = q; } }
        for (int i = 0; i < 64; ++i) { float th = i * 2.3999f, u = fmodf(i * 0.137f + t * 1.2f, 1.f), u2 = std::min(1.f, u + 0.15f);
            DrawLineEx(hproj(hring(R, th, u * u * L)), hproj(hring(R, th, u2 * u2 * L)), 1.1f + 1.4f * (1 - u),
                       Color{ 150, 200, 255, (unsigned char)(190 * (1 - u) * open) }); }
        Vector2 vp = hproj({ HYP_D.x * 1e5f, HYP_D.y * 1e5f, HYP_D.z * 1e5f });
        drawGlow(vp, 36 * open, Color{ 200, 225, 255, (unsigned char)(200 * open) }); drawGlow(vp, 12 * open, Color{ 255, 255, 255, (unsigned char)(255 * open) });
        EndBlendMode();
    }
    if (t > 0.3f && t < 0.9f) {                                  // the tear before it opens
        float u = hs(0.3f, 0.6f, t) * (1 - hs(0.7f, 0.9f, t));
        BeginBlendMode(BLEND_ADDITIVE);
        DrawRectangleV({ HYP_PC.x - 130 * u, HYP_PC.y - 1.5f }, { 260 * u, 3 }, Color{ 230, 240, 255, (unsigned char)(255 * u) });
        drawGlow(HYP_PC, 60 * u, Color{ 140, 190, 255, (unsigned char)(200 * u) });
        EndBlendMode();
    }
    if (!hyperSwitched) {                                        // the ship: line up, reach the mouth, dive down the tunnel
        Vector2 pos = Vector2Lerp(hyperStart, { W / 2.0f, 640 }, hs(0, 0.7f, t));
        float sc = 1, alpha = 1;
        if (dive > 0) {
            float ap = hs(0, 0.45f, dive);
            if (ap < 1) pos = Vector2Lerp(pos, hproj({ 0, 0, 0 }), ap);
            else { float d = hs(0.35f, 1.0f, dive); d = d * d * 700; float k; pos = hproj({ HYP_D.x * d, HYP_D.y * d, HYP_D.z * d }, &k); sc = k; }
            alpha = 1 - hs(0.8f, 1.0f, dive);
        }
        if (alpha > 0.01f) {
            float S = 50 * sc, charge = hs(1.0f, 2.0f, t);
            BeginBlendMode(BLEND_ADDITIVE);
            for (float sx : { -1.f, 1.f }) {
                Vector2 n{ pos.x + sx * 6 * sc, pos.y + 20 * sc };
                float L2 = (22 + 60 * charge + 50 * dive) * sc * (0.9f + 0.1f * sinf(time * 50 + sx));
                DrawTriangle({ n.x - 2.5f * sc, n.y }, { n.x, n.y + L2 }, { n.x + 2.5f * sc, n.y }, Color{ 60, 130, 255, (unsigned char)(170 * alpha) });
                DrawTriangle({ n.x - 1.4f * sc, n.y }, { n.x, n.y + L2 * 0.6f }, { n.x + 1.4f * sc, n.y }, Color{ 230, 245, 255, (unsigned char)(220 * alpha) });
                drawGlow({ n.x, n.y + 3 }, (10 + 8 * charge) * sc, Color{ 90, 160, 255, (unsigned char)(190 * alpha) });
            }
            EndBlendMode();
            drawShip(gfx.player, pos, S, 0, dive <= 0, Color{ 255, 255, 255, (unsigned char)(255 * alpha) });
        }
    }
    if (open > 0.001f) rim(true);
    float cl = (t - 2.85f) / 0.6f;                               // collapse: flash + shockwave
    if (cl > 0 && cl < 1) {
        BeginBlendMode(BLEND_ADDITIVE);
        drawGlow(HYP_PC, 60 + 260 * cl, Color{ 200, 230, 255, (unsigned char)(200 * (1 - cl)) });
        for (int i = 0; i < 96; ++i) drawGlow(hproj(hring(40 + 320 * cl, i * 2 * PI / 96)), 5, Color{ 170, 210, 255, (unsigned char)(150 * (1 - cl)) });
        EndBlendMode();
    }
    float wf = hs(2.9f, 3.05f, t) * (1 - hs(3.05f, 3.6f, t));
    if (wf > 0) DrawRectangle(0, 0, W, H, Color{ 230, 240, 255, (unsigned char)(140 * wf) });
}
