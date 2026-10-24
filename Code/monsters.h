// monsters.h - space-monster enemies (regular waves). Procedural SDF parts rendered by spr::render (HD lighting).
// Each monster is rendered as an 8-frame looping animation (ph = 0..1) and cross-faded at runtime.
// Enemies face DOWN (+y = toward the player). ph = animation phase 0..1 (loops seamlessly).
#pragma once
namespace spr {

// ---- shared helpers -----------------------------------------------------------------
// Sinuous tapered tentacle from base along dir; returns distance; tOut = position along it (0..1) of the nearest segment.
inline float sdTentacle(Vector2 p, Vector2 base, Vector2 dir, float len, float r0, float r1, float amp, float freq, float phase,
                        float curl, float* tOut = nullptr) {
    Vector2 nrm{ -dir.y, dir.x }, prev = base; float d = 1e9f, bt = 0;
    const int SEG = 12;
    for (int i = 1; i <= SEG; ++i) {
        float t = (float)i / SEG;
        Vector2 q = Vector2Add(Vector2Add(base, Vector2Scale(dir, len * t)), Vector2Scale(nrm, amp * sinf(t * freq + phase) * t + curl * t * t));
        float ra = r0 + (r1 - r0) * (t - 1.0f / SEG), rb = r0 + (r1 - r0) * t;
        float di = sdCapsule(p, prev, q, ra, rb);
        if (di < d) { d = di; bt = t; }
        prev = q;
    }
    if (tOut) *tOut = bt;
    return d;
}
// Glowing monster eye: radial iris, vertical slit pupil, wet highlight.
inline void eyePat(Vector2 p, Vector2 c, float R, Vector3 inner, Vector3 outer, Vector3& col, float& h, float& e, float slit = 0.16f) {
    Vector2 q = Vector2Subtract(p, c); float r = Vector2Length(q) / R;
    col = mix(inner, outer, sstep(0.0f, 1.0f, r));
    e = 0.85f * (1 - r) * (1 - r) + 0.12f;
    float ring = fabsf(sinf(atan2f(q.y, q.x) * 14 + r * 6)) * 0.12f;   // iris fibres
    col = Vector3Scale(col, 0.9f + ring);
    if (r > 0.8f) { col = Vector3Scale(outer, 0.25f); e = 0.05f; }     // dark limbal ring
    float w = R * slit * sqrtf(std::max(0.f, 1 - (q.y / R) * (q.y / R)));
    if (fabsf(q.x) < w && r < 0.8f) { col = rgb(12, 4, 8); e = 0; h -= 0.01f; }
    float hl = Vector2Distance(q, { -0.34f * R, -0.36f * R }) / (0.2f * R);
    if (hl < 1) { col = mix(col, { 1, 1, 1 }, 0.85f * (1 - hl * hl)); e = std::max(e, 0.6f * (1 - hl)); h += 0.01f; }
}
// Faint stars inside a cosmic skin.
inline void cosmicSpeckle(Vector2 p, Vector3& col, float& e, float dens = 0.982f, float scale = 70) {
    float gx = floorf(p.x * scale), gy = floorf(p.y * scale);
    float hsh = hash2(gx, gy);
    if (hsh > dens) {
        float fx = p.x * scale - gx - 0.5f, fy = p.y * scale - gy - 0.5f;
        float s = 1 - sqrtf(fx * fx + fy * fy) * 2.2f;
        if (s > 0) { col = mix(col, rgb(230, 235, 255), s); e = std::max(e, 1.4f * s); }
    }
}

// ======================================== BEE  ->  NOVA URCHIN ========================================
// NOVA URCHIN: golden spiny orb, spines ripple in a wave around it, one amber eye in a glowing core.
inline Texture2D novaUrchin(int N, float ph) {
    Vector3 shell = rgb(200, 140, 40), spine = rgb(255, 200, 80), tip = rgb(255, 245, 190);
    const int S = 22;
    std::vector<Part> P;
    for (int i = 0; i < S; ++i) {
        float a = i * 2 * PI / S + 0.07f * sinf(ph * 2 * PI + i * 0.9f);
        float len = 0.5f + 0.1f * hash2((float)i, 2) + 0.08f * sinf(ph * 2 * PI - i * 2 * PI * 3 / S);   // travelling ripple
        Vector2 dir{ cosf(a), sinf(a) };
        P.push_back({ [=](Vector2 p) { return sdCapsule(p, Vector2Scale(dir, 0.22f), Vector2Scale(dir, 0.22f + len), 0.045f, 0.006f); },
                      spine, 0.12f, 0.08f, 0.04f, 0.3f, 0.9f, 0.15f, 0.95f,
                      [=](Vector2 p, float, Vector3& c, float& h, float& e) { float t = Clamp((Vector2Length(p) - 0.22f) / len, 0, 1);
                          c = mix(Vector3Scale(spine, 0.6f), tip, t); e = 0.15f + 0.9f * t * t; (void)h; } });
    }
    P.push_back({ [=](Vector2 p) { return sdCircle(p, { 0, 0 }, 0.34f); }, shell, 0.2f, 0.35f, 0.25f, 0.4f, 0.8f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      float r = Vector2Length(p) / 0.34f;
                      c = mix(rgb(255, 210, 90), shell, r);
                      // tubercle bumps with glowing pores
                      float a = atan2f(p.y, p.x); float ring = r * 5, fr = ring - floorf(ring); float sec = a / (2 * PI) * (8 + floorf(ring) * 4); float fs = sec - floorf(sec);
                      float bd = Vector2Length({ (fs - 0.5f) * 0.6f, fr - 0.5f });
                      if (bd < 0.25f) { h += 0.02f * (1 - bd / 0.25f); c = mix(c, rgb(255, 240, 170), 0.5f); if (bd < 0.1f) e = 0.6f; }
                      c = Vector3Scale(c, 0.85f + 0.25f * fbm({ p.x * 12, p.y * 12 }, 3));
                      (void)d;
                  } });
    float blink = 0.15f + 0.05f * sinf(ph * 2 * PI);
    P.push_back({ [=](Vector2 p) { return sdCircle(p, { 0, 0.02f }, 0.15f); }, rgb(255, 160, 40), 0.5f, 0.08f, 0.05f, 0.1f, 1.0f, 0.6f, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float& e) { eyePat(p, { 0, 0.02f }, 0.15f, rgb(255, 225, 100), rgb(215, 70, 20), c, h, e, blink); } });
    return render(N, P, rgb(255, 210, 120), nullptr, true);
}

