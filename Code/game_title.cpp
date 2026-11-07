// Star Swarm: the title-screen swarm, fonts and the loading screen.
#include "game.h"

Vector2 Game::bez3(Vector2 a, Vector2 b, Vector2 c, Vector2 d, float t) {
    float u = 1 - t;
    return { u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
             u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y };
}

std::vector<Game::SwarmMon> Game::buildSwarm(std::vector<std::pair<const char*, float>> lines, float yTop) {
    std::vector<SwarmMon> out; float y = yTop; int lineNo = 0;
    for (auto& [str, sz] : lines) {
        float sp = sz * 0.08f;
        Image im = ImageTextEx(swarmFont, str, sz, sp, WHITE);
        int iw = im.width, ih = im.height;
        Color* px = LoadImageColors(im);
        float step = sz > 88 ? 8.5f : 7.0f, x0 = W / 2.0f - iw / 2.0f;
        for (float yy = step / 2; yy < ih; yy += step)
            for (float xx = step / 2; xx < iw; xx += step) {
                // keep a grid point when enough of its cell is ink (not just the centre pixel), so thin strokes
                // such as the bar of an "e" or the dot of an "i" always get monsters
                int cov = 0, tot = 0;
                for (int cy = (int)(yy - step / 2); cy < (int)(yy + step / 2); ++cy)
                    for (int cx = (int)(xx - step / 2); cx < (int)(xx + step / 2); ++cx) {
                        if (cx < 0 || cy < 0 || cx >= iw || cy >= ih) continue;
                        ++tot; if (px[cy * iw + cx].a > 140) ++cov;
                    }
                if (tot == 0 || cov < 0.3f * tot) continue;
                SwarmMon m;
                m.tgt = { x0 + xx, y + yy };
                float side = m.tgt.x < W / 2 ? -1.f : 1.f;
                m.in0 = { W / 2 + side * (W / 2 + 60), frand(-60, 120) };
                m.in1 = { W / 2 - side * frand(60, 200), frand(330, 480) };
                m.in2 = { m.tgt.x + side * frand(40, 120), m.tgt.y + frand(80, 160) };
                float s2 = frand(0, 1) < 0.5f ? -1.f : 1.f;
                m.out1 = { m.tgt.x + s2 * frand(30, 70), m.tgt.y - frand(20, 50) };
                m.out2 = { m.tgt.x + s2 * frand(100, 180), m.tgt.y + frand(180, 300) };
                m.out3 = { m.tgt.x + s2 * frand(20, 90), H + 60.0f };
                m.dIn  = fabsf(m.tgt.x - W / 2) / (W / 2) * 0.9f + frand(0, 0.15f);   // outer letters fill first
                m.dOut = (m.tgt.x / W) * 0.7f + frand(0, 0.15f);                       // peel off left to right
                m.kind = lineNo == 0 ? (yy < ih * 0.45f ? Gfx::MON_JELLY : Gfx::MON_MANTA) : Gfx::MON_URCHIN;
                m.ph = frand(0, 1);
                out.push_back(m);
            }
        UnloadImageColors(px); UnloadImage(im);
        y += ih + 4; ++lineNo;
    }
    return out;
}

