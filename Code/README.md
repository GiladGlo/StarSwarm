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
| ← → / A D | Move |
| Space / ↑ | Fire photon torpedoes (up to 5 on screen) |
| P | Pause |
| F11 / Alt+Enter | Toggle fullscreen |
| Esc | Quit |

**Shield:** a force field absorbs one hit (enemy fire or a ramming diver) and shatters. It regenerates when you respawn and at the start of each stage.

## Stage structure, meteor fields, bosses & victory

The game runs in 10 cycles of 6 stages: **4 alien waves → a meteor field → a boss**. Destroy the 10th boss (stage 60) to win.

**Meteor fields** (stages 5, 11, 17 … 59): large rocks drift slowly down from above for ~30 s (a little longer each cycle). There are 3 sizes, and every torpedo hit splits a rock into two of the next size down: large → 2 medium → 2 small each → dust, so a large meteor takes 7 hits to clear completely. Scoring: large 20, medium 50, small 100 per hit, plus a field-cleared bonus. Touching a rock costs your shield (or your ship). The field is kept dense, starting near 30 % screen coverage and rising to about 45 % in later fields, but never above 50 %. Some rocks have glowing magma veins. About **5 % of spawned rocks are volatile**: always the largest size, rust-copper with ember-orange fissures and a soft, slowly breathing glow. Any hit (torpedo or ramming) makes one explode instantly with no fragments (200 pts), and the blast hits every rock within 150 units exactly like a torpedo (splitting them); other volatile rocks caught in it chain-react a split second later. The blast doesn't hurt your ship.

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

Bosses get more aggressive as their health bar drops (pink → orange → red). Lasers are telegraphed by a flickering targeting line. Destroying boss *n* awards 5,000 × *n*; each torpedo hit is worth 10.

## Debug stage select

On the title screen press **Ctrl+L** to open a dialog and start at any stage (1–60). Type a number, use ↑/↓ (±1) or ←/→ / « » (jump between boss stages), or click the buttons; **Enter** starts, **Esc** cancels. Debug runs show a *DEBUG* tag and don't record a high score.

## Scoring

| Enemy | In formation | Diving |
|---|---|---|
| Bee (yellow) | 50 | 100 |
| Butterfly (red) | 80 | 160 |
| Boss (green, 2 hits) | 150 | 400 |

Extra ship at 20,000, then every 50,000. Each stage gets faster, with more simultaneous divers and more enemy fire.