// ================================= BUTTERFLY  ->  VOID MANTA =====================================
// Star-speckled manta with crescent horn tips, perforated wings and a toothed maw; wings beat.
inline Texture2D voidManta(int N, float ph) {
    Vector3 skin = rgb(52, 40, 120), edge = rgb(120, 190, 255), maw = rgb(200, 90, 255), tooth = rgb(235, 220, 255);
    float wb = 0.5f - 0.5f * cosf(ph * 2 * PI), up = -0.14f * wb, span = 0.96f - 0.07f * wb;   // wing beat
    auto wing = [=](Vector2 p) {
        Vector2 q = M(p);
        float d = sdPoly(q, { { 0.08f, -0.22f }, { 0.45f, -0.32f + up * 0.5f }, { span, -0.62f + up }, { span - 0.12f, -0.36f + up },
                              { 0.62f, -0.02f + up * 0.4f }, { 0.36f, 0.22f }, { 0.12f, 0.36f } }) - 0.02f;
        float horn = std::max(sdCircle(q, { span - 0.22f, -0.42f + up }, 0.24f), -sdCircle(q, { span - 0.34f, -0.36f + up }, 0.22f));   // crescent tip
        d = std::min(d, horn);
        for (Vector2 h : { Vector2{ 0.42f, -0.1f }, Vector2{ 0.58f, -0.24f }, Vector2{ 0.3f, 0.08f }, Vector2{ 0.7f, -0.4f } })   // perforations
            d = std::max(d, -sdEllipse(q, { h.x, h.y + up * 0.4f * (h.x / 0.7f) }, { 0.055f, 0.032f }, 25));
        return d; };
    std::vector<Part> P;
    P.push_back({ wing, skin, 0.1f, 0.16f, 0.12f, 0.2f, 0.8f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      Vector2 q = M(p);
                      c = mix(skin, rgb(24, 18, 60), 0.5f * fbm({ p.x * 6, p.y * 6 }, 4));
                      // vein ridges radiating from the body
                      float v = fabsf(sinf(atan2f(q.y + 0.2f, q.x) * 9));
                      if (v < 0.08f) { h += 0.015f; c = mix(c, edge, 0.35f); }
                      cosmicSpeckle(p, c, e);
                      float rim = sstep(-0.06f, 0.0f, d);                                   // luminous edges
                      c = mix(c, edge, rim * 0.6f); e = std::max(e, rim * 0.45f);
                  } });
    // body ridge
    P.push_back({ [=](Vector2 p) { return smin(sdEllipse(p, { 0, -0.02f }, { 0.2f, 0.44f }), sdPoly(M(p), { { 0, -0.62f }, { 0.06f, -0.3f }, { 0, -0.3f } }), 0.05f); },
                  rgb(70, 60, 160), 0.22f, 0.22f, 0.12f, 0.2f, 0.85f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      Vector2 q = M(p);
                      c = mix(rgb(90, 80, 190), rgb(40, 30, 100), sstep(0.0f, 0.18f, q.x));
                      if (q.x < 0.012f && p.y < 0.1f) { c = edge; e = 0.45f; }                  // glowing spine
                      if (fabsf(q.x - 0.1f) < 0.01f && p.y < 0.2f) { c = Vector3Scale(c, 0.5f); h -= 0.01f; }
                      cosmicSpeckle(p, c, e, 0.975f, 80);
                      if (d > -0.02f) c = mix(c, edge, 0.4f);
                  } });
    // maw
    P.push_back({ [=](Vector2 p) { return sdEllipse(p, { 0, 0.22f }, { 0.13f, 0.17f + 0.035f * wb }); },
                  maw, 0.4f, 0.05f, 0.04f, 0.1f, 0.9f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      Vector2 q{ p.x, p.y - 0.22f };
                      float edgeD = -d;
                      float side = fabsf(q.x) / 0.13f;
                      float f = (q.y + 0.2f) * 22; f -= floorf(f);
                      float tl = (1 - fabsf(f - 0.5f) * 2) * 0.05f;
                      if (edgeD < tl + 0.008f) { c = tooth; e = 0.15f; h += 0.02f; }             // teeth lining the maw
                      else { float g = 1 - side; c = mix(rgb(70, 10, 110), maw, g); e = 0.4f + 1.4f * g * g; h -= 0.03f; }
                  } });
    return render(N, P, rgb(110, 130, 230), nullptr, true);
}

