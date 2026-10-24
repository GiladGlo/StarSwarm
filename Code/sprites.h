// =============================================================================
//  sprites.h  -  procedural "realistic" sprite renderer for Star Swarm
// -----------------------------------------------------------------------------
//  Each ship is described as layered signed-distance-field (SDF) parts. At
//  startup every part is rasterised into a height + material buffer, then lit
//  per-pixel (diffuse, Blinn-Phong specular, fresnel rim, fake environment
//  reflection for metals, emissive glow with bloom). Result: shaded, glossy,
//  high-resolution textures with no external image files.
// =============================================================================
#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>

namespace spr {

// ---------------------------------------------------------------- helpers ----
inline Vector3 rgb(int r, int g, int b) { return { r / 255.f, g / 255.f, b / 255.f }; }
inline Vector3 mix(Vector3 a, Vector3 b, float t) { return Vector3Lerp(a, b, t); }
inline float   sat(float x) { return std::clamp(x, 0.f, 1.f); }
inline float   sstep(float a, float b, float x) { float t = sat((x - a) / (b - a)); return t * t * (3 - 2 * t); }
inline float   hash2(float x, float y) { float h = sinf(x * 127.1f + y * 311.7f) * 43758.5453f; return h - floorf(h); }
inline Vector2 M(Vector2 p) { return { fabsf(p.x), p.y }; }           // mirror across centre line
inline Vector2 rot(Vector2 p, float deg) { float a = deg * DEG2RAD, c = cosf(a), s = sinf(a); return { c * p.x + s * p.y, -s * p.x + c * p.y }; }
inline float vnoise(Vector2 p) {
    float ix = floorf(p.x), iy = floorf(p.y), fx = p.x - ix, fy = p.y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
inline float fbm(Vector2 p, int oct) {
    float s = 0, a = 0.5f;
    for (int i = 0; i < oct; ++i) { s += a * vnoise(p); p = { p.x * 2.03f + 1.7f, p.y * 2.03f + 9.2f }; a *= 0.5f; }
    return s;
}


// ------------------------------------------------------------ SDF primitives --
inline float sdCircle(Vector2 p, Vector2 c, float r) { return Vector2Distance(p, c) - r; }
inline float sdEllipse(Vector2 p, Vector2 c, Vector2 r, float deg = 0) {
    Vector2 q = rot(Vector2Subtract(p, c), deg);
    float k0 = sqrtf((q.x / r.x) * (q.x / r.x) + (q.y / r.y) * (q.y / r.y));
    float k1 = sqrtf((q.x / (r.x * r.x)) * (q.x / (r.x * r.x)) + (q.y / (r.y * r.y)) * (q.y / (r.y * r.y)));
    return k1 < 1e-6f ? -std::min(r.x, r.y) : k0 * (k0 - 1) / k1;
}
inline float sdCapsule(Vector2 p, Vector2 a, Vector2 b, float ra, float rb) {
    Vector2 pa = Vector2Subtract(p, a), ba = Vector2Subtract(b, a);
    float h = sat(Vector2DotProduct(pa, ba) / Vector2DotProduct(ba, ba));
    return Vector2Length(Vector2Subtract(pa, Vector2Scale(ba, h))) - (ra + (rb - ra) * h);
}
inline float sdPoly(Vector2 p, const std::vector<Vector2>& v) {
    float d = Vector2DotProduct(Vector2Subtract(p, v[0]), Vector2Subtract(p, v[0])), s = 1;
    for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i, ++i) {
        Vector2 e = Vector2Subtract(v[j], v[i]), w = Vector2Subtract(p, v[i]);
        Vector2 b = Vector2Subtract(w, Vector2Scale(e, sat(Vector2DotProduct(w, e) / Vector2DotProduct(e, e))));
        d = std::min(d, Vector2DotProduct(b, b));
        bool c1 = p.y >= v[i].y, c2 = p.y < v[j].y, c3 = e.x * w.y > e.y * w.x;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * sqrtf(d);
}
inline float smin(float a, float b, float k) { float h = sat(0.5f + 0.5f * (b - a) / k); return b + (a - b) * h - k * h * (1 - h); }

// ------------------------------------------------------------------- parts --
using SdfFn = std::function<float(Vector2)>;
// pattern: (point, signed distance inside part, colour, height, emissive)
using PatFn = std::function<void(Vector2, float, Vector3&, float&, float&)>;

struct Part {
    SdfFn   sdf;
    Vector3 col;
    float   base = 0.1f, dome = 0.2f, bevel = 0.1f;   // height profile
    float   metal = 0.3f, gloss = 0.5f, emis = 0.f, alpha = 1.f;
    PatFn   pat = nullptr;
};

struct Surf { Vector3 col{ 0, 0, 0 }; float h = 0, a = 0, emis = 0, gloss = 0, metal = 0; };

inline float g_roll = 0;
inline thread_local Image* g_imgOut = nullptr;   // when set, render() hands back the CPU image instead of a texture (worker threads)   // radians: renders the sprite banked about its long (y) axis
inline Texture2D render(int N, const std::vector<Part>& parts, Vector3 rim = { 0.45f, 0.65f, 1.f },
                        std::vector<unsigned char>* maskOut = nullptr, bool hd = false) {
    const float px = 2.0f / N;
    std::vector<Surf> buf(N * N);

    // 1) rasterise layered parts into height/material buffer (painter's order)
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            Vector2 p{ -1 + (x + 0.5f) * px, -1 + (y + 0.5f) * px };
            if (g_roll != 0) {   // bank: the dipping wing foreshortens more than the rising one
                bool dip = (p.x > 0) == (g_roll > 0);
                p.x /= cosf(fabsf(g_roll) * (dip ? 1.5f : 0.5f));
            }
            Surf& s = buf[y * N + x];
            for (const Part& pt : parts) {
                float d = pt.sdf(p);
                float cov = sat(0.5f - d / px);
                if (cov <= 0) continue;
                float t = sat(-d / pt.bevel);
                float h = pt.base + pt.dome * sqrtf(1 - (1 - t) * (1 - t));
                Vector3 c = pt.col; float e = pt.emis;
                if (pt.pat) pt.pat(p, d, c, h, e);
                float a = cov * pt.alpha;
                if (s.a < 1e-3f) { s.col = c; s.h = h * a; s.emis = e; s.gloss = pt.gloss; s.metal = pt.metal; }
                else {
                    s.col = mix(s.col, c, a); s.h = s.h + (h - s.h) * a; s.emis += (e - s.emis) * a;
                    s.gloss += (pt.gloss - s.gloss) * a; s.metal += (pt.metal - s.metal) * a;
                }
                s.a = a + s.a * (1 - a);
            }
        }

    // 2) light it
    const Vector3 L  = Vector3Normalize({ -0.45f, -0.65f, 0.75f });
    const Vector3 Hv = Vector3Normalize(Vector3Add(L, { 0, 0, 1 }));
    std::vector<Vector3> lit(N * N), glow(N * N);
    std::vector<float> alpha(N * N);
    auto H = [&](int x, int y) { x = std::clamp(x, 0, N - 1); y = std::clamp(y, 0, N - 1); return buf[y * N + x].h; };
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const Surf& s = buf[y * N + x];
            alpha[y * N + x] = s.a;
            if (s.a <= 0) continue;
            float dx = (H(x + 1, y) - H(x - 1, y)) / (2 * px), dy = (H(x, y + 1) - H(x, y - 1)) / (2 * px);
            Vector3 n = Vector3Normalize({ -dx * 0.55f, -dy * 0.55f, 1 });
            if (g_roll != 0) {   // bank: tilt normals by the roll plus a little wing dihedral on each side
                float side = x < N / 2 ? -1.f : 1.f;
                float ang = g_roll * (1 + 0.6f * side * (g_roll > 0 ? 1 : -1));
                float cr = cosf(ang), sr = sinf(ang); n = { n.x * cr + n.z * sr, n.y, -n.x * sr + n.z * cr }; }
            float diff = std::max(0.f, Vector3DotProduct(n, L));
            float spec = powf(std::max(0.f, Vector3DotProduct(n, Hv)), 10 + s.gloss * 110) * (0.1f + s.gloss * 1.3f);
            float fres = powf(1 - n.z, 2.2f);
            // cavity / ambient occlusion from local height
            float avg = (H(x - 3, y) + H(x + 3, y) + H(x, y - 3) + H(x, y + 3)) * 0.25f;
            float ao = sat(1 - std::max(0.f, avg - s.h) * 6);
            float shadow = 0;
            if (hd) {
                // wider ambient occlusion (two more radii, 8 directions)
                for (int rr : { N / 64, N / 28 }) {
                    float occ = 0;
                    for (int k = 0; k < 8; ++k) { float a = k * PI / 4;
                        occ += std::max(0.f, H(x + (int)(cosf(a) * rr), y + (int)(sinf(a) * rr)) - s.h); }
                    ao *= sat(1 - occ / 8 * 3.0f);
                }
                // self-shadowing: march towards the light over the height field
                const float lx = -0.569f, ly = -0.822f, rise = 2.2f;   // light direction (xy) and how fast the ray climbs
                for (int k = 1; k <= N / 14; ++k) {
                    float dist = k * 1.5f * px;
                    float hs = H(x + (int)lroundf(lx * k * 1.5f), y + (int)lroundf(ly * k * 1.5f));
                    float occl = hs - (s.h + dist * rise);
                    if (occl > 0) shadow = std::max(shadow, sat(occl / 0.02f) * (1 - (float)k / (N / 14 + 1) * 0.5f));
                }
            }
            Vector3 env = mix(rgb(10, 12, 30), rgb(150, 175, 230), sat(0.5f - 0.6f * n.y));
            Vector3 c = s.col;
            diff *= 1 - 0.75f * shadow; spec *= 1 - shadow;
            Vector3 out = Vector3Scale(c, (0.22f + 0.9f * diff) * (1 - s.metal * 0.45f) * ao);
            out = Vector3Add(out, Vector3Multiply(Vector3Scale(env, 0.55f * s.metal), c));
            out = Vector3Add(out, Vector3Scale(mix({ 1, 1, 1 }, c, s.metal * 0.5f), spec));
            out = Vector3Add(out, Vector3Scale(rim, fres * 0.35f * ao));
            out = Vector3Add(out, Vector3Scale(c, s.emis));
            lit[y * N + x] = out;
            glow[y * N + x] = Vector3Scale(c, s.emis * s.a);
        }

