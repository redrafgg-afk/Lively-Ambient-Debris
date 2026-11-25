# 🍃 Lively Ambient Debris

[![Platform](https://img.shields.io/badge/Platform-GTA_San_Andreas_PC_(v1.0_US)-blue?style=for-the-badge)](#3-requirements--installation)
[![Version](https://img.shields.io/badge/Version-1.0.0-2ea44f?style=for-the-badge)](#📋-changelog)
[![Architecture](https://img.shields.io/badge/Built_With-Plugin--SDK_%2F_RenderWare_D3D9-orange?style=for-the-badge)](#2-open-source--architecture-overview)

> **A high-performance, biome-aware ambient particle and physics simulation ASI plugin for Grand Theft Auto: San Andreas—bringing streets, parks, forests, and deserts to life with drifting canopy leaves, blowing trash, rolling tumbleweeds, fluttering insects, and dynamic vehicle/pedestrian/explosion wake physics.**

---

## 📑 Table of Contents

1. [Introduction & Key Features](#1-introduction--key-features)
   - [Why This Mod Exists](#why-this-mod-exists)
   - [11 Physically Simulated Debris Types](#11-physically-simulated-debris-types)
   - [Performance Engineering](#performance-engineering)
2. [Open Source & Architecture Overview](#2-open-source--architecture-overview)
3. [Requirements & Installation](#3-requirements--installation)
   - [Prerequisites](#prerequisites)
   - [Installation Steps](#installation-steps)
4. [Configuration Guide (`LivelyAmbientDebris.ini`)](#4-configuration-guide-livelyambientdebrisini)
   - [`[General]` — Quotas, Scale & Distance](#general--quotas-scale--distance)
   - [`[LeafDistribution]` — Canopy vs. Ground Split](#leafdistribution--canopy-vs-ground-split)
   - [`[DebrisWeights_*]` — Per-Biome Spawn Percentages](#debrisweights_--per-biome-spawn-percentages)
   - [`[Physics]` & `[WindSimulation]` — Forces & Triggers](#physics--windsimulation--forces--triggers)
5. [Troubleshooting & FAQ](#5-troubleshooting--faq)
6. [📋 Changelog](#-changelog)

---

## 1. Introduction & Key Features

### Why This Mod Exists
While San Andreas features iconic weather cycles and vast environmental diversity, its roads, sidewalks, parks, and desert highways often feel static at ground level. **Lively Ambient Debris** fills the world around the player with hundreds of lightweight, physically reactive particles that respond naturally to wind gusts, sprinting pedestrians, passing traffic, helicopter rotor downwash, and explosion shockwaves—without relying on heavy external texture packs or sacrificing FPS.

### 11 Physically Simulated Debris Types
Every particle is procedurally generated into a crisp **256×256 16-slot RenderWare texture atlas** with extruded mip-safe borders and simulated with individualized mass, aerodynamic drag, buoyancy, and rendering behavior:

| Category | Debris Types | Physical & Visual Behavior |
| :--- | :--- | :--- |
| **Foliage & Flora** | **Leaves** (3 autumn/green variants), **Tumbleweeds** | Canopy leaves gently drift down from overhead; ground leaves settle along terrain normals and float on water. Tumbleweeds spawn in outer desert rings and roll in 3D across highways. |
| **Urban Litter** | **Newspapers**, **Plastic Bags**, **Cans / Bottle Caps**, **Cigarette Butts**, **Glass Shards** | Newspapers and semi-transparent plastic bags catch wind easily and drift on water; heavier cans, cigarette butts, and glinting glass shards bounce off pavement with realistic mass damping. |
| **Natural Ground** | **Gravel / Brick Chips**, **Feathers**, **Pollen & Dust** | Feathers float delicately on air currents; gravel scatters under tires; soft billboarded dust motes drift through sunlight and desert air. |
| **Ambient Insects** | **Butterflies / Moths**, **Fireflies**, **Dragonflies**, **Beetles / Gnats** | Hover and dart organically above land (avoiding water), land briefly on ground surfaces, and feature **bioluminescent glowing fireflies** at dusk and night. |

### Performance Engineering
Designed from the ground up to run smoothly even with `850+` active particles:
- **Zero-Allocation Hot Loop:** Fixed-size pre-allocated particle pools (`MAX_LEAVES_LIMIT = 1500`) and RenderWare `RwIm3D` vertex buffers—zero heap allocations per frame.
- **Uniform Spatial Hash Grid (`16×16` cells):** Pedestrians, vehicles, rotors, and explosions query only overlapping spatial cells rather than scanning all particles.
- **Distance-Based Simulation & Render LOD:** Near particles (`< 12m`) simulate at `60Hz` with full 3D rotation and dynamic vehicle headlight illumination; mid-range (`12–22m`) steps at `30Hz`; distant particles (`> 22m`) step at `15Hz` using lightweight billboards and cached lighting.
- **Dynamic Quota Scaling:** Automatically throttles active particle quotas if frame times spike and smoothly restores them once FPS recovers.

---

## 2. Open Source & Architecture Overview

**Lively Ambient Debris** is cleanly modularized into Core math/rendering utilities, Environment scanners, and the Leaf/Debris simulation & batch renderer:

```text
source/
├── Main.cpp                              # ASI entry point, INI bootstrapper & GTA:SA game loop hooks
├── Core/
│   ├── DX9Utils.h                        # Direct3D 9 / RenderWare device state helpers
│   └── MathUtils.h                       # FastInvSqrt, deterministic Xorshift RNG & vector math
├── Environment/
│   ├── SurfaceScanner.h / .cpp           # Biome detection (Desert/Forest/City) & ground raycasting
│   └── WindSystem.h / .cpp               # Multi-octave spatial wind gusts & weather-linked wind force
└── Features/
    └── Leaf/
        ├── LeafBatchRenderer.h / .cpp    # Single-pass RwIm3D batched quad submission
        └── LeafSystem.h / .cpp           # Procedural atlas, spatial grid, physics LOD & INI parser
```

For a detailed engineering breakdown of every subsystem, see **[`CODE_BREAKDOWN.md`](./CODE_BREAKDOWN.md)**.

---

## 3. Requirements & Installation

### Prerequisites
- **Grand Theft Auto: San Andreas PC (`v1.0 US` / Hoodlum)**
- **ASI Loader** (such as [Silent's ASI Loader](https://www.gtagarage.com/mods/show.php?id=21709) or the one bundled with ModLoader)

### Installation Steps
1. Download the latest release archive (`LivelyAmbientDebris.zip`).
2. Extract **`LivelyAmbientDebris.asi`** (and optionally **`LivelyAmbientDebris.ini`**) directly into your **GTA San Andreas root directory** (where `gta_sa.exe` is located), or inside any subfolder in `modloader/`.
3. Launch the game. If `LivelyAmbientDebris.ini` is not present, the plugin will automatically generate a fully documented default configuration file next to `LivelyAmbientDebris.asi`.

---

## 4. Configuration Guide (`LivelyAmbientDebris.ini`)

All parameters can be customized in **`LivelyAmbientDebris.ini`** using any text editor:

### `[General]` — Quotas, Scale & Distance
```ini
[General]
InstanceCount=850          ; Maximum active debris particles around the player (16 - 1500)
DynamicQuotaScaling=1      ; Auto-scale particle count when frame time spikes (1 = On, 0 = Off)
SpawnHeight=15.0           ; Max height (meters) above ground where canopy leaves spawn
PositionRange=32.0         ; Horizontal spawn/recycle radius (meters) around the player
ScaleMin=0.065             ; Minimum base particle scale
ScaleMax=0.125             ; Maximum base particle scale
ClumpRatio=0.65            ; Ratio of debris grouped in natural clusters (0.0 - 1.0)
ClumpRadius=3.8            ; Radius (meters) of each debris cluster
DrawDistance=42.0          ; Maximum camera render distance (meters)
SnapToGroundOnStart=1      ; Place ground debris directly onto surfaces at startup (1 = Yes)
```

### `[LeafDistribution]` — Canopy vs. Ground Split
Control the exact ratio between **elevated leaves drifting down from tree canopies** and **ground-level debris**:
```ini
[LeafDistribution]
CanopyLeafPercentage=12.0  ; Leaves spawning high in the air (4.5m to SpawnHeight) and drifting down
GroundLeafPercentage=88.0  ; Leaves and debris spawning near or resting on the ground surface
```
*(Values are automatically normalized if their sum is not `100.0`.)*

### `[DebrisWeights_*]` — Per-Biome Spawn Percentages
Customize the spawn percentage (`0.0` – `100.0`) of all **11 debris types** across **4 distinct environmental zones**. Each section is automatically normalized to `100%`, and setting any entry to `0.0` completely disables that debris type in that zone:
- **`[DebrisWeights_Desert]`** — Bone County & Tierra Robada desert zones (tumbleweeds, dust, gravel, dry leaves).
- **`[DebrisWeights_ForestRural]`** — Red County, Flint County, Whetstone & Mount Chiliad forests/countryside.
- **`[DebrisWeights_CityNatural]`** — Urban parks, grass lawns, dirt paths, and natural ground inside cities.
- **`[DebrisWeights_CityUrban]`** — City streets, sidewalks, asphalt, concrete, and industrial zones.

```ini
[DebrisWeights_CityUrban]
Leaf=47.0
Newspaper=6.0
PlasticBag=3.0
CanCap=2.5
GravelBrick=13.0
CigaretteNail=4.0
GlassShard=1.5
Feather=7.0
Tumbleweed=0.0
Insect=2.0
PollenDust=14.0
```

### `[Physics]` & `[WindSimulation]` — Forces & Triggers
Fine-tune pedestrian footstep disturbance (`MaxTriggerPeds`, `TriggerChance`, `Radius`, `Force`), vehicle/helicopter wake turbulence (`MaxTriggerVehicles`, `VehicleLiftChance`, `VehicleForce`, `VehicleRadius`), gravity/friction (`Gravity`, `UpForce`, `Friction`), and procedural wind gusts (`WindForceX/Y/Z`, `WindScale`, `WindFrequency`, `WindThreshold`).

---

## 5. Troubleshooting & FAQ

- **Can I disable specific debris types (like cigarette butts or insects)?**
  Yes! Open `LivelyAmbientDebris.ini` and set `CigaretteNail=0.0` or `Insect=0.0` in the `[DebrisWeights_*]` sections. The remaining weights automatically normalize to `100%`.
- **How do I make more leaves fall from trees above me?**
  Increase `CanopyLeafPercentage` (e.g., `CanopyLeafPercentage=30.0` and `GroundLeafPercentage=70.0`) under `[LeafDistribution]` in `LivelyAmbientDebris.ini`.
- **Does this mod require any `.txd` or `.dff` model replacements?**
  No. All 16 sprite variants are procedurally generated in memory at startup and rendered via RenderWare's native 3D immediate-mode batching pipeline.

---

## 📋 Changelog

### v1.0.0 (Release)
- **Full Biome & Surface-Aware Debris Ecosystem:** Added 11 distinct debris types with individualized physics, buoyancy, and 16 procedural atlas variants (including 4 insect species with nocturnal glowing fireflies).
- **Separated Canopy & Ground Leaf Spawning:** Added `[LeafDistribution]` (`CanopyLeafPercentage` & `GroundLeafPercentage`) alongside per-location percentage weight tables (`[DebrisWeights_Desert]`, `[DebrisWeights_ForestRural]`, `[DebrisWeights_CityNatural]`, `[DebrisWeights_CityUrban]`) in `LivelyAmbientDebris.ini`.
- **Multi-Stage Performance Pipeline:** Implemented `16×16` uniform spatial grid partitioning, segregated active lists (`AIRBORNE`, `WATER_DRIFT`, `DORMANT_GROUND`), deferred ring-buffer respawning, hierarchical cluster culling, distance-based simulation/render LOD, and split cutout/alpha RenderWare batching.
- **Dynamic Entity & World Reactions:** Added support for pedestrian footsteps, vehicle slipstreams, helicopter/plane rotor downwash, spatial wind gusts, explosion shockwaves, and dynamic night headlight illumination.