// ======================================= BOSS  ->  NEBULA JELLY ======================================
// Translucent green-teal bell with one great eye and a ring of small ones; trailing tentacles sway.
// damaged: bruised violet, cracked bell, half the small eyes shut.
inline Texture2D nebulaJelly(int N, float aph, bool damaged) {
    Vector3 bell = damaged ? rgb(120, 70, 160) : rgb(60, 200, 170), bell2 = damaged ? rgb(60, 30, 90) : rgb(30, 110, 130),
            spots = damaged ? rgb(255, 90, 120) : rgb(255, 120, 200), tent = damaged ? rgb(150, 80, 170) : rgb(90, 220, 200);
    float ph = aph * 2 * PI, pulse = sinf(ph);           // tentacle wave + bell pulsing
    std::vector<Part> P;
    // tentacles (drawn first, under the bell)
    for (int i = 0; i < 7; ++i) {
        float x0 = -0.42f + 0.14f * i;
        float len = 0.55f + 0.12f * (i % 2) - 0.1f * fabsf(x0);
        P.push_back({ [=](Vector2 p) { return sdTentacle(p, { x0, 0.2f }, { x0 * 0.25f, 1 }, len, 0.07f, 0.02f, 0.07f, 7.0f, ph + i * 1.1f, 0); },
                      tent, 0.1f, 0.08f, 0.04f, 0.1f, 0.9f, 0.2f, 0.92f,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                          float t; sdTentacle(p, { x0, 0.2f }, { x0 * 0.25f, 1 }, len, 0.07f, 0.02f, 0.07f, 7.0f, ph + i * 1.1f, 0, &t);
                          c = mix(tent, Vector3Scale(bell2, 1.2f), t);
                          c = Vector3Scale(c, 0.75f + 0.4f * fbm({ p.x * 14, p.y * 14 }, 2));
                          float s = (t * 5) - floorf(t * 5);
                          float nd = Vector2Length({ (s - 0.5f) * 0.16f, d + 0.03f }) / 0.022f;
                          if (nd < 1 && t > 0.15f) { c = mix(spots, rgb(255, 230, 250), 1 - nd); e = 0.7f * (1 - nd); }   // glowing nodes
                          e = std::max(e, 0.18f * (1 - t));
                          if (d > -0.01f) c = Vector3Scale(c, 1.2f);
                      } });
    }
    // the bell
    P.push_back({ [=](Vector2 p) { float d = sdEllipse(p, { 0, -0.12f - 0.02f * pulse }, { 0.62f * (1 + 0.04f * pulse), 0.5f * (1 - 0.05f * pulse) });
                                   d = smin(d, sdEllipse(p, { 0, 0.2f }, { 0.56f, 0.12f }), 0.08f);
                                   for (int k = -3; k <= 3; ++k) d = std::max(d, -sdCircle(p, { k * 0.16f, 0.36f }, 0.06f));   // scalloped skirt
                                   return d; },
                  bell, 0.18f, 0.4f, 0.3f, 0.1f, 1.0f, 0.15f, 0.9f,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      float r = Vector2Length({ p.x / 0.62f, (p.y + 0.12f) / 0.5f });
                      c = mix(Vector3Scale(bell, 1.1f), bell2, sstep(0.0f, 1.0f, r));
                      float n = fbm({ p.x * 7 + (damaged ? 3 : 0), p.y * 7 }, 4);
                      c = mix(c, damaged ? rgb(70, 30, 120) : rgb(40, 70, 170), 0.45f * sstep(0.45f, 0.75f, n));   // nebula swirls
                      c = Vector3Scale(c, 0.75f + 0.45f * n);
                      // gelatinous blobs / organs seen through the bell
                      float blob = fbm({ p.x * 4.5f + 11, p.y * 4.5f }, 3);
                      if (blob > 0.6f) { c = mix(c, spots, (blob - 0.6f) * 2.5f); h += 0.02f * (blob - 0.6f); e = std::max(e, (blob - 0.6f) * 1.6f); }
                      cosmicSpeckle(p, c, e, 0.985f, 60);
                      e = std::max(e, 0.08f + 0.22f * (1 - r));                               // inner bioluminescence
                      if (d > -0.03f) { c = mix(c, damaged ? rgb(230, 170, 255) : rgb(190, 255, 240), 0.35f); e = std::max(e, 0.3f); }
                      if (damaged) {                                                         // cracks
                          float v = fabsf(fbm({ p.x * 5 + 40, p.y * 5 }, 3) * 2 - 1);
                          if (v < 0.05f) { c = rgb(255, 80, 90); e = 1.2f; h -= 0.02f; }
                      }
                  } });
    // small eyes ring + great eye
    const Vector2 small[6] = { { -0.36f, -0.2f }, { 0.36f, -0.2f }, { -0.22f, 0.08f }, { 0.22f, 0.08f }, { -0.18f, -0.42f }, { 0.18f, -0.42f } };
    for (int i = 0; i < 6; ++i) {
        Vector2 ec = small[i]; bool shut = damaged && (i % 2 == 0);
        P.push_back({ [=](Vector2 p) { return sdCircle(p, ec, 0.065f); }, rgb(255, 140, 60), 0.5f, 0.05f, 0.03f, 0.1f, 1.0f, 0.5f, 1,
                      [=](Vector2 p, float, Vector3& c, float& h, float& e) {
                          if (shut) { c = Vector3Scale(bell2, 0.8f); e = 0; if (fabsf(p.y - ec.y) < 0.008f) c = rgb(20, 10, 20); return; }
                          eyePat(p, ec, 0.065f, rgb(255, 200, 70), rgb(210, 40, 30), c, h, e); } });
    }
    Vector2 big{ 0, -0.16f };
    P.push_back({ [=](Vector2 p) { return sdCircle(p, big, 0.15f); }, rgb(255, 140, 60), 0.55f, 0.08f, 0.05f, 0.1f, 1.0f, 0.8f, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float& e) { eyePat(p, big, 0.15f, rgb(255, 205, 80), rgb(215, 35, 30), c, h, e); } });
    return render(N, P, damaged ? rgb(220, 120, 255) : rgb(140, 255, 220), nullptr, true);
}