    // 3) bloom: blur emissive and add
    int R = std::max(2, N / 36);
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<Vector3> tmp(N * N);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            Vector3 acc{ 0, 0, 0 };
            for (int k = -R; k <= R; ++k) acc = Vector3Add(acc, glow[y * N + std::clamp(x + k, 0, N - 1)]);
            tmp[y * N + x] = Vector3Scale(acc, 1.f / (2 * R + 1));
        }
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            Vector3 acc{ 0, 0, 0 };
            for (int k = -R; k <= R; ++k) acc = Vector3Add(acc, tmp[std::clamp(y + k, 0, N - 1) * N + x]);
            glow[y * N + x] = Vector3Scale(acc, 1.f / (2 * R + 1));
        }
    }

    Image img = GenImageColor(N, N, BLANK);
    Color* pix = (Color*)img.data;
    for (int i = 0; i < N * N; ++i) {
        Vector3 g = Vector3Scale(glow[i], 1.6f);
        float ga = sat(std::max({ g.x, g.y, g.z }));
        float a = alpha[i] + ga * (1 - alpha[i]);
        if (a <= 0.002f) { pix[i] = { 0, 0, 0, 0 }; continue; }
        Vector3 c = Vector3Scale(Vector3Add(Vector3Scale(lit[i], alpha[i]), g), 1.f / a);
        pix[i] = { (unsigned char)(sat(c.x) * 255), (unsigned char)(sat(c.y) * 255), (unsigned char)(sat(c.z) * 255),
                   (unsigned char)(sat(a) * 255) };
    }
    // dilate colour into transparent texels so filtering doesn't create dark fringes
    for (int pass = 0; pass < 4; ++pass)
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            Color& c = pix[y * N + x];
            if (c.a != 0 || (c.r | c.g | c.b)) continue;
            int r = 0, g = 0, b = 0, n = 0;
            for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                int xx = x + ox, yy = y + oy;
                if (xx < 0 || yy < 0 || xx >= N || yy >= N) continue;
                Color q = pix[yy * N + xx];
                if (q.a || (q.r | q.g | q.b)) { r += q.r; g += q.g; b += q.b; ++n; }
            }
            if (n) c = { (unsigned char)(r / n), (unsigned char)(g / n), (unsigned char)(b / n), 0 };
        }

    if (maskOut) { maskOut->resize(N * N); for (int i = 0; i < N * N; ++i) (*maskOut)[i] = pix[i].a; }
    if (g_imgOut) { *g_imgOut = img; return Texture2D{}; }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    return tex;
}

// ===================================================================== SHIPS ==
// Coordinates: unit square [-1,1]^2, +y is DOWN on screen.
// Player faces UP (nose at -y). Enemies face DOWN (head at +y).

inline Texture2D playerShip(int N, float rollDeg = 0) {   // detailed fighter; rollDeg renders it banked
    g_roll = rollDeg * DEG2RAD;
    Vector3 hull = rgb(226, 229, 238), hull2 = rgb(188, 192, 204), red = rgb(205, 30, 42), blue = rgb(35, 85, 215),
            gun = rgb(66, 72, 88), dark = rgb(34, 36, 44);
    std::vector<Part> P;
    auto seam = [](Vector3& c, float& h, float k = 0.55f) { h -= 0.02f; c = Vector3Scale(c, k); };
    // swept wings: panels, flaps, leading-edge strip, stripes, chevrons, vents
    P.push_back({ [](Vector2 p) { Vector2 q = M(p); return sdPoly(q, { { 0.08f, -0.15f }, { 0.86f, 0.36f }, { 0.9f, 0.58f },
                                                                       { 0.74f, 0.62f }, { 0.08f, 0.48f } }) - 0.02f; },
                  hull, 0.10f, 0.12f, 0.10f, 0.55f, 0.6f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      Vector2 q = M(p); float ax = q.x;
                      float lead = (q.y + 0.15f) - (ax - 0.08f) * (0.51f / 0.78f);   // distance-ish behind the leading edge
                      if (ax > 0.6f) c = red;
                      if (fabsf(ax - 0.44f) < 0.035f) c = blue;
                      if (fabsf(ax - 0.44f) < 0.035f && fabsf(ax - 0.44f) > 0.026f) c = Vector3Scale(blue, 0.6f);
                      if (lead < 0.06f && ax < 0.6f) c = Vector3Scale(hull2, 0.95f);                // leading-edge strip
                      if (fabsf(lead - 0.06f) < 0.005f && ax < 0.6f) seam(c, h);
                      // panel lines
                      if (fabsf(ax - 0.28f) < 0.006f || fabsf(p.y - 0.30f + ax * 0.3f) < 0.005f) seam(c, h);
                      if (fabsf(ax - 0.62f) < 0.005f) seam(c, h, 0.6f);
                      // trailing-edge flaps
                      if (q.y > 0.4f + (ax - 0.08f) * 0.12f && ax > 0.3f && ax < 0.72f) {
                          c = Vector3Scale(c, 0.9f);
                          if (fabsf(ax - 0.51f) < 0.005f) seam(c, h);
                      }
                      if (fabsf(q.y - (0.4f + (ax - 0.08f) * 0.12f)) < 0.005f && ax > 0.3f && ax < 0.72f) seam(c, h);
                      // hazard chevrons near the root
                      if (ax > 0.12f && ax < 0.24f && q.y > 0.05f && q.y < 0.2f) {
                          float v = (q.y + ax * 0.6f) * 22; if (v - floorf(v) < 0.5f) c = mix(c, rgb(240, 190, 40), 0.85f); }
                      // intake vents
                      if (ax > 0.31f && ax < 0.4f && q.y > 0.12f && q.y < 0.3f) {
                          float v = q.y * 60; c = Vector3Scale(c, 0.6f); if (v - floorf(v) < 0.45f) { c = dark; h -= 0.012f; } }
                      // dust / wear
                      c = Vector3Scale(c, 0.93f + 0.1f * fbm({ p.x * 14, p.y * 14 }, 3));
                      if (d > -0.015f) c = Vector3Scale(c, 0.82f);
                      (void)e;
                  } });
    // wing-tip missile pods with three missiles each
    P.push_back({ [](Vector2 p) { return sdCapsule(M(p), { 0.82f, 0.05f }, { 0.82f, 0.62f }, 0.035f, 0.06f); },
                  red, 0.22f, 0.14f, 0.06f, 0.7f, 0.8f, 0, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float& e) {
                      Vector2 q = M(p);
                      if (q.y < 0.14f) { c = rgb(235, 235, 240); if (q.y < 0.09f) c = rgb(60, 60, 70); }     // seeker heads
                      if (fabsf(q.y - 0.3f) < 0.006f || fabsf(q.y - 0.46f) < 0.006f) seam(c, h, 0.5f);
                      if (q.y > 0.56f) { c = dark; }
                      (void)e;
                  } });
    // cannons with cooling rings and muzzle
    P.push_back({ [](Vector2 p) { return sdCapsule(M(p), { 0.5f, -0.38f }, { 0.5f, 0.2f }, 0.022f, 0.04f); },
                  gun, 0.2f, 0.08f, 0.04f, 0.9f, 0.8f, 0, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float&) {
                      float r = p.y * 40; if (p.y < -0.05f && r - floorf(r) < 0.25f) seam(c, h, 0.5f);
                      if (p.y < -0.36f) c = rgb(20, 20, 26);
                  } });
    // twin engine nacelles: intakes, ribbing, heat-tinted rear
    P.push_back({ [](Vector2 p) { return sdCapsule(M(p), { 0.24f, 0.05f }, { 0.24f, 0.74f }, 0.09f, 0.13f); },
                  gun, 0.22f, 0.2f, 0.12f, 0.85f, 0.7f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      Vector2 q = M(p);
                      if (q.y < 0.12f) { c = dark; h -= 0.02f; if (fabsf(q.x - 0.24f) < 0.008f) c = gun; }  // intake mouth
                      float f = q.y * 16 - floorf(q.y * 16);
                      if (f < 0.15f && q.y > 0.3f) seam(c, h);
                      c = mix(c, rgb(95, 80, 140), 0.5f * sstep(0.5f, 0.74f, q.y));                       // heat tint
                      if (fabsf(q.x - 0.24f) < 0.01f && q.y > 0.15f && q.y < 0.5f) c = Vector3Scale(c, 1.25f);  // spine highlight
                      if (d > -0.012f) c = Vector3Scale(c, 1.15f);
                      (void)e;
                  } });
    // engine nozzles: petals + hot core
    P.push_back({ [](Vector2 p) { return sdEllipse(M(p), { 0.24f, 0.78f }, { 0.10f, 0.06f }); },
                  rgb(120, 200, 255), 0.25f, 0.05f, 0.04f, 0.0f, 0.3f, 1.4f, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      Vector2 q = M(p); float r = sat(-d / 0.05f);
                      c = mix(rgb(60, 110, 200), rgb(235, 248, 255), r * r); e = 0.6f + 1.4f * r;
                      float a = atan2f(q.y - 0.78f, q.x - 0.24f);
                      if (r < 0.35f && fabsf(sinf(a * 8)) < 0.3f) { c = rgb(70, 74, 90); e = 0; h += 0.01f; }   // nozzle petals
                  } });
    // fuselage: nose cone, spine, panel lines, intake grille, sensor bumps, stripes
    P.push_back({ [](Vector2 p) { return sdCapsule(p, { 0, -0.86f }, { 0, 0.66f }, 0.05f, 0.17f); },
                  hull, 0.28f, 0.32f, 0.16f, 0.5f, 0.85f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      float ax = fabsf(p.x);
                      if (ax < 0.03f && p.y > 0.0f && p.y < 0.6f) c = red;
                      if (ax < 0.03f && ax > 0.022f && p.y > 0.0f && p.y < 0.6f) c = Vector3Scale(red, 0.6f);
                      if (p.y < -0.72f) c = red;
                      if (fabsf(p.y + 0.72f) < 0.006f) seam(c, h, 0.4f);
                      if (p.y < -0.78f && ax < 0.012f) c = rgb(240, 240, 245);                         // pitot tip
                      for (float ly : { 0.08f, 0.38f, -0.52f }) if (fabsf(p.y - ly) < 0.006f) seam(c, h, 0.5f);
                      if (fabsf(ax - 0.09f) < 0.005f && p.y > -0.5f && p.y < 0.55f) seam(c, h, 0.65f);  // longitudinal panel seams
                      // dorsal intake grille behind the canopy
                      if (p.y > -0.02f && p.y < 0.06f && ax < 0.07f && ax > 0.035f) {
                          float v = p.y * 120; c = dark; if (v - floorf(v) < 0.4f) c = gun; h -= 0.01f; }
                      // sensor bumps
                      for (float sy : { 0.2f, 0.27f }) { float dd = sqrtf((ax - 0.06f) * (ax - 0.06f) + (p.y - sy) * (p.y - sy));
                          if (dd < 0.018f) { h += 0.015f * (1 - dd / 0.018f); c = Vector3Scale(hull2, 0.9f); } }
                      // squadron chevron on the nose
                      if (p.y > -0.68f && p.y < -0.6f) { float v = (p.y + ax * 0.8f) * 30; if (v - floorf(v) < 0.45f) c = blue; }
                      c = Vector3Scale(c, 0.94f + 0.09f * fbm({ p.x * 16, p.y * 16 }, 3));
                      if (p.y > 0.45f) c = Vector3Scale(c, 1 - 0.3f * sstep(0.45f, 0.66f, p.y));       // exhaust soot
                      (void)d; (void)e;
                  } });
    // canopy: framed glass with reflection and a pilot silhouette
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, -0.28f }, { 0.075f, 0.22f }); },
                  rgb(25, 70, 150), 0.55f, 0.18f, 0.08f, 0.2f, 1.0f, 0.15f, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      if (d > -0.012f) { c = rgb(60, 64, 76); e = 0; return; }                         // frame
                      if (fabsf(p.y + 0.3f) < 0.006f) { c = rgb(60, 64, 76); e = 0; return; }          // canopy bow
                      float hy = sqrtf(p.x * p.x * 1.8f + (p.y + 0.22f) * (p.y + 0.22f));
                      if (hy < 0.035f) { c = mix(rgb(20, 22, 30), rgb(230, 230, 235), sstep(0.03f, 0.0f, hy) * 0.3f); e = 0; } // helmet
                      c = mix(c, rgb(170, 220, 255), 0.6f * sstep(-0.32f, -0.44f, p.y) * (p.x < 0.01f));     // sky reflection
                      if (p.x > 0.03f) c = Vector3Scale(c, 0.8f);
                      (void)h;
                  } });
    Texture2D t = render(N, P, { 0.45f, 0.65f, 1.f }, nullptr, true);
    g_roll = 0;
    return t;
}

