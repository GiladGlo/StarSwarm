// =============================================================================
//  STAR SWARM  -  entry point: platform glue, the startup loader/cache and the main loop.
//  The game itself is struct Game (game.h); its code is split by area into game_*.cpp.
// =============================================================================
#include "game.h"

// On laptops with hybrid graphics, ask the NVIDIA (Optimus) and AMD (PowerXpress) drivers
// to run the game on the discrete, high-performance GPU. Ignored on single-GPU systems.
#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// ----------------------------------------------------------------------------
// ============================================================== STARTUP: LOADING SCREEN + CACHE ==
// Everything the game generates at startup (sprites, monster animations, 3D asteroid atlases, sound effects
// and music) is built while a loading screen keeps the window alive. The results are saved to a compressed
// cache in the user's local app-data folder, so later launches only decompress and upload them.
//
// The cache is used only if all of these match: magic + format version, the content fingerprint baked in at
// build time (CONTENT_HASH: a hash of the generator sources), the generation parameters, the exact asset
// list, and a CRC-32 over the whole file. Anything else means "rebuild it": the cache is never required.
#if __has_include("contenthash.h")
#include "contenthash.h"
#endif
#ifndef CONTENT_HASH
#define CONTENT_HASH "dev"
#endif

namespace startup {

enum Group { G_GAMEMUSIC, G_TITLEMUSIC, G_PLAYER, G_MON, G_BOSSICON, G_METEORFLAT, G_COURIER, G_DRONE, G_OUTPOST,
             G_SFX, G_TITLEFX, G_METEOR, G_COUNT };
struct Spec { Group g; int a, b; std::string name; int weight; };
struct Asset { int w = 0, h = 0; std::vector<unsigned char> rgba; std::vector<float> pcm; };

static const int CACHE_FORMAT = 1;
// generation settings that change the output without touching the generator sources
static std::string genParams() {
    return TextFormat("player=320 mon=%d x%d bossicon=192 courier=256 drone=256 outpost=384 meteor=%dx%d@%d rate=44100",
                      Gfx::MON_N, Gfx::MON_FRAMES, m3d::COLS, m3d::ROWS, m3d::CELL);
}

// The full asset list. Meteor "flats" are only a fallback input for the GPU atlases, so they are never cached.
static std::vector<Spec> manifest() {
    std::vector<Spec> m;
    for (int k = 0; k < sfx::GT_COUNT; ++k) m.push_back({ G_GAMEMUSIC, k, 0, TextFormat("gamemusic%d", k), 30 });
    m.push_back({ G_TITLEMUSIC, 0, 0, "titlemusic", 24 });
    for (int i = 0; i < 5; ++i) m.push_back({ G_PLAYER, i, 0, TextFormat("player%d", i), 8 });
    for (int k = 0; k < Gfx::MON_COUNT; ++k) for (int f = 0; f < Gfx::MON_FRAMES; ++f) m.push_back({ G_MON, k, f, TextFormat("mon%d_%d", k, f), 3 });
    for (int b = 0; b < 10; ++b) m.push_back({ G_BOSSICON, b, 0, TextFormat("bossicon%d", b), 3 });
    for (int i = 0; i < 7; ++i) m.push_back({ G_METEORFLAT, i, 0, TextFormat("meteorflat%d", i), 1 });
    for (int i = 0; i < 2; ++i) m.push_back({ G_COURIER, i, 0, TextFormat("courier%d", i), 3 });
    m.push_back({ G_DRONE, 0, 0, "drone", 2 });
    m.push_back({ G_OUTPOST, 0, 0, "outpost", 4 });
    for (int k = 0; k < SFX_COUNT; ++k) m.push_back({ G_SFX, k, 0, TextFormat("sfx%d", k), 1 });
    for (int k = 0; k < sfx::TFX_COUNT; ++k) m.push_back({ G_TITLEFX, k, 0, TextFormat("titlefx%d", k), 1 });
    for (int i = 0; i < 7; ++i) m.push_back({ G_METEOR, i, 0, TextFormat("meteor%d", i), 6 });
    return m;
}
static bool cached(Group g) { return g != G_METEORFLAT; }
static bool gpuJob(Group g) { return g == G_METEOR; }

// What the loading screen says while a group is being built (in the order players notice them)
static const char* statusFor(Group g) {
    switch (g) {
        case G_PLAYER: return "Building your starfighter";
        case G_MON: case G_BOSSICON: return "Hatching space monsters";
        case G_METEORFLAT: case G_METEOR: return "Sculpting asteroids";
        case G_COURIER: case G_DRONE: case G_OUTPOST: return "Fitting out the Couriers";
        case G_GAMEMUSIC: case G_TITLEMUSIC: return "Composing the soundtrack";
        default: return "Tuning sound effects";
    }
}

// CPU generation (worker threads): sprites come back as images through spr::g_imgOut
static void generate(const Spec& s, Asset& out) {
    auto grab = [&](auto&& fn) {
        Image img{}; spr::g_imgOut = &img; fn(); spr::g_imgOut = nullptr;
        out.w = img.width; out.h = img.height;
        out.rgba.assign((unsigned char*)img.data, (unsigned char*)img.data + (size_t)img.width * img.height * 4);
        UnloadImage(img);
    };
    float ph = (float)s.b / Gfx::MON_FRAMES;
    switch (s.g) {
        case G_GAMEMUSIC:  out.pcm = sfx::renderGameTrack((sfx::GameTrack)s.a, 44100); break;
        case G_TITLEMUSIC: out.pcm = sfx::renderTitleMusic(44100); break;
        case G_SFX:        out.pcm = sfx::render(SFX_SYNTH[s.a], 44100); break;
        case G_TITLEFX:    out.pcm = sfx::renderTitleFx((sfx::TitleFx)s.a, 44100); break;
        case G_PLAYER:     grab([&] { spr::playerShip(320, s.a == 2 ? 0.0f : -30.0f + 15 * s.a); }); break;
        case G_BOSSICON:   grab([&] { spr::bossShip(s.a, 192, nullptr); }); break;
        case G_METEORFLAT: grab([&] { spr::meteor(192, s.a); }); break;
        case G_COURIER:    grab([&] { spr::courier(256, s.a == 1); }); break;
        case G_DRONE:      grab([&] { spr::wingDrone(256); }); break;
        case G_OUTPOST:    grab([&] { spr::miningOutpost(384); }); break;
        case G_MON:
            grab([&] { switch (s.a) {
                case Gfx::MON_URCHIN:    spr::novaUrchin(Gfx::MON_N, ph); break;
                case Gfx::MON_MANTA:     spr::voidManta(Gfx::MON_N, ph); break;
                case Gfx::MON_JELLY:     spr::nebulaJelly(Gfx::MON_N, ph, false); break;
                case Gfx::MON_JELLY_HIT: spr::nebulaJelly(Gfx::MON_N, ph, true); break;
                case Gfx::MON_IMP:       spr::cometImp(Gfx::MON_N, ph); break;
                case Gfx::MON_BRUTE:     spr::asteroidBrute(Gfx::MON_N, ph); break;
                case Gfx::MON_LEVIATHAN: spr::cosmicLeviathan(Gfx::MON_N, ph); break;
                default:                 spr::leviathanSpawn(Gfx::MON_N, ph); break; } });
            break;
        default: break;
    }
}

static Texture2D upload(const Asset& a, bool mips = true) {
    Image img{ (void*)a.rgba.data(), a.w, a.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D t = LoadTextureFromImage(img);
    if (mips) GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

// ---- cache file ----
static std::string cachePath() {
    if (const char* o = getenv("STARSWARM_CACHE_DIR")) return std::string(o) + "/startup.cache";
#ifdef _WIN32
    const char* base = getenv("LOCALAPPDATA");
    return base ? std::string(base) + "\\StarSwarm\\startup.cache" : std::string("startup.cache");
#else
    if (const char* x = getenv("XDG_CACHE_HOME")) return std::string(x) + "/starswarm/startup.cache";
    if (const char* h = getenv("HOME")) return std::string(h) + "/.cache/starswarm/startup.cache";
    return "startup.cache";
#endif
}
static uint32_t crc32(const unsigned char* p, size_t n) {
    static uint32_t T[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); T[i] = c; } init = true; }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = T[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
struct Writer { std::vector<unsigned char> b;
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((unsigned char)(v >> (8 * i))); }
    void str(const std::string& s) { u32((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
    void raw(const unsigned char* p, size_t n) { b.insert(b.end(), p, p + n); } };
struct Reader { const unsigned char* p; size_t n, i = 0; bool ok = true;
    uint32_t u32() { if (i + 4 > n) { ok = false; return 0; } uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= (uint32_t)p[i + k] << (8 * k); i += 4; return v; }
    std::string str() { uint32_t len = u32(); if (!ok || i + len > n) { ok = false; return ""; } std::string s((const char*)p + i, len); i += len; return s; } };

static std::vector<unsigned char> payloadOf(const Asset& a) {   // images as RGBA8, sound as 16-bit samples
    if (!a.pcm.empty()) { std::vector<unsigned char> v(a.pcm.size() * 2);
        for (size_t i = 0; i < a.pcm.size(); ++i) { int16_t s = (int16_t)(std::clamp(a.pcm[i], -1.0f, 1.0f) * 32767); memcpy(&v[i * 2], &s, 2); }
        return v; }
    return a.rgba;
}

// Compress every asset on all cores and write the file (atomically: temp file, then rename)
static void saveCache(const std::vector<Spec>& specs, const std::vector<Asset>& assets) {
    std::vector<std::vector<unsigned char>> comp(specs.size()); std::vector<uint32_t> rawSize(specs.size(), 0);
    std::atomic<int> next{ 0 };
    auto worker = [&] {
        for (int j; (j = next++) < (int)specs.size();) {
            if (!cached(specs[j].g)) continue;
            std::vector<unsigned char> raw = payloadOf(assets[j]); rawSize[j] = (uint32_t)raw.size();
            int cs = 0; unsigned char* c = CompressData(raw.data(), (int)raw.size(), &cs);
            if (c) { comp[j].assign(c, c + cs); MemFree(c); }
        }
    };
    int nt = std::max(1, (int)std::thread::hardware_concurrency() - 1);
    std::vector<std::thread> pool; for (int i = 0; i < nt; ++i) pool.emplace_back(worker); for (auto& t : pool) t.join();
    Writer w;
    w.raw((const unsigned char*)"SSCACHE1", 8); w.u32(CACHE_FORMAT); w.str(CONTENT_HASH); w.str(genParams());
    uint32_t n = 0; for (auto& s : specs) n += cached(s.g);
    w.u32(n);
    for (size_t j = 0; j < specs.size(); ++j) {
        if (!cached(specs[j].g)) continue;
        if (comp[j].empty()) return;                      // compression failed: don't write a partial cache
        w.str(specs[j].name); w.u32(assets[j].pcm.empty() ? 0 : 1); w.u32((uint32_t)assets[j].w); w.u32((uint32_t)assets[j].h);
        w.u32(rawSize[j]); w.u32((uint32_t)comp[j].size()); w.raw(comp[j].data(), comp[j].size());
    }
    w.u32(crc32(w.b.data(), w.b.size()));
    std::string path = cachePath(), dir = GetDirectoryPath(path.c_str()), tmp = path + ".tmp";
    if (!dir.empty() && !DirectoryExists(dir.c_str())) MakeDirectory(dir.c_str());
    { std::ofstream f(tmp, std::ios::binary); if (!f) return; f.write((const char*)w.b.data(), (std::streamsize)w.b.size()); if (!f) { f.close(); std::remove(tmp.c_str()); return; } }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) std::remove(tmp.c_str());
}

// Read + verify + decompress (worker thread). Returns false on any mismatch or damage.
static bool loadCache(const std::vector<Spec>& specs, std::vector<Asset>& assets, std::atomic<int>& progressUnits) {
    std::ifstream f(cachePath(), std::ios::binary | std::ios::ate); if (!f) return false;
    std::streamsize sz = f.tellg(); if (sz < 64) return false;
    std::vector<unsigned char> buf((size_t)sz); f.seekg(0); if (!f.read((char*)buf.data(), sz)) return false;
    uint32_t stored = 0; for (int k = 0; k < 4; ++k) stored |= (uint32_t)buf[buf.size() - 4 + k] << (8 * k);
    if (crc32(buf.data(), buf.size() - 4) != stored) return false;
    Reader r{ buf.data(), buf.size() - 4 };
    if (memcmp(buf.data(), "SSCACHE1", 8) != 0) return false; r.i = 8;
    if (r.u32() != (uint32_t)CACHE_FORMAT || r.str() != CONTENT_HASH || r.str() != genParams()) return false;
    uint32_t n = r.u32(); uint32_t want = 0; for (auto& s : specs) want += cached(s.g);
    if (!r.ok || n != want) return false;
    struct Ent { size_t j, off; uint32_t isPcm, w, h, raw, comp; };
    std::vector<Ent> ents;
    for (size_t j = 0; j < specs.size(); ++j) {
        if (!cached(specs[j].g)) continue;
        Ent e{}; e.j = j;
        if (r.str() != specs[j].name) return false;
        e.isPcm = r.u32(); e.w = r.u32(); e.h = r.u32(); e.raw = r.u32(); e.comp = r.u32();
        if (!r.ok || r.i + e.comp > r.n) return false;
        e.off = r.i; r.i += e.comp; ents.push_back(e);
    }
    std::atomic<int> next{ 0 }; std::atomic<bool> bad{ false };
    auto worker = [&] {
        for (int k; (k = next++) < (int)ents.size() && !bad;) {
            const Ent& e = ents[k]; int rs = 0;
            unsigned char* d = DecompressData(buf.data() + e.off, (int)e.comp, &rs);
            if (!d || (uint32_t)rs != e.raw) { if (d) MemFree(d); bad = true; return; }
            Asset& a = assets[e.j]; a.w = (int)e.w; a.h = (int)e.h;
            if (e.isPcm) { a.pcm.resize(rs / 2); for (size_t i = 0; i < a.pcm.size(); ++i) { int16_t s; memcpy(&s, d + i * 2, 2); a.pcm[i] = s / 32767.0f; } }
            else { if ((size_t)rs != (size_t)e.w * e.h * 4) { MemFree(d); bad = true; return; } a.rgba.assign(d, d + rs); }
            MemFree(d);
            progressUnits += specs[e.j].weight * 85;   // decompressing = first 85% of the bar, uploading the rest
        }
    };
    int nt = std::max(1, (int)std::thread::hardware_concurrency() - 1);
    std::vector<std::thread> pool; for (int i = 0; i < nt; ++i) pool.emplace_back(worker); for (auto& t : pool) t.join();
    return !bad;
}

// ---- the loader: runs on worker threads while the main thread keeps drawing the loading screen ----
struct Loader {
    std::vector<Spec> specs = manifest();
    std::vector<Asset> assets = std::vector<Asset>(specs.size());
    std::vector<std::atomic<bool>> ready = std::vector<std::atomic<bool>>(specs.size());
    std::atomic<int> unitsDone{ 0 }; int unitsTotal = 0;    // progress in weight x 100 units
    std::atomic<int> lastGroup{ G_PLAYER };                 // most recently started non-music job (for the status line)
    std::atomic<bool> cancel{ false }, cacheDone{ false }, cacheOk{ false };
    std::vector<std::thread> pool;
    std::thread cacheThread, saveThread;
    enum Phase { READ_CACHE, GENERATE, UPLOAD, DONE } phase = READ_CACHE;
    bool fromCache = false, firstRun = false;
    size_t uploadNext = 0;
    Texture2D flats[7]{}; int nextMeteor = 0;

    void start() {
        for (auto& s : specs) unitsTotal += s.weight * 100;
        for (auto& r : ready) r = false;
        firstRun = !FileExists(cachePath().c_str());
        if (getenv("STARSWARM_NO_CACHE")) firstRun = true;
        if (firstRun) { startGenerate(); return; }
        cacheThread = std::thread([this] { cacheOk = loadCache(specs, assets, unitsDone); cacheDone = true; });
    }
    void startGenerate() {
        phase = GENERATE; unitsDone = 0;
        for (auto& a : assets) a = Asset{};
        std::shared_ptr<std::atomic<int>> next = std::make_shared<std::atomic<int>>(0);
        auto worker = [this, next] {
            for (int j; !cancel && (j = (*next)++) < (int)specs.size();) {
                if (gpuJob(specs[j].g)) continue;
                if (specs[j].g != G_GAMEMUSIC && specs[j].g != G_TITLEMUSIC) lastGroup = specs[j].g;
                generate(specs[j], assets[j]);
                ready[j] = true; unitsDone += specs[j].weight * 100;
            }
        };
        int nt = std::max(1, (int)std::thread::hardware_concurrency() - 1);   // leave a core for the loading screen
        for (int i = 0; i < nt; ++i) pool.emplace_back(worker);
    }
    // Called once per frame on the main thread. Returns true when everything is ready.
    bool step(Game& game) {
        if (phase == READ_CACHE) {
            if (!cacheDone) return false;
            cacheThread.join();
            if (cacheOk) { fromCache = true; phase = UPLOAD; unitsDone = unitsTotal * 85 / 100; }
            else { firstRun = true; startGenerate(); }
            return false;
        }
        if (phase == GENERATE) {
            // GPU work on the main thread: one asteroid atlas per frame once its flat fallback sprite exists
            if (nextMeteor < 7) {
                size_t fj = 0, mj = 0;
                for (size_t j = 0; j < specs.size(); ++j) { if (specs[j].g == G_METEORFLAT && specs[j].a == nextMeteor) fj = j; if (specs[j].g == G_METEOR && specs[j].a == nextMeteor) mj = j; }
                if (ready[fj]) {
                    lastGroup = G_METEOR;
                    flats[nextMeteor] = upload(assets[fj], false);
                    Image keep{};
                    gfx.meteor[nextMeteor] = m3d::atlas(nextMeteor, &flats[nextMeteor], &keep);
                    assets[mj].w = keep.width; assets[mj].h = keep.height;
                    assets[mj].rgba.assign((unsigned char*)keep.data, (unsigned char*)keep.data + (size_t)keep.width * keep.height * 4);
                    UnloadImage(keep); UnloadTexture(flats[nextMeteor]);
                    ready[mj] = true; unitsDone += specs[mj].weight * 100; ++nextMeteor;
                }
            }
            for (size_t j = 0; j < specs.size(); ++j) if (!ready[j]) return false;
            for (auto& t : pool) t.join(); pool.clear();
            phase = UPLOAD;
            return false;
        }
        if (phase == UPLOAD) {
            // spread the GPU uploads over a few frames so the loading screen keeps moving
            double t0 = GetTime();
            while (uploadNext < specs.size() && GetTime() - t0 < 0.03) apply(game, uploadNext++);
            if (uploadNext < specs.size()) { unitsDone = std::max(unitsDone.load(), (int)(unitsTotal * (0.85 + 0.15 * uploadNext / specs.size()))); return false; }
            finish(game);
            phase = DONE;
            unitsDone = unitsTotal;
        }
        return phase == DONE;
    }
    std::vector<std::vector<float>> sfxPcm = std::vector<std::vector<float>>(SFX_COUNT), titleFxPcm = std::vector<std::vector<float>>(sfx::TFX_COUNT),
                                    gameMusicPcm = std::vector<std::vector<float>>(sfx::GT_COUNT);
    std::vector<float> titleMusicPcm;
    void apply(Game& game, size_t j) {
        const Spec& s = specs[j]; Asset& a = assets[j]; (void)game;
        switch (s.g) {
            case G_PLAYER:   gfx.playerRoll[s.a] = upload(a); if (s.a == 2) gfx.player = gfx.playerRoll[2]; break;
            case G_MON:      gfx.mon[s.a][s.b] = upload(a); break;
            case G_BOSSICON: gfx.bossIcon[s.a] = upload(a); break;
            case G_COURIER:  gfx.courier[s.a] = upload(a); break;
            case G_DRONE:    gfx.drone = upload(a); break;
            case G_OUTPOST:  gfx.outpost = upload(a); break;
            case G_METEOR:   if (fromCache) gfx.meteor[s.a] = upload(a); break;   // generated atlases are already on the GPU
            case G_SFX:      sfxPcm[s.a] = a.pcm; break;
            case G_TITLEFX:  titleFxPcm[s.a] = a.pcm; break;
            case G_TITLEMUSIC: titleMusicPcm = a.pcm; break;
            case G_GAMEMUSIC:  gameMusicPcm[s.a] = a.pcm; break;
            default: break;
        }
    }
    void finish(Game& game) {
        gfx.glow = spr::glow(64);
        gfx.nebula = spr::nebula(300, 400);
        game.loadSfx(sfxPcm);
        game.loadTitleAudio(titleMusicPcm, titleFxPcm);
        game.loadGameMusic(gameMusicPcm);
        sfxPcm.clear(); titleFxPcm.clear(); titleMusicPcm.clear();
        if (!fromCache && !getenv("STARSWARM_NO_CACHE"))   // write the cache in the background; the game starts right away
            saveThread = std::thread([this] { saveCache(specs, assets); assets.clear(); });
        else assets.clear();
    }
    const char* status() const {
        if (phase == READ_CACHE) return "Loading";
        if (phase == UPLOAD || phase == DONE) return fromCache ? "Loading" : "Ready";
        bool musicLeft = false;
        for (size_t j = 0; j < specs.size(); ++j) if (!ready[j]) {
            if (specs[j].g == G_GAMEMUSIC || specs[j].g == G_TITLEMUSIC) musicLeft = true;
            else return statusFor((Group)lastGroup.load());
        }
        return musicLeft ? statusFor(G_GAMEMUSIC) : "Ready";
    }
    float progress() const { return std::clamp((float)unitsDone / std::max(1, unitsTotal), 0.0f, 1.0f); }
    void shutdown() {   // on exit: stop generating, but let a cache write finish so the file is never half-written
        cancel = true;
        for (auto& t : pool) if (t.joinable()) t.join();
        if (cacheThread.joinable()) cacheThread.join();
        if (saveThread.joinable()) saveThread.join();
    }
};

} // namespace startup


#ifdef __ANDROID__
// raylib's Android backend keeps the audio device running while the app is in the background, and the game loop
// is blocked then. Its app-command handler is wrapped so the game goes quiet and opens the pause menu instead.
#include <android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <android/log.h>
#include <unistd.h>
extern "C" struct android_app* GetAndroidApp(void);
// raylib 5.5's OpenGL ES 3 path calls the extension name glDrawBuffersEXT (rlActiveDrawBuffers); Android's GLES3
// library exports the core glDrawBuffers, so the name is forwarded.
extern "C" void glDrawBuffersEXT(GLsizei n, const GLenum* bufs) { glDrawBuffers(n, bufs); }
static void (*raylibAppCmd)(struct android_app*, int32_t) = nullptr;
static std::atomic<bool> appLostFocus{ false };
static Game* runningGame = nullptr;
static void appCmdHook(struct android_app* app, int32_t cmd) {
    raylibAppCmd(app, cmd);
    switch (cmd) {
    case APP_CMD_PAUSE:        // the app may be killed in the background without warning: keep the save current
        if (runningGame) runningGame->saveHiScore();
        [[fallthrough]];
    case APP_CMD_LOST_FOCUS: SetMasterVolume(0); appLostFocus = true; break;
    case APP_CMD_GAINED_FOCUS: SetMasterVolume(1); break;
    case APP_CMD_DESTROY:
        // raylib 5.5 bug: while unfocused its event loop blocks for the next event even after a destroy request,
        // so the game never sees WindowShouldClose(). Marking the app active again lets the loop return.
        raylibAppCmd(app, APP_CMD_GAINED_FOCUS);
        break;
    }
}
// Does this device offer a 4x MSAA display config? raylib requests MSAA with eglChooseConfig() but never checks the
// result, and with no match it carries on with no usable config, so there is no surface and the screen stays black.
// So the game asks first, with the same attributes raylib uses, and only requests MSAA when a config exists.
static int msaaConfigCount() {
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, nullptr, nullptr)) return 0;
    const EGLint attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                             EGL_DEPTH_SIZE, 16, EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, 4, EGL_NONE };
    EGLint n = 0;
    if (!eglChooseConfig(d, attrs, nullptr, 0, &n)) n = 0;   // count only; raylib initializes the display again itself
    return n;
}