// ======================================= DART  ->  COMET IMP ==========================================
// Living comet: a flaming wisp with a single fiery eye at its head (bottom), tails streaming back (up).
inline Texture2D cometImp(int N, float ph) {
    Vector3 hot = rgb(255, 90, 170), cool = rgb(110, 140, 255);
    float fl = ph * 2 * PI / 1.4f, fh = 0.4f * sinf(ph * 2 * PI);   // flame flicker
    std::vector<Part> P;
    struct Tail { float x, len, amp, ph, r; };
    const Tail tails[7] = { { -0.2f, 0.72f, 0.14f, 0.0f, 0.12f }, { 0.2f, 0.7f, 0.14f, 2.0f, 0.12f }, { 0, 0.95f, 0.1f, 1.0f, 0.15f },
                            { -0.36f, 0.52f, 0.12f, 3.0f, 0.08f }, { 0.36f, 0.54f, 0.12f, 4.0f, 0.08f },
                            { -0.5f, 0.34f, 0.1f, 5.0f, 0.06f }, { 0.5f, 0.36f, 0.1f, 6.0f, 0.06f } };
    for (const Tail& tl : tails)
        P.push_back({ [=](Vector2 p) { return sdTentacle(p, { tl.x * 0.4f, 0.3f }, Vector2Normalize({ tl.x, -1 }), tl.len, tl.r, 0.015f, tl.amp, 6.5f, tl.ph + fl * 1.4f, tl.x * 0.15f); },
                      hot, 0.1f, 0.12f, 0.08f, 0.0f, 0.6f, 0.8f, 0.9f,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                          float t; sdTentacle(p, { tl.x * 0.4f, 0.3f }, Vector2Normalize({ tl.x, -1 }), tl.len, tl.r, 0.015f, tl.amp, 6.5f, tl.ph + fl * 1.4f, tl.x * 0.15f, &t);
                          c = mix(mix(rgb(255, 200, 230), hot, sstep(0.0f, 0.4f, t)), cool, sstep(0.5f, 1.0f, t));   // white-hot -> pink -> blue tips
                          e = 0.7f - 0.45f * t;
                          c = Vector3Scale(c, 0.8f + 0.35f * fbm({ p.x * 12, p.y * 12 }, 3));
                          if (d > -0.015f) e += 0.25f;
                          cosmicSpeckle(p, c, e, 0.97f, 90);
                          (void)h;
                      } });
    // head (teardrop) with the eye
    P.push_back({ [=](Vector2 p) { return smin(sdCircle(p, { 0, 0.42f }, 0.26f), sdPoly(p, { { -0.2f, 0.32f }, { 0.2f, 0.32f }, { 0, -0.15f } }), 0.1f); },
                  hot, 0.2f, 0.25f, 0.15f, 0.0f, 0.8f, 0.6f, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      c = mix(rgb(255, 140, 210), rgb(150, 60, 200), sstep(0.3f, -0.1f, p.y));
                      e = 0.3f + 0.35f * fbm({ p.x * 8, p.y * 8 + fh }, 3);
                      if (d > -0.03f) { c = rgb(255, 190, 235); e = 0.7f; }
                      (void)h;
                  } });
    Vector2 ec{ 0, 0.45f };
    P.push_back({ [=](Vector2 p) { return sdEllipse(p, ec, { 0.15f, 0.17f }); }, rgb(255, 160, 60), 0.45f, 0.06f, 0.04f, 0.1f, 1.0f, 0.8f, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float& e) { eyePat(p, ec, 0.165f, rgb(255, 210, 80), rgb(220, 50, 30), c, h, e, 0.13f); } });
    return render(N, P, rgb(255, 150, 220), nullptr, true);
}

