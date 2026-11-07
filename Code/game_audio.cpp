// Star Swarm: sound effects, title music and in-game music.
#include "game.h"

void Game::loadSfx(const std::vector<std::vector<float>>& pcm) {
    for (int i = 0; i < SFX_COUNT; ++i) {
        sfx[i] = makeSound((SfxKind)i, pcm[i]);
        if (sfx[i].stream.buffer)   // no audio device: keep the empty sounds (playing them is a no-op)
            for (int v = 0; v < SFX_VOICES - 1; ++v) { sfxVoice[i][v] = LoadSoundAlias(sfx[i]); SetSoundVolume(sfxVoice[i][v], sfx::gain(SFX_SYNTH[i])); }
    }
}

void Game::playSfx(SfxKind k) {
    int v = sfxNext[k]; sfxNext[k] = (v + 1) % SFX_VOICES;
    PlaySound(v == 0 ? sfx[k] : sfxVoice[k][v - 1]);
}

std::vector<unsigned char> Game::makeWav(const std::vector<float>& v, int rate) {
    std::vector<unsigned char> b(44 + v.size() * 2);
    auto w32 = [&](size_t o, uint32_t x) { memcpy(&b[o], &x, 4); }; auto w16 = [&](size_t o, uint16_t x) { memcpy(&b[o], &x, 2); };
    memcpy(&b[0], "RIFF", 4); w32(4, (uint32_t)(36 + v.size() * 2)); memcpy(&b[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
    w32(24, rate); w32(28, rate * 2); w16(32, 2); w16(34, 16); memcpy(&b[36], "data", 4); w32(40, (uint32_t)(v.size() * 2));
    for (size_t i = 0; i < v.size(); ++i) { int16_t x = (int16_t)(Clamp(v[i], -1.0f, 1.0f) * 32767); memcpy(&b[44 + i * 2], &x, 2); }
    return b;
}

void Game::loadTitleAudio(const std::vector<float>& music, const std::vector<std::vector<float>>& fx) {
    const int rate = 44100;
    titleMusicWav = makeWav(music, rate);
    titleMusic = LoadMusicStreamFromMemory(".wav", titleMusicWav.data(), (int)titleMusicWav.size());
    titleMusicOk = titleMusic.stream.buffer != nullptr;
    if (titleMusicOk) titleMusic.looping = true;
    for (int k = 0; k < sfx::TFX_COUNT; ++k) {
        const std::vector<float>& v = fx[k];
        short* data = (short*)malloc(sizeof(short) * v.size());
        for (size_t i = 0; i < v.size(); ++i) data[i] = (short)(Clamp(v[i], -1.0f, 1.0f) * 32767);
        Wave w{ (unsigned)v.size(), (unsigned)rate, 16, 1, data };
        titleFx[k] = LoadSoundFromWave(w); UnloadWave(w);
        SetSoundVolume(titleFx[k], sfx::titleFxGain((sfx::TitleFx)k));
    }
}

void Game::loadGameMusic(std::vector<std::vector<float>>& pcm) {
    for (int k = 0; k < sfx::GT_COUNT; ++k) {
        gameMusicWav[k] = makeWav(pcm[k], 44100);
        std::vector<float>().swap(pcm[k]);
        gameMusic[k] = LoadMusicStreamFromMemory(".wav", gameMusicWav[k].data(), (int)gameMusicWav[k].size());
        gameMusicOk[k] = gameMusic[k].stream.buffer != nullptr;
        if (gameMusicOk[k]) gameMusic[k].looping = true;
    }
}

int Game::wantedGameTrack() const {
    if (mode != PLAYING) return -1;
    if (stageKind(stage) == SK_BOSS && (!boss.active || boss.state == B_DYING)) return -1;   // boss destroyed: quiet until the next sector
    return sfx::trackForStageInCycle((stage - 1) % CYCLE);
}

void Game::updateGameMusic(float dt) {
    bool hold = paused || escMenu;                   // pause the music with the game
    if (hold != gameMusicPaused) {
        for (int k = 0; k < sfx::GT_COUNT; ++k) if (gameMusicOk[k] && gameMusicVol[k] > 0) { if (hold) PauseMusicStream(gameMusic[k]); else ResumeMusicStream(gameMusic[k]); }
        gameMusicPaused = hold;
    }
    if (hold) return;
    int want = wantedGameTrack();
    for (int k = 0; k < sfx::GT_COUNT; ++k) {
        if (!gameMusicOk[k]) continue;
        float& v = gameMusicVol[k];
        if (k == want) {
            if (v <= 0 && !IsMusicStreamPlaying(gameMusic[k])) { StopMusicStream(gameMusic[k]); PlayMusicStream(gameMusic[k]); }   // from the top
            v = std::min(1.0f, v + dt / 1.5f);
        } else v = std::max(0.0f, v - dt / 1.5f);
        if (IsMusicStreamPlaying(gameMusic[k])) {
            SetMusicVolume(gameMusic[k], gameMusicMuted ? 0.0f : sfx::GAME_MUSIC_GAIN * v);   // muted: keeps running silently
            UpdateMusicStream(gameMusic[k]);
            if (v <= 0 && k != want) StopMusicStream(gameMusic[k]);
        }
    }
}

void Game::unloadTitleAudio() {
    if (titleMusicOk) { StopMusicStream(titleMusic); UnloadMusicStream(titleMusic); }
    for (auto& s : titleFx) UnloadSound(s);
}

void Game::updateTitleAudio(float dt) {
    bool onTitle = mode == TITLE;
    if (titleMusicOk) {
        if (onTitle && !IsMusicStreamPlaying(titleMusic)) { SeekMusicStream(titleMusic, fmodf(titleT, sfx::TITLE_LOOP)); PlayMusicStream(titleMusic); musicVol = 0; }
        musicVol = onTitle ? std::min(1.0f, musicVol + dt / 1.5f) : std::max(0.0f, musicVol - dt / 1.0f);
        if (IsMusicStreamPlaying(titleMusic)) {
            SetMusicVolume(titleMusic, titleMuted ? 0.0f : sfx::TITLE_MUSIC_GAIN * musicVol);   // muted: keeps playing silently, so unmuting resumes in place
            UpdateMusicStream(titleMusic);
            if (!onTitle && musicVol <= 0) StopMusicStream(titleMusic);
        }
    }
    if (!onTitle || stageDialog || titleMuted) { titleFxPrevT = onTitle ? titleT : -1; return; }
    auto crossed = [&](float at) {   // did fmod(titleT, cycle) pass `at` since last frame?
        if (titleFxPrevT < 0) return at == 0.0f;
        float a = titleFxPrevT, b = titleT;
        return floorf((b - at) / SW_CYCLE) > floorf((a - at) / SW_CYCLE);
    };
    if (crossed(0.0f))      PlaySound(titleFx[sfx::TFX_SWARM_IN]);
    if (crossed(SW_IN))     PlaySound(titleFx[sfx::TFX_SWARM_LOCK]);
    if (crossed(SW_OUT_AT)) PlaySound(titleFx[sfx::TFX_SWARM_OUT]);
    titleFxPrevT = titleT;
}
