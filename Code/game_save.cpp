// Star Swarm: the save file.
#include "game.h"

uint32_t Game::crc32(const unsigned char* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}

void Game::writeSave() {
#ifdef STARSWARM_BOT
    if (botOn) return;                      // bot games never touch the player's save
#endif
    unsigned char b[SAVE_SIZE] = {};
    memcpy(b, "SSAV", 4); b[4] = SAVE_VERSION; b[5] = 0; b[6] = SAVE_SIZE; b[7] = 0;
    uint32_t bits = 0; for (int i = 0; i < MD_COUNT; ++i) if (medal[i]) bits |= 1u << i;
    put32(b + 8, (uint32_t)hiScore); put32(b + 12, bits); put32(b + 16, (uint32_t)courierKillsTotal); put32(b + 20, (uint32_t)flakShotTotal);
    b[24] = (titleMuted ? 1 : 0) | (gameMusicMuted ? 2 : 0);
    put32(b + 28, crc32(b, 28));
    std::string path = savePath(), dir = userDataDir(), tmp = path + ".tmp";   // write a temp file, then swap it in
    if (!DirectoryExists(dir.c_str())) MakeDirectory(dir.c_str());
    { std::ofstream f(tmp, std::ios::binary); if (!f) return; f.write((const char*)b, SAVE_SIZE); if (!f) return; }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) std::remove(tmp.c_str());
}

bool Game::readSave(const std::string& path) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    unsigned char b[SAVE_SIZE];
    if (!f.read((char*)b, SAVE_SIZE) || memcmp(b, "SSAV", 4) != 0 || b[4] != SAVE_VERSION || get32(b + 28) != crc32(b, 28)) return false;
    hiScore = (int)get32(b + 8);
    uint32_t bits = get32(b + 12); for (int i = 0; i < MD_COUNT; ++i) medal[i] = (bits >> i) & 1u;
    courierKillsTotal = (int)get32(b + 16); flakShotTotal = (int)get32(b + 20);
    titleMuted = (b[24] & 1) != 0; gameMusicMuted = (b[24] & 2) != 0;
    return true;
}

bool Game::readLegacyFiles() {
    bool any = false; std::string line;
    if (std::ifstream f{ LEGACY_HISCORE_FILE }) { f >> hiScore; any = true; }
    if (std::ifstream f{ LEGACY_SETTINGS_FILE }) { any = true;
        while (std::getline(f, line)) {
            if (line.rfind("title_muted=", 0) == 0) titleMuted = line.size() > 12 && line[12] == '1';
            if (line.rfind("game_music_muted=", 0) == 0) gameMusicMuted = line.size() > 17 && line[17] == '1';
        } }
    if (std::ifstream f{ LEGACY_MEDALS_FILE }) { any = true;
        while (std::getline(f, line)) {
            if (line.rfind("medals=", 0) == 0) for (int i = 0; i < MD_COUNT && 7 + i < (int)line.size(); ++i) medal[i] = line[7 + i] == '1';
            else if (line.rfind("couriers=", 0) == 0) courierKillsTotal = atoi(line.c_str() + 9);
            else if (line.rfind("flak=", 0) == 0) flakShotTotal = atoi(line.c_str() + 5);
        } }
    return any;
}

void Game::loadHiScore() {
    if (readSave(savePath())) return;
    if (readSave(LEGACY_SAVE_FILE)) {                                 // old location: move it
        writeSave();
        if (readSave(savePath())) std::remove(LEGACY_SAVE_FILE);
        return;
    }
    bool migrated = readLegacyFiles();
    writeSave();
    if (migrated && readSave(savePath()))                            // only remove the old files once the new one is safe
        for (const char* fn : { LEGACY_HISCORE_FILE, LEGACY_SETTINGS_FILE, LEGACY_MEDALS_FILE }) std::remove(fn);
}