// One game per process. raylib keeps internal state that a second InitWindow() in the same process can't
// reset, so when the activity ends the process ends too, and Android starts a fresh one next time.
// (Linked with -Wl,--wrap=android_main: the native-app glue calls this, and this calls raylib's android_main.)
extern "C" void __real_android_main(struct android_app* app);
extern "C" void __wrap_android_main(struct android_app* app) {
    static std::atomic<bool> started{ false };
    if (started.exchange(true)) _exit(0);   // a new activity in a process whose game already ran: restart clean
    __real_android_main(app);               // returns once the activity has finished and been destroyed
    _exit(0);
}
#endif

#include "bot.h"   // empty unless STARSWARM_BOT

int main(int argc, char** argv) {
    // --selftest: auto-plays for a few seconds, saves a screenshot, exits (for CI/headless checks)
    bool selftest = argc > 1 && std::string(argv[1]) == "--selftest";
#ifdef STARSWARM_BOT
    bool botWatch = argc > 1 && std::string(argv[1]) == "--watch";   // --watch [seed] [startStage]: one bot game at normal speed
    bool botMode = botWatch || (argc > 1 && std::string(argv[1]) == "--bot");   // --bot [runs] [seed] [startStage]: see bot.h
    bool botFast = botMode && !botWatch;
#else
    const bool botMode = false, botFast = false;
#endif
#ifdef __ANDROID__
    {   // save file and startup cache live in the app's private storage
        const char* dir = GetAndroidApp()->activity->internalDataPath;
        setenv("STARSWARM_DATA_DIR", dir, 1);
        setenv("STARSWARM_CACHE_DIR", dir, 1);
    }
#endif

    bool msaa = true;
#ifdef __ANDROID__
    {   // 4x MSAA only where the device offers it (see msaaConfigCount)
        int n = msaaConfigCount();
        msaa = n > 0;
        __android_log_print(ANDROID_LOG_INFO, "starswarm", "MSAA 4x: %d matching EGL configs -> %s", n, msaa ? "on" : "off");
    }
#endif
    SetConfigFlags((botFast ? 0 : FLAG_VSYNC_HINT) | FLAG_WINDOW_RESIZABLE | (msaa ? FLAG_MSAA_4X_HINT : 0));   // bot: uncapped
    InitWindow(TOUCH_UI ? 0 : W, TOUCH_UI ? 0 : H, "Star Swarm");   // Android: 0x0 = the display's native size (sharp, no upscaling)
#ifdef __ANDROID__
    raylibAppCmd = GetAndroidApp()->onAppCmd;
    GetAndroidApp()->onAppCmd = appCmdHook;
#else   // desktop window sizing (on a phone raylib always runs full screen)
    SetWindowMinSize(W / 2, H / 2);
    if (selftest) SetWindowSize(960, 1100);   // exercise scaling + letterboxing
    else {
        // Size the window to ~88% of the monitor height (3:4 aspect) and centre it.
        int m = GetCurrentMonitor();
        int mw = GetMonitorWidth(m), mh = GetMonitorHeight(m);
        int wh = (int)(mh * 0.88f), ww = wh * W / H;
        if (ww > mw) { ww = (int)(mw * 0.9f); wh = ww * H / W; }
        Vector2 mp = GetMonitorPosition(m);
        SetWindowSize(ww, wh);
        SetWindowPosition((int)mp.x + (mw - ww) / 2, (int)mp.y + std::max(30, (mh - wh) / 2));
    }
#endif
    SetExitKey(KEY_NULL);   // ESC handled manually (closes the debug dialog instead of quitting)
    if (!botFast) InitAudioDevice();   // the bot plays silently (sounds and music load as no-ops without a device)
    SetTargetFPS(botFast ? 0 : 60);
    SetRandomSeed((unsigned)std::time(nullptr));

    static Game game;
#ifdef __ANDROID__
    runningGame = &game;
#endif
    game.initStars();
    game.loadHiScore();
    game.loadFont();
    // Build (or load from the cache) all sprites, asteroid atlases, sounds and music behind a live loading screen
    static startup::Loader loader;
    loader.start();
    for (;;) {
        if (WindowShouldClose()) { loader.shutdown(); CloseAudioDevice(); CloseWindow(); return 0; }
        bool done = loader.step(game);
        game.drawLoading(loader.progress(), loader.status(), loader.firstRun, std::min(GetFrameTime(), 0.05f));
        if (done) break;
    }
    game.buildTitleParticles();
#ifdef STARSWARM_BOT
    if (botMode) {
        int a = botWatch ? 1 : 2;   // --watch has no run count
        int runs = botWatch ? 1 : argc > 2 ? std::max(1, atoi(argv[2])) : 20, seed = argc > a + 1 ? atoi(argv[a + 1]) : 1;
        int start = argc > a + 2 ? std::clamp(atoi(argv[a + 2]), 1, FINAL_STAGE) : 1;
        bot::run(game, runs, seed, start, botWatch);
        gfx.unload(); loader.shutdown(); game.unloadGameMusic(); game.unloadTitleAudio(); game.unloadSfx();
        CloseWindow();
        return 0;
    }
#endif
    if (selftest) {
        int st = (argc > 3 && isdigit((unsigned char)argv[3][0])) ? atoi(argv[3]) : 1;
        if (argc > 3 && std::string(argv[3]) == "boss") st = CYCLE;
        if (argc > 3 && std::string(argv[3]) == "dialog") { game.stageDialog = true; game.dlgText = "35"; }
        else if (argc > 3 && std::string(argv[3]) == "title") { /* stay on the title screen */ }
        else { game.debugRun = st != 1; game.newGame(st); }
        if (argc > 4) { game.boss.hp = std::min(game.boss.hp, (float)atof(argv[4])); }   // optional: cap boss HP
    }

    int frame = 0;
    int shotFrame = (selftest && argc > 2) ? atoi(argv[2]) : 900;
    while (!WindowShouldClose()) {
        bool padBack = game.padPressed(GAMEPAD_BUTTON_MIDDLE_LEFT);   // controller Back / View = Esc (never quits)
#ifdef __ANDROID__
        if (appLostFocus.exchange(false) && game.mode == PLAYING && !game.escMenu) game.openEscMenu();   // back from the background: paused
#endif
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK) || padBack) {   // phone Back button acts as Esc
            if (game.stageDialog) game.stageDialog = false;
            else if (game.escMenu) game.escMenu = false;               // ESC again = resume
            else if (game.mode == TITLE && game.medalsOpen) game.medalsOpen = false;
            else if (game.mode == TITLE) { if (!padBack) break; }       // title screen: Esc quits as before
            else game.openEscMenu();
        }
        if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER)))
            ToggleBorderlessWindowed();
        float dt = std::min(GetFrameTime(), 1.0f / 30.0f);
        if (selftest) {
            dt = 1.0f / 60.0f;
            game.autoFire = !getenv("SS_NOFIRE");
            if (frame == 0 && getenv("SS_WEAPON")) { game.weapon = std::clamp(atoi(getenv("SS_WEAPON")), 0, W_COUNT - 1);
                game.wlevel = getenv("SS_LEVEL") ? std::clamp(atoi(getenv("SS_LEVEL")), 0, 3) : 0; }
            game.player.x = W / 2.0f + sinf(frame * 0.02f) * 200;
            if (getenv("SS_GOD")) game.invuln = 0.05f;   // selftest-only invulnerability
            if (getenv("SS_ESC") && frame == atoi(getenv("SS_ESC"))) { game.openEscMenu(); if (getenv("SS_ESCSEL")) game.escSel = atoi(getenv("SS_ESCSEL")); }
            if (getenv("SS_ESCDO") && frame == atoi(getenv("SS_ESCDO"))) game.escChoose(game.escSel);
        }
        if (selftest && argc > 3 && std::string(argv[3]) == "break" && frame == shotFrame - 6 && game.shield) game.breakShield({ game.player.x, game.player.y - SHIELD_RADIUS });
        game.update(dt * game.timeScale());   // boss-kill slow motion
        game.tickRealTime(dt);
        game.updateTitleAudio(dt);
        game.updateGameMusic(dt);
        if (game.quitRequested) break;
        if (game.paused || game.escMenu || game.mode != PLAYING || !game.playerAlive) game.stopLaserLoop();   // beam hum never outlives the beam
        game.draw();
        if (selftest && getenv("SS_SEQ") && frame >= shotFrame - atoi(getenv("SS_SEQ"))) TakeScreenshot(TextFormat("seq%03d.png", frame));
        if (selftest && ++frame == shotFrame) { TakeScreenshot("selftest.png"); break; }
    }

    if (game.score >= game.hiScore) game.saveHiScore();
    if (game.customFont) UnloadFont(game.font);
    gfx.unload();
    rumble::set(0, 0, 0);
    loader.shutdown();                                  // let a first-run cache write finish
    game.unloadGameMusic();
    game.unloadTitleAudio();
    game.unloadSfx();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
