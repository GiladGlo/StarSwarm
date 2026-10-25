// Star Swarm in-game music: five dark, driving space-ambient scores (16-bar seamless loops), synthesised in code.
// Per boss cycle: waves 1-4 rotate DRIFT / VANGUARD / NEBULA RUN / DRIFT, the meteor field plays ASTEROID FIELD,
// the boss plays CAPITAL SHIP; the rotation restarts after every boss.
#pragma once
#include "titlemusic.h"

namespace sfx {

enum GameTrack { GT_DRIFT, GT_VANGUARD, GT_NEBULA, GT_ASTEROIDS, GT_BOSS, GT_COUNT };
static const char* GT_NAMES[GT_COUNT] = { "drift", "vanguard", "nebula_run", "asteroid_field", "capital_ship" };

struct TrackDef {
    float bpm;
    float chords[4][4];        // 4 chords x 4 notes (midi), one chord per 4 bars
    float bassRoot[4];         // midi bass root per chord
    int   bass[16];            // 16th-step bass pattern per bar: semitone offset from root (+12 = octave), -99 = rest
    int   kick[16], snare[16], hat[16];   // 0 = off, 1..3 = velocity
    int   arp[16];             // arp step: chord tone index 0..3 (+4 = octave up), -1 = rest
    float arpLevel, padLevel, padCut, bellLevel, drive, hatLevel;
    int   metalTick;           // 1 = clock-like metallic ticks instead of hats (meteor field)
    int   dissonance;          // adds a minor-2nd shimmer to the pad (tension)
};

inline const TrackDef& trackDef(GameTrack id) {
    static const TrackDef T[GT_COUNT] = {
        // DRIFT - 104 BPM, D minor: Dm - Bb - F - C ; pulsing 8th bass, soft kick on 1 & 3
        { 104, { { 62, 65, 69, 72 }, { 58, 62, 65, 69 }, { 60, 65, 69, 72 }, { 60, 64, 67, 71 } }, { 38, 34, 41, 36 },
          { 0, -99, 0, -99, 12, -99, 0, -99, 7, -99, 0, -99, 12, -99, 10, -99 },
          { 3, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
          { 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1 },
          { 0, -1, 2, -1, 1, -1, 3, -1, 0, -1, 2, -1, 5, -1, 3, -1 }, 0.10f, 0.8f, 900, 0.8f, 1.2f, 0.8f, 0, 0 },
        // VANGUARD - 110 BPM, A minor: Am - F - G - Em ; syncopated bass, soft snare on 2 & 4, 16th pluck arp
        { 110, { { 57, 60, 64, 67 }, { 57, 60, 65, 69 }, { 59, 62, 67, 71 }, { 59, 64, 67, 71 } }, { 33, 29, 31, 28 },
          { 0, -99, -99, 0, -99, -99, 12, -99, 0, -99, -99, 0, -99, 7, -99, 10 },
          { 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 1 },
          { 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 2, 1 },
          { 0, 1, 2, 4, 2, 1, 0, 1, 3, 2, 1, 2, 4, 5, 6, 5 }, 0.09f, 0.65f, 1100, 0.5f, 1.3f, 0.8f, 0, 0 },
        // NEBULA RUN - 116 BPM, C minor: Cm - Ab - Eb - Bb ; rolling 16th bass, pulse toms
        { 116, { { 60, 63, 67, 70 }, { 60, 63, 68, 72 }, { 58, 63, 67, 70 }, { 58, 62, 65, 70 } }, { 36, 32, 39, 34 },
          { 0, 0, 12, 0, 0, 0, 10, 0, 0, 0, 12, 0, 7, 0, 10, 12 },
          { 3, 0, 0, 0, 0, 0, 2, 0, 3, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0 },
          { 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 2 },
          { 0, -1, 4, -1, 2, -1, 5, -1, 1, -1, 6, -1, 3, -1, 7, -1 }, 0.08f, 0.8f, 1200, 0.6f, 1.3f, 0.7f, 0, 0 },
        // ASTEROID FIELD - 100 BPM, B minor, tense: drone, sparse sub stabs, ticking metal, dissonant shimmer
        { 100, { { 59, 62, 66, 69 }, { 59, 62, 67, 71 }, { 57, 62, 66, 69 }, { 58, 61, 66, 70 } }, { 35, 31, 33, 34 },
          { 0, -99, -99, -99, -99, -99, 0, -99, -99, -99, 1, -99, -99, -99, -99, -99 },
          { 3, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
          { 2, 0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0 },
          { 0, -1, -1, -1, -1, -1, -1, -1, 4, -1, -1, -1, -1, -1, -1, -1 }, 0.08f, 1.0f, 700, 0.4f, 1.2f, 0.9f, 1, 1 },
        // CAPITAL SHIP - 124 BPM, D phrygian: Dm - Eb - Dm - C ; four-on-the-floor, driving saw bass, alarm motif
        { 124, { { 62, 65, 69, 74 }, { 63, 67, 70, 75 }, { 62, 65, 69, 74 }, { 60, 64, 67, 72 } }, { 38, 39, 38, 36 },
          { 0, 0, 12, 0, 0, 0, 12, 0, 0, 0, 12, 0, 0, 1, 12, 0 },
          { 3, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 1 }, { 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0 },
          { 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0 },
          { 4, -1, 4, -1, 5, -1, 4, -1, 6, -1, 5, -1, 4, -1, 3, -1 }, 0.08f, 0.7f, 1300, 0.3f, 1.5f, 0.7f, 0, 0 },
    };
    return T[id];
}

inline float trackLoopSeconds(GameTrack id) { return 16 * 4 * 60.0f / trackDef(id).bpm; }

inline std::vector<float> renderGameTrack(GameTrack id, float sr = 44100) {
    const TrackDef& D = trackDef(id);
    const float beat = 60.0f / D.bpm, step = beat / 4, bar = beat * 4, loopLen = 16 * bar, warm = 4 * bar, cf = 0.5f;
    int N = (int)((warm + loopLen + cf) * sr);
    std::vector<float> x(N, 0.f);
    Rng rn(500 + id); SVF padF, bassF, arpF, hatF, snF, wind; Reverb rv(sr, 0.86f); OnePole hpSend;
    float bassPh = 0, subPh = 0;
    for (int i = 0; i < N; ++i) {
        float t = i / sr, tl = fmodf(t, loopLen);
        int barNo = (int)(tl / bar), chord = (barNo / 4) % 4;
        float inBar = fmodf(tl, bar); int st = std::min(15, (int)(inBar / step)); float ts = inBar - st * step;   // time since step start
        // chord crossfade weight (short, around chord boundaries) for the pad
        float pad = 0;
        for (int k = 0; k < 4; ++k) {
            float cs = k * 4 * bar, d = fmodf(tl - cs + loopLen, loopLen);       // time since chord k started
            float w = d < 4 * bar ? std::min(1.f, d / 0.6f) : (d < 4 * bar + 1.2f ? 1 - (d - 4 * bar) / 1.2f : 0.f);
            if (w <= 0) continue;
            for (int n = 0; n < 4; ++n) { float f = midiHz(D.chords[k][n] - 12);
                for (float det : { -0.004f, 0.0047f }) { float ph = fmodf(t * f * (1 + det), 1.f); pad += w * (ph * 2 - 1) * 0.06f; } }
            if (D.dissonance) { float f = midiHz(D.chords[k][0] - 11); pad += w * sinf(TAU * f * t) * 0.05f * (0.5f + 0.5f * sinf(TAU * t / bar)); }
        }
        float L, B, H;
        padF.run(pad, D.padCut * (0.75f + 0.25f * sinf(TAU * tl / (8 * bar))), 0.7f, sr, L, B, H);
        float padOut = L * D.padLevel;
        // bass: per 16th step, short envelope; saw through a plucky low-pass + clean sub
        float bass = 0;
        int bo = D.bass[st];
        if (bo > -99) { float f = midiHz(D.bassRoot[chord] + bo); bassPh += f / sr; subPh += f / sr;
            float e = env(ts, 0.003f, id == GT_BOSS ? 0.16f : 0.12f);
            float saw = fmodf(bassPh, 1.f) * 2 - 1;
            bassF.run(saw, 180 + 1400 * expf(-ts / 0.05f), 0.5f, sr, L, B, H);
            bass = (L * 0.7f + sinf(subPh * TAU) * 0.6f) * e; }
        // drums
        float drums = 0;
        int kv = D.kick[st]; if (kv) { float a = kv / 3.f; drums += sinf(TAU * (45 * ts + 75 * 0.03f * (1 - expf(-ts / 0.03f)))) * env(ts, 0.001f, 0.16f) * 0.9f * a; }
        int sv = D.snare[st]; float n = rn.next();
        if (sv) { float a = sv / 3.f; snF.run(n, 1800, 0.6f, sr, L, B, H); drums += (B * 1.4f + sinf(TAU * 190 * ts) * 0.3f) * env(ts, 0.001f, 0.09f) * a * 0.8f; }
        int hv = D.hat[st];
        if (hv) { float a = hv / 3.f; hatF.run(n, 7000, 0.4f, sr, L, B, H);
            if (D.metalTick) drums += (sinf(TAU * 3150 * ts) * 0.6f + sinf(TAU * 4710 * ts) * 0.4f) * env(ts, 0.0005f, 0.012f) * a * 0.35f * D.hatLevel;
            else drums += H * env(ts, 0.0005f, 0.018f) * a * 0.28f * D.hatLevel; }
        // arp pluck (chord tones, 16ths or 8ths)
        float arp = 0; int av = D.arp[st];
        if (av >= 0) { float f = midiHz(D.chords[chord][av % 4] + 12 * (av / 4));
            float s = 0; for (int h = 1; h <= 6; ++h) s += sinf(TAU * f * h * ts) / (h * h);
            arpF.run(s, 900 + 3500 * expf(-ts / 0.04f), 0.6f, sr, L, B, H);
            arp = L * env(ts, 0.002f, 0.11f) * D.arpLevel * 4; }
        // occasional bell at the top of every 2nd bar
        float bell = 0;
        if (D.bellLevel > 0) for (int bb = 0; bb < 16; bb += 2) {
            float tb = fmodf(tl - bb * bar + loopLen, loopLen); if (tb > 3.0f) continue;
            float f = midiHz(D.chords[(bb / 4) % 4][(bb / 2) % 4] + 12);
            bell += sinf(TAU * f * tb + 2.0f * expf(-tb / 0.5f) * sinf(TAU * f * 3.5f * tb)) * expf(-tb / 1.1f) * std::min(1.f, tb / 0.004f) * 0.07f * D.bellLevel; }
        // airy wind bed
        wind.run(n, 900 + 500 * sinf(TAU * tl / loopLen), 0.3f, sr, L, B, H);
        float air = B * 0.03f;
        float dry = padOut + bass + drums + arp + bell + air;
        float send = padOut * 0.5f + arp * 0.7f + bell * 1.3f + air + drums * 0.15f;
        float wet = rv.run(send - hpSend.lp(send, 220, sr));
        x[i] = sat(dry * 0.6f + wet * 0.6f, D.drive);
    }
    int a = (int)(warm * sr), Ln = (int)(loopLen * sr), C = (int)(cf * sr);
    std::vector<float> out(x.begin() + a, x.begin() + a + Ln);
    for (int i = 0; i < C; ++i) { float g = (float)i / C; out[i] = out[i] * g + x[a + Ln + i] * (1 - g); }
    { float z = 0, px = 0; for (float& v : out) { float y = v - px + 0.997f * z; px = v; z = y; v = y; } }   // DC blocker
    normalize(out, 0.85f);
    return out;
}

// stage within a boss cycle (0..5) -> track: waves rotate, then meteor field, then boss
inline GameTrack trackForStageInCycle(int s) {
    static const GameTrack R[6] = { GT_DRIFT, GT_VANGUARD, GT_NEBULA, GT_DRIFT, GT_ASTEROIDS, GT_BOSS };
    return R[std::clamp(s, 0, 5)];
}
constexpr float GAME_MUSIC_GAIN = 0.2f;    // background level: well under the sound effects

} // namespace sfx