// ==================================== SHIELDED  ->  ASTEROID BRUTE ======================================
// A living boulder of basalt plates split by molten seams; a slit eye burns in its core; rock shards orbit it.
inline Texture2D asteroidBrute(int N, float ph) {
    Vector3 rock = rgb(66, 60, 68), lava = rgb(255, 110, 25);
    float glow = 1.0f + 0.2f * sinf(ph * 2 * PI), orb = ph * 2 * PI / 9;   // magma pulse + shards orbit one slot per loop
    std::vector<Part> P;
    // orbiting shards
    for (int i = 0; i < 9; ++i) {
        float a = i * 0.698f + orb, r = 0.78f + 0.1f * hash2((float)i, 3);
        Vector2 cpt{ cosf(a) * r, sinf(a) * r * 0.9f };
        float sz = 0.045f + 0.035f * hash2((float)i, 7);
        P.push_back({ [=](Vector2 p) { Vector2 q = rot(Vector2Subtract(p, cpt), i * 40.0f);
                          return sdPoly(q, { { -sz, -sz * 0.6f }, { sz * 0.2f, -sz }, { sz, -sz * 0.1f }, { sz * 0.4f, sz }, { -sz * 0.8f, sz * 0.6f } }); },
                      rock, 0.2f, 0.06f, 0.04f, 0.1f, 0.4f, 0, 1,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& e) { c = Vector3Scale(rock, 0.8f + 0.4f * vnoise({ p.x * 40, p.y * 40 }));
                          if (d > -0.008f) { c = mix(c, lava, 0.5f); e = 0.5f * glow; } (void)h; } });
    }
    // main body
    P.push_back({ [=](Vector2 p) {
                      Vector2 q = M(p);
                      float d = sdPoly(q, { { 0, -0.66f }, { 0.14f, -0.52f }, { 0.3f, -0.68f }, { 0.4f, -0.44f }, { 0.62f, -0.4f }, { 0.56f, -0.16f },
                                            { 0.72f, 0.04f }, { 0.56f, 0.2f }, { 0.6f, 0.42f }, { 0.38f, 0.46f }, { 0.26f, 0.66f }, { 0.08f, 0.54f }, { 0, 0.62f } });
                      d += 0.07f * (vnoise({ p.x * 7, p.y * 7 }) - 0.5f);
                      return d - 0.02f; },
                  rock, 0.15f, 0.42f, 0.25f, 0.1f, 0.35f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      // basalt plates = voronoi cells; seams glow with magma
                      float s = 0.24f, gxf = p.x / s, gyf = p.y / s, gx = floorf(gxf), gy = floorf(gyf), f1 = 9, f2 = 9, id = 0;
                      for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                          float cx = gx + ox, cy = gy + oy, px = cx + 0.2f + 0.6f * hash2(cx, cy), py = cy + 0.2f + 0.6f * hash2(cy * 1.7f, cx);
                          float dd = sqrtf((gxf - px) * (gxf - px) + (gyf - py) * (gyf - py));
                          if (dd < f1) { f2 = f1; f1 = dd; id = hash2(cx + 5, cy); } else if (dd < f2) f2 = dd; }
                      float seam = f2 - f1;
                      h += 0.12f * (1 - std::min(1.f, f1 * 1.1f)) + 0.05f * id;              // faceted, raised chunks
                      c = Vector3Scale(rock, 0.6f + 0.7f * id);
                      c = Vector3Scale(c, 0.8f + 0.35f * vnoise({ p.x * 30, p.y * 30 }));
                      float r = Vector2Length({ p.x, p.y - 0.05f });
                      float heat = (1.2f - r) * glow;                                         // seams glow hotter toward the core
                      float sw = 0.09f + 0.14f * std::max(0.f, heat - 0.4f);
                      if (seam < sw) { float g = 1 - seam / sw; c = mix(c, mix(lava, rgb(255, 230, 120), g * 0.6f), g); e = std::max(e, g * 1.5f * heat); h -= 0.03f * g; }
                      if (d > -0.02f) c = Vector3Scale(c, 0.7f);
                  } });
    // the eye socket and eye
    Vector2 ec{ 0, 0.08f };
    P.push_back({ [=](Vector2 p) { return sdEllipse(p, ec, { 0.11f, 0.2f }); }, lava, 0.35f, 0.04f, 0.03f, 0.1f, 1.0f, 1.2f, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float& e) { eyePat(p, ec, 0.19f, rgb(255, 235, 140), rgb(240, 80, 10), c, h, e, 0.18f); e *= glow; } });
    return render(N, P, rgb(255, 150, 80), nullptr, true);
}