inline Texture2D bee(int N, int frame) {
    Vector3 yel = rgb(250, 196, 30), dark = rgb(38, 30, 28), thor = rgb(60, 72, 150);
    std::vector<Part> P;
    // translucent wings (two pairs)
    float a1 = frame ? 62.f : 28.f, a2 = frame ? 40.f : 12.f, s = frame ? 0.8f : 1.0f;
    auto wing = [](Vector3& c, Vector2 p, float cx, float cy, float h) {
        float ang = atan2f(p.y - cy, fabsf(p.x) - cx);
        if (fabsf(sinf(ang * 7)) < 0.07f || fabsf(sinf((fabsf(p.x) + p.y) * 30)) < 0.05f) c = Vector3Scale(c, 0.45f);
        (void)h;
    };
    P.push_back({ [=](Vector2 p) { return sdEllipse(M(p), { 0.42f, -0.22f }, { 0.42f * s, 0.17f }, a1); },
                  rgb(170, 215, 255), 0.05f, 0.05f, 0.05f, 0.2f, 1.0f, 0.05f, 0.5f,
                  [=](Vector2 p, float, Vector3& c, float& h, float&) { wing(c, p, 0.1f, 0.0f, h); } });
    P.push_back({ [=](Vector2 p) { return sdEllipse(M(p), { 0.32f, -0.48f }, { 0.30f * s, 0.12f }, a2); },
                  rgb(170, 215, 255), 0.05f, 0.05f, 0.05f, 0.2f, 1.0f, 0.05f, 0.45f,
                  [=](Vector2 p, float, Vector3& c, float& h, float&) { wing(c, p, 0.1f, -0.3f, h); } });
    // legs
    for (int i = 0; i < 3; ++i) {
        float y0 = 0.02f + i * 0.13f, kx = 0.36f + 0.04f * i, ky = y0 + (i - 1) * 0.12f, fx = 0.30f + 0.06f * i, fy = ky + 0.22f;
        P.push_back({ [=](Vector2 p) { Vector2 q = M(p); return std::min(sdCapsule(q, { 0.1f, y0 }, { kx, ky }, 0.035f, 0.028f),
                                                                       sdCapsule(q, { kx, ky }, { fx, fy }, 0.028f, 0.015f)); },
                      dark, 0.08f, 0.06f, 0.03f, 0.5f, 0.7f });
    }
    // abdomen with stripes and segments
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, -0.36f }, { 0.27f, 0.42f }); },
                  yel, 0.2f, 0.35f, 0.22f, 0.15f, 0.75f, 0, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float&) {
                      float sv = sinf((p.y + 0.36f) * 21);
                      c = mix(yel, dark, sstep(0.15f, 0.35f, sv));
                      h += 0.035f * cosf((p.y + 0.36f) * 21 * 0.5f + 1.57f);
                      c = Vector3Scale(c, 0.92f + 0.08f * hash2(p.x * 40, p.y * 40));
                  } });
    // stinger
    P.push_back({ [](Vector2 p) { return sdPoly(p, { { -0.06f, -0.74f }, { 0.06f, -0.74f }, { 0, -0.95f } }); },
                  dark, 0.15f, 0.08f, 0.04f, 0.6f, 0.9f });
    // thorax
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, 0.12f }, { 0.21f, 0.19f }); },
                  thor, 0.3f, 0.3f, 0.18f, 0.55f, 0.8f, 0, 1,
                  [](Vector2 p, float, Vector3& c, float&, float&) { if (fabsf(p.x) < 0.02f) c = Vector3Scale(c, 0.6f); } });
    // antennae
    P.push_back({ [](Vector2 p) { Vector2 q = M(p); return std::min(sdCapsule(q, { 0.06f, 0.5f }, { 0.16f, 0.72f }, 0.02f, 0.016f),
                                                                   sdCapsule(q, { 0.16f, 0.72f }, { 0.28f, 0.84f }, 0.016f, 0.012f)); },
                  dark, 0.2f, 0.04f, 0.02f, 0.5f, 0.7f });
    P.push_back({ [](Vector2 p) { return sdCircle(M(p), { 0.29f, 0.85f }, 0.035f); }, rgb(255, 150, 60), 0.25f, 0.05f, 0.03f, 0, 0.9f, 1.2f });
    // head
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, 0.43f }, { 0.19f, 0.16f }); },
                  rgb(225, 170, 30), 0.3f, 0.25f, 0.14f, 0.3f, 0.85f });
    // compound eyes
    P.push_back({ [](Vector2 p) { return sdEllipse(M(p), { 0.12f, 0.49f }, { 0.085f, 0.075f }, 20); },
                  rgb(220, 20, 30), 0.5f, 0.12f, 0.07f, 0.2f, 1.0f, 0.5f, 1,
                  [](Vector2 p, float, Vector3& c, float&, float&) {
                      if (fabsf(sinf(p.x * 90)) < 0.15f || fabsf(sinf(p.y * 90)) < 0.15f) c = Vector3Scale(c, 0.65f); } });
    return render(N, P, rgb(255, 210, 120));
}

inline Texture2D butterfly(int N, int frame) {
    Vector3 red = rgb(200, 28, 40), wht = rgb(240, 240, 248), blk = rgb(22, 18, 30), blu = rgb(40, 90, 220);
    std::vector<Part> P;
    float s = frame ? 0.72f : 1.0f;
    auto wingPat = [=](Vector2 p, float d, Vector3& c, float& h, float& e, Vector2 spot) {
        Vector2 q = M(p);
        float vein = fabsf(sinf(atan2f(q.y - 0.05f, q.x) * 9));
        c = mix(c, Vector3Scale(c, 0.6f), sstep(0.12f, 0.0f, vein));
        float ds = Vector2Distance(q, { spot.x * (0.6f + 0.4f * s), spot.y });
        if (ds < 0.13f) c = wht;
        if (ds < 0.09f) { c = blu; e = 0.35f; }
        if (ds < 0.04f) c = blk;
        if (d > -0.08f) c = blk;
        float ang = atan2f(q.y - spot.y, q.x - spot.x * 0.5f);
        if (d > -0.06f && d < -0.025f && sinf(ang * 22) > 0.55f) c = wht;   // dotted white margin
        h += 0.015f * sinf(atan2f(q.y - 0.05f, q.x) * 9);                   // gentle vein ridges
        c = Vector3Scale(c, 0.95f + 0.05f * sinf(q.x * 31 + q.y * 17));
    };
    // fore- and hind-wings
    P.push_back({ [=](Vector2 p) { Vector2 q = M(p); q.x /= s; return sdEllipse(q, { 0.42f, 0.0f }, { 0.48f, 0.30f }, -18) * s; },
                  red, 0.06f, 0.08f, 0.1f, 0.15f, 0.6f, 0, 0.97f,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) { wingPat(p, d, c, h, e, { 0.55f, -0.05f }); } });
    P.push_back({ [=](Vector2 p) { Vector2 q = M(p); q.x /= s; return sdEllipse(q, { 0.32f, -0.42f }, { 0.32f, 0.22f }, 28) * s; },
                  rgb(170, 20, 35), 0.05f, 0.08f, 0.1f, 0.15f, 0.6f, 0, 0.97f,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) { wingPat(p, d, c, h, e, { 0.38f, -0.45f }); } });
    // body
    P.push_back({ [](Vector2 p) { return sdCapsule(p, { 0, -0.62f }, { 0, 0.42f }, 0.06f, 0.1f); },
                  wht, 0.25f, 0.28f, 0.1f, 0.4f, 0.85f, 0, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float&) {
                      if (sinf(p.y * 30) > 0.4f) { c = blu; h -= 0.02f; } } });
    // antennae
    P.push_back({ [](Vector2 p) { Vector2 q = M(p); return sdCapsule(q, { 0.04f, 0.58f }, { 0.22f, 0.88f }, 0.018f, 0.012f); },
                  blk, 0.2f, 0.03f, 0.02f, 0.5f, 0.6f });
    P.push_back({ [](Vector2 p) { return sdCircle(M(p), { 0.23f, 0.9f }, 0.03f); }, wht, 0.25f, 0.05f, 0.03f, 0, 1, 0.8f });
    // head + eyes
    P.push_back({ [](Vector2 p) { return sdCircle(p, { 0, 0.52f }, 0.12f); }, blu, 0.35f, 0.2f, 0.1f, 0.5f, 0.9f });
    P.push_back({ [](Vector2 p) { return sdCircle(M(p), { 0.07f, 0.58f }, 0.045f); }, rgb(255, 255, 220), 0.5f, 0.06f, 0.04f, 0, 1, 0.9f });
    return render(N, P, rgb(255, 120, 140));
}

inline Texture2D boss(int N, int frame, bool damaged) {
    Vector3 armor = damaged ? rgb(140, 65, 225) : rgb(36, 170, 92);
    Vector3 dark  = damaged ? rgb(60, 25, 95)   : rgb(16, 70, 40);
    Vector3 gold = rgb(230, 185, 70), eye = rgb(255, 120, 20);
    std::vector<Part> P;
    float open = frame ? 10.f : 0.f;
    auto cracks = [=](Vector2 p, Vector3& c, float& h, float& e) {
        if (!damaged) return;
        float k = fabsf(sinf(p.x * 13 + sinf(p.y * 7) * 2.2f) * sinf(p.y * 11 + cosf(p.x * 9) * 1.7f));
        if (k < 0.05f) { c = rgb(255, 130, 40); e = 1.1f; h -= 0.05f; }
    };
    // pincers / claws
    {
        P.push_back({ [=](Vector2 p) {
                          Vector2 q = rot(Vector2Subtract(M(p), { 0.48f, 0.22f }), -open);
                          return std::min(sdCapsule(q, { 0, 0 }, { 0.26f, 0.42f }, 0.09f, 0.05f),
                                          sdCapsule(q, { 0.26f, 0.42f }, { 0.12f, 0.66f }, 0.05f, 0.015f)); },
                      dark, 0.12f, 0.2f, 0.07f, 0.7f, 0.8f, 0, 1,
                      [=](Vector2 p, float, Vector3& c, float& h, float& e) { cracks(p, c, h, e); } });
    }
    // rear spikes
    P.push_back({ [](Vector2 p) { Vector2 q = M(p); return sdPoly(q, { { 0.18f, -0.5f }, { 0.42f, -0.92f }, { 0.4f, -0.45f } }); },
                  dark, 0.1f, 0.15f, 0.06f, 0.7f, 0.8f });
    // carapace shield
    P.push_back({ [](Vector2 p) {
                      float a = sdEllipse(p, { 0, -0.12f }, { 0.6f, 0.5f });
                      float b = sdPoly(M(p), { { 0.0f, -0.7f }, { 0.78f, -0.18f }, { 0.55f, 0.25f }, { 0.0f, 0.3f } });
                      return smin(a, b, 0.12f); },
                  armor, 0.22f, 0.35f, 0.3f, 0.75f, 0.65f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      Vector2 q = M(p);
                      if (q.x < 0.012f) { h -= 0.05f; c = Vector3Scale(c, 0.4f); }
                      float plate = (p.y + 0.12f) * 5.5f - floorf((p.y + 0.12f) * 5.5f);
                      if (plate < 0.06f && q.x > 0.05f) { h -= 0.04f; c = Vector3Scale(c, 0.45f); }
                      if (fabsf(q.x - 0.32f - p.y * 0.25f) < 0.01f) { h -= 0.03f; c = Vector3Scale(c, 0.5f); }
                      if (d > -0.045f) { c = gold; }
                      c = Vector3Scale(c, 0.93f + 0.07f * hash2(q.x * 50, p.y * 50));
                      cracks(p, c, h, e);
                  } });
    // power core
    P.push_back({ [](Vector2 p) { return sdCircle(p, { 0, -0.18f }, 0.11f); },
                  damaged ? rgb(255, 90, 60) : rgb(255, 230, 90), 0.62f, 0.12f, 0.08f, 0.1f, 1.0f, 1.3f });
    // head plate
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, 0.42f }, { 0.27f, 0.21f }); },
                  dark, 0.45f, 0.22f, 0.14f, 0.7f, 0.8f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) { if (d > -0.03f) c = gold; cracks(p, c, h, e); } });
    // mandibles
    P.push_back({ [=](Vector2 p) { Vector2 q = rot(Vector2Subtract(M(p), { 0.1f, 0.58f }), -open * 0.8f);
                                   return sdCapsule(q, { 0, 0 }, { -0.05f, 0.28f }, 0.045f, 0.01f); },
                  gold, 0.4f, 0.1f, 0.04f, 1.0f, 0.9f });
    // eyes
    P.push_back({ [](Vector2 p) { return sdEllipse(M(p), { 0.13f, 0.47f }, { 0.075f, 0.055f }, -25); },
                  eye, 0.65f, 0.08f, 0.05f, 0, 1, 1.5f });
    return render(N, P, damaged ? rgb(220, 150, 255) : rgb(140, 255, 190));
}

