// meteor3d.h - tumbling 3D meteors.
// Each rock is a ray-marched, noise-displaced, cratered ellipsoid, rendered on the GPU once at
// startup into an 8x8 atlas of 64 rotation frames (one full turn about a tilted axis). At runtime
// the frames are cross-faded, so rocks appear to turn smoothly in 3D with fixed lighting.
#pragma once
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>

namespace m3d {

constexpr int COLS = 8, ROWS = 8, FRAMES = COLS * ROWS, CELL = 160;

static const char* FS = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform vec4  cellRect;      // x0, y0, w, h of the cell in atlas UV space
uniform float frame;         // 0..FRAMES-1
uniform float frames;
uniform vec3  axis;
uniform float seed;
uniform vec3  rockCol;
uniform vec3  lavaCol;
uniform vec3  rimCol;
uniform vec3  ell;           // ellipsoid half-extents
uniform float magma;         // 0/1
uniform float hot;           // 0/1 (volatile rock)
uniform float pix;           // one pixel in local units

float hash(vec3 p) { p = fract(p * 0.3183099 + vec3(0.11, 0.17, 0.13)); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float vnoise(vec3 x) {
    vec3 i = floor(x), f = fract(x); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i), hash(i + vec3(1,0,0)), f.x), mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x), mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}
float fbm(vec3 p, int oct) { float s = 0.0, a = 0.5; for (int i = 0; i < oct; i++) { s += a * vnoise(p); p = p * 2.03 + vec3(1.7, 9.2, 3.1); a *= 0.5; } return s; }

vec3  cDir[12]; float cRad[12];
vec3  pDir[4];  float pOff[4];
float crater;   // bowl depth at the last sdf() point (for shading)

float sdf(vec3 q) {
    float base = (length(q / ell) - 1.0) * min(ell.x, min(ell.y, ell.z));
    vec3 s = q + vec3(seed);
    float d = base - (fbm(s * 1.1, 4) - 0.5) * 0.34 - (fbm(s * 5.5, 2) - 0.5) * 0.05;
    for (int k = 0; k < 4; k++) d = max(d, dot(q, pDir[k]) - pOff[k]);   // broken-off faces -> angular chunks
    vec3 n = normalize(q);
    crater = 0.0;
    for (int k = 0; k < 12; k++) {
        float t = length(n - cDir[k]) / cRad[k];
        float bowl = t < 1.0 ? 1.0 - t * t : 0.0;
        float rim = exp(-((t - 1.0) / 0.28) * ((t - 1.0) / 0.28));
        d += cRad[k] * (0.42 * bowl - 0.13 * rim);
        crater = max(crater, bowl);
    }
    return d;
}

mat3 rot(vec3 a, float ang) {
    float c = cos(ang), s = sin(ang), t = 1.0 - c;
    return mat3(t*a.x*a.x + c,     t*a.x*a.y + s*a.z, t*a.x*a.z - s*a.y,
                t*a.x*a.y - s*a.z, t*a.y*a.y + c,     t*a.y*a.z + s*a.x,
                t*a.x*a.z + s*a.y, t*a.y*a.z - s*a.x, t*a.z*a.z + c);
}

