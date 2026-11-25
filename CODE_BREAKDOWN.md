# 🏗️ Architecture & Code Breakdown (`v1.0.0`)

This document provides a complete module-by-module engineering breakdown of **Lively Ambient Debris (`v1.0.0`)**. The plugin is written in C++ using **Plugin-SDK** and **RenderWare D3D9**, engineered around a zero-allocation, multi-stage spatial simulation and batched rendering architecture capable of sustaining `850–1500` interactive particles at high frame rates.

---

## 📑 Table of Contents

1. [High-Level Architecture & 4-Stage Optimization Pipeline](#1-high-level-architecture--4-stage-optimization-pipeline)
2. [`Main.cpp` — Plugin Lifecycle, INI Bootstrap & Render Hook](#2-maincpp--plugin-lifecycle-ini-bootstrap--render-hook)
3. [`Core/` — Math Utilities & Direct3D 9 State Helpers](#3-core--math-utilities--direct3d-9-state-helpers)
   - [`Core/MathUtils.h`](#coremathutilsh)
   - [`Core/DX9Utils.h`](#coredx9utilsh)
4. [`Environment/` — Biome Detection, Ground Raycasting & Spatial Wind](#4-environment--biome-detection-ground-raycasting--spatial-wind)
   - [`Environment/SurfaceScanner.h` & `.cpp`](#environmentsurfacescannerh--cpp)
   - [`Environment/WindSystem.h` & `.cpp`](#environmentwindsystemh--cpp)
5. [`Features/Leaf/LeafBatchRenderer.h` & `.cpp` — Single-Pass RenderWare Batching](#5-featuresleafleafbatchrendererh--cpp--single-pass-renderware-batching)
6. [`Features/Leaf/LeafSystem.h` & `.cpp` — Core Simulation & Rendering Engine](#6-featuresleafleafsystemh--cpp--core-simulation--rendering-engine)
   - [Data Layout & Segregated Active State Lists](#data-layout--segregated-active-state-lists)
   - [Configuration Loader & Normalized Percentage Tables](#configuration-loader--normalized-percentage-tables)
   - [Procedural 256×256 16-Slot Atlas Generator](#procedural-256256-16-slot-atlas-generator)
   - [Uniform `16×16` Spatial Hash Grid & Ground Sample Cache](#uniform-1616-spatial-hash-grid--ground-sample-cache)
   - [Event-Driven Activation & Distance-Based Simulation LOD](#event-driven-activation--distance-based-simulation-lod)
   - [Hierarchical Cluster Culling & Two-Pass Cutout/Alpha Rendering](#hierarchical-cluster-culling--two-pass-cutoutalpha-rendering)

---

## 1. High-Level Architecture & 4-Stage Optimization Pipeline

The simulation and rendering pipeline is structured into four performance stages (`Stage 1`–`Stage 4`) to eliminate per-frame heap allocations, redundant ground raycasts, and expensive RenderWare state switches:

| Stage | Subsystem | Engineering Summary |
| :--- | :--- | :--- |
| **Stage 1** | **Precomputed Physics & Temporal Caching** | Precomputes mass, inverse mass, drag, gravity multipliers, and alpha mode into `LeafPhysicsProps` on spawn. Caches dynamic TimeCycle/headlight vertex colors across `1` / `3` / `6` frames based on camera distance. |
| **Stage 2** | **Segregated Active Lists & Deferred Respawn** | Separates particles into $O(1)$ swap-remove lists (`aAirborneList`, `aWaterList`, `aDormantList`) so resting ground debris consumes near-zero CPU. Out-of-bounds particles enter a ring buffer (`aRespawnQueue`, max `8` respawns/frame) to prevent frame-time spikes. |
| **Stage 3** | **Uniform `16×16` Spatial Grid & Ground Cache** | Maps the local player bubble (`96m × 96m`, `6.0m` cells) into a wrap-around spatial hash grid for localized ped/vehicle/explosion queries and shares ground raycast results (`aGroundCache`, `2.5s` TTL, `16` raycasts/frame budget). |
| **Stage 4** | **Hierarchical Culling & Split-Pass Batching** | Performs cluster bounding-sphere frustum culling (`CLUSTER_COUNT = 12`), classifies visible particles into `NEAR_FULL` (`0–12m`), `MID_REDUCED` (`12–22m`), and `FAR_BILLBOARD` (`>22m`), and renders opaque cutouts (`Z-Write ON` + Alpha Test) before translucent particles (`Z-Write OFF` + Alpha Blend). |

---

## 2. `Main.cpp` — Plugin Lifecycle, INI Bootstrap & Render Hook

`Main.cpp` binds the modular subsystems into GTA San Andreas's native execution loop via **Plugin-SDK**:
- **Module Directory Resolution (`GetModuleDirectory()`):** Uses `GetModuleHandleExA` with `GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS` so `LivelyAmbientDebris.ini` is always resolved relative to `LivelyAmbientDebris.asi`—even when loaded from a `modloader/` subfolder.
- **Auto-Config Generation (`EnsureConfigExists()`):** Checks for `LivelyAmbientDebris.ini` and invokes `CLeafSystem::SaveDefaultConfig(f)` + `CWindSystem::SaveDefaultConfig(f)` if missing.
- **Game Loop & Render Hooks:**
  - `Events::initGameEvent`: Initializes `CWindSystem` and `CLeafSystem`.
  - `Events::gameProcessEvent`: Computes clamped frame delta time (`CTimer::ms_fTimeStep / 50.0f`) and advances `CWindSystem::Update()` and `CLeafSystem::Update()`.
  - `patch::RedirectCall(0x53E175, RenderAllSystems)`: Hooks directly into GTA:SA's post-world translucent/particle render pass so debris sorts cleanly against world geometry, water, and vehicles.
  - `Events::shutdownRwEvent`: Releases RenderWare textures and rasters cleanly before device reset or exit.

---

## 3. `Core/` — Math Utilities & Direct3D 9 State Helpers

### `Core/MathUtils.h`
Provides lightweight, inline math and deterministic PRNG primitives designed for high-frequency particle updates:
- **`FastInvSqrt(float x)` & `FastVecNormalize(CVector& v)`:** Quake-style fast inverse square root with one Newton-Raphson iteration and zero-length guard.
- **`FastSin(float x)` & `FastCos(float x)`:** Branchless range-reduced parabolic sine/cosine approximation (`[-pi, pi]`) used for wind gusts, insect wing flutter, and water bobbing.
- **`HashToFloat01(uint32_t& state)`:** Per-particle Xorshift32 PRNG producing deterministic `[0, 1)` floats without touching global `rand()` state.

### `Core/DX9Utils.h`
Provides RenderWare-to-Direct3D9 device queries (`GetD3D9Device()`) and state-block helpers for low-level graphics state inspection.

---

## 4. `Environment/` — Biome Detection, Ground Raycasting & Spatial Wind

### `Environment/SurfaceScanner.h` & `.cpp`
Bridges GTA San Andreas's world geometry and surface material system (`CWorld`, `CColPoint`, `g_surfaceInfos`):
- **`DetectBiome(const CVector& pos)`:** Classifies world coordinates into `eBiomeZone::DESERT` (Bone County & Tierra Robada bounding regions), `eBiomeZone::FOREST_RURAL` (Red County, Flint County, Whetstone & Mount Chiliad), or `eBiomeZone::CITY_URBAN` (Los Santos, San Fierro, Las Venturas).
- **`SampleGround(...)`:** Performs vertical collision line-of-sight probes (`CWorld::ProcessVerticalLine`) to extract ground elevation (`fGroundZ`), surface normal (`vecNormal`), surface material ID (`nSurfaceType`), and ambient/directional vertex lighting (`fShade01`).
- **`IsNaturalGroundSurface(unsigned char surfaceType)`:** Identifies grass, dirt, mud, sand, and foliage materials so city parks and lawns spawn natural park debris rather than urban street litter.

### `Environment/WindSystem.h` & `.cpp`
Simulates continuous, spatially coherent wind fields rather than uniform global drift:
- **Multi-Octave Wind Force (`Update(float currentTime)`):** Combines low-frequency directional waves with `CWeather::Wind` intensity to update `vecActiveWindForce` and `fCanopyDriftScale`.
- **`SampleSpatialGust(float x, float y)`:** Evaluates a 2D trigonometric turbulence field in world space so localized wind gusts sweep across resting ground leaves in organic waves.

---

## 5. `Features/Leaf/LeafBatchRenderer.h` & `.cpp` — Single-Pass RenderWare Batching

`CLeafBatchRenderer` eliminates per-particle draw calls by batching quads into pre-allocated `RwIm3DVertex` and `RwImVertexIndex` buffers (`MAX_BATCH_QUADS = 1500`):
- **`BeginBatch(RwRaster* raster)`:** Binds the 256×256 procedural atlas raster once (`rwRENDERSTATETEXTURERASTER`).
- **`PushQuad(...)`:** Writes 4 transformed world-space vertices (with pre-lit `RwRGBA` color and atlas `u, v` coordinates) and 6 triangle indices (`0-1-2`, `0-2-3`), auto-flushing if buffer capacity is reached.
- **`Flush()` & `EndBatch()`:** Submits the entire batch in a single `RwIm3DTransform` + `RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST)` call.

---

## 6. `Features/Leaf/LeafSystem.h` & `.cpp` — Core Simulation & Rendering Engine

### Data Layout & Segregated Active State Lists
- **Particle Pool (`CLeaf aLeaves[MAX_LEAVES_LIMIT]`):** Holds up to `1500` particles, each tracking world position/velocity, orthonormal basis (`m_vecForward`, `m_vecUp`), ground cache coordinates, per-instance PRNG seed (`m_nRngState`), intrusive spatial linked-list pointers (`m_nSpatialCell`, `m_nNextInCell`, `m_nPrevInCell`), and precomputed `LeafPhysicsProps`.
- **State Machine (`eLeafState`):**
  - `DORMANT_GROUND`: Resting flat along `m_vecGroundNormal`. Skips physics integration entirely until woken by a spatial trigger or wind gust.
  - `AIRBORNE`: Actively simulated under gravity, aerodynamic drag, canopy drift, wind turbulence, and entity impulses.
  - `WATER_DRIFT`: Buoyant debris (`LEAF`, `FEATHER`, `NEWSPAPER`, `PLASTIC_BAG`, `TUMBLEWEED`) drifting along water currents at `z ≈ 0.04m` until washing ashore.
  - `INACTIVE_RESPAWN`: Parked in `aRespawnQueue` awaiting staggered respawn (`maxPerFrame = 8`).

### Configuration Loader & Normalized Percentage Tables
- **`[LeafDistribution]` (`fCanopyLeafPct`, `fGroundLeafPct`):** Splits spawned leaves between elevated canopy-drifting leaves (`isCanopySpawn = true`, spawning `4.5m` to `SpawnHeight` above ground) and ground-level debris.
- **`DebrisSpawnWeights` (`sWeightsDesert`, `sWeightsForest`, `sWeightsCityNatural`, `sWeightsCityUrban`):** Stores user-configurable percentage weights (`0.0`–`100.0`) for all 11 `eDebrisType` enums. `PickWeightedDebrisType()` sums active weights dynamically and normalizes them to `100%` at runtime.

### Procedural 256×256 16-Slot Atlas Generator
`CreateProceduralAtlasTexture()` synthesizes a 4×4 grid of 64×64 RGBA8888 sprites in memory at startup:
- **Slots `0–2`:** Three leaf shapes/hues (olive green, autumn amber, golden yellow) with central veins.
- **Slots `3–8`:** Crinkled newspaper with headline/text lines, translucent plastic bag, red-labeled aluminum can, faceted brick/gravel chip, filtered cigarette butt, and specular glass shard.
- **Slots `9–10`:** Curved white feather and interwoven desert tumbleweed sphere.
- **Slots `11–15`:** Monarch-style butterfly/moth (`11`), radial soft pollen/dust mote (`12`), glowing firefly (`13`), blue-bodied dragonfly (`14`), and dark beetle/gnat (`15`).
- **Mipmap Bleed Safety:** Each 64×64 tile renders into an inner 62×62 region and extrudes a 1-pixel border around all four edges, paired with a half-texel UV inset (`0.5f / 256.0f`) during rendering to prevent bilinear texture bleeding.

### Uniform `16×16` Spatial Hash Grid & Ground Sample Cache
- **Sliding Window Grid (`nGridOriginCellX`, `nGridOriginCellY`):** Covers a `96m × 96m` area (`16 × 16` cells of `6.0m` each) centered on the player. Whenever the player crosses a cell boundary, the grid origin shifts and active particles re-bin into their new spatial cells.
- **Ground Sample Cache (`QueryGroundCached`):** Stores ground height, surface normal, shade, material type, and water flag per spatial cell with a `2.5s` expiration window, reducing `CWorld::ProcessVerticalLine` calls to at most `16` per frame via a round-robin eligibility cursor (`nRaycastCursor`).

### Event-Driven Activation & Distance-Based Simulation LOD
- **`DispatchEventDrivenActivations()`:**
  - Scans GTA:SA's explosion pool (`0xC88950`), active pedestrians (`CPools::ms_pPedPool`), and active vehicles/aircraft (`CPools::ms_pVehiclePool`).
  - Queries only the spatial grid cells overlapped by each entity's trigger radius to wake `DORMANT_GROUND` particles with directional slipstreams, helicopter/plane radial rotor downwash, or radial blast waves.
  - Evaluates ambient wind gusts (`CWindSystem::SampleSpatialGust`) and insect takeoffs across a `1/4` staggered slice of `aDormantList` per frame.
- **Adaptive Simulation Frequency (`UpdateAirborne` & `UpdateWaterDrift`):** Accumulates `m_fSimAccumDt` across skipped frames and steps particles at `60Hz` (`< 12m`), `30Hz` (`12–22m`), or `15Hz` (`> 22m`), skipping 3D angular rotation math beyond `22m`.

### Hierarchical Cluster Culling & Two-Pass Cutout/Alpha Rendering
- **Cluster Frustum Pre-Pass:** Tests bounding spheres for the 12 spawn clusters (`aClusterOffsets`) against `TheCamera.IsSphereVisible()` before inspecting individual particles.
- **Split-Pass Rendering (`Render()`):**
  - **Pass A (Cutout Alpha):** Renders crisp opaque-interior debris (`LEAF`, `NEWSPAPER`, `CAN_CAP`, `GRAVEL_BRICK`, `CIGARETTE_NAIL`, `TUMBLEWEED`) with `rwRENDERSTATEZWRITEENABLE = TRUE` and `rwALPHATESTFUNCTIONGREATEREQUAL` (`ref = 128`).
  - **Pass B (Translucent Alpha):** Renders soft/semi-transparent particles (`PLASTIC_BAG`, `GLASS_SHARD`, `FEATHER`, `INSECT`, `POLLEN_DUST`) with `rwRENDERSTATEZWRITEENABLE = FALSE` and standard alpha blending.
  - **Geometry LOD:** Tumbleweeds render as **3 crossed 3D quads** at near range (`< 12m`), **2 crossed quads** at mid range (`12–22m`), and a **single camera-facing billboard** beyond `22m`.