// =============================================================== BOSSES ==
// Ten data-driven capital ships (one every 5th stage). All face DOWN.
// Gameplay reads the same design data for gun, core, laser and engine positions.
struct BossDesign {
    const char* name;
    const char* title;
    int hull, wings;                         // shape ids (see bossHullSdf / bossWingSdf)
    Vector3 hullCol, hull2Col, trim, accent, core, engine, glass;
    std::vector<Vector2> engines, turrets;   // unit coords; engines exhaust UP (-y)
    Vector2 coreP; float coreR;
    Vector2 muzzle;                          // prow laser / cannon
    bool organic;
    float size;                              // drawn size in logical units
};

inline const std::vector<BossDesign>& bossDesigns() {
    static const std::vector<BossDesign> D = {
        { "VANGUARD", "Picket Dreadnought", 0, 1,
          rgb(92, 88, 112), rgb(66, 62, 84), rgb(225, 180, 70), rgb(190, 30, 50), rgb(255, 60, 200), rgb(200, 90, 255), rgb(140, 20, 40),
          { { -0.24f, -0.5f }, { 0.24f, -0.5f } }, { { -0.62f, 0.06f }, { 0.62f, 0.06f } }, { 0, 0.1f }, 0.085f, { 0, 0.55f }, false, 250 },
        { "TALON", "Strike Carrier", 1, 4,
          rgb(150, 105, 60), rgb(95, 65, 40), rgb(255, 200, 90), rgb(255, 120, 30), rgb(255, 150, 40), rgb(255, 170, 60), rgb(40, 30, 20),
          { { -0.3f, -0.45f }, { 0, -0.4f }, { 0.3f, -0.45f } }, { { -0.42f, 0.0f }, { 0.42f, 0.0f } }, { 0, -0.05f }, 0.08f, { 0, 0.58f }, false, 260 },
        { "ECLIPSE", "Void Crescent", 2, 0,
          rgb(40, 38, 50), rgb(25, 24, 32), rgb(240, 200, 90), rgb(255, 220, 120), rgb(255, 250, 220), rgb(255, 230, 150), rgb(255, 200, 80),
          { { -0.35f, -0.36f }, { 0.35f, -0.36f } }, { { -0.5f, 0.3f }, { 0.5f, 0.3f } }, { 0, -0.12f }, 0.1f, { 0, 0.1f }, false, 270 },
        { "HYDRA", "Hive Mother", 3, 5,
          rgb(70, 150, 70), rgb(35, 90, 45), rgb(180, 255, 120), rgb(120, 40, 140), rgb(190, 255, 80), rgb(140, 255, 100), rgb(220, 60, 60),
          { { -0.2f, -0.48f }, { 0.2f, -0.48f } }, { { -0.3f, 0.12f }, { 0.3f, 0.12f }, { 0, 0.36f } }, { 0, -0.08f }, 0.11f, { 0, 0.4f }, true, 270 },
        { "BASTION", "Orbital Fortress", 4, 1,
          rgb(80, 105, 140), rgb(50, 66, 92), rgb(200, 220, 240), rgb(60, 160, 255), rgb(80, 200, 255), rgb(90, 180, 255), rgb(30, 60, 120),
          { { -0.3f, -0.45f }, { 0, -0.5f }, { 0.3f, -0.45f } }, { { -0.62f, 0.08f }, { -0.25f, 0.3f }, { 0.25f, 0.3f }, { 0.62f, 0.08f } },
          { 0, 0 }, 0.12f, { 0, 0.48f }, false, 280 },
        { "MANTIS", "Blade Interceptor", 5, 2,
          rgb(40, 140, 140), rgb(25, 85, 90), rgb(160, 255, 230), rgb(20, 60, 60), rgb(80, 255, 220), rgb(80, 255, 220), rgb(200, 255, 80),
          { { -0.12f, -0.55f }, { 0.12f, -0.55f } }, { { -0.78f, -0.22f }, { 0.78f, -0.22f } }, { 0, 0.05f }, 0.075f, { 0, 0.62f }, false, 270 },
        { "LEVIATHAN", "Siege Cruiser", 6, 1,
          rgb(85, 55, 120), rgb(55, 35, 80), rgb(210, 170, 255), rgb(170, 60, 255), rgb(200, 90, 255), rgb(170, 110, 255), rgb(60, 20, 90),
          { { -0.18f, -0.68f }, { 0.18f, -0.68f }, { -0.42f, -0.35f }, { 0.42f, -0.35f } },
          { { -0.33f, -0.1f }, { 0.33f, -0.1f }, { -0.33f, 0.22f }, { 0.33f, 0.22f } }, { 0, -0.2f }, 0.09f, { 0, 0.68f }, false, 290 },
        { "CATACLYSM", "Twin Hull Annihilator", 7, 0,
          rgb(70, 30, 34), rgb(40, 16, 20), rgb(255, 110, 70), rgb(255, 40, 30), rgb(255, 90, 40), rgb(255, 120, 60), rgb(255, 60, 40),
          { { -0.3f, -0.62f }, { 0.3f, -0.62f } }, { { -0.3f, 0.55f }, { 0.3f, 0.55f }, { 0, 0.18f } }, { 0, -0.02f }, 0.1f, { 0, 0.3f }, false, 290 },
        { "SERAPH", "Radiant Throne", 8, 3,
          rgb(225, 225, 235), rgb(180, 180, 200), rgb(255, 210, 90), rgb(255, 230, 140), rgb(255, 255, 210), rgb(255, 240, 170), rgb(100, 180, 255),
          { { -0.2f, -0.45f }, { 0.2f, -0.45f } }, { { -0.78f, 0.0f }, { 0.78f, 0.0f }, { -0.4f, 0.66f }, { 0.4f, 0.66f } }, { 0, 0 }, 0.14f, { 0, 0.45f }, false, 300 },
        { "OBLIVION", "Void Emperor", 9, 4,
          rgb(30, 22, 36), rgb(16, 10, 20), rgb(255, 40, 70), rgb(200, 0, 40), rgb(255, 30, 60), rgb(255, 40, 90), rgb(255, 20, 50),
          { { -0.5f, -0.55f }, { 0.5f, -0.55f }, { 0, -0.62f } }, { { -0.6f, 0.45f }, { 0.6f, 0.45f }, { -0.82f, -0.05f }, { 0.82f, -0.05f } },
          { 0, 0 }, 0.15f, { 0, 0.45f }, false, 320 },
    };
    return D;
}

inline float bossHullSdf(int id, Vector2 p) {
    Vector2 q = M(p);
    switch (id) {
        case 0: return smin(sdEllipse(p, { 0, -0.08f }, { 0.52f, 0.27f }),
                            sdPoly(p, { { -0.3f, -0.12f }, { 0.3f, -0.12f }, { 0.13f, 0.44f }, { -0.13f, 0.44f } }), 0.1f);
        case 1: return sdPoly(p, { { 0, 0.6f }, { 0.55f, -0.22f }, { 0.32f, -0.45f }, { 0, -0.3f }, { -0.32f, -0.45f }, { -0.55f, -0.22f } }) - 0.03f;
        case 2: return std::max(sdEllipse(p, { 0, 0 }, { 0.68f, 0.42f }), -sdEllipse(p, { 0, 0.33f }, { 0.48f, 0.3f }));
        case 3: { float d = sdCircle(p, { 0, -0.08f }, 0.3f);
                  d = smin(d, sdCircle(q, { 0.3f, 0.06f }, 0.19f), 0.12f);
                  d = smin(d, sdCircle(p, { 0, 0.26f }, 0.17f), 0.12f);
                  d = smin(d, sdCircle(q, { 0.18f, -0.36f }, 0.15f), 0.12f);
                  return smin(d, sdCapsule(q, { 0.3f, 0.12f }, { 0.4f, 0.48f }, 0.08f, 0.02f), 0.06f); }
        case 4: { std::vector<Vector2> h; for (int i = 0; i < 6; ++i) { float a = (i * 60 + 30) * DEG2RAD; h.push_back({ cosf(a) * 0.5f, sinf(a) * 0.5f }); }
                  return std::min(sdPoly(p, h) - 0.02f, sdPoly(p, { { -0.12f, 0.3f }, { 0.12f, 0.3f }, { 0.06f, 0.52f }, { -0.06f, 0.52f } })); }
        case 5: return std::min(sdCapsule(p, { 0, -0.5f }, { 0, 0.32f }, 0.15f, 0.1f),
                                sdPoly(p, { { -0.16f, 0.25f }, { 0.16f, 0.25f }, { 0, 0.66f } }));
        case 6: { float d = sdCapsule(p, { 0, -0.62f }, { 0, 0.6f }, 0.2f, 0.12f);
                  d = smin(d, sdEllipse(q, { 0.3f, -0.1f }, { 0.12f, 0.2f }), 0.06f);
                  return smin(d, sdEllipse(q, { 0.3f, 0.22f }, { 0.1f, 0.15f }), 0.06f); }
        case 7: { float d = sdCapsule(q, { 0.3f, -0.58f }, { 0.3f, 0.52f }, 0.14f, 0.09f);
                  d = std::min(d, sdPoly(p, { { -0.3f, -0.16f }, { 0.3f, -0.16f }, { 0.3f, 0.12f }, { -0.3f, 0.12f } }));
                  return smin(d, sdCircle(p, { 0, -0.02f }, 0.2f), 0.05f); }
        case 8: return std::min(sdCircle(p, { 0, 0 }, 0.42f), sdPoly(p, { { -0.1f, 0.35f }, { 0.1f, 0.35f }, { 0, 0.55f } }));
        case 9: { float d = sdCircle(p, { 0, 0 }, 0.3f);
                  for (int i = 0; i < 4; ++i) { float a = (45 + i * 90) * DEG2RAD;
                      d = smin(d, sdCapsule(p, { 0, 0 }, { cosf(a) * 0.82f, sinf(a) * 0.82f }, 0.16f, 0.03f), 0.08f); }
                  return smin(d, sdCapsule(p, { 0, 0 }, { 0, 0.5f }, 0.12f, 0.03f), 0.05f); }
    }
    return 1;
}