// ================================== SPLITTER  ->  COSMIC LEVIATHAN ======================================
// A crowned eye-beast: fin petals fan out behind one great eye, suckered tentacles curl below.
inline Texture2D cosmicLeviathan(int N, float aph, float scaleDetail = 1) {
    Vector3 fin = rgb(70, 110, 190), fin2 = rgb(150, 70, 170), tent = rgb(90, 60, 160), suck = rgb(255, 90, 170);
    float ph = aph * 2 * PI, cf = 1 + 0.18f * sinf(ph);
    std::vector<Part> P;
    // fin petals (crown) fanning upward/outward
    for (int i = 0; i < 7; ++i) {
        float a = (-90 + (i - 3) * 30) * DEG2RAD;
        Vector2 dir{ cosf(a), sinf(a) };
        float len = 0.66f - 0.06f * fabsf(i - 3.0f);
        P.push_back({ [=](Vector2 p) { return sdTentacle(p, { 0, -0.02f }, dir, len, 0.21f, 0.015f, 0.06f, 3.5f, i * 0.9f + ph, (i - 3) * 0.06f); },
                      fin, 0.12f, 0.16f, 0.1f, 0.15f, 0.8f, 0, 1,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                          float t; sdTentacle(p, { 0, -0.02f }, dir, len, 0.21f, 0.015f, 0.06f, 3.5f, i * 0.9f + ph, (i - 3) * 0.06f, &t);
                          c = mix(fin2, fin, t);
                          c = Vector3Scale(c, 0.8f + 0.35f * fbm({ p.x * 10, p.y * 10 }, 3));
                          float rib = fabsf(sinf(t * 22)); if (rib < 0.15f) { h += 0.01f; c = mix(c, rgb(170, 220, 255), 0.4f); }
                          if (d > -0.015f) { c = mix(c, rgb(150, 230, 255), 0.5f); e = 0.35f; }
                          cosmicSpeckle(p, c, e);
                      } });
    }
    // tentacles curling below / to the sides
    for (int i = 0; i < 6; ++i) {
        float sgn = i < 3 ? -1.f : 1.f; int j = i % 3;
        float a = (90 + sgn * (25 + j * 28)) * DEG2RAD;
        Vector2 dir{ cosf(a), sinf(a) };
        float len = 0.62f - j * 0.06f, curl = sgn * (0.16f + 0.05f * j) * cf;
        P.push_back({ [=](Vector2 p) { return sdTentacle(p, { 0, 0.12f }, dir, len, 0.1f, 0.03f, 0.05f, 4.0f, ph + i, curl); },
                      tent, 0.14f, 0.16f, 0.08f, 0.1f, 0.85f, 0, 1,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                          float t; sdTentacle(p, { 0, 0.12f }, dir, len, 0.1f, 0.03f, 0.05f, 4.0f, ph + i, curl, &t);
                          c = mix(tent, rgb(60, 90, 200), t);
                          cosmicSpeckle(p, c, e, 0.985f, 80);
                          float s = t * 9 - floorf(t * 9);
                          float sd = Vector2Length({ (s - 0.5f) * 0.12f, d + 0.022f }) / 0.016f;
                          if (sd < 1 && t > 0.1f) { c = mix(suck, rgb(255, 220, 240), 1 - sd); e = 0.6f * (1 - sd); h += 0.006f; }   // glowing suckers
                      } });
    }
    // head + eye
    P.push_back({ [=](Vector2 p) { return sdEllipse(p, { 0, 0.04f }, { 0.25f, 0.3f }); }, rgb(150, 70, 160), 0.3f, 0.25f, 0.12f, 0.1f, 0.9f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      c = mix(rgb(190, 90, 170), rgb(90, 50, 140), sstep(-0.2f, 0.3f, p.y));
                      float v = fabsf(sinf(p.x * 30) * sinf(p.y * 24)); if (v < 0.1f) { c = Vector3Scale(c, 0.7f); h -= 0.006f; }
                      if (d > -0.02f) c = mix(c, rgb(240, 160, 230), 0.5f);
                      (void)e;
                  } });
    Vector2 ec{ 0, 0.02f };
    P.push_back({ [=](Vector2 p) { return sdCircle(p, ec, 0.17f); }, rgb(80, 220, 255), 0.5f, 0.08f, 0.05f, 0.1f, 1.0f, 0.8f, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float& e) { eyePat(p, ec, 0.17f, rgb(170, 255, 235), rgb(20, 90, 200), c, h, e, 0.1f); } });
    (void)scaleDetail;
    return render(N, P, rgb(170, 150, 255), nullptr, true);
}