void main() {
    vec2 uv = (fragTexCoord - cellRect.xy) / cellRect.zw;          // 0..1 inside the cell
    vec2 p = uv * 2.0 - 1.0;                                         // y down, like the sprites
    for (int k = 0; k < 12; k++) {
        float h1 = hash(vec3(seed, k, 1.3)), h2 = hash(vec3(k, seed, 7.1)), h3 = hash(vec3(2.7, k, seed)), h4 = hash(vec3(seed * 1.7, k * 3.0, 0.5));
        cDir[k] = normalize(vec3(h1, h2, h3) * 2.0 - 1.0);
        cRad[k] = 0.14 + 0.26 * h4 * h4;
    }
    for (int k = 0; k < 4; k++) {
        pDir[k] = normalize(vec3(hash(vec3(k, 4.1, seed)), hash(vec3(seed, 2.3, k)), hash(vec3(5.5, seed, k))) * 2.0 - 1.0);
        pOff[k] = 0.50 + 0.14 * hash(vec3(k, seed, 9.9));
    }
    mat3 R = rot(normalize(axis), frame / frames * 6.2831853);
    mat3 Ri = transpose(R);
    // orthographic ray towards -z
    vec3 ro = vec3(p, 1.3), rd = vec3(0, 0, -1);
    float t = 0.0, minD = 1e9; bool hit = false; vec3 pos;
    for (int i = 0; i < 90; i++) {
        pos = ro + rd * t;
        float d = sdf(Ri * pos);
        minD = min(minD, d);
        if (d < 0.0015) { hit = true; break; }
        t += max(d * 0.6, 0.004);
        if (t > 2.6) break;
    }
    float alpha = hit ? 1.0 : clamp(1.0 - minD / pix, 0.0, 1.0);
    if (alpha <= 0.0) { finalColor = vec4(0.0); return; }
    vec3 q = Ri * pos;
    vec2 e = vec2(0.006, -0.006);
    vec3 nObj = normalize(e.xyy * sdf(q + e.xyy) + e.yyx * sdf(q + e.yyx) + e.yxy * sdf(q + e.yxy) + e.xxx * sdf(q + e.xxx));
    sdf(q);
    float bowl = crater;
    vec3 n = R * nObj;                                              // world-space normal
    vec3 s = q + vec3(seed);
    // albedo: mottled rock, darker crater floors, fine grit
    float m = fbm(s * 3.0 + 5.0, 4);
    vec3 col = rockCol * (0.62 + 0.62 * m) * (1.0 - 0.22 * bowl) * (0.92 + 0.16 * vnoise(s * 28.0));
    float emis = 0.0;
    if (magma > 0.5) {
        float fq = hot > 0.5 ? 1.4 : 1.0;
        vec3 w = s * 1.6 * fq + 11.0;
        w += vec3(fbm(w, 2), fbm(w + 3.1, 2), fbm(w + 7.7, 2)) * 1.2;           // domain warp -> flowing cracks
        float v = abs(vnoise(w) * 2.0 - 1.0);
        float glow = smoothstep(hot > 0.5 ? 0.09 : 0.07, 0.0, v);
        float halo = smoothstep(hot > 0.5 ? 0.30 : 0.22, 0.0, v);
        col = mix(col * (1.0 - 0.35 * halo), lavaCol, glow);
        emis = (hot > 0.5 ? 1.8 : 1.35) * glow + 0.25 * halo * (1.0 - glow);
        if (hot > 0.5) emis = max(emis, 0.14);
    }
    // ambient occlusion from the distance field
    float ao = 0.0;
    for (int k = 1; k <= 3; k++) { float h = 0.035 * float(k); ao += clamp(sdf(q + nObj * h) / h, 0.0, 1.0); }
    ao = 0.35 + 0.65 * ao / 3.0;
    vec3 L = normalize(vec3(-0.45, -0.65, 0.75));
    float diff = clamp((dot(n, L) + 0.08) / 1.08, 0.0, 1.0);
    vec3 H = normalize(L + vec3(0, 0, 1));
    float spec = pow(max(dot(n, H), 0.0), 24.0) * 0.10;
    float fres = pow(1.0 - clamp(n.z, 0.0, 1.0), 2.2);
    vec3 env = mix(vec3(0.04, 0.05, 0.12), vec3(0.59, 0.69, 0.9), clamp(0.5 - 0.6 * n.y, 0.0, 1.0));
    vec3 c = col * (0.16 + 0.95 * diff) * ao + col * env * 0.12 * ao + vec3(spec) * ao + rimCol * fres * 0.30 * ao + lavaCol * emis;
    finalColor = vec4(min(c, vec3(1.0)), alpha);
}
)";

struct Params { float seed; Vector3 rock; bool magma, hot; Vector3 ell, axis; };

inline Params params(int variant) {
    auto c = [](int r, int g, int b) { return Vector3{ r / 255.f, g / 255.f, b / 255.f }; };
    bool hot = variant == 6;
    Params P;
    P.seed  = 3.7f + 5.3f * variant;
    P.rock  = hot ? c(104, 60, 48) : variant % 3 == 0 ? c(128, 112, 100) : (variant % 3 == 1 ? c(104, 100, 110) : c(140, 120, 92));
    P.magma = variant == 1 || variant == 4 || hot;
    P.hot   = hot;
    static const Vector3 ells[7] = { { .86f, .66f, .64f }, { .80f, .72f, .62f }, { .88f, .60f, .68f }, { .72f, .82f, .64f },
                                     { .84f, .66f, .70f }, { .80f, .70f, .60f }, { .82f, .76f, .70f } };
    static const Vector3 axes[7] = { { 1, .35f, .25f }, { .3f, 1, -.3f }, { .8f, -.6f, .3f }, { -.5f, .9f, .4f },
                                     { 1, .1f, -.35f }, { .6f, .8f, .2f }, { .9f, .5f, .3f } };
    P.ell = ells[variant % 7]; P.axis = axes[variant % 7];
    return P;
}