inline float bossWingSdf(int id, Vector2 p) {
    Vector2 q = M(p);
    switch (id) {
        case 1: return sdPoly(q, { { 0.25f, -0.24f }, { 0.94f, 0.02f }, { 0.93f, 0.2f }, { 0.78f, 0.24f }, { 0.25f, 0.16f } }) - 0.015f;
        case 2: return std::min(sdPoly(q, { { 0.1f, 0.05f }, { 0.9f, -0.42f }, { 0.94f, -0.3f }, { 0.14f, 0.26f } }),
                                sdPoly(q, { { 0.1f, 0.22f }, { 0.6f, 0.5f }, { 0.55f, 0.58f }, { 0.08f, 0.34f } }));
        case 3: return std::max(fabsf(Vector2Length(p) - 0.78f) - 0.05f, -(p.y + 0.62f));
        case 4: { float d = 10; for (int i = 0; i < 3; ++i) { float y = -0.3f + i * 0.28f;
                      d = std::min(d, sdPoly(q, { { 0.15f, y - 0.07f }, { 0.97f, y + 0.18f }, { 0.15f, y + 0.07f } })); }
                  return d; }
        case 5: { float d = sdEllipse(q, { 0.55f, -0.08f }, { 0.42f, 0.24f }, 10);
                  for (float x : { 0.36f, 0.58f, 0.8f }) d = std::max(d, -sdCircle(q, { x, 0.2f + (x - 0.58f) * 0.2f }, 0.1f));
                  return d; }
    }
    return 1;
}

// ---- HD detail helpers -------------------------------------------------------
// Plated, greebled, weathered hull surface. sc: detail scale; wing: lighter detailing; seed: per-ship variation.
inline void hullDetail(Vector2 p, float d, Vector3& c, float& h, float& e, const struct BossDesign& B, float sc, float seed, bool wing);
inline void chitinDetail(Vector2 p, float d, Vector3& c, float& h, float& e, const struct BossDesign& B, float sc);

// Outermost point of a boss silhouette (starboard side, unit coords): where its navigation lights sit.
inline Vector2 bossNavTip(int idx) {
    const BossDesign& B = bossDesigns()[idx];
    Vector2 tip{ 0, 0 };
    for (float y = -0.95f; y <= 0.95f; y += 0.01f) for (float x = 0.95f; x > tip.x; x -= 0.01f) {
        float dd = std::min(bossHullSdf(B.hull, { x, y }), B.wings ? bossWingSdf(B.wings, { x, y }) : 1.f);
        if (dd < -0.035f) { tip = { x, y }; break; }
    }
    return tip;
}

inline Texture2D bossShip(int idx, int N, std::vector<unsigned char>* mask) {
    const BossDesign& B = bossDesigns()[idx];
    std::vector<Part> P;
    bool org = B.organic;
    float seed = 7.3f * (idx + 1);
    Vector3 gun = rgb(52, 54, 64);
    // engines: ribbed pods with heat-tinted collars, nozzles with hot cores
    for (Vector2 e : B.engines) {
        P.push_back({ [=](Vector2 p) { return sdCapsule(p, { e.x, e.y + 0.22f }, { e.x, e.y }, 0.095f, 0.075f); },
                      B.hull2Col, 0.14f, 0.18f, 0.07f, org ? 0.2f : 0.85f, 0.7f, 0, 1,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& em) {
                          if (org) { chitinDetail(p, d, c, h, em, B, 0.6f); return; }
                          float f = p.y * 26 - floorf(p.y * 26);
                          if (f < 0.16f) { c = Vector3Scale(c, 0.45f); h -= 0.015f; }
                          else if (f < 0.3f) { h += 0.004f; c = Vector3Scale(c, 1.12f); }
                          float collar = sstep(e.y + 0.08f, e.y, p.y);                 // heat discolouration near the nozzle
                          c = mix(c, mix(rgb(70, 60, 110), Vector3Scale(B.engine, 0.6f), 0.4f), 0.55f * collar);
                          if (fabsf(p.x - e.x) < 0.012f && p.y > e.y + 0.05f) { c = Vector3Scale(c, 0.6f); h -= 0.006f; }  // fuel line
                          if (d > -0.012f) c = Vector3Scale(c, 1.25f);                   // worn edge
                      } });
        P.push_back({ [=](Vector2 p) { return sdEllipse(p, { e.x, e.y - 0.02f }, { 0.075f, 0.038f }); },
                      B.engine, 0.2f, 0.04f, 0.03f, 0, 0.3f, 1.6f, 1,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& em) {
                          float r = sat(-d / 0.035f);                                   // 0 at rim -> 1 at centre
                          c = mix(Vector3Scale(B.engine, 0.55f), mix(B.engine, { 1, 1, 1 }, 0.7f), r * r);
                          em = 0.5f + 1.6f * r;
                          if (fabsf(r - 0.35f) < 0.06f) { c = Vector3Scale(c, 0.4f); em *= 0.4f; h -= 0.01f; }   // nozzle vane ring
                      } });
    }
    // wings
    if (B.wings)
        P.push_back({ [=](Vector2 p) { return bossWingSdf(B.wings, p); },
                      B.hull2Col, 0.12f, 0.14f, 0.07f, org ? 0.15f : 0.8f, org ? 0.8f : 0.6f, 0, org ? 0.9f : 1.0f,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                          if (org) { chitinDetail(p, d, c, h, e, B, 0.8f);
                                     if (d > -0.03f) c = Vector3Scale(B.accent, 0.8f);
                                     float v = fabsf(sinf(atan2f(p.y, fabsf(p.x)) * 10)); if (v < 0.08f) { c = B.trim; e = 0.25f; } return; }
                          hullDetail(p, d, c, h, e, B, 0.8f, seed + 3, true);
                          if (d > -0.03f) c = Vector3Scale(B.accent, 0.92f + 0.12f * fbm({ p.x * 40, p.y * 40 }, 2));   // painted leading edge
                          if (d > -0.03f && d < -0.024f) { c = Vector3Scale(c, 0.5f); h -= 0.006f; }
                          if (B.wings == 3 && fabsf(sinf(atan2f(p.y, p.x) * 24)) < 0.15f) { c = B.trim; e = 0.5f; }
                      } });
    // turret mounts with twin barrels
    for (Vector2 t : B.turrets) {
        for (float sx : { -0.022f, 0.022f })
            P.push_back({ [=](Vector2 p) { return sdCapsule(p, { t.x + sx, t.y }, { t.x + sx, t.y + 0.21f }, 0.017f, 0.013f); },
                          gun, 0.3f, 0.06f, 0.03f, 0.9f, 0.85f, 0, 1,
                          [=](Vector2 p, float, Vector3& c, float& h, float& em) {
                              float f = (p.y - t.y) * 60; f -= floorf(f); if (f < 0.2f) { c = Vector3Scale(c, 0.55f); h -= 0.004f; }
                              if (p.y > t.y + 0.19f) { c = rgb(20, 20, 24); em = 0; }   // muzzle bore
                          } });
        P.push_back({ [=](Vector2 p) { return sdCircle(p, t, 0.075f); },
                      org ? B.accent : B.hullCol, 0.32f, 0.12f, 0.06f, org ? 0.2f : 0.85f, 0.85f, org ? 0.4f : 0.f, 1,
                      [=](Vector2 p, float d, Vector3& c, float& h, float& em) {
                          Vector2 r = Vector2Subtract(p, t); float rr = Vector2Length(r), a = atan2f(r.y, r.x);
                          if (d > -0.015f) { c = B.trim; if (sinf(a * 12) > 0.7f) { c = Vector3Scale(c, 0.6f); h -= 0.005f; } }   // bolted ring
                          else if (fabsf(rr - 0.036f) < 0.005f) { c = Vector3Scale(c, 0.45f); h -= 0.008f; }
                          else if (rr < 0.03f) { c = Vector3Scale(c, 1.1f); h += 0.01f; }
                          if (!org && fabsf(r.x) < 0.006f && r.y < -0.02f && r.y > -0.055f) { c = B.core; em = 1.2f; }   // sensor
                      } });
    }
    // main hull
    P.push_back({ [=](Vector2 p) { return bossHullSdf(B.hull, p); },
                  B.hullCol, 0.22f, 0.32f, 0.2f, org ? 0.15f : 0.8f, org ? 0.85f : 0.65f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      if (org) { chitinDetail(p, d, c, h, e, B, 1.0f); if (d > -0.03f) c = Vector3Scale(c, 0.6f); return; }
                      hullDetail(p, d, c, h, e, B, 1.0f, seed, false);
                      Vector2 q = M(p);
                      if (q.x < 0.012f && p.y > -0.4f) { h -= 0.03f; c = Vector3Scale(c, 0.35f); }        // keel trench
                      if (q.x < 0.012f && p.y > -0.4f && fmodf(p.y + 2, 0.05f) < 0.008f) { c = B.core; e = 1.0f; }   // trench lights
                      if (fabsf(q.x - 0.22f) < 0.02f && p.y > -0.3f && p.y < 0.3f) {                  // racing stripes
                          c = Vector3Scale(B.accent, 0.85f * (0.85f + 0.3f * fbm({ p.x * 30, p.y * 30 }, 2))); }
                      if (d > -0.025f) { c = Vector3Scale(B.trim, 0.92f + 0.12f * fbm({ p.x * 40, p.y * 40 }, 2));
                                         if (d < -0.019f) { c = Vector3Scale(c, 0.55f); h -= 0.006f; } }
                  } });
    // armoured ring + reactor core
    P.push_back({ [=](Vector2 p) { return sdCircle(p, B.coreP, B.coreR + 0.055f); },
                  B.hull2Col, 0.5f, 0.12f, 0.06f, org ? 0.2f : 0.9f, 0.85f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& em) {
                      float a = atan2f(p.y - B.coreP.y, p.x - B.coreP.x);
                      float rr = Vector2Distance(p, B.coreP);
                      if (sinf(a * 8) > 0.6f) { c = org ? B.accent : B.trim; h += 0.02f; }
                      if (fabsf(sinf(a * 8)) < 0.08f) { c = Vector3Scale(c, 0.4f); h -= 0.01f; }       // segment gaps
                      if (!org && sinf(a * 8) <= 0.6f && fabsf(rr - (B.coreR + 0.028f)) < 0.006f && sinf(a * 32) > 0.3f) {
                          c = B.core; em = 0.9f; }                                                    // conduit lights
                      if (d > -0.008f) c = Vector3Scale(c, 0.6f);
                  } });
    P.push_back({ [=](Vector2 p) { return sdCircle(p, B.coreP, B.coreR); }, B.core, 0.62f, 0.12f, 0.08f, 0.1f, 1.0f, 1.6f, 1,
                  [=](Vector2 p, float d, Vector3& c, float&, float& em) {
                      float r = sat(-d / B.coreR); float a = atan2f(p.y - B.coreP.y, p.x - B.coreP.x);
                      c = mix(B.core, { 1, 1, 1 }, r * r * 0.8f);
                      em = 1.2f + 1.0f * r + 0.25f * sinf(a * 6 + r * 9);                         // swirling plasma
                  } });
    // bridge: glass canopy with lit window rows / organic eyes
    Vector2 br{ B.coreP.x, B.coreP.y - B.coreR - 0.12f };
    if (org) {
        P.push_back({ [=](Vector2 p) { return sdEllipse(M(p), { 0.1f, br.y + 0.02f }, { 0.06f, 0.04f }, -20); },
                      B.glass, 0.55f, 0.08f, 0.04f, 0.1f, 1.0f, 1.2f, 1,
                      [=](Vector2 p, float d, Vector3& c, float&, float& em) {
                          float r = sat(-d / 0.035f); c = mix(Vector3Scale(B.glass, 0.6f), rgb(255, 230, 120), r * r); em = 0.8f + r;
                          if (fabsf(M(p).x - 0.1f) < 0.006f) { c = rgb(10, 5, 5); em = 0; }   // slit pupil
                      } });
    } else {
        P.push_back({ [=](Vector2 p) { return sdEllipse(p, br, { 0.14f, 0.065f }); },
                      B.glass, 0.55f, 0.12f, 0.06f, 0.2f, 1.0f, 0.35f, 1,
                      [=](Vector2 p, float d, Vector3& c, float&, float& em) {
                          if (d > -0.012f) { c = B.trim; em = 0; return; }                   // frame
                          float wx = (p.x - br.x) * 95, wy = (p.y - br.y) * 95;
                          bool frame = fabsf(sinf(wx)) < 0.25f || fabsf(sinf(wy * 1.3f)) < 0.2f;
                          if (frame) { c = Vector3Scale(B.trim, 0.5f); em = 0; }
                          else { bool lit = hash2(floorf(wx / PI), floorf(wy * 1.3f / PI)) > 0.15f;
                                 c = lit ? mix(rgb(255, 220, 160), B.glass, 0.35f) : Vector3Scale(B.glass, 0.5f); em = lit ? 0.9f : 0.15f; }
                      } });
    }
    // navigation lights at the outermost points of the silhouette (red port, green starboard)
    if (!org) {
        Vector2 tip = bossNavTip(idx);
        for (int sgn : { -1, 1 }) {
            Vector2 lp{ tip.x * sgn, tip.y };
            Vector3 lc = sgn < 0 ? rgb(255, 50, 50) : rgb(60, 255, 110);
            P.push_back({ [=](Vector2 p) { return sdCircle(p, lp, 0.016f); }, lc, 0.4f, 0.03f, 0.02f, 0, 1, 1.8f });
        }
    }
    // prow cannon
    Vector2 m = B.muzzle;
    P.push_back({ [=](Vector2 p) { return sdCapsule(p, { m.x, m.y - 0.24f }, m, 0.048f, 0.03f); }, B.trim, 0.45f, 0.1f, 0.04f, 1.0f, 0.9f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float&) {
                      float f = (p.y - m.y) * 45; f -= floorf(f); if (f < 0.2f) { c = Vector3Scale(c, 0.5f); h -= 0.006f; }   // cooling rings
                      if (fabsf(p.x - m.x) < 0.006f) c = Vector3Scale(c, 1.3f);
                      if (d > -0.008f) c = Vector3Scale(c, 0.7f);
                  } });
    P.push_back({ [=](Vector2 p) { return sdCircle(p, m, 0.03f); }, B.core, 0.5f, 0.03f, 0.02f, 0, 1, 1.4f });
    return render(N, P, Vector3Scale(B.core, 0.8f), mask, true);
}

