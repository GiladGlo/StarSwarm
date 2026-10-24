# Star Swarm — a Galaga-inspired shooter (C++17 / raylib)

Single-file arcade shooter (renders at native resolution — resizable window, fullscreen, sharp text): formation fly-ins, sway/breathe formation, dive-bombing attacks, boss escorts, 2-hit bosses, synthesized sound, particle explosions, high-score saving. No asset files — ships and enemies are rendered at startup by a built-in per-pixel lighting renderer (`sprites.h`: SDF shapes + specular, fresnel rim, ambient occlusion, emissive bloom), and all audio is synthesized in code.

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

The title rotates between **STAR SWARM**, **Developed by Gilad Globen** and **Dedicated to Daniel Globen**. Each one holds for 5 s, becomes unstable for 2 s (tremor, glitch slices, colour split, sparks), blows apart into particles that fly off screen (2 s), and the same particles fly back and form the next title (2 s).

## Controls

| Key | Action |
|---|---|
| ← → ↑ ↓ / W A S D | Move (up/down within the lower ~60% of the screen; the ship always stays fully on screen) |
| Space | Fire (hold to keep firing; each weapon has its own on-screen limit) |
| P | Pause |
| Esc | Pause menu: **Resume game**, **End current game** (back to the title screen) or **Exit to desktop**. Pick with ↑/↓ + Enter, keys 1–3 or the mouse; Esc again resumes. On the title screen Esc quits |
| F11 / Alt+Enter | Toggle fullscreen |

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

| Weapon | Behaviour | Limit (Lv0 → Lv3) |
|---|---|---|
| Photon Torpedo | the default; Lv2 fires 2, Lv3 fires 3 per volley | 3 → 5 volleys on screen |
| Spread Cannon | fan of pellets, wider and denser with level | 3 → 7 pellets, 2 → 3 volleys |
| Pulse Laser | continuous beam while held; builds heat and overheats (cool-down until the gauge drops). Lv3 pierces | beam width 4.5 → 11 |
| Homing Swarm | mini-missiles that steer to the nearest enemy; in meteor fields each one locks onto the closest rock ahead of your ship (or flies straight if there is none) | 2 → 4 per shot, 6 → 10 on screen |
| Ion Burst | slow orb that explodes in an area; Lv3 chains lightning to 3 more enemies | 2 → 3 orbs on screen |

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

**Mining outposts:** three times per field (at about 30 %, 60 % and 90 % of it) a large rock drifts in carrying a mining outpost: habitat module, glass dome, drill rig over a glowing borehole, solar arrays, ore containers, landing pad and a spinning radar dish with blinking beacons. The rock is stabilised (it turns slowly instead of tumbling). One hit from anything — including a volatile rock's blast — blows the outpost off (500 pts) and releases a package with the same odds as a Courier's cargo; the rock then splits into two medium asteroids like any large rock.

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