// Build the 64-frame atlas for one rock. fallback: a static sprite rotated in 2D (if shaders fail).
// keep: if set, also returns a CPU copy of the atlas (for the startup cache).
inline Texture2D atlas(int variant, const Texture2D* fallback = nullptr, Image* keep = nullptr) {
    const int Wd = COLS * CELL, Ht = ROWS * CELL;
    RenderTexture2D rt = LoadRenderTexture(Wd, Ht);
    Shader sh = LoadShaderFromMemory(nullptr, FS);
    bool ok = sh.id > 0 && sh.id != rlGetShaderIdDefault();
    Image white = GenImageColor(Wd, Ht, WHITE);
    Texture2D wt = LoadTextureFromImage(white); UnloadImage(white);
    BeginTextureMode(rt);
    ClearBackground(BLANK);
    rlSetBlendFactorsSeparate(1, 0, 1, 0, 0x8006, 0x8006);   // overwrite: cells don't overlap
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    Params P = params(variant);
    for (int f = 0; f < FRAMES; ++f) {
        Rectangle cell{ (float)(f % COLS) * CELL, (float)(f / COLS) * CELL, (float)CELL, (float)CELL };
        if (ok) {
            BeginShaderMode(sh);
            float rect[4] = { cell.x / Wd, cell.y / Ht, (float)CELL / Wd, (float)CELL / Ht };
            float fr = (float)f, frs = (float)FRAMES, mg = P.magma, ht = P.hot, px = 2.0f / CELL;
            Vector3 lava = P.hot ? Vector3{ 1.0f, 0.42f, 0.18f } : Vector3{ 1.0f, 0.45f, 0.1f };
            Vector3 rim = P.hot ? Vector3{ 1.0f, 0.5f, 0.35f } : P.magma ? Vector3{ 1.0f, 0.55f, 0.3f } : Vector3{ 0.67f, 0.63f, 0.59f };
            SetShaderValue(sh, GetShaderLocation(sh, "cellRect"), rect, SHADER_UNIFORM_VEC4);
            SetShaderValue(sh, GetShaderLocation(sh, "frame"), &fr, SHADER_UNIFORM_FLOAT);
            SetShaderValue(sh, GetShaderLocation(sh, "frames"), &frs, SHADER_UNIFORM_FLOAT);
            SetShaderValue(sh, GetShaderLocation(sh, "axis"), &P.axis, SHADER_UNIFORM_VEC3);
            SetShaderValue(sh, GetShaderLocation(sh, "seed"), &P.seed, SHADER_UNIFORM_FLOAT);
            SetShaderValue(sh, GetShaderLocation(sh, "rockCol"), &P.rock, SHADER_UNIFORM_VEC3);
            SetShaderValue(sh, GetShaderLocation(sh, "lavaCol"), &lava, SHADER_UNIFORM_VEC3);
            SetShaderValue(sh, GetShaderLocation(sh, "rimCol"), &rim, SHADER_UNIFORM_VEC3);
            SetShaderValue(sh, GetShaderLocation(sh, "ell"), &P.ell, SHADER_UNIFORM_VEC3);
            SetShaderValue(sh, GetShaderLocation(sh, "magma"), &mg, SHADER_UNIFORM_FLOAT);
            SetShaderValue(sh, GetShaderLocation(sh, "hot"), &ht, SHADER_UNIFORM_FLOAT);
            SetShaderValue(sh, GetShaderLocation(sh, "pix"), &px, SHADER_UNIFORM_FLOAT);
            DrawTexturePro(wt, cell, cell, { 0, 0 }, 0, WHITE);   // texcoords = atlas UV
            EndShaderMode();
            rlDrawRenderBatchActive();
        } else if (fallback) {
            DrawTexturePro(*fallback, { 0, 0, (float)fallback->width, (float)fallback->height },
                           { cell.x + CELL / 2.f, cell.y + CELL / 2.f, CELL * 0.98f, CELL * 0.98f }, { CELL * 0.49f, CELL * 0.49f },
                           360.0f * f / FRAMES, WHITE);
        }
    }
    EndBlendMode();
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    if (keep) *keep = ImageCopy(img);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img); UnloadRenderTexture(rt); UnloadTexture(wt);
    if (sh.id > 0 && ok) UnloadShader(sh);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

inline Rectangle frameRect(int f) { f = ((f % FRAMES) + FRAMES) % FRAMES; return { (float)(f % COLS) * CELL, (float)(f / COLS) * CELL, (float)CELL, (float)CELL }; }

// Draw a rock at turn angle phaseDeg (360 = one full tumble), cross-fading the two nearest frames.
// The cell's rock fills ~0.8 of the cell, matching the old sprites' size convention.
inline void draw(const Texture2D& t, float phaseDeg, Vector2 pos, float size, float tilt, Color tint = WHITE, bool fade = true) {
    float fpos = phaseDeg / 360.0f * FRAMES;
    fpos -= floorf(fpos / FRAMES) * FRAMES;
    int f0 = (int)fpos; float a = fpos - f0;
    Rectangle dst{ pos.x, pos.y, size, size };
    Vector2 org{ size / 2, size / 2 };
    DrawTexturePro(t, frameRect(f0), dst, org, tilt, tint);
    if (fade && a > 0.02f)
        DrawTexturePro(t, frameRect(f0 + 1), dst, org, tilt, Color{ tint.r, tint.g, tint.b, (unsigned char)(tint.a * a) });
}

} // namespace m3d