inline void hullDetail(Vector2 p, float d, Vector3& c, float& h, float& e, const BossDesign& B, float sc, float seed, bool wing) {
    Vector2 q = M(p);
    // --- plating: staggered rows of plates, some split in two; raised/recessed with bevelled seams
    float rowH = 0.12f * sc, ry = (p.y + 1.3f) / rowH, row = floorf(ry);
    float colW = (0.14f + 0.12f * hash2(row, seed)) * sc, off = hash2(seed, row) * colW;
    float rx = (q.x + off) / colW, col = floorf(rx);
    float lx = rx - col, ly = ry - row, pw = colW, ph = rowH, sub = 0;
    float r = hash2(row * 1.3f + seed, col * 2.1f);
    if (r < 0.35f)      { if (ly < 0.5f) ly *= 2; else { ly = ly * 2 - 1; sub = 1; } ph *= 0.5f; }
    else if (r > 0.72f) { if (lx < 0.5f) lx *= 2; else { lx = lx * 2 - 1; sub = 2; } pw *= 0.5f; }
    float sdx = std::min(lx, 1 - lx) * pw, sdy = std::min(ly, 1 - ly) * ph, sd = std::min(sdx, sdy);
    float pid = hash2(row + sub * 7.1f, col + seed);
    float bev = sstep(0.0f, 0.011f, sd);
    h += ((pid - 0.5f) * 0.016f + 0.005f) * bev;
    c = Vector3Scale(c, 0.93f + 0.16f * pid);
    if (pid > 0.9f) c = mix(c, B.hull2Col, 0.65f);                                  // replacement plates
    if (sd < 0.004f) { h -= 0.008f; c = Vector3Scale(c, 0.4f); }
    float along = sdx < sdy ? ly * ph : lx * pw;
    (void)along;

    // --- window rows (hull only)
    if (!wing && d < -0.07f && hash2(row, seed + 3) < 0.08f && ly > 0.42f && ly < 0.58f && sd > 0.012f) {
        float wx = fmodf(q.x + 2, 0.022f);
        if (wx < 0.008f) {
            bool lit = hash2(floorf((q.x + 2) / 0.022f), row) > 0.3f;
            c = lit ? rgb(235, 190, 130) : Vector3Scale(c, 0.35f); e = lit ? 0.7f : 0; h -= 0.004f;
            return;
        }
    }
    // --- greebles on a finer grid (kept away from the outline)
    if (d < -0.035f) {
        float gs = 0.058f * sc;
        float gxf = q.x / gs, gyf = (p.y + 1.3f) / gs, gx = floorf(gxf), gy = floorf(gyf);
        float u = gxf - gx - 0.5f, v = gyf - gy - 0.5f;
        float gr = hash2(gx * 3.7f + seed, gy * 1.9f + seed * 0.3f);
        float dens = wing ? 0.45f : 0.7f;
        if (gr < 0.10f * dens) {                                                        // raised equipment box
            float bw = 0.26f + 0.16f * hash2(gx, gy + 3), bh = 0.2f + 0.2f * hash2(gx + 5, gy);
            float bd = std::max(fabsf(u) - bw, fabsf(v) - bh);
            if (bd < 0) { h += 0.02f * sstep(0, 0.07f, -bd); c = Vector3Scale(c, bd > -0.05f ? 0.62f : 0.8f);
                          if (fabsf(u) < bw - 0.08f && fabsf(v) < 0.04f) c = Vector3Scale(c, 0.6f); }
        } else if (gr < 0.18f * dens) {                                                 // vent grille
            if (fabsf(u) < 0.4f && fabsf(v) < 0.32f) {
                float sl = (v + 0.32f) * 7; sl -= floorf(sl);
                c = Vector3Scale(c, 0.62f);
                if (sl < 0.45f) { h -= 0.01f; c = Vector3Scale(c, 0.3f); }
            }
        } else if (gr < 0.23f * dens) {                                                 // round hatch
            float rr = sqrtf(u * u + v * v);
            if (fabsf(rr - 0.33f) < 0.05f) { h -= 0.008f; c = Vector3Scale(c, 0.45f); }
            else if (rr < 0.28f) { h += 0.004f; c = Vector3Scale(c, 0.92f); if (fabsf(u) < 0.04f || fabsf(v) < 0.04f) c = Vector3Scale(c, 0.8f); }
        } else if (gr < 0.30f * dens) {                                                 // pipe run
            if (fabsf(v) < 0.12f) { float t = v / 0.12f; h += 0.016f * sqrtf(std::max(0.f, 1 - t * t));
                                    c = mix(Vector3Scale(c, 0.7f), rgb(160, 160, 170), 0.35f);
                                    float j = u + 0.5f; j = j * 2 - floorf(j * 2); if (j < 0.1f) c = Vector3Scale(c, 0.55f); }
        } else if (gr < 0.33f * dens) {                                                 // marker / beacon light
            if (u * u + v * v < 0.014f) { c = hash2(gx, gy * 3) > 0.5f ? B.core : rgb(255, 240, 220); e = 1.3f; }
        }
    }
    // --- weathering: grime streaks, dusty mottling, soot behind engines, chipped edges
    float streak = fbm({ q.x * 60 + seed, p.y * 3.5f }, 3);
    float dust = fbm({ p.x * 8 + seed, p.y * 8 }, 4);
    c = Vector3Scale(c, (0.82f + 0.3f * streak) * (0.86f + 0.24f * dust));
    for (Vector2 en : B.engines) {
        float dd = Vector2Distance(p, { en.x, en.y + 0.25f });
        c = Vector3Scale(c, 1 - 0.35f * expf(-dd * dd / 0.012f));
    }
    if (d > -0.05f && hash2(floorf(p.x * 160), floorf(p.y * 160)) > 0.95f) c = Vector3Scale(c, 1.15f);
}

inline void chitinDetail(Vector2 p, float d, Vector3& c, float& h, float& e, const BossDesign& B, float sc) {
    Vector2 q = M(p);
    float s = 0.07f * sc, gxf = q.x / s, gyf = (p.y + 1.3f) / s, gx = floorf(gxf), gy = floorf(gyf);
    float f1 = 9, f2 = 9, id = 0;
    for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
        float cx = gx + ox, cy = gy + oy;
        float px = cx + 0.15f + 0.7f * hash2(cx, cy), py = cy + 0.15f + 0.7f * hash2(cy * 1.3f, cx);
        float dd = sqrtf((gxf - px) * (gxf - px) + (gyf - py) * (gyf - py));
        if (dd < f1) { f2 = f1; f1 = dd; id = hash2(cx + 3, cy); } else if (dd < f2) f2 = dd;
    }
    float border = f2 - f1;
    h += 0.028f * (1 - std::min(1.f, f1 * 1.15f));                                   // domed scales
    c = Vector3Scale(c, 0.7f + 0.45f * id);
    if (border < 0.09f) { h -= 0.018f; c = Vector3Scale(c, 0.3f); }
    else if (border < 0.16f) c = Vector3Scale(c, 0.7f);
    if (id > 0.92f && f1 < 0.28f && d < -0.03f) { float g = 1 - f1 / 0.28f; c = mix(c, B.core, g); e = 1.3f * g; h += 0.01f * g; }   // glowing pustules
    float v = fabsf(fbm({ q.x * 6.5f, p.y * 6.5f }, 3) * 2 - 1);
    if (v < 0.035f) { c = mix(c, B.accent, 0.75f); h -= 0.008f; e = std::max(e, 0.15f); }   // veins
    c = Vector3Scale(c, 0.85f + 0.25f * fbm({ p.x * 12, p.y * 12 }, 3));
}