// ================================ MINI  ->  LEVIATHAN SPAWN ========================================
inline Texture2D leviathanSpawn(int N, float aph) {
    Vector3 tent = rgb(110, 70, 180), suck = rgb(255, 100, 180);
    float ph = aph * 2 * PI, cf = 1 + 0.25f * sinf(ph);
    std::vector<Part> P;
    for (int i = 0; i < 4; ++i) {
        float a = (45 + 90 * i) * DEG2RAD + 0.2f; Vector2 dir{ cosf(a), sinf(a) };
        float curl = (i % 2 ? 0.2f : -0.2f) * cf;
        P.push_back({ [=](Vector2 p) { return sdTentacle(p, { 0, 0 }, dir, 0.78f, 0.17f, 0.04f, 0.06f, 4.0f, ph + i, curl); },
                      tent, 0.14f, 0.18f, 0.1f, 0.1f, 0.85f, 0, 1,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                          float t; sdTentacle(p, { 0, 0 }, dir, 0.78f, 0.17f, 0.04f, 0.06f, 4.0f, ph + i, curl, &t);
                          c = mix(tent, rgb(70, 110, 220), t);
                          float s = t * 6 - floorf(t * 6);
                          float sd = Vector2Length({ (s - 0.5f) * 0.14f, d + 0.035f }) / 0.025f;
                          if (sd < 1 && t > 0.12f) { c = mix(suck, rgb(255, 220, 240), 1 - sd); e = 0.6f * (1 - sd); }
                          (void)h;
                      } });
    }
    Vector2 ec{ 0, 0 };
    P.push_back({ [=](Vector2 p) { return sdCircle(p, ec, 0.3f); }, rgb(80, 220, 255), 0.5f, 0.12f, 0.08f, 0.1f, 1.0f, 0.8f, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      if (d > -0.05f) { c = rgb(150, 80, 170); e = 0.1f; return; }
                      eyePat(p, ec, 0.25f, rgb(190, 255, 220), rgb(30, 130, 210), c, h, e, 0.12f); } });
    return render(N, P, rgb(170, 150, 255), nullptr, true);
}

} // namespace spr