void Game::buildTitleParticles() {
    swarmTitles.clear();
    swarmFont = font;
    for (const char* f : { "C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                           "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/system/fonts/DroidSans-Bold.ttf", "/system/fonts/Roboto-Regular.ttf" })
        if (FileExists(f)) { Font ft = loadFontFile(f, 112); if (ft.texture.id) { swarmFont = ft; swarmFontOwn = true; break; } }
    swarmTitles.push_back({ nullptr, buildSwarm({ { "STAR", 92.0f }, { "SWARM", 92.0f } }, 70) });
    swarmTitles.push_back({ "DEVELOPED BY", buildSwarm({ { "GILAD", 86.0f }, { "GLOBEN", 86.0f } }, 90) });
    swarmTitles.push_back({ "DEDICATED TO", buildSwarm({ { "DANIEL", 86.0f }, { "GLOBEN", 86.0f } }, 90) });
}

void Game::drawTitleParticles() {
    if (swarmTitles.empty()) return;
    int ti = (int)(titleT / SW_CYCLE) % (int)swarmTitles.size();
    float t = fmodf(titleT, SW_CYCLE);
    const SwarmTitle& T = swarmTitles[ti];
    float breathe = 1 + 0.015f * sinf(time * 2.2f), sway = 6 * sinf(time * 1.1f);
    static std::vector<std::pair<Vector2, const SwarmMon*>> vis;
    vis.clear();
    for (const auto& m : T.mons) {
        if (t < m.dIn) continue;
        Vector2 home{ W / 2 + (m.tgt.x - W / 2) * breathe + sway, m.tgt.y };
        float uIn = swEase(0, SW_IN, t - m.dIn), uOut = Clamp((t - SW_OUT_AT - m.dOut) / SW_OUT, 0, 1);
        Vector2 p = uOut > 0 ? bez3(home, m.out1, m.out2, m.out3, uOut * uOut) : (uIn < 1 ? bez3(m.in0, m.in1, m.in2, home, uIn) : home);
        vis.push_back({ p, &m });
    }
    BeginBlendMode(BLEND_ADDITIVE);
    for (auto& [p, m] : vis) {
        Color gc = m->kind == Gfx::MON_JELLY ? Color{ 80, 230, 200, 85 } : m->kind == Gfx::MON_MANTA ? Color{ 120, 150, 255, 85 } : Color{ 255, 200, 80, 85 };
        drawGlow(p, 8, gc);
    }
    EndBlendMode();
    for (auto& [p, m] : vis) {
        int f = (int)(fmodf(time * 0.9f + m->ph, 1.f) * Gfx::MON_FRAMES) % Gfx::MON_FRAMES;
        const Texture2D& tx = gfx.mon[m->kind][f];
        float S = m->tgt.y > 0 && T.label ? (m->kind == Gfx::MON_URCHIN ? 10.5f : 11.5f) : (m->kind == Gfx::MON_URCHIN ? 12 : 13);
        DrawTexturePro(tx, { 0, 0, (float)tx.width, (float)tx.height }, { p.x, p.y, S, S }, { S / 2, S / 2 }, 0, WHITE);
    }
}

void Game::drawTitleText() {
    if (swarmTitles.empty()) return;
    int ti = (int)(titleT / SW_CYCLE) % (int)swarmTitles.size();
    float t = fmodf(titleT, SW_CYCLE);
    const char* label = swarmTitles[ti].label;
    if (!label) return;
    float a = swEase(1.2f, 2.0f, t) * (1 - swEase(4.8f, 5.4f, t));
    drawCentered(label, 62, 20, Color{ 170, 200, 255, (unsigned char)(255 * a) });
}

Font Game::loadFontFile(const char* f, int size) {
#ifdef __ANDROID__
    std::ifstream in(f, std::ios::binary);
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.empty()) return Font{};
    return LoadFontFromMemory(".ttf", d.data(), (int)d.size(), size, nullptr, 0);
#else
    return LoadFontEx(f, size, nullptr, 0);
#endif
}

void Game::loadFont() {
    const char* candidates[] = {
        "C:/Windows/Fonts/consolab.ttf", "C:/Windows/Fonts/lucon.ttf", "C:/Windows/Fonts/courbd.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/system/fonts/DroidSansMono.ttf", "/system/fonts/CutiveMono.ttf" };   // Android
    for (const char* f : candidates) {
        if (!FileExists(f)) continue;
        font = loadFontFile(f, 128);
        if (font.texture.id != 0) {
            GenTextureMipmaps(&font.texture);
            SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
            customFont = true;
            return;
        }
    }
    font = GetFontDefault();
}

void Game::drawLoading(float p, const char* status, bool firstRun, float dt) {
    computeView();
    for (auto& st : stars) { st.pos.y += st.speed * 0.35f * dt; if (st.pos.y > H) { st.pos.y = 0; st.pos.x = frand(0, W); } }
    loadT += dt;
    BeginDrawing();
    ClearBackground(BLACK);
    BeginScissorMode((int)origin.x, (int)origin.y, (int)ceilf(W * scale), (int)ceilf(H * scale));
    BeginMode2D(Camera2D{ origin, { 0, 0 }, 0, scale });
    DrawRectangle(0, 0, W, H, Color{ 4, 4, 14, 255 });
    for (auto& st : stars) DrawRectangleV(st.pos, { 1.6f, 1.6f }, Color{ 200, 215, 255, (unsigned char)(70 + 120 * (0.5f + 0.5f * sinf(loadT * 2 + st.phase))) });
    Rectangle bar{ W / 2.0f - 180, 572, 360, 12 };
    DrawRectangleRounded(bar, 1, 8, Color{ 25, 28, 50, 255 });
    if (p > 0.01f) {
        Rectangle fill{ bar.x, bar.y, bar.width * p, bar.height };
        DrawRectangleRounded(fill, 1, 8, Color{ 70, 200, 255, 255 });
        float sx = fmodf(loadT * 260, bar.width + 80) - 40;     // moving shine
        if (sx < fill.width) for (int i = 0; i < 30; ++i) { float x = bar.x + sx - 30 + i * 2; if (x >= bar.x && x <= bar.x + fill.width - 2)
            DrawRectangleV({ x, bar.y + 1 }, { 2, bar.height - 2 }, Color{ 255, 255, 255, (unsigned char)(i * 4) }); }
    }
    DrawRectangleRoundedLinesEx({ bar.x - 2, bar.y - 2, bar.width + 4, bar.height + 4 }, 1, 8, 1.2f, Color{ 90, 120, 180, 160 });
    EndMode2D();
    drawCentered("STAR", 190, 92, Color{ 90, 200, 200, 255 });
    drawCentered("SWARM", 290, 92, Color{ 230, 190, 90, 255 });
    drawCentered("Ten sectors. Ten capital ships. One pilot.", 405, 15, Color{ 150, 155, 185, 255 });
    bool busy = strcmp(status, "Ready") != 0;
    int dots = (int)(loadT / 0.3f) % 3 + 1;
    {   // centre on the text without the animated dots, so the line doesn't jitter
        float fs = 17 * scale, sp = customFont ? fs * 0.04f : fs / 10, w = MeasureTextEx(font, status, fs, sp).x / scale;
        text(busy ? TextFormat("%s%.*s", status, dots, "...") : status, W / 2.0f - w / 2, 540, 17, Color{ 200, 210, 235, 255 });
    }
    drawCentered(TextFormat("%d%%", (int)(p * 100)), 594, 13, Color{ 140, 150, 180, 255 });
    if (firstRun) drawCentered("First launch: building graphics and music. Later launches are much faster.", 640, 11, Color{ 110, 115, 145, 255 });
    drawCentered("DEVELOPED BY GILAD GLOBEN", 760, 11, Color{ 90, 95, 125, 255 });
    EndScissorMode();
    EndDrawing();
}
