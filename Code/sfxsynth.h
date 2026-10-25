// Star Swarm sound design: procedural, bass-heavy "modern" SFX (sub drops, filtered noise, FM growl,
// saturation and a small reverb). Pure C++ (no raylib), renders mono float samples.
#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace sfx {

enum Id {
    FIRE_PHOTON, FIRE_SPREAD, FIRE_HOMING, FIRE_ION, LASER_LOOP, OVERHEAT,
    IMPACT, ION_BLAST, HIT, BOOM, BIGBOOM, DEATH,
    SHIELD_UP, SHIELD_BREAK, STAGE, LIFE, ALARM, DIVE, BOSS_LASER, FLAK,
    COUNT
};
static const char* NAMES[COUNT] = {
    "fire_photon", "fire_spread", "fire_homing", "fire_ion", "laser_loop", "overheat",
    "impact", "ion_blast", "hit", "boom", "bigboom", "death",
    "shield_up", "shield_break", "stage", "life", "alarm", "dive", "boss_laser", "flak" };

constexpr float TAU = 6.28318530718f;

struct Rng { uint32_t s; explicit Rng(uint32_t seed) : s(seed * 2654435761u + 1) {}
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(s & 0xFFFFFF) / 8388608.0f - 1.0f; } };

// Chamberlin state-variable filter (q = damping, ~0.2 resonant .. 1.4 soft)
struct SVF { float lp = 0, bp = 0;
    void run(float in, float fc, float q, float sr, float& L, float& B, float& H) {
        float f = 2 * sinf(3.14159265f * std::min(fc, sr / 6) / sr);
        H = in - lp - q * bp; bp += f * H; lp += f * bp; L = lp; B = bp; } };
struct OnePole { float z = 0; float lp(float in, float fc, float sr) { float a = 1 - expf(-TAU * fc / sr); z += a * (in - z); return z; } };

inline float sat(float x, float drive) { return tanhf(x * drive) / tanhf(drive); }
inline float env(float t, float att, float tau) { return (att > 0 ? std::min(1.0f, t / att) : 1.0f) * expf(-t / tau); }

// Freeverb-lite: 4 combs + 2 allpasses, mono
struct Reverb {
    std::vector<float> c[4], a[2]; int ci[4]{}, ai[2]{}; float cf[4]{};
    Reverb(float sr, float size) {
        static const int CL[4] = { 1116, 1188, 1277, 1356 }, AL[2] = { 556, 441 };
        for (int k = 0; k < 4; ++k) c[k].assign((size_t)(CL[k] * sr / 44100.0f), 0.f);
        for (int k = 0; k < 2; ++k) a[k].assign((size_t)(AL[k] * sr / 44100.0f), 0.f);
        for (float& f : cf) f = size; }
    float run(float x) {
        float o = 0;
        for (int k = 0; k < 4; ++k) { float y = c[k][ci[k]]; c[k][ci[k]] = x + y * cf[k] * 0.98f; ci[k] = (ci[k] + 1) % (int)c[k].size(); o += y; }
        o *= 0.25f;
        for (int k = 0; k < 2; ++k) { float b = a[k][ai[k]]; float y = -o + b; a[k][ai[k]] = o + b * 0.5f; ai[k] = (ai[k] + 1) % (int)a[k].size(); o = y; }
        return o; }
};

// sine with an exponential pitch drop f(t) = f1 + (f0 - f1) * e^(-t/tau)
struct Drop { float ph = 0; float run(float t, float f0, float f1, float tau, float sr) { ph += (f1 + (f0 - f1) * expf(-t / tau)) / sr; return sinf(ph * TAU); } };

inline void normalize(std::vector<float>& v, float peak) {
    float m = 1e-6f; for (float x : v) m = std::max(m, fabsf(x));
    for (float& x : v) x *= peak / m;
}
inline void fadeOut(std::vector<float>& v, float sr, float secs) {
    int n = std::min((int)v.size(), (int)(secs * sr));
    for (int i = 0; i < n; ++i) v[v.size() - 1 - i] *= (float)i / n;
}