// ============================================================== METEORS ==
// Lumpy cratered rock. variant picks shape/colour; some carry glowing magma veins.
// Variant 6 is the volatile (explosive) rock: rust-copper with ember fissures and a faint inner glow.
inline Texture2D meteor(int N, int variant) {
    float seed = 13.7f * (variant + 1);
    bool hot = variant == 6;
    Vector3 rock = hot ? rgb(118, 72, 58) : variant % 3 == 0 ? rgb(120, 104, 92) : (variant % 3 == 1 ? rgb(96, 92, 100) : rgb(132, 112, 86));
    bool magma = variant == 1 || variant == 4 || hot;
    Vector3 lava = hot ? rgb(255, 105, 45) : rgb(255, 110, 25);
    auto radius = [=](float a) {           // irregular outline from summed harmonics
        float r = 0.78f;
        for (int k = 2; k <= 7; ++k) r += (0.09f / k * 1.6f) * sinf(a * k + seed * k * 0.7f + hash2(seed, (float)k) * 6.28f);
        return r;
    };
    std::vector<Part> P;
    P.push_back({ [=](Vector2 p) { float a = atan2f(p.y, p.x); return (Vector2Length(p) - radius(a)) * 0.9f; },
                  rock, 0.05f, 0.55f, 0.55f, 0.05f, 0.25f, 0, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float& e) {
                      // fbm-ish surface roughness
                      float n = 0, amp = 0.5f, f = 6;
                      for (int o = 0; o < 4; ++o, amp *= 0.5f, f *= 2.1f) n += amp * sinf(p.x * f + seed + sinf(p.y * f * 0.8f + o));
                      h += 0.04f * n;
                      c = Vector3Scale(c, 0.8f + 0.25f * (0.5f + 0.5f * n) + 0.08f * (hash2(p.x * 90, p.y * 90) - 0.5f));
                      // craters
                      for (int k = 0; k < 7; ++k) {
                          Vector2 cc{ (hash2(seed, k * 3.1f) - 0.5f) * 1.1f, (hash2(k * 1.7f, seed) - 0.5f) * 1.1f };
                          float cr = 0.07f + 0.13f * hash2(seed * 2, (float)k);
                          float d = Vector2Distance(p, cc) / cr;
                          float bowl = d < 1 ? (1 - d * d) : 0;
                          float rim = expf(-((d - 1) / 0.22f) * ((d - 1) / 0.22f));
                          h += cr * (-0.45f * bowl + 0.18f * rim);
                          if (d < 1) c = Vector3Scale(c, 0.8f + 0.2f * d);
                      }
                      if (magma) {
                          float fq = hot ? 1.5f : 1.0f;
                          float v = fabsf(sinf(p.x * 5 * fq + sinf(p.y * 4 * fq + seed) * 2.2f) * sinf(p.y * 4.5f * fq + cosf(p.x * 3.5f * fq) * 1.8f));
                          float glow = sstep(hot ? 0.09f : 0.06f, 0.0f, v);
                          if (glow > 0) { c = mix(c, lava, glow); e = (hot ? 1.8f : 1.3f) * glow; h -= 0.05f * glow; }
                          if (hot) e = std::max(e, 0.14f);   // faint inner heat: the whole rock glows slightly
                      }
                  } });
    return render(N, P, hot ? rgb(255, 130, 90) : magma ? rgb(255, 140, 80) : rgb(170, 160, 150));
}

// ========================================================= NEW WAVE ENEMIES ==
// DART: sleek crimson interceptor that dives straight down. Faces DOWN.
inline Texture2D dart(int N) {
    Vector3 red = rgb(200, 35, 45), silver = rgb(200, 205, 215), dark = rgb(50, 50, 62);
    std::vector<Part> P;
    P.push_back({ [](Vector2 p) { return sdPoly(M(p), { { 0.0f, -0.25f }, { 0.62f, -0.62f }, { 0.55f, -0.38f }, { 0.12f, 0.2f } }) - 0.01f; },
                  dark, 0.08f, 0.1f, 0.05f, 0.8f, 0.7f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float&, float&) { if (d > -0.03f) c = red; } });
    P.push_back({ [](Vector2 p) { return sdPoly(p, { { 0, 0.9f }, { 0.24f, -0.3f }, { 0.12f, -0.6f }, { -0.12f, -0.6f }, { -0.24f, -0.3f } }) - 0.02f; },
                  silver, 0.2f, 0.28f, 0.14f, 0.85f, 0.85f, 0, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float&) {
                      if (fabsf(p.x) < 0.05f && p.y > -0.45f) { c = red; }
                      if (fabsf(p.y - 0.15f) < 0.012f || fabsf(p.y + 0.25f) < 0.012f) { h -= 0.03f; c = Vector3Scale(c, 0.5f); }
                  } });
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, 0.32f }, { 0.07f, 0.16f }); }, rgb(255, 200, 60), 0.45f, 0.12f, 0.06f, 0.1f, 1, 0.9f });
    P.push_back({ [](Vector2 p) { return sdEllipse(M(p), { 0.08f, -0.62f }, { 0.06f, 0.04f }); }, rgb(255, 120, 40), 0.25f, 0.04f, 0.03f, 0, 0.3f, 1.6f });
    return render(N, P, rgb(255, 150, 150));
}

// SHIELDED: armoured beetle carrying a projected energy shield (shield drawn in-game). Faces DOWN.
inline Texture2D shieldedBug(int N, int frame) {
    Vector3 steel = rgb(70, 140, 150), dark = rgb(30, 60, 70), gold = rgb(210, 180, 80);
    std::vector<Part> P;
    float leg = frame ? 0.06f : -0.04f;
    P.push_back({ [=](Vector2 p) { Vector2 q = M(p); float d = 10;
                      for (int i = 0; i < 3; ++i) { float y = -0.2f + i * 0.22f;
                          d = std::min(d, sdCapsule(q, { 0.3f, y }, { 0.62f, y + 0.12f + leg * (i - 1) }, 0.04f, 0.02f)); }
                      return d; },
                  dark, 0.08f, 0.06f, 0.03f, 0.6f, 0.7f });
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, -0.02f }, { 0.42f, 0.5f }); },
                  steel, 0.22f, 0.4f, 0.3f, 0.75f, 0.75f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float&) {
                      Vector2 q = M(p);
                      if (q.x < 0.012f) { h -= 0.05f; c = Vector3Scale(c, 0.4f); }
                      float hx = sinf(q.x * 22) * sinf(p.y * 22 + q.x * 6);
                      if (fabsf(hx) < 0.08f) { h -= 0.02f; c = Vector3Scale(c, 0.7f); }
                      if (d > -0.035f) c = gold;
                  } });
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, 0.5f }, { 0.22f, 0.14f }); }, dark, 0.4f, 0.15f, 0.08f, 0.6f, 0.8f });
    P.push_back({ [](Vector2 p) { return sdCircle(M(p), { 0.09f, 0.53f }, 0.05f); }, rgb(80, 255, 240), 0.55f, 0.05f, 0.03f, 0, 1, 1.4f });
    P.push_back({ [](Vector2 p) { return sdCapsule(M(p), { 0.12f, 0.6f }, { 0.05f, 0.85f }, 0.04f, 0.01f); }, gold, 0.4f, 0.06f, 0.03f, 1, 0.9f });
    P.push_back({ [](Vector2 p) { return sdCircle(p, { 0, -0.05f }, 0.1f); }, rgb(80, 255, 240), 0.62f, 0.1f, 0.06f, 0.1f, 1, 1.2f });
    return render(N, P, rgb(120, 255, 240));
}

// SPLITTER: twin-lobed jelly creature with a glowing seam; splits in two when destroyed. Faces DOWN.
inline Texture2D splitter(int N, int frame) {
    Vector3 purple = rgb(150, 60, 205), mag = rgb(255, 80, 220);
    float puff = frame ? 1.06f : 0.96f;
    std::vector<Part> P;
    P.push_back({ [=](Vector2 p) { Vector2 q = M(p); float d = 10;
                      for (int i = 0; i < 4; ++i) { float a = (-0.6f + i * 0.35f); d = std::min(d, sdCapsule(q, { 0.25f, 0.25f }, { 0.3f + 0.25f * cosf(a), 0.6f + 0.2f * sinf(a * 3) * puff }, 0.035f, 0.012f)); }
                      return d; },
                  rgb(110, 40, 160), 0.05f, 0.06f, 0.03f, 0.1f, 0.8f, 0.3f, 0.85f });
    P.push_back({ [=](Vector2 p) { return smin(sdCircle(p, { -0.24f, 0 }, 0.36f * puff), sdCircle(p, { 0.24f, 0 }, 0.36f * puff), 0.12f); },
                  purple, 0.2f, 0.45f, 0.35f, 0.1f, 0.95f, 0.15f, 0.95f,
                  [=](Vector2 p, float, Vector3& c, float& h, float& e) {
                      float seam = fabsf(p.x);
                      if (seam < 0.03f) { c = mag; e = 1.4f; h -= 0.08f; }
                      float spots = sinf(p.x * 25) * sinf(p.y * 25);
                      if (spots > 0.85f) c = Vector3Scale(c, 1.3f);
                  } });
    P.push_back({ [](Vector2 p) { return sdCircle(M(p), { 0.24f, 0.18f }, 0.075f); }, rgb(255, 250, 200), 0.6f, 0.08f, 0.05f, 0, 1, 1.0f,
                  1, [](Vector2 p, float, Vector3& c, float&, float&) { if (Vector2Distance(M(p), { 0.24f, 0.21f }) < 0.035f) c = rgb(20, 0, 30); } });
    return render(N, P, rgb(255, 140, 255));
}

// ================================================================ PLANETS ==
// style: 0 rocky, 1 banded gas giant, 2 eclipsed, 3 ice, 4 star, 5 black hole
inline Texture2D planet(int N, int style, Vector3 c1, Vector3 c2, bool ring) {
    std::vector<Part> P;
    const float R = 0.52f;
    auto ringSdf = [](Vector2 p) { return fabsf(sdEllipse(p, { 0, 0 }, { 0.92f, 0.2f }, -14)) - 0.05f; };
    Vector3 ringCol = style == 5 ? rgb(255, 150, 60) : c2;
    float ringEm = style == 5 ? 1.6f : 0.0f;
    if (ring || style == 5)   // back half of the ring
        P.push_back({ [=](Vector2 p) { return rot(p, -14).y < 0 ? ringSdf(p) : 1.0f; }, ringCol, 0.02f, 0.02f, 0.02f, 0.2f, 0.4f, ringEm, 0.8f,
                      [=](Vector2 p, float, Vector3& c, float&, float&) { float b = sinf(Vector2Length(p) * 70); c = Vector3Scale(c, 0.75f + 0.25f * b); } });
    P.push_back({ [=](Vector2 p) { return sdCircle(p, { 0, 0 }, R); },
                  c1, 0.0f, 1.0f, R, 0.05f, style == 3 ? 0.6f : 0.15f, style == 4 ? 1.8f : 0.0f, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float& e) {
                      switch (style) {
                          case 0: {   // rocky: mottled terrain + craters
                              float n = sinf(p.x * 9 + sinf(p.y * 7) * 2) * sinf(p.y * 8 + cosf(p.x * 6));
                              c = mix(c1, c2, 0.5f + 0.5f * n);
                              for (int k = 0; k < 6; ++k) { Vector2 cc{ (hash2(k, 1) - 0.5f) * 0.8f, (hash2(1, k) - 0.5f) * 0.8f };
                                  float d = Vector2Distance(p, cc) / (0.04f + 0.06f * hash2(k, 7)); if (d < 1) c = Vector3Scale(c, 0.7f + 0.3f * d); }
                              break; }
                          case 1: case 3: {   // bands (gas giant) / icy bands with polar caps
                              float b = sinf(p.y * 16 + sinf(p.x * 4 + p.y * 3) * 1.6f);
                              c = mix(c1, c2, 0.5f + 0.5f * b);
                              if (style == 3 && fabsf(p.y) > 0.38f) c = mix(c, rgb(240, 250, 255), 0.8f);
                              break; }
                          case 2: c = Vector3Scale(c1, 0.25f); break;    // eclipsed: nearly black disc, corona from rim light
                          case 4: { float g = 0.7f + 0.3f * sinf(p.x * 20 + sinf(p.y * 15) * 2); c = Vector3Scale(c1, g); e = 1.5f; break; }
                          case 5: c = rgb(0, 0, 0); e = 0; h = 0.0f; break;
                      }
                  } });
    if (ring || style == 5)   // front half of the ring
        P.push_back({ [=](Vector2 p) { return rot(p, -14).y >= 0 ? ringSdf(p) : 1.0f; }, ringCol, 0.6f, 0.02f, 0.02f, 0.2f, 0.4f, ringEm, 0.85f,
                      [=](Vector2 p, float, Vector3& c, float&, float&) { float b = sinf(Vector2Length(p) * 70); c = Vector3Scale(c, 0.75f + 0.25f * b); } });
    Vector3 rim = style == 2 ? rgb(255, 220, 140) : (style == 5 ? rgb(255, 140, 60) : Vector3Scale(c2, 0.6f));
    return render(N, P, rim);
}

