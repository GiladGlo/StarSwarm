# Star Swarm — a Galaga-inspired shooter (C++17 / raylib)

Single-file arcade shooter (renders at native resolution — resizable window, fullscreen, sharp text): formation fly-ins, sway/breathe formation, dive-bombing attacks, boss escorts, 2-hit bosses, synthesized sound, particle explosions, high-score saving. No asset files — ships and enemies are rendered at startup by a built-in per-pixel lighting renderer (`sprites.h`: SDF shapes + specular, fresnel rim, ambient occlusion, emissive bloom), and all audio is synthesized in code (`sfxsynth.h`).

## Build

Requires CMake 3.16+ and a C++17 compiler. raylib 5.5 is downloaded automatically if not installed.

**Windows (Visual Studio 2022 / Build Tools):**
```
cmake -B build
cmake --build build --config Release
build\Release\starswarm.exe
```

**macOS / Linux:**
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/starswarm
```
Linux needs X11/GL dev packages: `sudo apt install libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libgl1-mesa-dev`

## Title screen

The titles **STAR SWARM**, **Developed by GILAD GLOBEN** and **Dedicated to DANIEL GLOBEN** are spelled by swarms of tiny animated monsters (Nebula Jellies, Void Mantas and Nova Urchins). Each swarm streams in from both sides along curved entry paths, holds as a gently breathing formation for a few seconds, then peels off left to right and dives away as the next swarm arrives (7 s per title).

Below the titles, an **enemy roster** lists every regular enemy in a 4 × 2 grid, each with its animated sprite and points (formation / diving). The last cell, **BOSS**, rotates through all ten bosses, cross-fading every 1.6 s with each boss's number and name (5,000–50,000 points). Under the grid, a gold-framed **Courier** row shows its points and hit count, plus the five packages it can drop (Spread, Pulse, Homing, Ion and Upgrade), lighting up one at a time.

## Status bar

The bottom of the screen is a status bar with four fixed areas, so indicators never overlap: **spare ships** (up to 4 icons, then one icon with a count, e.g. "x 8") · **weapon** (icon, level, laser heat) with a reserved slot for the **wingman drone** timer · **shield** state and recharge countdown · **stage** number and the campaign bar.

## Controls

| Key | Action |
|---|---|
| ← → ↑ ↓ / W A S D | Move (up/down within the lower ~60% of the screen; the ship always stays fully on screen) |
| Space | Fire (hold to keep firing; each weapon has its own on-screen limit) |
| P | Pause |
| Esc | Pause menu: **Resume game**, **End current game** (back to the title screen) or **Exit to desktop**. Pick with ↑/↓ + Enter, keys 1–3 or the mouse; Esc again resumes. On the title screen Esc quits |
| F11 / Alt+Enter | Toggle fullscreen |
| M | Mute / unmute the music (title screen: title music + effects; in game: background music only). Also the speaker icons in the corners |
| Tab | Title screen: open / close the **Medals** screen (also the medal button next to the mute button) |

**Your fighter:** a detailed ship (panelled wings with flaps, missile pods, ribbed cannons, engine intakes and petalled nozzles, framed canopy with a pilot) with live effects: it banks into turns, its twin engines burn with shock-diamond plumes and stream exhaust, side thrusters puff on sharp direction changes, red/green wingtip and white tail lights strobe, a glint sweeps across the canopy, the cannons flash when firing, and it warps in on every (re)spawn.

**Shield:** a force field absorbs one hit (enemy fire or a ramming diver) and shatters. It recharges by itself 15 s after breaking (the label at the bottom counts down over a recharge bar), and is restored immediately when you respawn and at the start of each stage.

**Shooting down projectiles:** your weapons destroy *physical* enemy projectiles: boss Seeker Missiles, Courier seekers and Meteor Storm rocks (25 pts each). The Pulse Laser beam burns through them and Ion Burst blasts clear them. *Energy* shots (enemy bolts, plasma orbs, tracers, spiral orbs, Nova Ring crystals, lasers and other Courier shots) can't be shot down — dodge them.

## Couriers & the arsenal

From stage 2, every alien wave has one **Courier** in its formation: an armoured cargo ship that never dives. It takes 8–12 hits (its health bar shows above it), its outer armour plates blow off at half health, and its cargo pod glows the colour of what it carries. It shoots back with the basic form of the weapon it carries (or of *your* current weapon if it carries an upgrade). When destroyed it bursts into hull fragments that fly off screen, and a hexagonal **package** falls slowly — fly into it to collect it.

| Package | Effect |
|---|---|
| New weapon (coloured icon) | Switch to that weapon class, keeping your upgrade level |
| Upgrade (gold chevrons) | +1 level, up to Lv3; at Lv3 it's worth 1,000 points **and summons a wingman drone** |

While you have the un-upgraded Photon Torpedo, half the packages are new weapons; once you hold another weapon or an upgraded Photon, 80 % are upgrades. New-weapon drops come from a shuffle bag, so you see each of the other weapon classes once before any repeats.

| Weapon | Role | Behaviour | Limit (Lv0 → Lv3) |
|---|---|---|---|
| Photon Torpedo | all-rounder | the default; fast and precise. Lv2 fires 2, Lv3 fires 3 per volley | 3 → 5 volleys on screen |
| Spread Cannon | close-range crowd control | fan of pellets that burn out after ~430 px, so you must fly up to reach the top rows or a boss. Pellets do half damage to bosses | 3 → 5 pellets (fan widens 30° → 62°), 2 → 3 volleys |
| Pulse Laser | sustained beam, boss melter | continuous beam while held; builds heat and overheats (about 2.5 s at Lv0). Each tick does double damage to bosses. Lv3 pierces | beam width 4.5 → 11 |
| Homing Swarm | hands-free, safe while dodging | mini-missiles that steer to the nearest enemy; in meteor fields each one locks onto the closest rock ahead of your ship. Slower and a little less agile; ¾ damage to bosses | 1 → 4 per shot, 4 → 8 on screen |
| Ion Burst | area heavy cannon | fast orb that explodes in an area (radius 70 → 85) and clears missiles and rocks. A direct boss hit does triple damage. Lv2+ chains lightning to 3 more enemies | 2 → 4 orbs on screen |

The weapons were balanced with an automated 120-game test (each weapon at Lv0 and Lv3, early and late waves, meteor fields and bosses), so each is within reach of the Photon Torpedo overall and wins in its own niche.

**Wingman drone:** catching an upgrade when you're already at Lv3 also brings a small escort drone for 30 s (another one refreshes the timer). It flies on an arc around your ship (left, over the top, right; never below or on it), drifting naturally and dodging incoming enemy shots at up to 75% more than your ship's speed, and fires one basic homing missile at a time (its missiles don't count against your weapon's limit). One enemy hit destroys it; it's also lost if you lose a life, and it doesn't follow you through a hyperspace jump. A green timer next to the weapon indicator counts down; the drone blinks in its last 5 s.

**Losing a life** costs one upgrade level (at Lv0 you drop back to the Photon Torpedo). Your weapon and level carry over between stages, bosses included. The compact indicator next to your spare ships shows the current weapon, its level (dots) and the laser heat gauge.

## Stage structure, meteor fields, bosses & victory

The game runs in 10 cycles of 6 stages: **4 alien waves → a meteor field → a boss**. Destroy the 10th boss (stage 60) to win. The **campaign bar** in the bottom-right corner shows the current stage out of 60 and your progress through the whole game, with a crest for each of the 10 bosses that turns gold once it's defeated (the next boss's crest pulses).

**Alien waves:** each wave stage uses a different formation (Classic, Chevron, Diamond, Twin Pillars, Checkerboard, Ring, Arrowhead, Wings) and mixes six entry paths (swoops, side sweeps, loop-the-loops, rises from the bottom corners, spirals and figure-eights). New enemy types arrive gradually, two at first and then one more every other wave:

| Enemy | First appears | Behaviour | Points (formation / diving) |
|---|---|---|---|
| Comet Imp | stage 2 | living comet with a fiery eye; fast, near-straight strike dive | 120 / 240 |
| Asteroid Brute | stage 8 | magma-veined rock beast; energy shield absorbs the first hit | 150 / 300 |
| Cosmic Leviathan | stage 14 | crowned eye-beast; bursts into two fast Leviathan Spawn when destroyed | 100 / 200 |

Each 6-stage cycle has its own look: sky colours, nebula tint and a distant planet, star or black hole matching that cycle's boss.

**Meteor fields** (stages 5, 11, 17 … 59): large rocks drift slowly down from above for ~30 s (a little longer each cycle). There are 3 sizes, and every torpedo hit splits a rock into two of the next size down: large → 2 medium → 2 small each → dust, so a large meteor takes 7 hits to clear completely. Scoring: large 20, medium 50, small 100 per hit, plus a field-cleared bonus. Touching a rock costs your shield (or your ship). The field is kept dense, starting near 30 % screen coverage and rising to about 45 % in later fields, but never above 50 %. Every rock is a lumpy, cratered, chipped 3D body that tumbles about its own axis as it drifts (rendered on the GPU at startup into 64 rotation frames per rock, cross-faded while playing); some have glowing magma veins. About **5 % of spawned rocks are volatile**: always the largest size, rust-copper with ember-orange fissures and a soft, slowly breathing glow. Any hit (torpedo or ramming) makes one explode instantly with no fragments (200 pts), and the blast hits every rock within 150 units exactly like a torpedo (splitting them); other volatile rocks caught in it chain-react a split second later. The blast doesn't hurt your ship.

**Mining outposts:** three times per field (at about 30 %, 60 % and 90 % of it) a large rock drifts in carrying a mining outpost: habitat module, glass dome, drill rig over a glowing borehole, solar arrays, ore containers, landing pad and a spinning radar dish with blinking beacons. The rock is stabilised (it turns slowly instead of tumbling). It drifts with the field at about 70 % of the average rock speed.

- **Armour:** an outpost takes 6 hits in the first cycle, plus one per boss cycle up to 12. Each hit sparks and flashes it, and a green → yellow → red armour bar appears under the rock. Below ⅔ health the radar dish jams, smoke trails off and the hull starts to burn. Below ⅓ the lights flicker, sparks fly and the fire grows. A volatile blast or an ion burst counts as one hit.
- **Point-defence turret:** a red-striped dome on the station tracks your ship. Every 2.4 s (down to 1.8 s in later cycles) it fires a slow, spinning red flak shell at you, after a half-second red glow at the muzzle as a warning. Flak can be shot down like enemy missiles. It only fires while the outpost is on screen and well above you.
- **Destruction:** the last hit blows the station (500 pts) and releases a package with the same odds as a Courier's cargo. The station's fuel then detonates like a volatile asteroid: no fragments, and a blast that hits every rock in range and can set off chain reactions.

Outposts are drawn on top of the other rocks so they're never hidden.

**Bosses** (stages 6, 12, 18 … 60):

| # | Stage | Boss | Hits | New attack |
|---|---|---|---|---|
| 1 | 6 | VANGUARD – Picket Dreadnought | 40 | plasma fans, turret bursts, fighters |
| 2 | 12 | TALON – Strike Carrier | 60 | spiral barrage |
| 3 | 18 | ECLIPSE – Void Crescent | 85 | charged prow laser |
| 4 | 24 | HYDRA – Hive Mother | 110 | homing missiles |
| 5 | 30 | BASTION – Orbital Fortress | 140 | ring bursts |
| 6 | 36 | MANTIS – Blade Interceptor | 170 | twin lasers |
| 7 | 42 | LEVIATHAN – Siege Cruiser | 200 | meteor storm |
| 8 | 48 | CATACLYSM – Twin Hull Annihilator | 235 | sweeping laser |
| 9 | 54 | SERAPH – Radiant Throne | 270 | every attack |
| 10 | 60 | OBLIVION – Void Emperor | 320 | every attack, fastest |

**Boss weapons:** Plasma Fan (pulsing orb fans), Hunter Volley (aimed tracer bolts), Swarm Launch (fighters emerge from glowing hatches), Vortex Spiral (colour-shifting spiral arms with ribbon trails), Lance Beam (charged prow laser), Seeker Missiles (homing missiles with lock-on reticle; they detonate when out of fuel), Nova Ring (spinning crystals riding an expanding shockwave), Twin Pillars (two fixed beams), Meteor Storm (tumbling rocks; red markers at the top warn where each will enter) and Scythe Beam (a beam that leans and leaves a burn line as the boss sweeps). Every gun shows a muzzle flash when it fires.

**Boss detail:** every boss is built while you fly the meteor field before it, with plated, greebled hulls (equipment boxes, vents, hatches, pipes, lit window rows, a framed bridge canopy), weathering and self-shadowing; HYDRA has domed chitin scales, veins and glowing pustules. At runtime the reactor core beats like a heart (double pulse with shockwave rings) inside counter-rotating containment arcs with plasma crackle, navigation lights strobe red/green with a white keel beacon, and engine plumes flicker. The heartbeat quickens as the boss is damaged, and below half health its hull sparks, smokes and (below a quarter) burns.

**Hyperspace jump:** after each boss (except the last) a window tears open ahead of you, tilted so you can see into its tunnel; the stars stretch into hyperspace streaks along the window's axis, your ship lines up and dives into the tunnel, the window snaps shut with a flash, and you warp in at the next stage (about 4 s, no control or danger during the jump).

Bosses get more aggressive as their health bar drops (pink → orange → red). Lasers are telegraphed by a flickering targeting line. Destroying boss *n* awards 5,000 × *n*; each torpedo hit is worth 10.

## Debug stage select

On the title screen press **Ctrl+L** to open a dialog and start at any stage (1–60). Type a number, use ↑/↓ (±1) or ←/→ / « » (jump between boss stages), or click the buttons; **Enter** starts, **Esc** cancels. Debug runs show a *DEBUG* tag and don't record a high score. During a debug run, keys **1–5** select a weapon and **+ / −** change its level.

## Scoring

All regular enemies are space monsters with smooth looping animation (8 frames each, cross-faded; every monster runs its own cycle so a wave never moves in lockstep). The frames are rendered on all CPU cores at startup.


| Enemy | In formation | Diving |
|---|---|---|
| Nova Urchin (golden, spiny) | 50 | 100 |
| Void Manta (blue, winged) | 80 | 160 |
| Nebula Jelly (teal, 2 hits; turns violet when hit) | 150 | 400 |

Extra ship at 20,000, then every 50,000. Each stage gets faster, with more simultaneous divers and more enemy fire.

## GitHub auto-push

Run `Code\tools\setup-autopush.cmd` once: it does the first push to https://github.com/GiladGlo/StarSwarm and creates a Windows scheduled task (*StarSwarm AutoPush*) that every 15 minutes commits any pending changes and pushes new commits, silently. Activity is logged in `autopush.log` (repo root, not committed). Remove with `schtasks /Delete /TN "StarSwarm AutoPush" /F`.

## Controller

Xbox and PlayStation controllers work alongside the keyboard (the first connected controller is used):

| Button | Action |
|---|---|
| Left stick / D-pad | Move (analog on the stick) |
| A / Right trigger | Fire (hold) |
| Start | Start game (title) / pause |
| Back / View | Pause menu (D-pad to choose, A to confirm, B to resume) |
| Y | Mute / unmute the music |
| X | Title screen: open / close the Medals screen |

With a controller connected, the title prompt reads "PRESS SPACE OR (A) TO START".

**Rumble** (Xbox-compatible controllers on Windows, through XInput):

| Event | Rumble |
|---|---|
| Ion Burst shot | Light |
| Shield breaking | Medium |
| Losing a ship or destroying a boss | Strong |

## Medals

16 medals reward skill and progress. When you earn one, a gold "MEDAL UNLOCKED" banner slides in below the score. On the title screen, the medal button (or **Tab** / **X**) opens a 4×4 grid: earned medals in gold, locked ones grey. Select one to see how to earn it. Medals and the lifetime Courier and flak counters are saved in `starswarm_medals.txt`. Debug runs (Ctrl+L) never earn medals.

| Medal | How to earn it |
|---|---|
| Untouchable | Clear a meteor field without taking a hit |
| Flawless Sector | Clear a whole sector without losing a ship |
| Purist | Defeat a boss with the Photon Torpedo at Lv0 |
| Outpost Raider | Destroy all 3 mining outposts in one field |
| Chain Reaction | Destroy 5 rocks with one volatile blast (chain reactions count) |
| Sharpshooter | Finish a wave with 80% accuracy or better |
| Courier Hunter | Destroy 25 Couriers (lifetime) |
| Max Power | Reach Lv3 with any weapon |
| Arsenal | Use all five weapons in one game |
| Wingman | Call in the wingman drone |
| Flak Dancer | Shoot down 20 outpost flak shells (lifetime) |
| Blitz | Destroy a boss in under 30 seconds |
| Sector 2 | Make your first hyperspace jump |
| Halfway There | Defeat the 5th capital ship |
| No Continues | Reach sector 6 without losing a ship |
| Savior | Defeat all ten capital ships |

## Boss kill and sector summary

When a boss's core detonates, the game drops into slow motion with a white flash, then eases back to full speed over about 1.5 s. A **SECTOR CLEARED** panel then shows:

- sector time, accuracy, ships lost, outposts destroyed and boss time
- the medals earned in that sector
- the **sector bonus**: 1,000 × the sector number, +2,000 if no ships were lost, +1,000 for 70% accuracy or better, and +500 per outpost destroyed

The hyperspace jump follows after 6 s, or sooner if you press Space (or A).

## Sound

All sound effects are synthesised at startup by `sfxsynth.h`, a small procedural sound engine. It builds bass-heavy, modern sounds from layers of pitch-dropping sub-bass, filtered noise, FM growl, saturation and a short reverb, at 44.1 kHz. Each sound can play up to 3 overlapping copies, so rapid fire and chained explosions don't cut each other off, and each has its own mix level.

- **Weapons:** each has its own fire sound. The Photon Torpedo is a punchy sub thump with a plasma zap, the Spread Cannon a gritty triple-burst shotgun crack, and the Homing Swarm a launch thump, rocket whoosh and seeker chirps. The Ion Burst is a growling FM "vwoom" with electric crackle that lands with an electric thunderclap. The Pulse Laser hums with a looping bass beam and vents steam when it overheats.
- **Explosions:** punchy enemy explosions, long rumbling detonations for bosses, volatile rocks and outposts, and a falling-tone version when your ship is destroyed.
- **Title screen:** a looping dark space-ambient track (`titlemusic.h`, 28 s, D minor). It has a sub drone, breathing pads, a distant pulse, solar wind and sparse FM bells, and its chord changes with each title. The swarm gets effects too: a whoosh as it streams in, a bell shimmer as the letters lock in, and a falling swoosh as it dives away. Pressing Space plays an engine launch, and the music fades out over about a second.
- **In-game music:** five dark, driving scores (`gamemusic.h`, 31–38 s seamless loops) play quietly under the sound effects. Each sector runs **Drift** (wave 1, 104 BPM), **Vanguard** (wave 2, 110 BPM), **Nebula Run** (wave 3, 116 BPM), **Drift** (wave 4), **Asteroid Field** (meteor field, 100 BPM) and **Capital Ship** (boss, 124 BPM), then the rotation restarts.
  - Stages crossfade into each other over 1.5 s.
  - The boss music fades out as the boss explodes, so the hyperspace jump is quiet.
  - The music pauses with the game and fades out on game over and victory.
- **In-game music mute:** the speaker icon in the top-right corner during play (click it or press **M**, also while paused) turns the background music off or on. Sound effects keep playing. The setting is saved in `starswarm_settings.txt`.
- **Mute button:** the speaker icon in the bottom-left corner of the title screen (click it or press **M**) mutes or unmutes the title music and effects. Gameplay sound isn't affected. The setting is saved in `starswarm_settings.txt` next to the exe.
- **Everything else:** armour hits, torpedo impacts, the outpost flak cannon, shield up and break, stage and extra-life jingles, the boss alarm, enemy dives and the boss lance beam.
