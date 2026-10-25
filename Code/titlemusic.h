// Star Swarm title screen: dark space-ambient music loop (28 s = 4 title cycles of 7 s, one chord per cycle)
// and the title effects (swarm whoosh in, shimmer lock, swoosh out, start-game launch). Uses sfxsynth.h helpers.
#pragma once
#include "sfxsynth.h"

namespace sfx {

constexpr float TITLE_LOOP = 28.0f, TITLE_CYCLE = 7.0f;

inline float midiHz(float m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

// smooth periodic weight of chord k (0..3) at time t (crossfade 2.5 s around each 7 s boundary)
inline float chordWeight(int k, float t) {
    float u = fmodf(t, TITLE_LOOP) / TITLE_CYCLE;            // 0..4
    float c = k + 0.5f;                                       // centre of chord k
    float d = fabsf(u - c); d = std::min(d, 4 - d);           // circular distance in cycles
    float x = std::clamp((0.5f + 0.18f - d) / 0.36f, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

inline std::vector<float> renderTitleMusic(float sr = 44100) {
    const float warm = 6.0f, cf = 1.0f;                      // render a warm-up, keep 28 s, crossfade 1 s at the seam
    int N = (int)((warm + TITLE_LOOP + cf) * sr);
    std::vector<float> x(N, 0.f);
    // D minor: Dm(add9) -> Bbmaj7 -> Gm9 -> Asus4(b9-ish)
    static const float CH[4][5] = { { 50, 57, 62, 64, 65 }, { 46, 53, 57, 60, 62 }, { 43, 50, 55, 58, 62 }, { 45, 52, 57, 62, 64 } };
    static const float ROOT[4] = { 26, 22, 19, 21 };          // bass notes (D1, Bb0, G0, A0) — played an octave up below
    Rng rn(77); SVF padF, windF, droneF; Reverb rv(sr, 0.9f); OnePole hpSend;
    float bph[4] = { 0, 0, 0, 0 };
    // sparse bell notes (pentatonic, deterministic): time in loop, midi
    struct Bell { float t, m; };
    static const Bell BELLS[] = { { 1.9f, 81 }, { 4.3f, 86 }, { 8.8f, 77 }, { 11.2f, 84 }, { 15.9f, 79 }, { 18.4f, 86 },
                                  { 22.9f, 81 }, { 25.1f, 88 }, { 26.4f, 84 } };
    for (int i = 0; i < N; ++i) {
        float t = i / sr, tl = fmodf(t, TITLE_LOOP);
        // --- sub drone: root of the current chord, slow swell (period 14 s)
        float sub = 0;
        for (int k = 0; k < 4; ++k) { float w = chordWeight(k, t); if (w <= 0) continue;
            float f = midiHz(ROOT[k] + 12); bph[k] += f / sr;
            sub += w * (sinf(bph[k] * TAU) + 0.25f * sinf(bph[k] * TAU * 2)); }
        sub *= 0.55f * (0.8f + 0.2f * sinf(TAU * t / 14.0f));
        // --- pad: detuned saw stacks per chord note, slow filter breathing
        float pad = 0;
        for (int k = 0; k < 4; ++k) { float w = chordWeight(k, t); if (w <= 0) continue;
            for (int n = 0; n < 5; ++n) { float f = midiHz(CH[k][n]);
                for (float det : { -0.0045f, 0.0f, 0.0052f }) { float ph = fmodf(t * f * (1 + det), 1.0f); pad += w * (ph * 2 - 1) * 0.06f; } } }
        float L, B, H;
        padF.run(pad, 500 + 450 * (0.5f + 0.5f * sinf(TAU * t / 28.0f)) + 250 * sinf(TAU * t / 7.0f), 0.7f, sr, L, B, H);
        float padOut = L * 1.1f;
        // --- distant pulse: soft low thump every 3.5 s
        float tp = fmodf(t, 3.5f);
        float pulse = sinf(TAU * (48 + 30 * expf(-tp / 0.05f)) * tp) * expf(-tp / 0.35f) * std::min(1.f, tp / 0.01f) * 0.35f;
        // --- solar wind: band-passed noise sweeping slowly
        float n = rn.next();
        windF.run(n, 700 + 600 * sinf(TAU * t / 28.0f + 1.3f), 0.25f, sr, L, B, H);
        float wind = B * 0.05f * (0.6f + 0.4f * sinf(TAU * t / 9.333f));
        // --- bells: FM, long decay
        float bell = 0;
        for (const Bell& b : BELLS) for (float off : { 0.0f, TITLE_LOOP }) {   // previous-loop tail too
            float tb = tl - b.t + off; if (tb < 0 || tb > 4.5f) continue;
            float f = midiHz(b.m);
            bell += sinf(TAU * f * tb + 2.2f * expf(-tb / 0.6f) * sinf(TAU * f * 3.5f * tb)) * expf(-tb / 1.4f) * std::min(1.f, tb / 0.004f) * 0.11f; }
        float dry = sub + padOut + pulse + wind + bell;
        float send = padOut * 0.6f + bell * 1.4f + wind;
        float wet = rv.run(send - hpSend.lp(send, 200, sr));
        x[i] = sat(dry * 0.8f + wet * 0.9f, 1.3f);
    }
    // keep [warm, warm + 28 s) and crossfade its first second with the continuation after its end -> seamless loop
    int a = (int)(warm * sr), L = (int)(TITLE_LOOP * sr), C = (int)(cf * sr);
    std::vector<float> out(x.begin() + a, x.begin() + a + L);
    for (int i = 0; i < C; ++i) { float g = (float)i / C; out[i] = out[i] * g + x[a + L + i] * (1 - g); }
    normalize(out, 0.85f);
    return out;
}

enum TitleFx { TFX_SWARM_IN, TFX_SWARM_LOCK, TFX_SWARM_OUT, TFX_LAUNCH, TFX_COUNT };
static const char* TFX_NAMES[TFX_COUNT] = { "swarm_in", "swarm_lock", "swarm_out", "launch" };

inline std::vector<float> renderTitleFx(TitleFx id, float sr = 44100) {
    float dur = id == TFX_SWARM_IN ? 2.0f : id == TFX_SWARM_LOCK ? 2.2f : id == TFX_SWARM_OUT ? 1.6f : 1.8f;
    int N = (int)(dur * sr);
    std::vector<float> out(N, 0.f);
    Rng rn(300 + id); SVF f1, f2; Drop d1; float ph1 = 0;
    float L, B, H, revMix = 0.3f;
    for (int i = 0; i < N; ++i) {
        float t = i / sr, p = t / dur, n = rn.next(), s = 0;
        switch (id) {
        case TFX_SWARM_IN: {   // whoosh streaming in from both sides: rising band-passed noise + airy flutter
            float sw = sinf(3.14159f * std::min(1.f, p * 1.1f));
            f1.run(n, 400 + 3200 * p * p, 0.3f, sr, L, B, H);
            float flutter = 0.75f + 0.25f * sinf(TAU * (18 + 30 * p) * t);
            ph1 += (110 + 160 * p) / sr;
            s = B * sw * flutter * 0.9f + sinf(ph1 * TAU) * sw * 0.12f;
            break; }
        case TFX_SWARM_LOCK: { // letters lock in: soft sub hit + rising shimmer of bell partials
            float sub = d1.run(t, 90, 45, 0.06f, sr) * env(t, 0.003f, 0.25f) * 0.7f;
            static const float M[5] = { 74, 77, 81, 84, 86 };
            float sh = 0;
            for (int k = 0; k < 5; ++k) { float tk = t - k * 0.06f; if (tk < 0) continue; float f = midiHz(M[k]);
                sh += sinf(TAU * f * tk + 1.5f * expf(-tk / 0.3f) * sinf(TAU * f * 2 * tk)) * env(tk, 0.003f, 0.6f) * 0.16f; }
            s = sub + sh; revMix = 0.45f;
            break; }
        case TFX_SWARM_OUT: {  // peel off and dive: falling swoosh + low rumble
            float sw = sinf(3.14159f * std::min(1.f, p * 1.2f));
            f1.run(n, 3000 * (1 - p) * (1 - p) + 250, 0.35f, sr, L, B, H);
            f2.run(n, 90, 1.0f, sr, L, B, H);
            s = f1.bp * sw * 0.9f + L * sw * 1.6f;
            break; }
        case TFX_LAUNCH: {     // engines spool up and the ship launches: rising saw + roar + boom at the end
            float rise = std::min(1.f, p / 0.75f);
            ph1 += (55 + 330 * rise * rise) / sr;
            float saw = 0; for (int h = 1; h <= 8; ++h) saw += sinf(ph1 * TAU * h) / h;
            f1.run(saw, 300 + 3000 * rise, 0.5f, sr, L, B, H);
            float eng = L * std::min(1.f, t / 0.2f) * (p < 0.78f ? 1.f : std::max(0.f, 1 - (p - 0.78f) / 0.22f)) * 0.7f;
            f2.run(n, 600 + 4000 * rise, 0.5f, sr, L, B, H);
            float roar = B * rise * (p < 0.8f ? 1.f : std::max(0.f, 1 - (p - 0.8f) / 0.2f)) * 0.6f;
            float tb = t - 0.75f * dur;
            float boom = tb > 0 ? d1.run(tb, 120, 30, 0.08f, sr) * env(tb, 0.002f, 0.3f) * 1.0f : 0.f;
            s = eng + roar + boom; revMix = 0.25f;
            break; }
        default: break;
        }
        out[i] = sat(s, 1.8f);
    }
    { Reverb rv(sr, 0.85f); OnePole hp; for (float& v : out) { float h = v - hp.lp(v, 250, sr); v += rv.run(h) * revMix * 1.5f; } }
    fadeOut(out, sr, 0.15f);
    normalize(out, 0.9f);
    return out;
}

inline float titleFxGain(TitleFx id) { return id == TFX_LAUNCH ? 0.7f : id == TFX_SWARM_LOCK ? 0.45f : 0.4f; }
constexpr float TITLE_MUSIC_GAIN = 0.55f;

} // namespace sfx