// ================================================================ COURIER ==
// Armoured cargo hauler carrying a weapon/upgrade package. Faces DOWN.
// The cargo pod is rendered white-hot; the game tints it with the package colour.
// damaged = outer armour plates blown off, cracks showing.
inline Texture2D courier(int N, bool damaged) {
    Vector3 steel = rgb(78, 84, 100), dark = rgb(44, 48, 60), gold = rgb(230, 185, 70);
    std::vector<Part> P;
    // engines (exhaust up)
    P.push_back({ [](Vector2 p) { return sdCapsule(M(p), { 0.2f, -0.42f }, { 0.2f, -0.66f }, 0.09f, 0.07f); }, dark, 0.12f, 0.16f, 0.07f, 0.85f, 0.7f });
    P.push_back({ [](Vector2 p) { return sdEllipse(M(p), { 0.2f, -0.68f }, { 0.07f, 0.035f }); }, rgb(255, 170, 70), 0.2f, 0.04f, 0.03f, 0, 0.3f, 1.6f });
    // armour plates: inner pair always, outer pair only when intact
    for (int k = 0; k < (damaged ? 1 : 2); ++k) {
        float x0 = 0.3f + k * 0.24f;
        P.push_back({ [=](Vector2 p) { Vector2 q = M(p);
                          return std::min(sdPoly(q, { { x0, -0.36f }, { x0 + 0.24f, -0.28f }, { x0 + 0.24f, -0.03f }, { x0, -0.03f } }),
                                          sdPoly(q, { { x0, 0.03f }, { x0 + 0.24f, 0.03f }, { x0 + 0.24f, 0.28f }, { x0, 0.36f } })) - 0.01f; },
                      steel, 0.14f, 0.16f, 0.06f, 0.8f, 0.65f, 0, 1,
                      [=](Vector2 p, float d, Vector3& c, float& h, float&) {
                          if (d > -0.025f) c = gold;
                          float r = fabsf(sinf(p.x * 47)) * fabsf(sinf(p.y * 31));
                          if (r > 0.92f) { c = Vector3Scale(c, 0.6f); h -= 0.02f; }   // rivets
                      } });
    }
    // main hull (octagon)
    P.push_back({ [](Vector2 p) { return sdPoly(p, { { -0.18f, -0.55f }, { 0.18f, -0.55f }, { 0.32f, -0.38f }, { 0.32f, 0.38f },
                                                     { 0.18f, 0.6f }, { -0.18f, 0.6f }, { -0.32f, 0.38f }, { -0.32f, -0.38f } }) - 0.02f; },
                  steel, 0.24f, 0.32f, 0.18f, 0.8f, 0.7f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      Vector2 q = M(p);
                      if (fabsf(p.y - 0.3f) < 0.025f || fabsf(p.y + 0.3f) < 0.025f) { h -= 0.03f; c = Vector3Scale(c, 0.5f); }
                      if (p.y > 0.4f && p.y < 0.52f && sinf((q.x + p.y) * 40) > 0.2f) c = mix(c, rgb(240, 190, 40), 0.85f);   // hazard stripes
                      if (d > -0.025f) c = gold;
                      if (damaged) {
                          float k = fabsf(sinf(p.x * 15 + sinf(p.y * 9) * 2.0f) * sinf(p.y * 13 + cosf(p.x * 8) * 1.6f));
                          if (k < 0.05f) { c = rgb(255, 120, 40); e = 1.0f; h -= 0.05f; }
                      }
                  } });
    // cargo bay frame + glowing pod
    P.push_back({ [](Vector2 p) { return sdCircle(p, { 0, -0.02f }, 0.24f); }, dark, 0.45f, 0.12f, 0.06f, 0.9f, 0.8f, 0, 1,
                  [=](Vector2 p, float, Vector3& c, float& h, float&) {
                      float a = atan2f(p.y + 0.02f, p.x);
                      if (sinf(a * 6) > 0.5f) { c = gold; h += 0.02f; } } });
    P.push_back({ [](Vector2 p) { return sdCircle(p, { 0, -0.02f }, 0.15f); }, rgb(255, 255, 255), 0.6f, 0.14f, 0.1f, 0.0f, 1.0f, 1.2f });
    // cockpit + chin gun
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, 0.42f }, { 0.11f, 0.06f }); }, rgb(40, 90, 160), 0.5f, 0.1f, 0.05f, 0.2f, 1.0f, 0.2f });
    P.push_back({ [](Vector2 p) { return sdCapsule(p, { 0, 0.55f }, { 0, 0.74f }, 0.04f, 0.025f); }, gold, 0.4f, 0.08f, 0.03f, 1, 0.9f });
    return render(N, P, rgb(255, 210, 140));
}

// Soft radial glow (white, alpha falloff) for particles, bullets and flashes
inline Texture2D glow(int N) {
    Image img = GenImageColor(N, N, BLANK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        float dx = (x + 0.5f) / N * 2 - 1, dy = (y + 0.5f) / N * 2 - 1;
        float r = sqrtf(dx * dx + dy * dy), a = sat(1 - r);
        a = a * a * (0.6f + 0.4f * a);
        px[y * N + x] = { 255, 255, 255, (unsigned char)(a * 255) };
    }
    Texture2D t = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

// Tileable fbm nebula for the background
inline Texture2D nebula(int Wd, int Ht) {
    const int P = 16;
    auto lat = [&](int x, int y, int s) { x = ((x % (P * s)) + P * s) % (P * s); y = ((y % (P * s)) + P * s) % (P * s); return hash2((float)x, (float)y + s * 17.f); };
    auto noise = [&](float x, float y, int s) {
        int ix = (int)floorf(x), iy = (int)floorf(y); float fx = x - ix, fy = y - iy;
        fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
        float a = lat(ix, iy, s), b = lat(ix + 1, iy, s), c = lat(ix, iy + 1, s), d = lat(ix + 1, iy + 1, s);
        return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
    };
    Image img = GenImageColor(Wd, Ht, BLANK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < Ht; ++y) for (int x = 0; x < Wd; ++x) {
        float u = (float)x / Wd * P, v = (float)y / Ht * P, f = 0, amp = 0.5f;
        for (int o = 0, s = 1; o < 4; ++o, s *= 2, amp *= 0.5f) f += amp * noise(u * s, v * s, s);
        float g = 0, amp2 = 0.5f;
        for (int o = 0, s = 1; o < 4; ++o, s *= 2, amp2 *= 0.5f) g += amp2 * noise(u * s + 5.3f * s, v * s + 1.7f * s, s);
        float neb = sstep(0.45f, 0.85f, f);
        Vector3 c = mix(rgb(40, 20, 90), rgb(20, 80, 160), g);
        c = mix(c, rgb(170, 50, 120), sstep(0.55f, 0.8f, g) * 0.6f);
        px[y * Wd + x] = { (unsigned char)(c.x * 255), (unsigned char)(c.y * 255), (unsigned char)(c.z * 255),
                           (unsigned char)(neb * 120) };
    }
    Texture2D t = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

// Wingman drone: compact white/blue escort with homing-missile pods and one engine. Faces UP.
inline Texture2D wingDrone(int N) {
    Vector3 hull = rgb(222, 228, 238), blue = rgb(40, 100, 220), dark = rgb(50, 56, 70), green = rgb(110, 255, 120);
    std::vector<Part> P;
    // stubby forward-swept wings
    P.push_back({ [](Vector2 p) { Vector2 q = M(p); return sdPoly(q, { { 0.1f, 0.05f }, { 0.72f, -0.18f }, { 0.78f, 0.04f }, { 0.62f, 0.3f }, { 0.1f, 0.42f } }) - 0.03f; },
                  hull, 0.1f, 0.12f, 0.08f, 0.55f, 0.65f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      Vector2 q = M(p);
                      if (q.x > 0.5f) c = blue;
                      if (fabsf(q.x - 0.36f) < 0.008f || fabsf(q.y - 0.2f) < 0.007f) { c = Vector3Scale(c, 0.55f); h -= 0.015f; }
                      if (q.x > 0.62f && q.y < 0.0f) { c = green; e = 0.9f; }                 // wingtip lights
                      if (d > -0.02f) c = Vector3Scale(c, 0.8f);
                  } });
    // missile pods under each wing: two green-tipped missiles each
    for (int sgn : { -1, 1 }) for (float off : { -0.05f, 0.05f })
        P.push_back({ [=](Vector2 p) { return sdCapsule(p, { sgn * (0.42f + off), -0.32f }, { sgn * (0.42f + off), 0.16f }, 0.035f, 0.04f); },
                      hull, 0.22f, 0.08f, 0.04f, 0.6f, 0.8f, 0, 1,
                      [=](Vector2 p, float, Vector3& c, float& h, float& e) {
                          if (p.y < -0.22f) { c = green; e = 0.6f; }
                          else if (p.y > 0.08f) c = dark;
                          else if (fabsf(p.y + 0.05f) < 0.012f) { c = Vector3Scale(green, 0.6f); h -= 0.005f; }
                      } });
    // fuselage pod with a sensor dome
    P.push_back({ [](Vector2 p) { return sdCapsule(p, { 0, -0.62f }, { 0, 0.5f }, 0.1f, 0.18f); },
                  hull, 0.3f, 0.3f, 0.15f, 0.5f, 0.85f, 0, 1,
                  [=](Vector2 p, float d, Vector3& c, float& h, float& e) {
                      if (fabsf(p.x) < 0.025f && p.y > -0.1f && p.y < 0.45f) c = blue;
                      for (float ly : { -0.2f, 0.25f }) if (fabsf(p.y - ly) < 0.008f) { c = Vector3Scale(c, 0.5f); h -= 0.02f; }
                      if (p.y < -0.5f) c = blue;
                      c = Vector3Scale(c, 0.94f + 0.08f * fbm({ p.x * 16, p.y * 16 }, 3));
                      (void)d; (void)e;
                  } });
    P.push_back({ [](Vector2 p) { return sdCircle(p, { 0, -0.18f }, 0.085f); }, rgb(60, 200, 255), 0.55f, 0.1f, 0.06f, 0.2f, 1.0f, 0.5f, 1,
                  [](Vector2 p, float d, Vector3& c, float&, float& e) { float r = Vector2Distance(p, { 0, -0.18f }) / 0.085f;
                      c = mix(rgb(220, 250, 255), rgb(30, 140, 230), r); e = 0.4f + 0.8f * (1 - r); if (d > -0.015f) { c = rgb(50, 56, 70); e = 0; } } });
    // engine nozzle
    P.push_back({ [](Vector2 p) { return sdEllipse(p, { 0, 0.55f }, { 0.12f, 0.06f }); }, rgb(120, 200, 255), 0.25f, 0.05f, 0.04f, 0, 0.3f, 1.4f });
    return render(N, P, { 0.45f, 0.65f, 1.f }, nullptr, true);
}


} // namespace spr
