// Star Swarm: screen layout and the main draw pass.
#include "game.h"

void Game::computeView() {
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    scale  = std::min(sw / W, sh / H);
    origin = { (sw - W * scale) / 2, (sh - H * scale) / 2 };
    bandTop = bandBot = std::max(0.0f, (sh / scale - H) / 2);
    hudTopDy = -std::min(bandTop, TOP_HUD_H);
    hudBotDy = std::min(bandBot, BOTTOM_HUD_H);
}

void Game::text(const char* t, float x, float y, float size, Color c, bool centered, float dx) {
    float fs = size * scale, sp = customFont ? fs * 0.04f : fs / 10;
    if (centered) x = W / 2.0f - MeasureTextEx(font, t, fs, sp).x / scale / 2 + dx;
    DrawTextEx(font, t, { origin.x + x * scale, origin.y + (y + hudDy) * scale }, fs, sp, c);
}

void Game::drawBandBackground(const Theme& th) {
    float y0 = -bandTop, y1 = H + bandBot;
    beginScreenClip();
    BeginMode2D(Camera2D{ origin, { 0, 0 }, 0, scale });
    DrawRectangleRec({ 0, y0, (float)W, -y0 }, th.top);
    DrawRectangleRec({ 0, (float)H, (float)W, y1 - H }, th.bottom);
    float scroll = fmodf(time * 6.0f, (float)H), k = gfx.nebula.height / (float)H;
    DrawTexturePro(gfx.nebula, { 0, (y0 - scroll) * k, (float)gfx.nebula.width, (y1 - y0) * k }, { 0, y0, (float)W, y1 - y0 }, { 0, 0 }, 0, th.neb);
    BeginBlendMode(BLEND_ADDITIVE);
    float starFade = 1 - hyperStreak();
    for (auto& s : stars)
        for (float off : { -(float)H, (float)H }) {
            float y = s.pos.y + off;
            if (y < y0 || y > y1) continue;
            float tw = 0.5f + 0.5f * sinf(time * 3 + s.phase);
            Color c = s.col; c.a = (unsigned char)(std::min(255.0f, 60 + 195 * tw * (s.speed / 150.0f)) * starFade);
            float sz = s.speed > 100 ? 1.4f : 0.9f;
            DrawRectangleV({ s.pos.x - sz / 2, y - sz / 2 }, { sz, sz * (mode == PLAYING ? 2.2f : 1.0f) }, c);
        }
    EndBlendMode();
    DrawRectangleGradientV(0, (int)y0, W, (int)(bandTop * 0.6f), Color{ 0, 0, 0, 120 }, Color{ 0, 0, 0, 0 });   // soft fade at the
    DrawRectangleGradientV(0, (int)(y1 - bandBot * 0.6f), W, (int)(bandBot * 0.6f) + 1, Color{ 0, 0, 0, 0 }, Color{ 0, 0, 0, 120 });   // screen edges
    EndMode2D();
    EndScissorMode();
}