// sparse crackle: random impulses, density 0..1, through a high-pass
struct Crackle { Rng r; SVF f; Crackle(uint32_t s) : r(s) {}
    float run(float density, float fc, float sr) { float x = (r.next() * 0.5f + 0.5f) < density ? r.next() * 0.6f : 0.f;
        float L, B, H; f.run(x, fc, 0.6f, sr, L, B, H); return H; } };

inline std::vector<float> render(Id id, float sr = 44100) {
    float dur = 0.3f;
    switch (id) {
        case FIRE_PHOTON: dur = 0.26f; break;  case FIRE_SPREAD: dur = 0.34f; break;  case FIRE_HOMING: dur = 0.45f; break;
        case FIRE_ION: dur = 0.50f; break;     case LASER_LOOP: dur = 1.0f; break;    case OVERHEAT: dur = 0.9f; break;
        case IMPACT: dur = 0.30f; break;       case ION_BLAST: dur = 0.9f; break;     case HIT: dur = 0.16f; break;
        case BOOM: dur = 0.8f; break;          case BIGBOOM: dur = 2.6f; break;       case DEATH: dur = 2.0f; break;
        case SHIELD_UP: dur = 0.6f; break;     case SHIELD_BREAK: dur = 0.8f; break;  case STAGE: dur = 1.2f; break;
        case LIFE: dur = 0.9f; break;          case ALARM: dur = 2.0f; break;         case DIVE: dur = 0.45f; break;
        case BOSS_LASER: dur = 1.4f; break;    case FLAK: dur = 0.35f; break;         default: break;
    }
    int N = (int)(dur * sr);
    std::vector<float> out(N, 0.f);
    Rng rn(1000 + id);
    SVF f1, f2, f3; Drop d1, d2; OnePole op1, op2;
    float ph1 = 0, ph2 = 0, ph3 = 0, ph4 = 0;
    float L, B, H;
    float revMix = 0, revSize = 0.8f;
    switch (id) {
    case FIRE_PHOTON:   // punchy sub "thoomp" + downward plasma zap + click
        for (int i = 0; i < N; ++i) { float t = i / sr;
            float sub = d1.run(t, 190, 48, 0.025f, sr) * env(t, 0.002f, 0.085f);
            ph1 += (280 + 2300 * expf(-t / 0.018f)) / sr;
            float zap = (sinf(ph1 * TAU) + 0.35f * sinf(ph1 * TAU * 3)) * env(t, 0.001f, 0.045f) * 0.38f;
            float n = rn.next(); f1.run(n, 1400, 0.7f, sr, L, B, H);
            float click = H * expf(-t / 0.003f) * 0.8f + B * expf(-t / 0.05f) * 0.2f;
            out[i] = sat(sub * 0.95f + zap + click, 2.0f); }
        break;
    case FIRE_SPREAD:   // shotgun: three gritty bursts a few ms apart over a deep kick
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next(), burst = 0;
            for (int k = 0; k < 3; ++k) { float tk = t - k * 0.009f; if (tk < 0) continue; burst += expf(-tk / 0.035f) * (1 - 0.15f * k); }
            f1.run(n, 900 + 4200 * expf(-t / 0.04f), 0.5f, sr, L, B, H);
            float noise = (B * 0.9f + H * 0.25f) * burst;
            f2.run(n, 700, 1.0f, sr, L, B, H); float body = L * env(t, 0.001f, 0.09f) * 1.4f;
            float sub = d1.run(t, 150, 42, 0.03f, sr) * env(t, 0.002f, 0.12f);
            out[i] = sat(sub + noise * 1.4f + body, 3.0f); }
        break;
    case FIRE_HOMING:   // launch thump, rising rocket whoosh, two seeker chirps
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next();
            float thump = d1.run(t, 140, 55, 0.02f, sr) * env(t, 0.002f, 0.07f);
            float fc = 500 + 4200 * std::min(1.0f, t / 0.38f);
            f1.run(n, fc, 0.35f, sr, L, B, H);
            float whoosh = B * sinf(3.14159f * std::min(1.0f, t / dur)) * 0.55f;
            float chirp = 0;
            for (float c0 : { 0.05f, 0.13f }) { float tc = t - c0; if (tc > 0 && tc < 0.035f) { ph1 += (1500 + 9000 * tc) / sr; chirp += sinf(ph1 * TAU) * sinf(3.14159f * tc / 0.035f) * 0.22f; } }
            f2.run(n, 160, 0.9f, sr, L, B, H); float rumble = L * env(t, 0.01f, 0.18f) * 1.5f;
            out[i] = sat(thump + whoosh + chirp + rumble, 1.8f); }
        break;
    case FIRE_ION: {    // FM growl "vwoom" + electric crackle + rising whine
        Crackle cr(7);
        for (int i = 0; i < N; ++i) { float t = i / sr;
            float fc = 62 - 18 * t / dur;
            ph2 += fc * 2.0f / sr; float mod = sinf(ph2 * TAU) * (7.0f * expf(-t / 0.11f) + 0.8f);
            ph1 += fc / sr; float growl = sinf(ph1 * TAU + mod) * env(t, 0.004f, 0.22f);
            ph3 += (1800 + 1400 * t / dur + 300 * sinf(TAU * 34 * t)) / sr;
            float whine = sinf(ph3 * TAU) * 0.07f * env(t, 0.03f, 0.18f);
            float crack = cr.run(0.04f * expf(-t / 0.2f), 2500, sr) * 0.9f;
            float sub = d1.run(t, 120, 40, 0.04f, sr) * env(t, 0.002f, 0.14f) * 0.8f;
            out[i] = sat(growl * 0.9f + sub + whine + crack, 2.6f); }
        break; }
    case LASER_LOOP:    // seamless 1 s loop: all rates are whole Hz so the cycle closes exactly
        for (int i = 0; i < N; ++i) { float t = i / sr;
            float saw = 0; for (int h = 1; h <= 10; ++h) saw += sinf(TAU * 55 * h * t) / h;
            float lfo = sinf(TAU * 4 * t);
            f1.run(saw, 650 + 350 * lfo, 0.45f, sr, L, B, H);
            float bass = L * 0.7f + sinf(TAU * 55 * t) * 0.3f;
            float buzz = 0; for (int h = 1; h <= 6; ++h) buzz += (sinf(TAU * 440 * h * t) + sinf(TAU * 443 * h * t)) / h;
            f2.run(buzz, 1800, 0.5f, sr, L, B, H);
            float hi = B * 0.28f * (0.7f + 0.3f * sinf(TAU * 12 * t));
            out[i] = sat(bass + hi, 1.8f); }
        break;
    case OVERHEAT:      // steam vent hiss + falling power-down
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next();
            f1.run(n, 3500, 0.8f, sr, L, B, H);
            float hiss = H * env(t, 0.01f, 0.35f) * 0.5f;
            ph1 += (220 * expf(-t / 0.25f) + 35) / sr;
            float down = (sinf(ph1 * TAU) + 0.4f * sinf(ph1 * TAU * 2)) * env(t, 0.005f, 0.3f) * 0.8f;
            out[i] = sat(hiss + down, 2.0f); }
        break;
    case IMPACT:        // torpedo hit: crunchy thud
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next();
            float sub = d1.run(t, 170, 50, 0.02f, sr) * env(t, 0.001f, 0.07f);
            f1.run(n, 2600 * expf(-t / 0.05f) + 300, 0.6f, sr, L, B, H);
            float crunch = (B + H * 0.3f) * env(t, 0.001f, 0.06f) * 1.5f;
            out[i] = sat(sub + crunch, 3.0f); }
        revMix = 0.12f; revSize = 0.6f;
        break;
    case ION_BLAST: {   // electric thunderclap
        Crackle cr(11);
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next();
            float sub = d1.run(t, 110, 26, 0.09f, sr) * env(t, 0.002f, 0.32f);
            f1.run(n, 6000 * expf(-t / 0.06f) + 250, 0.7f, sr, L, B, H);
            float blast = L * env(t, 0.001f, 0.18f) * 2.4f;
            float crack = cr.run(0.08f * expf(-t / 0.35f), 3000, sr) * 1.1f;
            ph1 += (95 + 40 * sinf(TAU * 9 * t)) / sr; float zz = sinf(ph1 * TAU) * sinf(ph1 * TAU * 3.01f) * env(t, 0.01f, 0.25f) * 0.35f;
            out[i] = sat(sub * 1.1f + blast + crack + zz, 2.4f); }
        revMix = 0.25f; revSize = 0.82f;
        break; }
    case HIT:           // armour ping: inharmonic metal + small thump
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next();
            float metal = (sinf(TAU * 1240 * t) + 0.7f * sinf(TAU * 1873 * t) + 0.5f * sinf(TAU * 2961 * t)) * env(t, 0.0005f, 0.035f) * 0.55f;
            float sub = d1.run(t, 130, 70, 0.02f, sr) * env(t, 0.001f, 0.04f) * 0.8f;
            f1.run(n, 4000, 0.7f, sr, L, B, H);
            out[i] = sat(metal + sub + H * expf(-t / 0.004f) * 0.4f, 1.6f); }
        break;
    case BOOM: {        // enemy explosion: sub drop + sweeping noise body + crackle tail
        Crackle cr(5);
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next();
            float sub = d1.run(t, 130, 32, 0.055f, sr) * env(t, 0.001f, 0.19f);
            f1.run(n, 5200 * expf(-t / 0.07f) + 180, 0.8f, sr, L, B, H);
            float body = L * env(t, 0.001f, 0.16f) * 3.0f;
            float punch = d2.run(t, 320, 110, 0.03f, sr) * env(t, 0.001f, 0.07f) * 0.55f;
            float crack = cr.run(0.05f * expf(-t / 0.25f), 1800, sr) * 1.1f;
            out[i] = sat(sub * 1.0f + punch + body + crack, 2.6f); }
        revMix = 0.2f; revSize = 0.8f;
        break; }
    case BIGBOOM: case DEATH: {   // boss / volatile rock / player: long rumbling detonation
        Crackle cr(id == DEATH ? 3 : 9);
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next();
            float sub = d1.run(t, 90, 24, 0.22f, sr) * env(t, 0.002f, 0.75f);
            f1.run(n, 3600 * expf(-t / 0.25f) + 110, 0.9f, sr, L, B, H);
            float body = L * env(t, 0.002f, 0.55f) * 3.4f;
            f2.run(n, 70, 1.2f, sr, L, B, H); float rumble = L * env(t, 0.05f, 0.9f) * (1 + 0.5f * sinf(TAU * 7 * t)) * 2.0f;
            float punch = d2.run(t, 260, 90, 0.05f, sr) * env(t, 0.001f, 0.12f) * 0.6f;
            float crack = cr.run(0.035f * expf(-t / 0.7f), 1500, sr) * 1.3f + punch;
            float tone = 0;
            if (id == DEATH) { ph1 += (520 * expf(-t / 0.5f) + 45) / sr; ph2 += (526 * expf(-t / 0.5f) + 44) / sr;
                tone = (fmodf(ph1, 1.f) * 2 - 1 + fmodf(ph2, 1.f) * 2 - 1) * env(t, 0.01f, 0.6f) * 0.18f; }
            out[i] = sat(sub * 1.2f + body + rumble + crack + tone, 2.2f); }
        revMix = 0.32f; revSize = 0.86f;
        break; }
    case SHIELD_UP:     // bass swell + shimmering rising chord
        for (int i = 0; i < N; ++i) { float t = i / sr, p = t / dur;
            float sw = sinf(3.14159f * p);
            ph1 += (45 + 45 * p) / sr; float bass = sinf(ph1 * TAU) * sw * 0.35f;
            float ch = 0; for (float fq : { 523.f, 784.f, 1046.f, 1568.f }) ch += sinf(TAU * fq * (1 + 0.5f * p) * t) * 0.25f;
            ch *= sw * (0.6f + 0.4f * sinf(TAU * (8 + 14 * p) * t)) * 1.1f;
            float n = rn.next(); f1.run(n, 1500 + 5000 * p, 0.5f, sr, L, B, H);
            out[i] = sat(bass + ch + B * sw * 0.15f, 1.5f); }
        revMix = 0.25f; revSize = 0.8f;
        break;
    case SHIELD_BREAK:  // glassy shatter + sub thump
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next();
            float glass = 0; static const float P[6] = { 2210, 2973, 3517, 4402, 5310, 6620 };
            for (int k = 0; k < 6; ++k) glass += sinf(TAU * P[k] * t + k) * expf(-t / (0.05f + 0.04f * k));
            glass *= 0.32f;
            f1.run(n, 5000, 0.6f, sr, L, B, H);
            float shards = H * env(t, 0.001f, 0.12f) * (fmodf(t * 41, 1.f) < 0.45f ? 1.f : 0.35f) * 1.1f;
            float sub = d1.run(t, 150, 38, 0.04f, sr) * env(t, 0.001f, 0.15f);
            out[i] = sat(glass + shards + sub, 2.0f); }
        revMix = 0.3f; revSize = 0.84f;
        break;
    case STAGE: case LIFE: {   // synth arpeggio over a sub bassline
        static const float A1[] = { 523.25f, 659.25f, 783.99f, 1046.5f, 783.99f, 1046.5f };
        static const float A2[] = { 783.99f, 987.77f, 1174.66f, 1567.98f, 1174.66f, 1567.98f, 2093.0f, 1567.98f };
        const float* ar = id == STAGE ? A1 : A2; int na = id == STAGE ? 6 : 8; float step = id == STAGE ? 0.14f : 0.07f;
        float bassF = id == STAGE ? 65.41f : 98.0f;
        for (int i = 0; i < N; ++i) { float t = i / sr;
            int k = std::min(na - 1, (int)(t / step)); float tk = t - k * step;
            float fq = ar[k];
            float saw = 0; for (int h = 1; h <= 8; ++h) saw += sinf(TAU * fq * h * t) / h;
            f1.run(saw, 600 + 5000 * expf(-tk / 0.05f), 0.4f, sr, L, B, H);
            float lead = L * env(tk, 0.003f, 0.12f) * 0.35f * (k == na - 1 ? std::max(0.f, 1 - (t - k * step) / (dur - k * step)) * 2.5f : 1.f);
            float bass = (sinf(TAU * bassF * t) + 0.3f * sinf(TAU * bassF * 2 * t)) * env(t, 0.01f, 0.5f) * 0.6f;
            out[i] = sat(lead + bass, 1.6f); }
        revMix = 0.28f; revSize = 0.84f;
        break; }
    case ALARM:         // boss warning: deep pulsing two-tone synth horn
        for (int i = 0; i < N; ++i) { float t = i / sr; float u = fmodf(t, 0.5f);
            float fq = u < 0.25f ? 110.f : 82.41f;
            float gate = u < 0.42f ? std::min(1.f, u / 0.01f) : std::max(0.f, 1 - (u - 0.42f) / 0.03f);
            float saw = 0; for (int h = 1; h <= 12; ++h) saw += sinf(TAU * fq * h * t + h) / h;
            f1.run(saw, 600 + 2200 * expf(-fmodf(u, 0.25f) / 0.06f), 0.35f, sr, L, B, H);
            float sub = sinf(TAU * fq * 0.5f * t);
            out[i] = sat((L * 0.9f + sub * 0.3f) * gate, 2.0f) * 0.8f; }
        break;
    case DIVE:          // diving enemy: descending FM whistle with a low wobble
        for (int i = 0; i < N; ++i) { float t = i / sr, p = t / dur;
            ph1 += (950 - 520 * p) / sr; ph2 += (475 - 260 * p) / sr;
            float w = sinf(ph1 * TAU + 1.5f * sinf(ph2 * TAU)) * 0.35f;
            float wob = sinf(TAU * (70 - 25 * p) * t) * (0.5f + 0.5f * sinf(TAU * 12 * t)) * 0.4f;
            out[i] = sat((w + wob) * sinf(3.14159f * p), 1.4f) * 0.7f; }
        break;
    case BOSS_LASER:    // boss lance: charging bass hum into a roaring beam
        for (int i = 0; i < N; ++i) { float t = i / sr, p = t / dur; float n = rn.next();
            float saw = 0; for (int h = 1; h <= 10; ++h) saw += sinf(TAU * 41.2f * h * t) / h;
            f1.run(saw, 400 + 2600 * std::min(1.f, p * 2), 0.4f, sr, L, B, H);
            f2.run(n, 900, 0.5f, sr, L, B, H); float roar = B * std::min(1.f, p * 3) * 1.6f;
            float sub = sinf(TAU * 41.2f * t) * 0.4f;
            float e = std::min(1.f, t / 0.15f) * (1 - p * p);
            out[i] = sat((f1.lp * 0.8f + sub + roar) * e, 2.2f); }
        revMix = 0.15f;
        break;
    case FLAK:          // outpost flak cannon: hollow thunk
        for (int i = 0; i < N; ++i) { float t = i / sr; float n = rn.next();
            float sub = d1.run(t, 220, 70, 0.02f, sr) * env(t, 0.001f, 0.08f);
            f1.run(n, 900, 0.3f, sr, L, B, H);
            out[i] = sat(sub + B * env(t, 0.001f, 0.05f) * 0.8f, 2.2f) * 0.8f; }
        revMix = 0.15f; revSize = 0.7f;
        break;
    default: break;
    }
    if (revMix > 0) { Reverb rv(sr, revSize); OnePole hp;   // only the mids/highs go into the reverb (keeps the sub tight)
        for (float& x : out) { float h = x - hp.lp(x, 300, sr); x = x + rv.run(h) * revMix * 1.5f; } }
    if (id == LASER_LOOP) { float m = 0; for (float x : out) m += x; m /= out.size(); for (float& x : out) x -= m; }
    else { float z = 0, px = 0; for (float& x : out) { float y = x - px + 0.997f * z; px = x; z = y; x = y; }   // DC blocker (~20 Hz)
           fadeOut(out, sr, std::min(0.06f, dur * 0.3f)); }
    normalize(out, 0.92f);
    return out;
}

// Mix level per sound (applied in-game with SetSoundVolume) so rapid weapon fire doesn't drown out the action
inline float gain(Id id) {
    switch (id) {
        case FIRE_PHOTON: return 0.42f; case FIRE_SPREAD: return 0.45f; case FIRE_HOMING: return 0.40f; case FIRE_ION: return 0.55f;
        case LASER_LOOP: return 0.38f; case OVERHEAT: return 0.5f; case IMPACT: return 0.45f; case ION_BLAST: return 0.7f;
        case HIT: return 0.4f; case BOOM: return 0.6f; case BIGBOOM: return 0.95f; case DEATH: return 1.0f;
        case SHIELD_UP: return 0.55f; case SHIELD_BREAK: return 0.7f; case STAGE: return 0.55f; case LIFE: return 0.55f;
        case ALARM: return 0.6f; case DIVE: return 0.3f; case BOSS_LASER: return 0.65f; case FLAK: return 0.5f; default: return 0.5f;
    }
}

} // namespace sfx