void Game::draw() {
    computeView();
    BeginDrawing();
    ClearBackground(BLACK);   // letterbox bars
    const Theme& th = THEMES[mode == TITLE ? 0 : cycleIndex()];
    if (bandTop > 0) drawBandBackground(th);
    beginPlayfieldClip();

    Camera2D cam{}; cam.zoom = scale; cam.offset = origin;
    if (shake > 0) cam.offset = Vector2Add(origin, { frand(-6, 6) * shake * scale, frand(-6, 6) * shake * scale });
    viewCam = cam;
    BeginMode2D(cam);
    DrawRectangleGradientV(0, 0, W, H, th.top, th.bottom);
    if (mode != TITLE) {   // distant body for this cycle, drifting very slowly
        int ci = cycleIndex();
        const Texture2D& pt = planetTex(ci);
        float drift = fmodf(cycleT * 2.5f, H * 0.6f);
        Vector2 pp{ th.px * W, th.py * H + drift };
        if (th.style == 4 || th.style == 2 || th.style == 5) {
            BeginBlendMode(BLEND_ADDITIVE);
            Color halo = th.style == 5 ? Color{ 255, 120, 60, 50 } : Color{ 255, 220, 150, (unsigned char)(th.style == 4 ? 90 : 60) };
            drawGlow(pp, th.size * 0.9f, halo);
            EndBlendMode();
        }
        DrawTexturePro(pt, { 0, 0, (float)pt.width, (float)pt.height }, { pp.x, pp.y, th.size, th.size }, { th.size / 2, th.size / 2 }, 0,
                       Color{ 200, 200, 210, 165 });   // a little dimmer so enemies stand out against it
    }
    {   // slowly scrolling nebula (tileable texture), tinted per cycle
        float scroll = fmodf(time * 6.0f, (float)H);
        Rectangle src{ 0, -scroll * gfx.nebula.height / (float)H, (float)gfx.nebula.width, (float)gfx.nebula.height };
        DrawTexturePro(gfx.nebula, src, { 0, 0, (float)W, (float)H }, { 0, 0 }, 0, th.neb);
    }

    BeginBlendMode(BLEND_ADDITIVE);
    float starFade = 1 - hyperStreak();
    for (auto& s : stars) {
        float tw = 0.5f + 0.5f * sinf(time * 3 + s.phase);
        Color c = s.col; c.a = (unsigned char)(std::min(255.0f, 60 + 195 * tw * (s.speed / 150.0f)) * starFade);
        float sz = s.speed > 100 ? 1.4f : 0.9f;
        DrawRectangleV({ s.pos.x - sz / 2, s.pos.y - sz / 2 }, { sz, sz * (mode == PLAYING ? 2.2f : 1.0f) }, c);
        if (s.speed > 130) drawGlow(s.pos, 3.5f, Color{ c.r, c.g, c.b, (unsigned char)(c.a / 3) });
    }
    EndBlendMode();

    if (hyperT >= 0) drawHyper();

    // smoke under ships
    for (auto& p : parts) if (p.kind == SMOKE) {
        float t = p.life / p.maxLife;
        drawGlow(p.pos, p.size * (1.6f - 0.6f * t), Color{ p.col.r, p.col.g, p.col.b, (unsigned char)(110 * t) });
    }

    drawMeteors();
    drawBoss();

    for (auto& e : enemies) {
        if (e.state == DEAD || e.state == WAITING) continue;
        // each monster loops its own animation, offset per formation slot so a wave never moves in lockstep;
        // divers animate faster
        float off = spr::hash2(e.sx * 3.1f + 7, e.sy * 5.7f + 3);
        auto ph = [&](float loopsPerSec) { return time * loopsPerSec * (e.state == DIVING ? 1.8f : 1.0f) + off; };
        const int MF = Gfx::MON_FRAMES;
        float g = 0.3f + 0.7f * e.grow;
        if (e.grow < 1) {                  // engine burst while emerging from the hatch
            BeginBlendMode(BLEND_ADDITIVE);
            drawGlow(e.pos, 30 * (1 - e.grow) + 8, Color{ 120, 200, 255, (unsigned char)(200 * (1 - e.grow)) });
            EndBlendMode();
        }
        switch (e.type) {
            case BEE:       drawMonster(gfx.mon[Gfx::MON_URCHIN], MF, ph(0.55f), e.pos, 42 * g, e.angle); break;
            case BUTTERFLY: drawMonster(gfx.mon[Gfx::MON_MANTA], MF, ph(0.9f), e.pos, 46 * g, e.angle); break;
            case BOSS:      drawMonster(gfx.mon[e.hp < 2 ? Gfx::MON_JELLY_HIT : Gfx::MON_JELLY], MF, ph(0.45f), e.pos, 56 * g, e.angle); break;
            case DART: {
                if (e.state == DIVING) {        // comet trail while striking
                    BeginBlendMode(BLEND_ADDITIVE);
                    Vector2 v = Vector2Normalize(Vector2Subtract(e.pos, e.prev));
                    for (int k = 1; k <= 5; ++k) drawGlow(Vector2Subtract(e.pos, Vector2Scale(v, 8.0f * k)), 11 - k * 1.6f,
                                                          k < 3 ? Color{ 255, 120, 200, (unsigned char)(190 - k * 30) } : Color{ 120, 150, 255, (unsigned char)(170 - k * 28) });
                    EndBlendMode();
                }
                drawMonster(gfx.mon[Gfx::MON_IMP], MF, ph(1.3f), e.pos, 40 * g, e.angle); break;
            }
            case SHIELDED:
                drawMonster(gfx.mon[Gfx::MON_BRUTE], MF, ph(0.35f), e.pos, 44 * g, e.angle);
                if (e.hp > 1) {                 // projected energy shield
                    float pl = 0.6f + 0.4f * sinf(time * 5 + e.sx);
                    BeginBlendMode(BLEND_ADDITIVE);
                    drawGlow(e.pos, 30, Color{ 60, 220, 255, (unsigned char)(40 + 30 * pl) });
                    DrawRing(e.pos, 22.5f, 24.5f, 0, 360, 36, Color{ 110, 255, 240, (unsigned char)(120 + 80 * pl) });
                    DrawPolyLinesEx(e.pos, 6, 23, time * 40, 1.0f, Color{ 150, 255, 245, (unsigned char)(70 * pl) });
                    EndBlendMode();
                }
                break;
            case SPLITTER: drawMonster(gfx.mon[Gfx::MON_LEVIATHAN], MF, ph(0.5f), e.pos, 46 * g, e.angle); break;
            case COURIER: {
                float S = 62 * g;
                drawShip(gfx.courier[e.armourOff ? 1 : 0], e.pos, S, e.angle);
                Color c = e.cargo >= 0 ? WCOL[e.cargo] : UPG_COL;          // cargo pod glows the colour of what it carries
                Vector2 pc{ e.pos.x, e.pos.y - S * 0.01f };
                float rr = S * 0.075f, pl = 0.75f + 0.25f * sinf(time * 5);
                DrawCircleV(pc, rr * 1.05f, Color{ c.r, c.g, c.b, 235 });
                BeginBlendMode(BLEND_ADDITIVE);
                drawGlow(pc, rr * 3.4f * pl, Color{ c.r, c.g, c.b, 200 });
                drawGlow(pc, rr * 0.9f, Color{ 255, 250, 235, 230 });
                EndBlendMode();
                if (e.hp < e.maxHp) {                                         // health bar
                    float f = (float)e.hp / e.maxHp;
                    DrawRectangle((int)(e.pos.x - 24), (int)(e.pos.y - 38), 48, 5, Color{ 30, 25, 20, 220 });
                    DrawRectangle((int)(e.pos.x - 24), (int)(e.pos.y - 38), (int)(48 * f), 5, c);
                }
                break;
            }
            case MINI:     drawMonster(gfx.mon[Gfx::MON_SPAWN], MF, ph(0.9f), e.pos, 26 * g, e.angle); break;
        }
    }

    if ((mode == PLAYING || mode == VICTORY) && playerAlive && !(hyperT >= 0 && !hyperSwitched)) {
        bool blink = invuln > 0 && fmodf(time, 0.2f) < 0.1f;
        if (!blink) drawPlayerShip();
        drawDrone();
        if (shield) drawShield();
    }

    BeginBlendMode(BLEND_ADDITIVE);
    for (auto& p : parts) if (p.kind == TRAIL) {
        float t = p.life / p.maxLife;
        Color c = p.col; c.a = (unsigned char)(200 * t);
        drawGlow(p.pos, p.size * (0.6f + 0.4f * t), c);
    }
    for (auto& b : pBullets) {
        Vector2 dv = Vector2Length(b.vel) > 1 ? Vector2Normalize(b.vel) : Vector2{ 0, -1 };
        if (b.kind == PB_PHOTON) {         // photon torpedo
            float pulse = 1 + 0.18f * sinf(time * 38 + b.pos.x);
            float spin = time * 540 + b.pos.x * 3;
            for (int k = 1; k <= 6; ++k)    // comet tail
                drawGlow({ b.pos.x, b.pos.y + k * 6.0f }, 10 - k * 1.2f, Color{ 255, 100, 40, (unsigned char)(150 - k * 20) });
            drawGlow(b.pos, 30 * pulse, Color{ 255, 60, 25, 130 });
            drawGlow(b.pos, 17 * pulse, Color{ 255, 120, 50, 230 });
            drawGlow(b.pos, 8, Color{ 255, 210, 150, 255 });
            for (int k = 0; k < 2; ++k) {   // rotating star-flare spikes
                float a = spin + k * 90;
                DrawRectanglePro({ b.pos.x, b.pos.y, 30 * pulse, 1.6f }, { 15 * pulse, 0.8f }, a, Color{ 255, 170, 110, 170 });
                DrawRectanglePro({ b.pos.x, b.pos.y, 16, 1.0f }, { 8, 0.5f }, a + 45, Color{ 255, 230, 200, 140 });
            }
            DrawCircleV(b.pos, 3.2f, WHITE);
        } else if (b.kind == PB_SPREAD) {  // spread plasma bolt (shrinks and fades as it burns out)
            float k0 = Clamp((SPREAD_LIFE - b.age) / 0.15f, 0, 1), sc = 0.4f + 0.6f * k0;
            auto A = [&](int a) { return (unsigned char)(a * k0); };
            drawGlow(b.pos, 18 * sc, Color{ 255, 150, 40, A(210) });
            for (int k = 1; k <= 3; ++k) drawGlow(Vector2Subtract(b.pos, Vector2Scale(dv, k * 7.0f * sc)), (9 - k * 2.0f) * sc, Color{ 255, 140, 40, A(170 - k * 40) });
            DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 8 * sc)), Vector2Add(b.pos, Vector2Scale(dv, 6 * sc)), 5 * sc, Color{ 255, 220, 120, A(255) });
            DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 5 * sc)), Vector2Add(b.pos, Vector2Scale(dv, 5 * sc)), 2.2f * sc, Color{ 255, 255, 255, A(255) });
        } else if (b.kind == PB_HOMING || b.kind == PB_DRONE) {  // homing missile (body drawn here; trail from particles)
            drawGlow(Vector2Subtract(b.pos, Vector2Scale(dv, 7)), 12, Color{ 160, 255, 140, 230 });
            float ang = atan2f(dv.y, dv.x) * RAD2DEG;
            DrawRectanglePro({ b.pos.x, b.pos.y, 15, 4.5f }, { 7.5f, 2.25f }, ang, Color{ 215, 245, 215, 255 });
            DrawCircleV(Vector2Add(b.pos, Vector2Scale(dv, 7)), 2.4f, WCOL[W_HOMING]);
        } else if (b.kind == PB_ION) {     // ion orb with crackle
            drawGlow(b.pos, 32, Color{ 170, 90, 255, 180 });
            drawGlow(b.pos, 16, Color{ 210, 150, 255, 255 });
            DrawCircleV(b.pos, 5, WHITE);
        }
    }
    for (auto& b : pBullets) if (b.kind == PB_ION) {
        unsigned sd = (unsigned)(b.pos.y * 13 + b.pos.x) + (unsigned)(time * 20);
        for (int i = 0; i < 3; ++i) { float a = (sd % 628) / 100.0f + i * 2.1f;
            drawZap(b.pos, { b.pos.x + cosf(a) * 16, b.pos.y + sinf(a) * 16 }, sd + i, WCOL[W_ION], 1, 220); }
    }
    if (laserOn && playerAlive) {           // pulse laser beam
        static const float wid[4] = { 4.5f, 6, 8, 11 };
        float w = wid[wlevel], x = player.x, y0 = player.y - 26, y1 = laserTop;
        for (float y = y1; y < y0; y += 6) {
            float ww = w * (1 + 0.12f * sinf(y * 0.09f + time * 30));
            DrawRectangleV({ x - ww * 1.6f, y }, { ww * 3.2f, 6.5f }, Color{ 40, 180, 255, 45 });
            DrawRectangleV({ x - ww / 2, y }, { ww, 6.5f }, Color{ 70, 220, 255, 200 });
            DrawRectangleV({ x - ww * 0.18f, y }, { ww * 0.36f, 6.5f }, Color{ 230, 255, 255, 255 });
        }
        drawGlow({ x, y0 }, w * 2.4f, Color{ 70, 220, 255, 230 });
        if (y1 > 0) drawGlow({ x, y1 }, w * 2.8f, Color{ 120, 240, 255, 230 });
    }
    for (auto& z : zaps) {
        EndBlendMode();
        drawZap(z.a, z.b, z.seed, WCOL[W_ION], 2.2f, (unsigned char)(255 * std::min(1.0f, z.life / 0.15f)));
        BeginBlendMode(BLEND_ADDITIVE);
    }
    for (auto& b : eBullets) {
        Vector2 dv = Vector2Length(b.vel) > 1 ? Vector2Normalize(b.vel) : Vector2{ 0, 1 };
        if (b.kind == BK_ORB) {            // PLASMA FAN: big pulsing orb with a short comet tail
            float pl = 1 + 0.18f * sinf(time * 30 + b.pos.x);
            Color c = coreColor(170);
            for (int k = 1; k <= 3; ++k)
                drawGlow(Vector2Subtract(b.pos, Vector2Scale(dv, k * 7.0f)), 16 - k * 3.0f, Color{ c.r, c.g, c.b, (unsigned char)(200 - k * 50) });
            drawGlow(b.pos, 32 * pl, Color{ c.r, c.g, c.b, 210 });
            drawGlow(b.pos, 15, Color{ c.r, c.g, c.b, 255 });
            drawGlow(b.pos, 8, Color{ 255, 240, 250, 255 });
            DrawCircleV(b.pos, 4.2f * pl, WHITE);
        } else if (b.kind == BK_BOLT) {    // HUNTER VOLLEY: long tracer bolt along its flight path
            Vector2 tail = Vector2Subtract(b.pos, Vector2Scale(dv, 18)), tip = Vector2Add(b.pos, Vector2Scale(dv, 5));
            drawGlow(b.pos, 22, Color{ 255, 170, 60, 200 });
            DrawLineEx(tail, tip, 7, Color{ 255, 170, 60, 200 });
            DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 12)), tip, 2.6f, Color{ 255, 250, 220, 255 });
        } else if (b.kind == BK_SPIRAL) {  // VORTEX SPIRAL: hue-shifting orb (ribbon trail comes from particles)
            Color c = spiralColor(b.aux);
            drawGlow(b.pos, 26, Color{ c.r, c.g, c.b, 210 });
            drawGlow(b.pos, 11, Color{ c.r, c.g, c.b, 255 });
            DrawCircleV(b.pos, 3.2f, WHITE);
        } else if (b.kind == BK_CRYSTAL) { // NOVA RING: spinning diamond crystal
            Color c = coreColor();
            float rot = b.age * 360 + b.pos.x;
            drawGlow(b.pos, 26, Color{ c.r, c.g, c.b, 190 });
            DrawPoly(b.pos, 4, 9, rot, Color{ c.r, c.g, c.b, 240 });
            DrawPoly(b.pos, 4, 4, rot, WHITE);
        } else if (b.kind == BK_FLAK) {    // OUTPOST FLAK: slow spiky red shell (shootable)
            float rot = b.age * 300, pl = 0.85f + 0.15f * sinf(time * 25 + b.pos.x);
            drawGlow(b.pos, 20 * pl, Color{ 255, 70, 40, 200 });
            DrawPoly(b.pos, 4, 8, rot, Color{ 255, 120, 60, 255 });
            DrawPoly(b.pos, 4, 8, rot + 45, Color{ 255, 90, 50, 255 });
            DrawCircleV(b.pos, 3.4f, Color{ 255, 240, 200, 255 });
        } else if (b.kind == BK_MISSILE) { // SEEKER: engine flame (body drawn below, unblended)
            float fl = 0.8f + 0.2f * sinf(time * 60 + b.pos.x);
            Vector2 back = Vector2Subtract(b.pos, Vector2Scale(dv, 9));
            drawGlow(back, 22 * fl, Color{ 255, 130, 40, 230 });
            DrawLineEx(back, Vector2Subtract(back, Vector2Scale(dv, 20 * fl)), 6, Color{ 255, 190, 80, 230 });
            DrawLineEx(back, Vector2Subtract(back, Vector2Scale(dv, 10 * fl)), 2.5f, Color{ 255, 250, 220, 255 });
            if (fmodf(time + b.pos.x * 0.01f, 0.25f) < 0.12f) drawGlow(Vector2Add(b.pos, Vector2Scale(dv, 12)), 12, Color{ 255, 40, 40, 255 });
        } else if (b.kind == BK_C_SPREAD) {   // courier: basic spread bolt
            drawGlow(b.pos, 14, Color{ 255, 150, 40, 200 });
            DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 6)), Vector2Add(b.pos, Vector2Scale(dv, 4)), 4, Color{ 255, 220, 120, 255 });
        } else if (b.kind == BK_C_PULSE) {    // courier: short laser burst
            drawGlow(b.pos, 14, Color{ 70, 220, 255, 200 });
            DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 22)), b.pos, 4, Color{ 70, 220, 255, 230 });
            DrawLineEx(Vector2Subtract(b.pos, Vector2Scale(dv, 18)), b.pos, 1.6f, WHITE);
        } else if (b.kind == BK_C_HOMING) {   // courier: slow seeker
            drawGlow(b.pos, 14, Color{ 110, 255, 120, 200 });
            DrawRectanglePro({ b.pos.x, b.pos.y, 13, 4 }, { 6.5f, 2 }, atan2f(dv.y, dv.x) * RAD2DEG, Color{ 215, 245, 215, 255 });
        } else if (b.kind == BK_C_ION) {      // courier: slow ion orb
            drawGlow(b.pos, 24, Color{ 170, 90, 255, 180 });
            drawGlow(b.pos, 10, Color{ 230, 200, 255, 255 });
            DrawCircleV(b.pos, 3.2f, WHITE);
        } else if (b.kind == BK_C_PHOTON) {   // courier: basic torpedo
            drawGlow(b.pos, 18, Color{ 255, 90, 40, 170 });
            drawGlow(b.pos, 8, Color{ 255, 210, 150, 255 });
            DrawCircleV(b.pos, 2.6f, WHITE);
        } else if (b.kind == BK_METEOR) {  // METEOR STORM: fiery entry tail (rock drawn below)
            drawGlow(b.pos, 28, Color{ 255, 100, 30, 200 });
            for (int k = 1; k <= 6; ++k)
                drawGlow(Vector2Subtract(b.pos, Vector2Scale(dv, k * 8.0f)), 18 - k * 2.4f, Color{ 255, 150, 50, (unsigned char)(220 - k * 32) });
        } else {
            drawGlow(b.pos, 11, Color{ 255, 60, 80, 170 });
            drawGlow(b.pos, 4.5f, Color{ 255, 220, 220, 255 });
        }
    }
    for (auto& r : rings) {
        float t = 1 - r.life / r.maxLife;
        Color c = r.col; c.a = (unsigned char)(200 * (1 - t));
        DrawRing(r.pos, r.radius * t, r.radius * t + 3 * (1 - t) + 1, 0, 360, 48, c);
    }
    for (auto& p : parts) {
        float t = p.life / p.maxLife;
        if (p.kind == SPARK) {
            Color c = p.col; c.a = (unsigned char)(255 * t);
            drawGlow(p.pos, p.size * 3, c);
            DrawCircleV(p.pos, p.size * 0.45f, Color{ 255, 255, 255, (unsigned char)(255 * t) });
        } else if (p.kind == FLASH) {
            Color c = p.col; c.a = (unsigned char)(255 * t);
            drawGlow(p.pos, p.size * (1.3f - 0.5f * t), c);
        } else if (p.kind == EMBER) {
            Color c = p.col; c.a = (unsigned char)(200 * t);
            drawGlow(p.pos, p.size, c);
        }
    }
    EndBlendMode();
    for (auto& b : eBullets) {
        if (b.kind == BK_MISSILE) {
            float ang = atan2f(b.vel.y, b.vel.x) * RAD2DEG;
            Vector2 dv = Vector2Normalize(b.vel), pv{ -dv.y, dv.x };
            Vector2 tailC = Vector2Subtract(b.pos, Vector2Scale(dv, 6));
            for (int s2 : { -1, 1 }) {      // fins
                Vector2 a1 = Vector2Add(tailC, Vector2Scale(pv, s2 * 3.0f)), a2 = Vector2Add(Vector2Subtract(tailC, Vector2Scale(dv, 6)), Vector2Scale(pv, s2 * 9.0f)),
                        a3 = Vector2Add(Vector2Add(tailC, Vector2Scale(dv, 5)), Vector2Scale(pv, s2 * 3.0f));
                DrawTriangle(a1, a2, a3, Color{ 170, 45, 45, 255 }); DrawTriangle(a1, a3, a2, Color{ 170, 45, 45, 255 });
            }
            DrawRectanglePro({ b.pos.x, b.pos.y, 24, 7 }, { 12, 3.5f }, ang, Color{ 190, 190, 205, 255 });
            DrawRectanglePro({ b.pos.x, b.pos.y, 24, 2.5f }, { 12, 3.0f }, ang, Color{ 245, 245, 255, 255 });
            DrawCircleV(Vector2Add(b.pos, Vector2Scale(dv, 12)), 3.5f, Color{ 200, 50, 50, 255 });
        } else if (b.kind == BK_METEOR) {
            int v = std::clamp((int)b.aux, 0, 5);
            m3d::draw(gfx.meteor[v], b.age * (v % 2 ? 160 : -130), b.pos, (24 + v * 1.5f) * 1.08f, (float)(v * 25));
        }
    }
    if (playerAlive && mode == PLAYING) {   // missile lock-on reticle
        bool locked = false;
        for (auto& b : eBullets) if (b.kind == BK_MISSILE && b.age < 2.2f) { locked = true; break; }
        if (locked) {
            float r = 34 + 3 * sinf(time * 12), rot = time * 90;
            for (int k = 0; k < 4; ++k) {
                float a0 = (rot + k * 90) * DEG2RAD;
                Vector2 c{ player.x + cosf(a0) * r, player.y + sinf(a0) * r };
                DrawRing(player, r - 1.5f, r + 1.5f, rot + k * 90 - 14, rot + k * 90 + 14, 8, Color{ 255, 60, 60, 220 });
                DrawCircleV(c, 1.8f, Color{ 255, 120, 120, 255 });
            }
        }
    }
    for (auto& w : warnings) {               // meteor-storm entry markers
        bool on = fmodf(w.t, 0.12f) < 0.07f;
        Color c{ 255, 60, 50, (unsigned char)(on ? 255 : 120) };
        DrawTriangle({ w.x - 8, 4 }, { w.x, 18 }, { w.x + 8, 4 }, c);
        DrawRectangle((int)w.x - 1, 20, 2, 6, c);
    }
    for (auto& p : parts) if (p.kind == DEBRIS || p.kind == HULL) {
        float t = p.life / p.maxLife;
        Color c = p.col; c.a = (unsigned char)(255 * (p.kind == HULL ? std::min(1.0f, t * 3) : t));
        DrawRectanglePro({ p.pos.x, p.pos.y, p.size * 1.8f, p.size * 0.8f }, { p.size * 0.9f, p.size * 0.4f }, p.rot, c);
    }
    for (auto& pk : packages) drawPackage(pk.content, pk.pos, 44, pk.t);
    drawHudShapes();
    if (escMenu) { drawEscMenuShapes(); if (gameMuteVisible()) bandShapes(hudTopDy, [&] { drawGameMuteButton(); }); }   // mute button stays on top of the menu dimming
    EndMode2D();

    if (escMenu) { drawEscMenuText(); EndScissorMode(); EndDrawing(); return; }
    for (auto& p : popups) text(p.text.c_str(), p.pos.x - 16, p.pos.y, 18, SKYBLUE);
    drawHudText();
    drawOverlays();
    EndScissorMode();
    EndDrawing();
}

void Game::drawShield() {
    float g = shieldGrow, ease = 1 - (1 - g) * (1 - g) * (1 - g);
    float r = SHIELD_RADIUS * (0.4f + 0.6f * ease) * (1 + 0.025f * sinf(time * 6));
    float pulse = 0.75f + 0.25f * sinf(time * 4.3f);
    unsigned char a = (unsigned char)(255 * std::min(1.0f, g * 1.5f));
    auto A = [&](float f) { return (unsigned char)(a * std::min(1.0f, f)); };
    BeginBlendMode(BLEND_ADDITIVE);
    drawGlow(player, r * 1.25f, Color{ 40, 140, 255, A(0.22f * pulse) });           // soft inner fill
    DrawRing(player, r - 1.5f, r + 1.5f, 0, 360, 64, Color{ 90, 200, 255, A(0.85f * pulse) });  // rim
    DrawRing(player, r - 5, r - 1.5f, 0, 360, 64, Color{ 60, 150, 255, A(0.25f) });
    // rotating energy arcs
    for (int k = 0; k < 3; ++k) {
        float st = time * 120 + k * 120;
        DrawRing(player, r + 0.5f, r + 3.0f, st, st + 50, 16, Color{ 200, 245, 255, A(0.7f) });
    }
    // hex lattice clipped to the bubble
    float hs = 9.0f, off = fmodf(time * 6, hs * 1.732f);
    for (int row = -5; row <= 5; ++row)
        for (int col = -5; col <= 5; ++col) {
            Vector2 c{ player.x + col * hs * 1.5f, player.y + row * hs * 1.732f + (col & 1) * hs * 0.866f - off };
            float d = Vector2Distance(c, player);
            if (d > r - hs * 0.6f) continue;
            float edge = d / r;   // lattice is brighter near the rim, like a curved surface
            DrawPolyLinesEx(c, 6, hs * 0.95f, 0, 1.0f, Color{ 120, 210, 255, A(0.08f + 0.35f * edge * edge) });
        }
    // specular highlight
    DrawRing(player, r - 6.5f, r - 3.5f, 200, 255, 24, Color{ 230, 250, 255, A(0.55f) });   // crescent glint
    if (g < 1) DrawRing(player, r, r + 10 * (1 - g), 0, 360, 64, Color{ 200, 245, 255, A(1 - g) });  // power-up flash
    EndBlendMode();
}
