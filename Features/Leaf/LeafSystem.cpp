#include "LeafSystem.h"
#include "LeafBatchRenderer.h"
#include "../../Core/MathUtils.h"
#include "../../Environment/WindSystem.h"
#include "../../Environment/SurfaceScanner.h"

#include "CTimer.h"
#include "CCamera.h"
#include "CPools.h"
#include "CTimeCycle.h"
#include "CClock.h"
#include "CExplosion.h"
#include "common.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>

CLeaf CLeafSystem::aLeaves[MAX_LEAVES_LIMIT];
CVector CLeafSystem::aClusterOffsets[CLUSTER_COUNT];
RwTexture* CLeafSystem::pLeafTexture = nullptr;
RwRaster* CLeafSystem::pProcRaster = nullptr;
bool CLeafSystem::bInitialized = false;
unsigned int CLeafSystem::nFrameCounter = 0;
CVector CLeafSystem::vecPrevPlayerPos = CVector(0.0f, 0.0f, 0.0f);

bool CLeafSystem::bDynamicQuotaScaling = true;
int CLeafSystem::nEffectiveQuota = 850;
float CLeafSystem::fSmoothedFrameTime = 0.0167f;
int CLeafSystem::nGroundRaycastBudget = 16;

// Stage 2: Active Lists
int CLeafSystem::aAirborneList[MAX_LEAVES_LIMIT];
int CLeafSystem::nAirborneCount = 0;
int CLeafSystem::aWaterList[MAX_LEAVES_LIMIT];
int CLeafSystem::nWaterCount = 0;
int CLeafSystem::aDormantList[MAX_LEAVES_LIMIT];
int CLeafSystem::nDormantCount = 0;

// Stage 2: Deferred Respawn Queue
int CLeafSystem::aRespawnQueue[MAX_LEAVES_LIMIT];
bool CLeafSystem::aInRespawnQueue[MAX_LEAVES_LIMIT];
int CLeafSystem::nRespawnHead = 0;
int CLeafSystem::nRespawnTail = 0;
int CLeafSystem::nRespawnCount = 0;

int CLeafSystem::nRaycastCursor = 0;

// Stage 3: Spatial Grid & Ground Cache
int CLeafSystem::aSpatialCellHeads[SPATIAL_GRID_CELLS];
GroundSpatialCacheEntry CLeafSystem::aGroundCache[SPATIAL_GRID_CELLS];
int CLeafSystem::nGridOriginCellX = 0;
int CLeafSystem::nGridOriginCellY = 0;

// Stage 4: Visible Lists
VisibleParticleEntry CLeafSystem::aVisibleCutout[MAX_LEAVES_LIMIT];
int CLeafSystem::nVisibleCutoutCount = 0;
VisibleParticleEntry CLeafSystem::aVisibleAlpha[MAX_LEAVES_LIMIT];
int CLeafSystem::nVisibleAlphaCount = 0;
unsigned int CLeafSystem::nLastVisibilityPassFrame = 0xFFFFFFFFu;

int CLeafSystem::nInstanceCount = 850;
float CLeafSystem::fForce = 75.0f;
float CLeafSystem::fGravity = 10.0f;
float CLeafSystem::fUpForce = 1.65f;
int CLeafSystem::nMaxTriggerPeds = 5;
int CLeafSystem::nMaxTriggerVehicles = 4;
LeafTriggerSource CLeafSystem::aActivePeds[MAX_TRIGGER_PEDS_CAP];
int CLeafSystem::nActivePedCount = 0;
LeafTriggerSource CLeafSystem::aActiveVehs[MAX_TRIGGER_VEHS_CAP];
int CLeafSystem::nActiveVehCount = 0;

float CLeafSystem::fTriggerChance = 0.60f;
float CLeafSystem::fVehicleLiftChance = 0.70f;
float CLeafSystem::fVehicleForce = 95.0f;
float CLeafSystem::fVehicleRadius = 5.5f;
float CLeafSystem::fFriction = 0.50f;
float CLeafSystem::fRadius = 3.0f;

float CLeafSystem::fSpawnHeight = 15.0f;
float CLeafSystem::fPositionRange = 32.0f;
float CLeafSystem::fScaleMin = 0.065f;
float CLeafSystem::fScaleMax = 0.125f;
float CLeafSystem::fClumpRatio = 0.65f;
float CLeafSystem::fClumpRadius = 3.8f;
float CLeafSystem::fDrawDistance = 42.0f;
bool CLeafSystem::bSnapToGroundOnStart = true;

float CLeafSystem::fCanopyLeafPct = 12.0f;
float CLeafSystem::fGroundLeafPct = 88.0f;

// Order: Leaf, Newspaper, PlasticBag, CanCap, GravelBrick, CigaretteNail, GlassShard, Feather, Tumbleweed, Insect, PollenDust
DebrisSpawnWeights CLeafSystem::sWeightsDesert = { { 15.0f, 0.0f, 0.0f, 0.0f, 30.0f, 6.5f, 0.0f, 0.0f, 3.0f, 1.5f, 44.0f } };
DebrisSpawnWeights CLeafSystem::sWeightsForest = { { 64.0f, 0.0f, 0.0f, 0.0f,  6.0f, 0.0f, 0.0f, 8.0f, 0.0f, 6.0f, 16.0f } };
DebrisSpawnWeights CLeafSystem::sWeightsCityNatural = { { 62.0f, 7.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0f, 9.0f, 0.0f, 5.5f, 16.5f } };
DebrisSpawnWeights CLeafSystem::sWeightsCityUrban = { { 47.0f, 6.0f, 3.0f, 2.5f, 13.0f, 4.0f, 1.5f, 7.0f, 0.0f, 2.0f, 14.0f } };

// Cached active explosion shockwaves
struct ActiveExplosionWave {
    CVector pos;
    float radius;
    float radSq;
    float power;
    float upLift;
};
static ActiveExplosionWave s_Explosions[8];
static int s_ExplosionCount = 0;
static unsigned int s_PrevExplosionCounter[16] = { 0 };

// Cached headlights with spatial cell metadata (Stage 3 & 4)
struct CachedHeadlight {
    CVector pos;
    CVector fwd;
    int cellX;
    int cellY;
};
static CachedHeadlight s_Headlights[8];
static int s_HeadlightCount = 0;
static bool s_IsNightOrDusk = false;
static CVector s_PlayerPosCache(0.0f, 0.0f, 0.0f);

// Lookup table for mass & inverse mass indexed by eDebrisType (0..10)
static constexpr float kMassTable[11] = {
    1.00f, // LEAF
    0.75f, // NEWSPAPER
    0.55f, // PLASTIC_BAG
    2.50f, // CAN_CAP
    2.65f, // GRAVEL_BRICK
    1.75f, // CIGARETTE_NAIL
    1.75f, // GLASS_SHARD
    0.45f, // FEATHER
    1.10f, // TUMBLEWEED
    0.60f, // INSECT
    0.45f  // POLLEN_DUST
};

static constexpr float kInvMassTable[11] = {
    1.0000f,
    1.3333f,
    1.8182f,
    0.4000f,
    0.3774f,
    0.5714f,
    0.5714f,
    2.2222f,
    0.9091f,
    1.6667f,
    2.2222f
};

// Stage 1: Frame-rate independent exponential probability helper P = 1 - exp(-lambda * dt)
static inline bool RandomEventRate(float lambda, float dt, uint32_t& rngState) {
    float p = 1.0f - std::exp(-lambda * dt);
    return HashToFloat01(rngState) < p;
}

static inline int WorldToCellCoord(float v) {
    return static_cast<int>(std::floor(v * SPATIAL_INV_CELL_SIZE));
}

static inline int GetSpatialBucketIndex(int worldCellX, int worldCellY) {
    int lx = (worldCellX - CLeafSystem::nGridOriginCellX) & (SPATIAL_GRID_DIM - 1);
    int ly = (worldCellY - CLeafSystem::nGridOriginCellY) & (SPATIAL_GRID_DIM - 1);
    return (ly * SPATIAL_GRID_DIM) + lx;
}

static float ReadIniFloatLeaf(const char* section, const char* key, float defaultVal, const char* filePath) {
    char buf[64] = { 0 };
    char defStr[64] = { 0 };
    std::snprintf(defStr, sizeof(defStr), "%.4f", defaultVal);
    GetPrivateProfileStringA(section, key, defStr, buf, sizeof(buf), filePath);
    return static_cast<float>(std::atof(buf));
}

static const char* kDebrisIniKeys[11] = {
    "Leaf",
    "Newspaper",
    "PlasticBag",
    "CanCap",
    "GravelBrick",
    "CigaretteNail",
    "GlassShard",
    "Feather",
    "Tumbleweed",
    "Insect",
    "PollenDust"
};

static void WriteWeightSection(FILE* f, const char* sectionName, const char* description, const DebrisSpawnWeights& w) {
    std::fprintf(f, "; %s\n", description);
    std::fprintf(f, "[%s]\n", sectionName);
    for (int i = 0; i < 11; ++i) {
        std::fprintf(f, "%s=%.1f\n", kDebrisIniKeys[i], w.weights[i]);
    }
    std::fprintf(f, "\n");
}

static void ReadWeightSection(const char* sectionName, DebrisSpawnWeights& w, const char* iniPath) {
    for (int i = 0; i < 11; ++i) {
        float val = ReadIniFloatLeaf(sectionName, kDebrisIniKeys[i], w.weights[i], iniPath);
        w.weights[i] = (val < 0.0f) ? 0.0f : val;
    }
}

void CLeafSystem::SaveDefaultConfig(void* filePtr) {
    FILE* f = static_cast<FILE*>(filePtr);
    if (!f) return;

    std::fprintf(f, "; ============================================================================\n");
    std::fprintf(f, "; Lively Ambient Debris - Configuration File\n");
    std::fprintf(f, "; ============================================================================\n\n");

    std::fprintf(f, "[General]\n");
    std::fprintf(f, "; Maximum active debris particles around the player (16 - 1500)\n");
    std::fprintf(f, "InstanceCount=850\n");
    std::fprintf(f, "; Automatically scales particle quota when frame time spikes (1 = Enabled, 0 = Disabled)\n");
    std::fprintf(f, "DynamicQuotaScaling=1\n");
    std::fprintf(f, "; Maximum height (in meters) above ground where canopy leaves spawn and drift down\n");
    std::fprintf(f, "SpawnHeight=15.0\n");
    std::fprintf(f, "; Horizontal radius (in meters) around the player where debris spawns and recycles\n");
    std::fprintf(f, "PositionRange=32.0\n");
    std::fprintf(f, "; Minimum and maximum base particle scale\n");
    std::fprintf(f, "ScaleMin=0.065\n");
    std::fprintf(f, "ScaleMax=0.125\n");
    std::fprintf(f, "; Ratio of debris grouped in natural clusters vs. scattered evenly (0.0 - 1.0)\n");
    std::fprintf(f, "ClumpRatio=0.65\n");
    std::fprintf(f, "; Radius (in meters) of each debris cluster\n");
    std::fprintf(f, "ClumpRadius=3.8\n");
    std::fprintf(f, "; Maximum render distance (in meters) from the camera\n");
    std::fprintf(f, "DrawDistance=42.0\n");
    std::fprintf(f, "; Place ground debris directly onto surfaces at startup (1 = Yes, 0 = Drop from air)\n");
    std::fprintf(f, "SnapToGroundOnStart=1\n\n");

    std::fprintf(f, "[LeafDistribution]\n");
    std::fprintf(f, "; Split percentage between elevated canopy-drifting leaves and ground-level debris.\n");
    std::fprintf(f, "; Values are automatically normalized if their sum is not 100.\n");
    std::fprintf(f, "; CanopyLeafPercentage: Leaves spawning high in the air (4.5m to SpawnHeight) and drifting down.\n");
    std::fprintf(f, "CanopyLeafPercentage=12.0\n");
    std::fprintf(f, "; GroundLeafPercentage: Leaves and debris spawning near or resting on the ground surface.\n");
    std::fprintf(f, "GroundLeafPercentage=88.0\n\n");

    std::fprintf(f, "; ============================================================================\n");
    std::fprintf(f, "; Debris Spawn Percentages by Location (0.0 - 100.0)\n");
    std::fprintf(f, "; Each category is automatically normalized to 100%%, so you can freely adjust\n");
    std::fprintf(f, "; any value or set unwanted debris types to 0.0.\n");
    std::fprintf(f, "; ============================================================================\n\n");

    WriteWeightSection(f, "DebrisWeights_Desert",
        "Bone County & Tierra Robada desert zones (sand, arid scrub, tumbleweeds, dust)", sWeightsDesert);
    WriteWeightSection(f, "DebrisWeights_ForestRural",
        "Red County, Flint County, Whetstone & Mount Chiliad forests and countryside", sWeightsForest);
    WriteWeightSection(f, "DebrisWeights_CityNatural",
        "Urban parks, grass lawns, dirt paths, and natural surfaces inside cities", sWeightsCityNatural);
    WriteWeightSection(f, "DebrisWeights_CityUrban",
        "City streets, sidewalks, asphalt, concrete, and industrial areas", sWeightsCityUrban);

    std::fprintf(f, "[Physics]\n");
    std::fprintf(f, "; Maximum nearby pedestrians and vehicles that can disturb ground debris\n");
    std::fprintf(f, "MaxTriggerPeds=5\n");
    std::fprintf(f, "MaxTriggerVehicles=4\n");
    std::fprintf(f, "; Horizontal push force, gravity, and upward lift applied to disturbed debris\n");
    std::fprintf(f, "Force=75.0\n");
    std::fprintf(f, "Gravity=10.0\n");
    std::fprintf(f, "UpForce=1.65\n");
    std::fprintf(f, "; Probability (0.0 - 1.0) and radius (meters) for pedestrian footstep disturbance\n");
    std::fprintf(f, "TriggerChance=0.60\n");
    std::fprintf(f, "Friction=0.50\n");
    std::fprintf(f, "Radius=3.0\n");
    std::fprintf(f, "; Probability (0.0 - 1.0), force, and wake radius (meters) for passing vehicles\n");
    std::fprintf(f, "VehicleLiftChance=0.70\n");
    std::fprintf(f, "VehicleForce=95.0\n");
    std::fprintf(f, "VehicleRadius=5.5\n\n");
}

void CLeafSystem::LoadConfig(const char* iniPath) {
    int count = GetPrivateProfileIntA("General", "InstanceCount", 850, iniPath);
    nInstanceCount = (count < 16) ? 16 : ((count > MAX_LEAVES_LIMIT) ? MAX_LEAVES_LIMIT : count);
    bDynamicQuotaScaling = GetPrivateProfileIntA("General", "DynamicQuotaScaling", 1, iniPath) != 0;
    nEffectiveQuota = nInstanceCount;
    fSpawnHeight = ReadIniFloatLeaf("General", "SpawnHeight", 15.0f, iniPath);
    fPositionRange = ReadIniFloatLeaf("General", "PositionRange", 32.0f, iniPath);
    fScaleMin = ReadIniFloatLeaf("General", "ScaleMin", 0.065f, iniPath);
    fScaleMax = ReadIniFloatLeaf("General", "ScaleMax", 0.125f, iniPath);
    fClumpRatio = ClampFloat(ReadIniFloatLeaf("General", "ClumpRatio", 0.65f, iniPath), 0.0f, 1.0f);
    fClumpRadius = ReadIniFloatLeaf("General", "ClumpRadius", 3.8f, iniPath);
    fDrawDistance = ReadIniFloatLeaf("General", "DrawDistance", 42.0f, iniPath);
    bSnapToGroundOnStart = GetPrivateProfileIntA("General", "SnapToGroundOnStart", 1, iniPath) != 0;

    fCanopyLeafPct = ClampFloat(ReadIniFloatLeaf("LeafDistribution", "CanopyLeafPercentage", 12.0f, iniPath), 0.0f, 100.0f);
    fGroundLeafPct = ClampFloat(ReadIniFloatLeaf("LeafDistribution", "GroundLeafPercentage", 88.0f, iniPath), 0.0f, 100.0f);

    ReadWeightSection("DebrisWeights_Desert", sWeightsDesert, iniPath);
    ReadWeightSection("DebrisWeights_ForestRural", sWeightsForest, iniPath);
    ReadWeightSection("DebrisWeights_CityNatural", sWeightsCityNatural, iniPath);
    ReadWeightSection("DebrisWeights_CityUrban", sWeightsCityUrban, iniPath);

    int maxPeds = GetPrivateProfileIntA("Physics", "MaxTriggerPeds", 5, iniPath);
    nMaxTriggerPeds = (maxPeds < 0) ? 0 : ((maxPeds > MAX_TRIGGER_PEDS_CAP) ? MAX_TRIGGER_PEDS_CAP : maxPeds);

    int maxVehs = GetPrivateProfileIntA("Physics", "MaxTriggerVehicles", 4, iniPath);
    nMaxTriggerVehicles = (maxVehs < 0) ? 0 : ((maxVehs > MAX_TRIGGER_VEHS_CAP) ? MAX_TRIGGER_VEHS_CAP : maxVehs);

    fForce = ReadIniFloatLeaf("Physics", "Force", 75.0f, iniPath);
    fGravity = ReadIniFloatLeaf("Physics", "Gravity", 10.0f, iniPath);
    fUpForce = ReadIniFloatLeaf("Physics", "UpForce", 1.65f, iniPath);
    fTriggerChance = ClampFloat(ReadIniFloatLeaf("Physics", "TriggerChance", 0.60f, iniPath), 0.0f, 1.0f);
    fFriction = ReadIniFloatLeaf("Physics", "Friction", 0.50f, iniPath);
    fRadius = ReadIniFloatLeaf("Physics", "Radius", 3.0f, iniPath);
    fVehicleLiftChance = ClampFloat(ReadIniFloatLeaf("Physics", "VehicleLiftChance", 0.70f, iniPath), 0.0f, 1.0f);
    fVehicleForce = ReadIniFloatLeaf("Physics", "VehicleForce", 95.0f, iniPath);
    fVehicleRadius = ReadIniFloatLeaf("Physics", "VehicleRadius", 5.5f, iniPath);
}

// Stage 4: Extruded edge padding helper to prevent atlas bleeding under linear filtering
static inline void SetAtlasPixel(unsigned int* pixels, int stride, int cellX, int cellY, int px, int py, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    int gx = cellX * 64 + px;
    int gy = cellY * 64 + py;
    if (gx >= 0 && gx < 256 && gy >= 0 && gy < 256) {
        pixels[gy * stride + gx] = (static_cast<unsigned int>(a) << 24) |
            (static_cast<unsigned int>(r) << 16) |
            (static_cast<unsigned int>(g) << 8) |
            static_cast<unsigned int>(b);
    }
}

void CLeafSystem::CreateProceduralAtlasTexture() {
    pProcRaster = RwRasterCreate(256, 256, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
    if (!pProcRaster) return;

    RwUInt8* locked = RwRasterLock(pProcRaster, 0, rwRASTERLOCKWRITE);
    if (!locked) return;

    int stride = RwRasterGetStride(pProcRaster) / 4;
    unsigned int* pixels = reinterpret_cast<unsigned int*>(locked);
    std::memset(pixels, 0, 256 * stride * sizeof(unsigned int));

    for (int slot = 0; slot < 16; ++slot) {
        int cx = slot % 4;
        int cy = slot / 4;

        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                // Inner 62x62 active region with 1px extruded border safety
                float u = (x - 31.5f) / 30.5f;
                float v = (y - 31.5f) / 30.5f;
                float dist = std::sqrt(u * u + v * v);

                if (slot <= 2) {
                    float wave = std::sin(v * 3.14159f) * 0.12f;
                    float widthLimit = (1.0f - v * v) * 0.58f;
                    float du = std::fabs(u - wave);
                    if (std::fabs(v) < 0.88f && du < widthLimit) {
                        float vein = (du < 0.04f || std::fabs(std::sin((v + du * 1.4f) * 14.0f)) > 0.92f) ? 0.78f : 1.0f;
                        float grad = 0.85f + 0.15f * (1.0f - du / (widthLimit + 0.01f));
                        unsigned char r = 110, g = 140, b = 55;
                        if (slot == 1) { r = 155; g = 105; b = 50; }
                        else if (slot == 2) { r = 185; g = 145; b = 45; }
                        SetAtlasPixel(pixels, stride, cx, cy, x, y,
                            static_cast<unsigned char>(r * vein * grad),
                            static_cast<unsigned char>(g * vein * grad),
                            static_cast<unsigned char>(b * vein * grad), 245);
                    }
                }
                else if (slot == 3) {
                    float crinkle = std::sin(u * 6.0f) * 0.04f;
                    if (std::fabs(u) < 0.76f + crinkle && std::fabs(v) < 0.62f - crinkle) {
                        bool isHeadline = (v > -0.48f && v < -0.32f && std::fabs(u) < 0.62f);
                        bool isTextLine = ((y % 4 == 0) && std::fabs(u) < 0.65f && std::fabs(u) > 0.06f);
                        unsigned char shade = isHeadline ? 85 : (isTextLine ? 130 : 215);
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, shade, shade - 4, shade - 12, 242);
                    }
                }
                else if (slot == 4) {
                    float bagShape = std::fabs(u) + std::sin(v * 4.0f) * 0.08f;
                    bool handleCut = (v < -0.45f && std::fabs(u) < 0.28f);
                    if (!handleCut && bagShape < 0.68f && std::fabs(v) < 0.82f) {
                        float fold = 0.85f + 0.15f * std::sin((u + v) * 18.0f);
                        unsigned char alpha = static_cast<unsigned char>(135 * fold);
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, 225, 238, 250, alpha);
                    }
                }
                else if (slot == 5) {
                    if (std::fabs(u) < 0.34f && std::fabs(v) < 0.58f) {
                        float cylShade = 0.65f + 0.35f * std::cos(u * 4.2f);
                        bool isLabel = std::fabs(v) < 0.38f;
                        unsigned char r = isLabel ? 195 : 185;
                        unsigned char g = isLabel ? 45 : 190;
                        unsigned char b = isLabel ? 45 : 198;
                        SetAtlasPixel(pixels, stride, cx, cy, x, y,
                            static_cast<unsigned char>(r * cylShade),
                            static_cast<unsigned char>(g * cylShade),
                            static_cast<unsigned char>(b * cylShade), 255);
                    }
                }
                else if (slot == 6) {
                    float poly = std::fabs(u * 0.9f + v * 0.3f) + std::fabs(v * 0.85f - u * 0.25f);
                    if (poly < 0.62f) {
                        float facet = (u + v > 0.0f) ? 1.0f : 0.72f;
                        SetAtlasPixel(pixels, stride, cx, cy, x, y,
                            static_cast<unsigned char>(158 * facet),
                            static_cast<unsigned char>(98 * facet),
                            static_cast<unsigned char>(82 * facet), 255);
                    }
                }
                else if (slot == 7) {
                    if (std::fabs(u) < 0.14f && std::fabs(v) < 0.68f) {
                        bool isFilter = (v > 0.22f);
                        bool isAsh = (v < -0.52f);
                        unsigned char r = isAsh ? 60 : (isFilter ? 210 : 230);
                        unsigned char g = isAsh ? 55 : (isFilter ? 145 : 225);
                        unsigned char b = isAsh ? 55 : (isFilter ? 75 : 215);
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, r, g, b, 255);
                    }
                }
                else if (slot == 8) {
                    if (u > -0.45f && v > -0.45f && (u + v * 1.3f) < 0.35f) {
                        float glint = (std::fabs(u - v) < 0.12f) ? 255.0f : 195.0f;
                        SetAtlasPixel(pixels, stride, cx, cy, x, y,
                            static_cast<unsigned char>(glint * 0.85f),
                            static_cast<unsigned char>(glint * 0.96f),
                            static_cast<unsigned char>(glint), 165);
                    }
                }
                else if (slot == 9) {
                    float curve = u - std::sin(v * 2.2f) * 0.14f;
                    float w = (1.0f - v * v) * 0.32f;
                    if (std::fabs(v) < 0.85f && std::fabs(curve) < w) {
                        float alpha = (1.0f - std::fabs(curve) / (w + 0.01f)) * 220.0f;
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, 242, 242, 238, static_cast<unsigned char>(alpha));
                    }
                }
                else if (slot == 10) {
                    if (dist < 0.88f) {
                        float web1 = std::sin((u * 14.0f) + std::cos(v * 11.0f) * 2.5f);
                        float web2 = std::cos((v * 15.0f) - std::sin(u * 12.0f) * 2.5f);
                        if (std::fabs(web1) > 0.72f || std::fabs(web2) > 0.75f) {
                            float shade = 0.75f + 0.25f * (1.0f - dist);
                            SetAtlasPixel(pixels, stride, cx, cy, x, y,
                                static_cast<unsigned char>(168 * shade),
                                static_cast<unsigned char>(132 * shade),
                                static_cast<unsigned char>(82 * shade), 240);
                        }
                    }
                }
                else if (slot == 11) {
                    bool thorax = (u * u * 11.0f + v * v * 2.4f) < 0.30f;
                    bool foreWing = (v < 0.08f && v > -0.52f && std::fabs(u) > 0.10f && std::fabs(u) < 0.84f && (std::fabs(u) + std::fabs(v + 0.18f) * 0.85f) < 0.96f);
                    bool hindWing = (v >= 0.05f && v < 0.56f && std::fabs(u) > 0.08f && std::fabs(u) < 0.64f && (std::fabs(u) + (v - 0.12f) * 0.9f) < 0.72f);
                    bool antenna = (v < -0.45f && v > -0.78f && std::fabs(std::fabs(u) - (-v - 0.42f) * 0.55f) < 0.045f);

                    if (thorax || antenna) {
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, 42, 34, 28, 255);
                    }
                    else if (foreWing || hindWing) {
                        float pattern = 0.82f + 0.18f * std::cos(u * 12.0f + v * 9.0f);
                        SetAtlasPixel(pixels, stride, cx, cy, x, y,
                            static_cast<unsigned char>(218 * pattern),
                            static_cast<unsigned char>(182 * pattern),
                            static_cast<unsigned char>(118 * pattern), 220);
                    }
                }
                else if (slot == 12) {
                    if (dist < 1.0f) {
                        float falloff = (1.0f - dist) * (1.0f - dist);
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, 235, 228, 210, static_cast<unsigned char>(falloff * 210.0f));
                    }
                }
                else if (slot == 13) {
                    bool body = (u * u * 14.0f + v * v * 2.2f) < 0.34f;
                    bool wing = (v > -0.25f && v < 0.35f && std::fabs(u) > 0.12f && std::fabs(u) < 0.52f && (std::fabs(u) + std::fabs(v) * 0.6f) < 0.65f);
                    float glowDist = std::sqrt(u * u * 3.0f + (v - 0.32f) * (v - 0.32f) * 3.0f);

                    if (glowDist < 0.55f) {
                        float gFade = 1.0f - (glowDist / 0.55f);
                        SetAtlasPixel(pixels, stride, cx, cy, x, y,
                            static_cast<unsigned char>(210 + 45 * gFade),
                            static_cast<unsigned char>(240 + 15 * gFade),
                            static_cast<unsigned char>(95 * gFade),
                            static_cast<unsigned char>(160 + 95 * gFade));
                    }
                    else if (body) {
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, 48, 42, 34, 255);
                    }
                    else if (wing) {
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, 165, 175, 155, 145);
                    }
                }
                else if (slot == 14) {
                    bool abdomen = (std::fabs(u) < 0.065f && v > -0.65f && v < 0.78f);
                    bool head = (u * u + (v + 0.66f) * (v + 0.66f)) < 0.018f;
                    bool upperWing = (std::fabs(v + 0.24f) < 0.085f && std::fabs(u) > 0.06f && std::fabs(u) < 0.92f);
                    bool lowerWing = (std::fabs(v + 0.04f) < 0.075f && std::fabs(u) > 0.06f && std::fabs(u) < 0.78f);

                    if (abdomen || head) {
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, 48, 128, 145, 255);
                    }
                    else if (upperWing || lowerWing) {
                        float shimmer = 0.85f + 0.15f * std::sin(u * 20.0f);
                        SetAtlasPixel(pixels, stride, cx, cy, x, y,
                            static_cast<unsigned char>(210 * shimmer),
                            static_cast<unsigned char>(235 * shimmer),
                            static_cast<unsigned char>(245 * shimmer), 155);
                    }
                }
                else if (slot == 15) {
                    bool shell = (u * u * 5.2f + v * v * 3.4f) < 0.36f;
                    bool wing = (v > -0.15f && v < 0.38f && std::fabs(u) > 0.15f && std::fabs(u) < 0.62f && (u * u * 2.2f + v * v * 2.2f) < 0.55f);
                    if (shell) {
                        float sheen = (u < 0.0f && v < 0.0f) ? 1.35f : 0.95f;
                        SetAtlasPixel(pixels, stride, cx, cy, x, y,
                            static_cast<unsigned char>(ClampFloat(52.0f * sheen, 0.0f, 255.0f)),
                            static_cast<unsigned char>(ClampFloat(44.0f * sheen, 0.0f, 255.0f)),
                            static_cast<unsigned char>(ClampFloat(38.0f * sheen, 0.0f, 255.0f)), 255);
                    }
                    else if (wing) {
                        SetAtlasPixel(pixels, stride, cx, cy, x, y, 195, 195, 190, 130);
                    }
                }
            }
        }

        // Extrude 1-pixel edge border for each 64x64 tile to eliminate linear-filter atlas bleeding
        for (int i = 1; i < 63; ++i) {
            pixels[(cy * 64 + 0) * stride + (cx * 64 + i)] = pixels[(cy * 64 + 1) * stride + (cx * 64 + i)];
            pixels[(cy * 64 + 63) * stride + (cx * 64 + i)] = pixels[(cy * 64 + 62) * stride + (cx * 64 + i)];
            pixels[(cy * 64 + i) * stride + (cx * 64 + 0)] = pixels[(cy * 64 + i) * stride + (cx * 64 + 1)];
            pixels[(cy * 64 + i) * stride + (cx * 64 + 63)] = pixels[(cy * 64 + i) * stride + (cx * 64 + 62)];
        }
    }

    RwRasterUnlock(pProcRaster);

    pLeafTexture = RwTextureCreate(pProcRaster);
    if (pLeafTexture) {
        RwTextureSetFilterMode(pLeafTexture, rwFILTERLINEAR);
        RwTextureSetAddressing(pLeafTexture, rwTEXTUREADDRESSCLAMP);
    }
}

// =========================================================================
// Stage 2: Segregated Active Lists & Uniform Spatial Grid Management
// =========================================================================
void CLeafSystem::SetLeafState(int leafIdx, eLeafState newState) {
    if (leafIdx < 0 || leafIdx >= MAX_LEAVES_LIMIT) return;

    CLeaf& leaf = aLeaves[leafIdx];
    eLeafState oldState = leaf.m_nState;
    if (oldState == newState && leaf.m_nStateListPos >= 0) return;

    // Swap-remove from old active list with strict bounds check
    if (leaf.m_nStateListPos >= 0) {
        int pos = leaf.m_nStateListPos;
        if (oldState == eLeafState::AIRBORNE && nAirborneCount > 0 && pos < nAirborneCount) {
            int lastIdx = aAirborneList[--nAirborneCount];
            aAirborneList[pos] = lastIdx;
            aLeaves[lastIdx].m_nStateListPos = pos;
        }
        else if (oldState == eLeafState::WATER_DRIFT && nWaterCount > 0 && pos < nWaterCount) {
            int lastIdx = aWaterList[--nWaterCount];
            aWaterList[pos] = lastIdx;
            aLeaves[lastIdx].m_nStateListPos = pos;
        }
        else if (oldState == eLeafState::DORMANT_GROUND && nDormantCount > 0 && pos < nDormantCount) {
            int lastIdx = aDormantList[--nDormantCount];
            aDormantList[pos] = lastIdx;
            aLeaves[lastIdx].m_nStateListPos = pos;
        }
        leaf.m_nStateListPos = -1;
    }

    leaf.m_nState = newState;
    leaf.m_fSleepTimer = 0.0f;
    leaf.m_fSimAccumDt = 0.0f;

    // Append to new active list (prevent duplicates & overflow)
    if (newState == eLeafState::AIRBORNE && nAirborneCount < MAX_LEAVES_LIMIT) {
        leaf.m_nStateListPos = nAirborneCount;
        aAirborneList[nAirborneCount++] = leafIdx;
    }
    else if (newState == eLeafState::WATER_DRIFT && nWaterCount < MAX_LEAVES_LIMIT) {
        leaf.m_nStateListPos = nWaterCount;
        aWaterList[nWaterCount++] = leafIdx;
    }
    else if (newState == eLeafState::DORMANT_GROUND && nDormantCount < MAX_LEAVES_LIMIT) {
        leaf.m_nStateListPos = nDormantCount;
        aDormantList[nDormantCount++] = leafIdx;
    }
    else {
        leaf.m_nStateListPos = -1;
    }
}

void CLeafSystem::RemoveLeafFromSpatialCell(int leafIdx) {
    CLeaf& leaf = aLeaves[leafIdx];
    int cell = leaf.m_nSpatialCell;
    if (cell < 0 || cell >= SPATIAL_GRID_CELLS) return;

    if (leaf.m_nPrevInCell >= 0) {
        aLeaves[leaf.m_nPrevInCell].m_nNextInCell = leaf.m_nNextInCell;
    }
    else {
        aSpatialCellHeads[cell] = leaf.m_nNextInCell;
    }

    if (leaf.m_nNextInCell >= 0) {
        aLeaves[leaf.m_nNextInCell].m_nPrevInCell = leaf.m_nPrevInCell;
    }

    leaf.m_nSpatialCell = -1;
    leaf.m_nNextInCell = -1;
    leaf.m_nPrevInCell = -1;
}

void CLeafSystem::UpdateLeafSpatialCell(int leafIdx) {
    if (leafIdx < 0 || leafIdx >= MAX_LEAVES_LIMIT) return;

    CLeaf& leaf = aLeaves[leafIdx];
    if (leaf.m_nState == eLeafState::INACTIVE_RESPAWN) {
        RemoveLeafFromSpatialCell(leafIdx);
        return;
    }

    int wx = WorldToCellCoord(leaf.m_vecPos.x);
    int wy = WorldToCellCoord(leaf.m_vecPos.y);
    int newCell = GetSpatialBucketIndex(wx, wy);
    if (newCell < 0 || newCell >= SPATIAL_GRID_CELLS) return;

    if (newCell == leaf.m_nSpatialCell) return;

    RemoveLeafFromSpatialCell(leafIdx);

    int headIdx = aSpatialCellHeads[newCell];
    if (headIdx == leafIdx) return; // Prevent self-loop

    leaf.m_nSpatialCell = newCell;
    leaf.m_nPrevInCell = -1;
    leaf.m_nNextInCell = headIdx;
    if (headIdx >= 0 && headIdx < MAX_LEAVES_LIMIT) {
        aLeaves[headIdx].m_nPrevInCell = leafIdx;
    }
    aSpatialCellHeads[newCell] = leafIdx;
}

void CLeafSystem::EnqueueRespawn(int leafIdx) {
    if (aInRespawnQueue[leafIdx]) return;
    aInRespawnQueue[leafIdx] = true;
    SetLeafState(leafIdx, eLeafState::INACTIVE_RESPAWN);
    RemoveLeafFromSpatialCell(leafIdx);

    aRespawnQueue[nRespawnTail] = leafIdx;
    nRespawnTail = (nRespawnTail + 1) % MAX_LEAVES_LIMIT;
    ++nRespawnCount;
}

void CLeafSystem::ProcessRespawnQueue(const CVector& playerPos, const CVector& windVec, float currentTime, int maxPerFrame) {
    int processed = 0;
    while (nRespawnCount > 0 && processed < maxPerFrame) {
        int leafIdx = aRespawnQueue[nRespawnHead];
        nRespawnHead = (nRespawnHead + 1) % MAX_LEAVES_LIMIT;
        --nRespawnCount;
        aInRespawnQueue[leafIdx] = false;

        if (leafIdx < nEffectiveQuota) {
            aLeaves[leafIdx].ExecuteRespawn(playerPos, leafIdx, windVec, currentTime);
            UpdateLeafSpatialCell(leafIdx);
        }
        ++processed;
    }
}

// =========================================================================
// Stage 3: Spatial Ground Sample Cache
// =========================================================================
bool CLeafSystem::QueryGroundCached(float x, float y, float refZ, float currentTime, bool allowRaycast, GroundSampleResult& outGround, bool& outOverWater) {
    int wx = WorldToCellCoord(x);
    int wy = WorldToCellCoord(y);
    int bucket = GetSpatialBucketIndex(wx, wy);
    GroundSpatialCacheEntry& entry = aGroundCache[bucket];

    bool cacheValid = (entry.worldCellX == wx && entry.worldCellY == wy && (currentTime - entry.timestamp) < 2.5f);
    if (cacheValid && !allowRaycast) {
        outGround.bValid = !entry.overWater;
        outGround.fGroundZ = entry.groundZ;
        outGround.vecNormal = entry.normal;
        outGround.nSurfaceType = entry.surfaceType;
        outGround.fShade01 = static_cast<float>(entry.shade) * (1.0f / 255.0f);
        outOverWater = entry.overWater;
        return outGround.bValid;
    }

    if (allowRaycast && nGroundRaycastBudget > 0) {
        --nGroundRaycastBudget;
        if (CSurfaceScanner::SampleGround(x, y, refZ, 15.0f, 50.0f, outGround, true, false)) {
            outOverWater = (outGround.fGroundZ <= 0.05f);
            entry.worldCellX = wx;
            entry.worldCellY = wy;
            entry.timestamp = currentTime;
            entry.groundZ = outGround.fGroundZ;
            entry.normal = outGround.vecNormal;
            entry.surfaceType = outGround.nSurfaceType;
            entry.shade = static_cast<unsigned char>(outGround.fShade01 * 255.0f);
            entry.overWater = outOverWater;
            return true;
        }
        else {
            outGround.bValid = false;
            outGround.fGroundZ = 0.002f;
            outGround.vecNormal = CVector(0.0f, 0.0f, 1.0f);
            outGround.nSurfaceType = 0;
            outGround.fShade01 = 0.85f;
            outOverWater = true;

            entry.worldCellX = wx;
            entry.worldCellY = wy;
            entry.timestamp = currentTime;
            entry.groundZ = 0.002f;
            entry.normal = CVector(0.0f, 0.0f, 1.0f);
            entry.surfaceType = 0;
            entry.shade = 216;
            entry.overWater = true;
            return false;
        }
    }

    if (cacheValid) {
        outGround.bValid = !entry.overWater;
        outGround.fGroundZ = entry.groundZ;
        outGround.vecNormal = entry.normal;
        outGround.nSurfaceType = entry.surfaceType;
        outGround.fShade01 = static_cast<float>(entry.shade) * (1.0f / 255.0f);
        outOverWater = entry.overWater;
        return outGround.bValid;
    }

    return false;
}

// =========================================================================
// Stage 1 & 4: Dynamic Lighting with Spatial Headlight Filtering & LOD
// =========================================================================
static inline RwRGBA ComputeLeafDynamicColorLOD(
    const CLeaf& leaf,
    eRenderLOD lod,
    const CVector& ambientBase,
    const CVector& sunDir,
    const CVector& sunColor
) {
    float shadeNorm = static_cast<float>(leaf.m_nShade) * (1.0f / 255.0f);

    // Stage 4: Far particles skip normal NdotL & dynamic point/headlight evaluation
    if (lod == eRenderLOD::FAR_BILLBOARD) {
        float r = (ambientBase.x + sunColor.x * 0.45f) * shadeNorm;
        float g = (ambientBase.y + sunColor.y * 0.45f) * shadeNorm;
        float b = (ambientBase.z + sunColor.z * 0.45f) * shadeNorm;

        if (leaf.m_eType == eDebrisType::INSECT && s_IsNightOrDusk && leaf.m_nAtlasSlot == 13) {
            r += 0.55f;
            g += 0.70f;
            b += 0.20f;
        }

        RwRGBA outColor;
        outColor.red = static_cast<RwUInt8>(ClampFloat(r * 255.0f, 18.0f, 255.0f));
        outColor.green = static_cast<RwUInt8>(ClampFloat(g * 255.0f, 18.0f, 255.0f));
        outColor.blue = static_cast<RwUInt8>(ClampFloat(b * 255.0f, 18.0f, 255.0f));
        outColor.alpha = leaf.m_nAlpha;
        return outColor;
    }

    float ndotl = leaf.m_vecUp.x * sunDir.x + leaf.m_vecUp.y * sunDir.y + leaf.m_vecUp.z * sunDir.z;
    if (ndotl < 0.0f) ndotl = -ndotl * 0.35f;

    float r = (ambientBase.x + sunColor.x * ndotl) * shadeNorm;
    float g = (ambientBase.y + sunColor.y * ndotl) * shadeNorm;
    float b = (ambientBase.z + sunColor.z * ndotl) * shadeNorm;

    if (leaf.m_eType == eDebrisType::INSECT && s_IsNightOrDusk) {
        if (leaf.m_nAtlasSlot == 13) {
            r += 0.62f;
            g += 0.78f;
            b += 0.22f;
        }
        else {
            r += 0.28f;
            g += 0.30f;
            b += 0.14f;
        }
    }

    // Only NEAR_FULL evaluates vehicle headlights & player proximity light
    if (lod == eRenderLOD::NEAR_FULL && s_IsNightOrDusk) {
        int leafCellX = WorldToCellCoord(leaf.m_vecPos.x);
        int leafCellY = WorldToCellCoord(leaf.m_vecPos.y);

        for (int i = 0; i < s_HeadlightCount; ++i) {
            const CachedHeadlight& hl = s_Headlights[i];
            // Stage 3 & 4: Spatial cell rejection before vector math (20m cone = ~3.5 cells)
            if (std::abs(leafCellX - hl.cellX) > 4 || std::abs(leafCellY - hl.cellY) > 4) continue;

            float dx = leaf.m_vecPos.x - hl.pos.x;
            float dy = leaf.m_vecPos.y - hl.pos.y;
            float dz = leaf.m_vecPos.z - hl.pos.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq > 400.0f || distSq < 0.1f) continue;

            float invDist = FastInvSqrt(distSq);
            float dot = (dx * hl.fwd.x + dy * hl.fwd.y + dz * hl.fwd.z) * invDist;

            if (dot > 0.72f) {
                float coneAtten = (dot - 0.72f) * 3.5714f;
                float dist = distSq * invDist;
                float distAtten = 1.0f - (dist * 0.05f);
                float lightBoost = coneAtten * distAtten * 0.85f;

                r += 1.0f * lightBoost;
                g += 0.95f * lightBoost;
                b += 0.80f * lightBoost;
            }
        }

        float dxP = leaf.m_vecPos.x - s_PlayerPosCache.x;
        float dyP = leaf.m_vecPos.y - s_PlayerPosCache.y;
        float distPlayerSq = dxP * dxP + dyP * dyP;
        if (distPlayerSq < 144.0f && distPlayerSq > 0.001f) {
            float distP = distPlayerSq * FastInvSqrt(distPlayerSq);
            float atten = (1.0f - distP * 0.0833f) * 0.18f;
            r += 0.95f * atten;
            g += 0.80f * atten;
            b += 0.55f * atten;
        }
    }

    RwRGBA outColor;
    outColor.red = static_cast<RwUInt8>(ClampFloat(r * 255.0f, 18.0f, 255.0f));
    outColor.green = static_cast<RwUInt8>(ClampFloat(g * 255.0f, 18.0f, 255.0f));
    outColor.blue = static_cast<RwUInt8>(ClampFloat(b * 255.0f, 18.0f, 255.0f));
    outColor.alpha = leaf.m_nAlpha;
    return outColor;
}

// =========================================================================
// Stage 1: Precompute Particle Physics & Rendering Traits
// =========================================================================
void CLeaf::RebuildPhysicsProps() {
    int typeIdx = static_cast<int>(m_eType);
    m_Phys.mass = kMassTable[typeIdx];
    m_Phys.invMass = kInvMassTable[typeIdx];
    m_Phys.gravityFallMult = (m_bIsTreeLeaf ? 0.35f : 0.55f) * m_Phys.invMass;
    m_Phys.gravityRiseMult = m_bIsTreeLeaf ? 0.50f : 1.00f;
    m_Phys.dragMult = 2.8f * m_Phys.invMass;
    m_Phys.windLiftMult = m_Phys.invMass;

    m_Phys.freqMult = 1.0f;
    m_Phys.ampMult = 1.0f;
    m_Phys.flapSpeed = 38.0f;
    m_Phys.baseHoverZ = 0.75f;

    if (m_eType == eDebrisType::INSECT) {
        if (m_nAtlasSlot == 13) { // Firefly
            m_Phys.freqMult = 0.55f;
            m_Phys.ampMult = 0.55f;
            m_Phys.flapSpeed = 24.0f;
            m_Phys.baseHoverZ = 0.95f;
        }
        else if (m_nAtlasSlot == 14) { // Dragonfly
            m_Phys.freqMult = 1.45f;
            m_Phys.ampMult = 1.40f;
            m_Phys.flapSpeed = 52.0f;
            m_Phys.baseHoverZ = 0.85f;
        }
        else if (m_nAtlasSlot == 15) { // Beetle/Gnat
            m_Phys.freqMult = 1.85f;
            m_Phys.ampMult = 0.75f;
            m_Phys.flapSpeed = 60.0f;
            m_Phys.baseHoverZ = 0.50f;
        }
    }

    switch (m_eType) {
    case eDebrisType::LEAF:
    case eDebrisType::FEATHER:
    case eDebrisType::NEWSPAPER:
    case eDebrisType::PLASTIC_BAG:
    case eDebrisType::TUMBLEWEED:
        m_Phys.canFloatOnWater = true;
        break;
    default:
        m_Phys.canFloatOnWater = false;
        break;
    }

    // Stage 4: Alpha cutout vs translucent & billboard specialization flags
    switch (m_eType) {
    case eDebrisType::LEAF:
    case eDebrisType::NEWSPAPER:
    case eDebrisType::CAN_CAP:
    case eDebrisType::GRAVEL_BRICK:
    case eDebrisType::CIGARETTE_NAIL:
    case eDebrisType::TUMBLEWEED:
        m_Phys.isCutoutAlpha = true;
        break;
    default:
        m_Phys.isCutoutAlpha = false;
        break;
    }

    m_Phys.useBillboard = (m_eType == eDebrisType::POLLEN_DUST);
}

static eDebrisType PickWeightedDebrisType(const DebrisSpawnWeights& w, float roll01) {
    float totalWeight = 0.0f;
    for (int i = 0; i < 11; ++i) {
        totalWeight += w.weights[i];
    }
    if (totalWeight <= 0.0001f) {
        return eDebrisType::LEAF;
    }

    float target = roll01 * totalWeight;
    float accum = 0.0f;
    for (int i = 0; i < 11; ++i) {
        accum += w.weights[i];
        if (target <= accum && w.weights[i] > 0.0f) {
            return static_cast<eDebrisType>(i);
        }
    }
    return eDebrisType::LEAF;
}

void CLeaf::AssignTypeForLocation(eBiomeZone biome, unsigned char surfaceType, bool isTreeLeaf) {
    float roll = HashToFloat01(m_nRngState);
    float baseScale = CLeafSystem::fScaleMin + HashToFloat01(m_nRngState) * (CLeafSystem::fScaleMax - CLeafSystem::fScaleMin);
    bool isNaturalSurf = CSurfaceScanner::IsNaturalGroundSurface(surfaceType);

    m_nAlpha = 250;

    if (isTreeLeaf) {
        if (biome == eBiomeZone::DESERT) {
            float leafW = CLeafSystem::sWeightsDesert.weights[static_cast<int>(eDebrisType::LEAF)];
            float dustW = CLeafSystem::sWeightsDesert.weights[static_cast<int>(eDebrisType::POLLEN_DUST)];
            float sum = leafW + dustW;
            if (sum > 0.001f) {
                m_eType = (roll < (leafW / sum)) ? eDebrisType::LEAF : eDebrisType::POLLEN_DUST;
            }
            else {
                m_eType = eDebrisType::LEAF;
            }
        }
        else {
            m_eType = eDebrisType::LEAF;
        }
    }
    else {
        const DebrisSpawnWeights* activeWeights = &CLeafSystem::sWeightsCityUrban;
        if (biome == eBiomeZone::DESERT) {
            activeWeights = &CLeafSystem::sWeightsDesert;
        }
        else if (biome == eBiomeZone::FOREST_RURAL) {
            activeWeights = &CLeafSystem::sWeightsForest;
        }
        else if (isNaturalSurf) {
            activeWeights = &CLeafSystem::sWeightsCityNatural;
        }
        m_eType = PickWeightedDebrisType(*activeWeights, roll);
    }

    switch (m_eType) {
    case eDebrisType::LEAF:           m_nAtlasSlot = static_cast<unsigned char>(m_nRngState % 3u); SetScale(baseScale); break;
    case eDebrisType::NEWSPAPER:      m_nAtlasSlot = 3; SetScale(baseScale * 1.75f); break;
    case eDebrisType::PLASTIC_BAG:    m_nAtlasSlot = 4; SetScale(baseScale * 1.65f); m_nAlpha = 180; break;
    case eDebrisType::CAN_CAP:        m_nAtlasSlot = 5; SetScale(baseScale * 0.80f); break;
    case eDebrisType::GRAVEL_BRICK:   m_nAtlasSlot = 6; SetScale(baseScale * 0.55f); break;
    case eDebrisType::CIGARETTE_NAIL: m_nAtlasSlot = 7; SetScale(baseScale * 0.50f); break;
    case eDebrisType::GLASS_SHARD:    m_nAtlasSlot = 8; SetScale(baseScale * 0.55f); m_nAlpha = 185; break;
    case eDebrisType::FEATHER:        m_nAtlasSlot = 9; SetScale(baseScale * 0.85f); break;
    case eDebrisType::TUMBLEWEED:     m_nAtlasSlot = 10; SetScale(baseScale * 2.55f); break;
    case eDebrisType::INSECT: {
        uint32_t variant = m_nRngState & 3u;
        if (variant == 0) {
            m_nAtlasSlot = 11;
            SetScale(baseScale * 0.72f);
            m_nAlpha = 240;
        }
        else if (variant == 1) {
            m_nAtlasSlot = 13;
            SetScale(baseScale * 0.52f);
            m_nAlpha = 250;
        }
        else if (variant == 2) {
            m_nAtlasSlot = 14;
            SetScale(baseScale * 0.82f);
            m_nAlpha = 235;
        }
        else {
            m_nAtlasSlot = 15;
            SetScale(baseScale * 0.44f);
            m_nAlpha = 245;
        }
        break;
    }
    case eDebrisType::POLLEN_DUST:
        m_nAtlasSlot = 12;
        SetScale((biome == eBiomeZone::DESERT) ? (baseScale * 0.72f) : (baseScale * 0.24f));
        m_nAlpha = (biome == eBiomeZone::DESERT) ? 62 : 140;
        break;
    }

    RebuildPhysicsProps();
}

static void GenerateBalancedSpawnXY(const CVector& centerPos, int index, uint32_t& rngState, float& outX, float& outY, unsigned char& outClusterId) {
    float range = CLeafSystem::fPositionRange;
    if (HashToFloat01(rngState) < CLeafSystem::fClumpRatio) {
        int clusterIdx = index % CLUSTER_COUNT;
        outClusterId = static_cast<unsigned char>(clusterIdx);
        float angle = HashToFloat01(rngState) * 6.283185f;
        float r = std::sqrt(HashToFloat01(rngState)) * CLeafSystem::fClumpRadius;
        outX = ClampFloat(centerPos.x + CLeafSystem::aClusterOffsets[clusterIdx].x + FastCos(angle) * r, centerPos.x - range, centerPos.x + range);
        outY = ClampFloat(centerPos.y + CLeafSystem::aClusterOffsets[clusterIdx].y + FastSin(angle) * r, centerPos.y - range, centerPos.y + range);
    }
    else {
        outClusterId = CLUSTER_COUNT; // Unclustered / scattered
        outX = centerPos.x + (HashToFloat01(rngState) * 2.0f - 1.0f) * range;
        outY = centerPos.y + (HashToFloat01(rngState) * 2.0f - 1.0f) * range;
    }
}

static void GenerateDonutOuterSpawnXY(const CVector& centerPos, uint32_t& rngState, float& outX, float& outY) {
    float baseRange = CLeafSystem::fPositionRange;
    float angle = HashToFloat01(rngState) * 6.283185f;
    float dist = baseRange * (1.35f + HashToFloat01(rngState) * 0.65f);
    outX = centerPos.x + FastCos(angle) * dist;
    outY = centerPos.y + FastSin(angle) * dist;
}

void CLeaf::Init(const CVector& centerPos, int index, bool isTreeLeaf, bool snapToGround) {
    m_nRngState = 0x9E3779B9u ^ (static_cast<uint32_t>(index + 1) * 2654435761u);
    HashToFloat01(m_nRngState);

    m_nStateListPos = -1;
    m_nSpatialCell = -1;
    m_nNextInCell = -1;
    m_nPrevInCell = -1;
    m_nLastVisFrame = 0xFFFFFFFFu;
    m_bCachedVisible = false;
    m_nLastColorFrame = 0xFFFFFFFFu;
    m_CachedDynColor = { 200, 200, 200, 250 };
    m_fSleepTimer = 0.0f;
    m_fSimAccumDt = 0.0f;

    m_bIsTreeLeaf = isTreeLeaf;
    m_bCanopyDrift = false;
    m_bOverWater = false;
    m_nShade = 220;
    m_vecVel = CVector(0.0f, 0.0f, 0.0f);
    SetGroundNormal(CVector(0.0f, 0.0f, 1.0f));
    m_fLastTriggerTime = -10.0f;
    m_fLastSampleTime = -10.0f;

    float rx = 0.0f, ry = 0.0f;
    GenerateBalancedSpawnXY(centerPos, index, m_nRngState, rx, ry, m_nClusterId);
    float rz = centerPos.z + CLeafSystem::fSpawnHeight;

    float yaw = HashToFloat01(m_nRngState) * 6.283185f;
    m_vecForward = CVector(FastCos(yaw), FastSin(yaw), 0.0f);
    m_vecUp = CVector(0.0f, 0.0f, 1.0f);
    SetAngVel(CVector(
        (HashToFloat01(m_nRngState) * 10.0f) - 5.0f,
        (HashToFloat01(m_nRngState) * 10.0f) - 5.0f,
        (HashToFloat01(m_nRngState) * 8.0f) - 4.0f
    ));

    eBiomeZone biome = CSurfaceScanner::DetectBiome(CVector(rx, ry, centerPos.z));
    AssignTypeForLocation(biome, 0, isTreeLeaf);

    if (m_eType == eDebrisType::TUMBLEWEED) {
        GenerateDonutOuterSpawnXY(centerPos, m_nRngState, rx, ry);
    }

    GroundSampleResult ground;
    if (CSurfaceScanner::SampleGround(rx, ry, centerPos.z, 15.0f, 50.0f, ground, true, false)) {
        m_fGroundZ = ground.fGroundZ + 0.038f;
        m_nShade = static_cast<unsigned char>(ground.fShade01 * 255.0f);
        SetGroundNormal(ground.vecNormal);
        m_bOverWater = (ground.fGroundZ <= 0.05f);

        if (m_eType != eDebrisType::TUMBLEWEED) {
            AssignTypeForLocation(biome, ground.nSurfaceType, isTreeLeaf);
            if (m_eType == eDebrisType::TUMBLEWEED) {
                GenerateDonutOuterSpawnXY(centerPos, m_nRngState, rx, ry);
                if (CSurfaceScanner::SampleGround(rx, ry, centerPos.z, 15.0f, 50.0f, ground, true, false)) {
                    m_fGroundZ = ground.fGroundZ + 0.038f;
                    m_nShade = static_cast<unsigned char>(ground.fShade01 * 255.0f);
                    SetGroundNormal(ground.vecNormal);
                    m_bOverWater = (ground.fGroundZ <= 0.05f);
                }
            }
        }

        if (m_eType == eDebrisType::INSECT && m_bOverWater) {
            m_eType = eDebrisType::LEAF;
            m_nAtlasSlot = static_cast<unsigned char>(m_nRngState % 3u);
            SetScale(CLeafSystem::fScaleMin + HashToFloat01(m_nRngState) * (CLeafSystem::fScaleMax - CLeafSystem::fScaleMin));
            RebuildPhysicsProps();
        }

        bool allowHighSpawn = (biome != eBiomeZone::DESERT) && m_bIsTreeLeaf && (m_eType == eDebrisType::LEAF);
        float scale = GetScale();
        eLeafState initialState = eLeafState::AIRBORNE;

        if (m_eType == eDebrisType::TUMBLEWEED) {
            rz = m_fGroundZ + scale * 0.7f;
            m_vecVel = CVector(CWindSystem::vecActiveWindForce.x * 0.14f, CWindSystem::vecActiveWindForce.y * 0.14f, 0.0f);
            initialState = eLeafState::AIRBORNE;
        }
        else if (m_eType == eDebrisType::POLLEN_DUST) {
            rz = m_fGroundZ + 0.06f + HashToFloat01(m_nRngState) * 0.39f;
            m_vecVel = CVector(CWindSystem::vecActiveWindForce.x * 0.04f, CWindSystem::vecActiveWindForce.y * 0.04f, 0.0f);
            initialState = eLeafState::AIRBORNE;
        }
        else if (m_eType == eDebrisType::INSECT) {
            rz = m_fGroundZ + 0.45f + HashToFloat01(m_nRngState) * 1.10f;
            float ang = HashToFloat01(m_nRngState) * 6.283185f;
            m_vecVel = CVector(FastCos(ang) * 0.8f, FastSin(ang) * 0.8f, 0.0f);
            initialState = eLeafState::AIRBORNE;
        }
        else if (allowHighSpawn) {
            rz = m_fGroundZ + 4.5f + HashToFloat01(m_nRngState) * (CLeafSystem::fSpawnHeight - 4.5f);
            m_vecVel = CVector(
                CWindSystem::vecActiveWindForce.x * (0.02f + HashToFloat01(m_nRngState) * 0.03f),
                CWindSystem::vecActiveWindForce.y * (0.02f + HashToFloat01(m_nRngState) * 0.03f),
                -0.2f + HashToFloat01(m_nRngState) * 0.3f
            );
            m_bCanopyDrift = true;
            initialState = eLeafState::AIRBORNE;
        }
        else if (snapToGround) {
            rz = m_fGroundZ;
            m_vecUp = ground.vecNormal;
            CVector right = VecCross(m_vecForward, m_vecUp);
            FastVecNormalize(right);
            m_vecForward = VecCross(m_vecUp, right);
            initialState = eLeafState::DORMANT_GROUND;
        }
        else {
            rz = m_fGroundZ + 0.1f;
            initialState = eLeafState::AIRBORNE;
        }

        m_vecPos = CVector(rx, ry, rz);
        m_fLastSampleX = rx;
        m_fLastSampleY = ry;
        CLeafSystem::SetLeafState(index, initialState);
    }
    else {
        m_fGroundZ = 0.04f;
        m_bOverWater = true;

        if (m_eType == eDebrisType::INSECT) {
            m_eType = eDebrisType::LEAF;
            m_nAtlasSlot = static_cast<unsigned char>(m_nRngState % 3u);
            SetScale(CLeafSystem::fScaleMin + HashToFloat01(m_nRngState) * (CLeafSystem::fScaleMax - CLeafSystem::fScaleMin));
            RebuildPhysicsProps();
        }

        eLeafState initialState = eLeafState::AIRBORNE;
        if (m_Phys.canFloatOnWater) {
            rz = 0.04f;
            initialState = eLeafState::WATER_DRIFT;
        }
        else {
            rz = centerPos.z - 2.0f;
            initialState = eLeafState::AIRBORNE;
        }

        m_vecPos = CVector(rx, ry, rz);
        m_fLastSampleX = rx;
        m_fLastSampleY = ry;
        CLeafSystem::SetLeafState(index, initialState);
    }
}

void CLeaf::AlignToNormal(const CVector& targetNormal, float lerpSpeed) {
    m_vecUp = LerpVector(m_vecUp, targetNormal, ClampFloat(lerpSpeed, 0.0f, 1.0f));
    FastVecNormalize(m_vecUp);

    CVector right = VecCross(m_vecForward, m_vecUp);
    if (right.MagnitudeSqr() < 0.0001f) right = CVector(1.0f, 0.0f, 0.0f);
    else FastVecNormalize(right);

    m_vecForward = VecCross(m_vecUp, right);
    FastVecNormalize(m_vecForward);
}

bool CLeaf::CheckOutOfBounds(const CVector& playerPos) const {
    float range = (m_eType == eDebrisType::TUMBLEWEED) ? (CLeafSystem::fPositionRange * 2.0f) : CLeafSystem::fPositionRange;
    float dx = m_vecPos.x - playerPos.x;
    float dy = m_vecPos.y - playerPos.y;

    return (m_eType == eDebrisType::TUMBLEWEED)
        ? ((dx * dx + dy * dy) > (range * range))
        : (std::fabs(dx) > range || std::fabs(dy) > range);
}

void CLeaf::ExecuteRespawn(const CVector& playerPos, int index, const CVector& activeWindVec, float currentTime) {
    float range = (m_eType == eDebrisType::TUMBLEWEED) ? (CLeafSystem::fPositionRange * 2.0f) : CLeafSystem::fPositionRange;
    float dx = m_vecPos.x - playerPos.x;
    float dy = m_vecPos.y - playerPos.y;

    float newX = 0.0f, newY = 0.0f;
    GenerateBalancedSpawnXY(playerPos, index, m_nRngState, newX, newY, m_nClusterId);

    if (dx > range && newX > playerPos.x + range * 0.5f) newX -= range;
    else if (dx < -range && newX < playerPos.x - range * 0.5f) newX += range;

    if (dy > range && newY > playerPos.y + range * 0.5f) newY -= range;
    else if (dy < -range && newY < playerPos.y - range * 0.5f) newY += range;

    eBiomeZone biome = CSurfaceScanner::DetectBiome(CVector(newX, newY, playerPos.z));
    AssignTypeForLocation(biome, 0, m_bIsTreeLeaf);

    if (m_eType == eDebrisType::TUMBLEWEED) {
        GenerateDonutOuterSpawnXY(playerPos, m_nRngState, newX, newY);
    }

    m_vecPos.x = newX;
    m_vecPos.y = newY;
    m_fLastSampleX = newX;
    m_fLastSampleY = newY;
    m_fLastSampleTime = currentTime;
    m_fLastTriggerTime = currentTime - 0.5f;
    m_bCanopyDrift = false;
    m_bOverWater = false;
    m_nLastColorFrame = 0xFFFFFFFFu;

    GroundSampleResult ground;
    if (CSurfaceScanner::SampleGround(newX, newY, playerPos.z, 15.0f, 50.0f, ground, true, false)) {
        m_fGroundZ = ground.fGroundZ + 0.038f;
        m_nShade = static_cast<unsigned char>(ground.fShade01 * 255.0f);
        SetGroundNormal(ground.vecNormal);
        m_bOverWater = (ground.fGroundZ <= 0.05f);

        if (m_eType != eDebrisType::TUMBLEWEED) {
            AssignTypeForLocation(biome, ground.nSurfaceType, m_bIsTreeLeaf);
            if (m_eType == eDebrisType::TUMBLEWEED) {
                GenerateDonutOuterSpawnXY(playerPos, m_nRngState, newX, newY);
                m_vecPos.x = newX;
                m_vecPos.y = newY;
                m_fLastSampleX = newX;
                m_fLastSampleY = newY;
                if (CSurfaceScanner::SampleGround(newX, newY, playerPos.z, 15.0f, 50.0f, ground, true, false)) {
                    m_fGroundZ = ground.fGroundZ + 0.038f;
                    m_nShade = static_cast<unsigned char>(ground.fShade01 * 255.0f);
                    SetGroundNormal(ground.vecNormal);
                    m_bOverWater = (ground.fGroundZ <= 0.05f);
                }
            }
        }

        if (m_eType == eDebrisType::INSECT && m_bOverWater) {
            m_eType = eDebrisType::LEAF;
            m_nAtlasSlot = static_cast<unsigned char>(m_nRngState % 3u);
            SetScale(CLeafSystem::fScaleMin + HashToFloat01(m_nRngState) * (CLeafSystem::fScaleMax - CLeafSystem::fScaleMin));
            RebuildPhysicsProps();
        }

        bool allowHighSpawn = (biome != eBiomeZone::DESERT) && m_bIsTreeLeaf && (m_eType == eDebrisType::LEAF);
        float scale = GetScale();

        if (m_eType == eDebrisType::TUMBLEWEED) {
            m_vecPos.z = m_fGroundZ + scale * 0.7f;
            m_vecVel = CVector(activeWindVec.x * 0.14f, activeWindVec.y * 0.14f, 0.0f);
            CLeafSystem::SetLeafState(index, eLeafState::AIRBORNE);
        }
        else if (m_eType == eDebrisType::POLLEN_DUST) {
            m_vecPos.z = m_fGroundZ + 0.06f + HashToFloat01(m_nRngState) * 0.39f;
            m_vecVel = CVector(activeWindVec.x * 0.04f, activeWindVec.y * 0.04f, 0.0f);
            CLeafSystem::SetLeafState(index, eLeafState::AIRBORNE);
        }
        else if (m_eType == eDebrisType::INSECT) {
            m_vecPos.z = m_fGroundZ + 0.45f + HashToFloat01(m_nRngState) * 1.10f;
            float ang = HashToFloat01(m_nRngState) * 6.283185f;
            m_vecVel = CVector(FastCos(ang) * 0.8f, FastSin(ang) * 0.8f, 0.0f);
            CLeafSystem::SetLeafState(index, eLeafState::AIRBORNE);
        }
        else if (allowHighSpawn) {
            m_vecPos.z = m_fGroundZ + 5.0f + HashToFloat01(m_nRngState) * (CLeafSystem::fSpawnHeight - 5.0f);
            m_vecVel = CVector(
                activeWindVec.x * (0.02f + HashToFloat01(m_nRngState) * 0.04f),
                activeWindVec.y * (0.02f + HashToFloat01(m_nRngState) * 0.04f),
                -0.2f + HashToFloat01(m_nRngState) * 0.35f
            );
            m_bCanopyDrift = true;
            CLeafSystem::SetLeafState(index, eLeafState::AIRBORNE);
        }
        else {
            m_vecPos.z = m_fGroundZ;
            m_vecVel = CVector(0.0f, 0.0f, 0.0f);
            m_vecUp = ground.vecNormal;
            AlignToNormal(ground.vecNormal, 1.0f);
            CLeafSystem::SetLeafState(index, eLeafState::DORMANT_GROUND);
        }
    }
    else {
        m_fGroundZ = 0.04f;
        m_bOverWater = true;

        if (m_eType == eDebrisType::INSECT) {
            m_eType = eDebrisType::LEAF;
            m_nAtlasSlot = static_cast<unsigned char>(m_nRngState % 3u);
            SetScale(CLeafSystem::fScaleMin + HashToFloat01(m_nRngState) * (CLeafSystem::fScaleMax - CLeafSystem::fScaleMin));
            RebuildPhysicsProps();
        }

        if (m_Phys.canFloatOnWater) {
            m_vecPos.z = 0.04f;
            m_vecVel = CVector(activeWindVec.x * 0.035f, activeWindVec.y * 0.035f, 0.0f);
            m_vecUp = CVector(0.0f, 0.0f, 1.0f);
            CLeafSystem::SetLeafState(index, eLeafState::WATER_DRIFT);
        }
        else {
            m_vecPos.z = playerPos.z - 1.5f;
            CLeafSystem::SetLeafState(index, eLeafState::AIRBORNE);
        }
    }
}

// =========================================================================
// Stage 2 & 3: Event-Driven Spatial Activation for Dormant Particles
// =========================================================================
void CLeafSystem::DispatchEventDrivenActivations(float currentTime, const CVector& activeWindVec) {
    // 1. Explosion events -> query only spatial cells overlapped by explosion radius
    for (int e = 0; e < s_ExplosionCount; ++e) {
        const ActiveExplosionWave& exp = s_Explosions[e];
        int minCx = WorldToCellCoord(exp.pos.x - exp.radius);
        int maxCx = WorldToCellCoord(exp.pos.x + exp.radius);
        int minCy = WorldToCellCoord(exp.pos.y - exp.radius);
        int maxCy = WorldToCellCoord(exp.pos.y + exp.radius);

        for (int cy = minCy; cy <= maxCy; ++cy) {
            for (int cx = minCx; cx <= maxCx; ++cx) {
                int bucket = GetSpatialBucketIndex(cx, cy);
                if (bucket < 0 || bucket >= SPATIAL_GRID_CELLS) continue;
                int leafIdx = aSpatialCellHeads[bucket];
                int safetySteps = 0;
                while (leafIdx >= 0 && leafIdx < MAX_LEAVES_LIMIT && safetySteps++ < MAX_LEAVES_LIMIT) {
                    CLeaf& leaf = aLeaves[leafIdx];
                    int nextIdx = (leaf.m_nNextInCell != leafIdx) ? leaf.m_nNextInCell : -1;
                    if (leafIdx >= nEffectiveQuota) {
                        leafIdx = nextIdx;
                        continue;
                    }
                    if (leaf.m_nState == eLeafState::DORMANT_GROUND) {
                        float dx = leaf.m_vecPos.x - exp.pos.x;
                        float dy = leaf.m_vecPos.y - exp.pos.y;
                        float dz = leaf.m_vecPos.z - exp.pos.z;
                        float distSq = dx * dx + dy * dy + dz * dz;
                        if (distSq < exp.radSq && distSq > 0.001f) {
                            float invDist = FastInvSqrt(distSq);
                            float dist = distSq * invDist;
                            float falloff = 1.0f - (dist / exp.radius);
                            float invMass = leaf.m_Phys.invMass;
                            float radialImpulse = exp.power * falloff * invMass;

                            leaf.m_vecVel.x = dx * invDist * radialImpulse;
                            leaf.m_vecVel.y = dy * invDist * radialImpulse;
                            leaf.m_vecVel.z = exp.upLift * (0.45f + 0.75f * falloff) * invMass * (0.85f + HashToFloat01(leaf.m_nRngState) * 0.40f);
                            leaf.m_vecPos.z = leaf.m_fGroundZ + 0.12f;
                            leaf.m_bCanopyDrift = false;
                            leaf.m_fLastTriggerTime = currentTime;
                            SetLeafState(leafIdx, eLeafState::AIRBORNE);
                        }
                    }
                    leafIdx = nextIdx;
                }
            }
        }
    }

    // 2. Moving Ped events -> query only cells within ped trigger radius
    for (int p = 0; p < nActivePedCount; ++p) {
        const LeafTriggerSource& pedSrc = aActivePeds[p];
        float pRadSq = pedSrc.radius * pedSrc.radius;
        int minCx = WorldToCellCoord(pedSrc.pos.x - pedSrc.radius);
        int maxCx = WorldToCellCoord(pedSrc.pos.x + pedSrc.radius);
        int minCy = WorldToCellCoord(pedSrc.pos.y - pedSrc.radius);
        int maxCy = WorldToCellCoord(pedSrc.pos.y + pedSrc.radius);

        for (int cy = minCy; cy <= maxCy; ++cy) {
            for (int cx = minCx; cx <= maxCx; ++cx) {
                int bucket = GetSpatialBucketIndex(cx, cy);
                if (bucket < 0 || bucket >= SPATIAL_GRID_CELLS) continue;
                int leafIdx = aSpatialCellHeads[bucket];
                int safetySteps = 0;
                while (leafIdx >= 0 && leafIdx < MAX_LEAVES_LIMIT && safetySteps++ < MAX_LEAVES_LIMIT) {
                    CLeaf& leaf = aLeaves[leafIdx];
                    int nextIdx = (leaf.m_nNextInCell != leafIdx) ? leaf.m_nNextInCell : -1;
                    if (leafIdx >= nEffectiveQuota) {
                        leafIdx = nextIdx;
                        continue;
                    }
                    if (leaf.m_nState == eLeafState::DORMANT_GROUND && (currentTime - leaf.m_fLastTriggerTime) >= 0.65f) {
                        float dx = leaf.m_vecPos.x - pedSrc.pos.x;
                        float dy = leaf.m_vecPos.y - pedSrc.pos.y;
                        float dz = leaf.m_vecPos.z - pedSrc.pos.z;
                        float distPedSq = dx * dx + dy * dy + dz * dz;
                        if (distPedSq < pRadSq) {
                            leaf.m_fLastTriggerTime = currentTime;
                            if (HashToFloat01(leaf.m_nRngState) <= pedSrc.chance) {
                                float invDist = FastInvSqrt(distPedSq);
                                float dist = distPedSq * invDist;
                                float t = 1.0f - (dist / pedSrc.radius);
                                float invMass = leaf.m_Phys.invMass;

                                CVector diffPed(dx, dy, dz * 0.25f + fUpForce * 0.45f);
                                FastVecNormalize(diffPed);

                                float speedFactor = ClampFloat(pedSrc.speed * 0.35f, 0.40f, 1.15f);
                                float launchPower = pedSrc.force * (0.35f + 0.65f * t) * speedFactor * 0.095f * invMass;
                                leaf.m_vecVel = diffPed * launchPower;
                                leaf.m_vecVel.z = fUpForce * (1.35f + HashToFloat01(leaf.m_nRngState) * 1.00f) * speedFactor * invMass;
                                leaf.m_vecPos.z = leaf.m_fGroundZ + 0.04f;
                                leaf.m_bCanopyDrift = false;
                                SetLeafState(leafIdx, eLeafState::AIRBORNE);
                            }
                        }
                    }
                    leafIdx = nextIdx;
                }
            }
        }
    }

    // 3. Moving Vehicle / Rotor events -> query only cells within vehicle radius
    for (int v = 0; v < nActiveVehCount; ++v) {
        const LeafTriggerSource& vehSrc = aActiveVehs[v];
        float vRadSq = vehSrc.radius * vehSrc.radius;
        int minCx = WorldToCellCoord(vehSrc.pos.x - vehSrc.radius);
        int maxCx = WorldToCellCoord(vehSrc.pos.x + vehSrc.radius);
        int minCy = WorldToCellCoord(vehSrc.pos.y - vehSrc.radius);
        int maxCy = WorldToCellCoord(vehSrc.pos.y + vehSrc.radius);

        for (int cy = minCy; cy <= maxCy; ++cy) {
            for (int cx = minCx; cx <= maxCx; ++cx) {
                int bucket = GetSpatialBucketIndex(cx, cy);
                if (bucket < 0 || bucket >= SPATIAL_GRID_CELLS) continue;
                int leafIdx = aSpatialCellHeads[bucket];
                int safetySteps = 0;
                while (leafIdx >= 0 && leafIdx < MAX_LEAVES_LIMIT && safetySteps++ < MAX_LEAVES_LIMIT) {
                    CLeaf& leaf = aLeaves[leafIdx];
                    int nextIdx = (leaf.m_nNextInCell != leafIdx) ? leaf.m_nNextInCell : -1;
                    if (leafIdx >= nEffectiveQuota) {
                        leafIdx = nextIdx;
                        continue;
                    }
                    if (leaf.m_nState == eLeafState::DORMANT_GROUND && (currentTime - leaf.m_fLastTriggerTime) >= 0.65f) {
                        float dx = leaf.m_vecPos.x - vehSrc.pos.x;
                        float dy = leaf.m_vecPos.y - vehSrc.pos.y;
                        float dz = leaf.m_vecPos.z - vehSrc.pos.z;
                        float invMass = leaf.m_Phys.invMass;

                        if (vehSrc.isAircraftRotor) {
                            if (dz <= 2.0f && dz >= -26.0f) {
                                float horizSq = dx * dx + dy * dy;
                                if (horizSq < vRadSq && horizSq > 0.01f) {
                                    leaf.m_fLastTriggerTime = currentTime;
                                    float invDist = FastInvSqrt(horizSq);
                                    float dist = horizSq * invDist;
                                    float t = 1.0f - (dist / vehSrc.radius);
                                    float heightAtten = 1.0f - (std::fabs(dz) / 26.0f);

                                    float rx = dx * invDist;
                                    float ry = dy * invDist;
                                    float tx = -ry;
                                    float ty = rx;

                                    float blast = vehSrc.force * (0.35f + 0.65f * t) * heightAtten * 0.14f * invMass;
                                    leaf.m_vecVel.x = (rx * 0.78f + tx * 0.45f) * blast;
                                    leaf.m_vecVel.y = (ry * 0.78f + ty * 0.45f) * blast;
                                    leaf.m_vecVel.z = fUpForce * (1.8f + HashToFloat01(leaf.m_nRngState) * 1.6f) * (0.4f + 0.6f * t) * heightAtten * invMass;
                                    leaf.m_vecPos.z = leaf.m_fGroundZ + 0.05f;
                                    leaf.m_bCanopyDrift = false;
                                    SetLeafState(leafIdx, eLeafState::AIRBORNE);
                                }
                            }
                        }
                        else {
                            float distVehSq = dx * dx + dy * dy + dz * dz;
                            if (distVehSq < vRadSq) {
                                leaf.m_fLastTriggerTime = currentTime;
                                if (HashToFloat01(leaf.m_nRngState) <= vehSrc.chance) {
                                    float invDist = FastInvSqrt(distVehSq);
                                    float dist = distVehSq * invDist;
                                    float t = 1.0f - (dist / vehSrc.radius);

                                    CVector diffVeh(dx, dy, dz * 0.25f + fUpForce * (0.45f + HashToFloat01(leaf.m_nRngState) * 0.30f));
                                    FastVecNormalize(diffVeh);

                                    CVector pushDir = diffVeh + (vehSrc.moveDir * 5.0f);
                                    FastVecNormalize(pushDir);

                                    float speedScale = ClampFloat(vehSrc.speed * 0.06f, 0.50f, 1.50f);
                                    float launchPower = vehSrc.force * (0.4f + 0.6f * t) * speedScale * 0.12f * invMass;
                                    leaf.m_vecVel = pushDir * launchPower;
                                    leaf.m_vecVel.z = fUpForce * (2.10f + HashToFloat01(leaf.m_nRngState) * 1.50f) * speedScale * invMass;
                                    leaf.m_vecPos.z = leaf.m_fGroundZ + 0.04f;
                                    leaf.m_bCanopyDrift = false;
                                    SetLeafState(leafIdx, eLeafState::AIRBORNE);
                                }
                            }
                        }
                    }
                    leafIdx = nextIdx;
                }
            }
        }
    }

    // 4. Staggered ambient wind & insect takeoff over dormant list (1/4th slice per frame)
    if (nDormantCount > 0) {
        int slice = static_cast<int>(nFrameCounter & 3u);
        for (int i = nDormantCount - 1 - slice; i >= 0; i -= 4) {
            int leafIdx = aDormantList[i];
            CLeaf& leaf = aLeaves[leafIdx];
            if ((currentTime - leaf.m_fLastTriggerTime) < 0.65f) continue;

            if (leaf.m_eType == eDebrisType::INSECT && !leaf.m_bOverWater && HashToFloat01(leaf.m_nRngState) < 0.28f) {
                float ang = HashToFloat01(leaf.m_nRngState) * 6.283185f;
                leaf.m_vecVel = CVector(FastCos(ang) * 1.65f, FastSin(ang) * 1.65f, 0.45f + HashToFloat01(leaf.m_nRngState) * 0.60f);
                leaf.m_vecPos.z = leaf.m_fGroundZ + 0.08f;
                leaf.m_fLastTriggerTime = currentTime;
                SetLeafState(leafIdx, eLeafState::AIRBORNE);
            }
            else if (CWindSystem::fWindThreshold < 0.98f && leaf.m_Phys.mass <= 1.35f) {
                float windSample = CWindSystem::SampleSpatialGust(leaf.m_vecPos.x, leaf.m_vecPos.y);
                if (windSample > CWindSystem::fWindThreshold) {
                    float intensity = (windSample - CWindSystem::fWindThreshold) / (1.0f - CWindSystem::fWindThreshold);
                    float liftFactor = (0.18f + intensity * 0.20f) * leaf.m_Phys.invMass;
                    leaf.m_vecVel.x = activeWindVec.x * liftFactor * (0.75f + HashToFloat01(leaf.m_nRngState) * 0.50f);
                    leaf.m_vecVel.y = activeWindVec.y * liftFactor * (0.75f + HashToFloat01(leaf.m_nRngState) * 0.50f);
                    leaf.m_vecVel.z = activeWindVec.z * (0.06f + intensity * 0.08f) * (0.75f + HashToFloat01(leaf.m_nRngState) * 0.40f) * leaf.m_Phys.invMass;
                    leaf.m_vecPos.z = leaf.m_fGroundZ + 0.04f;
                    leaf.m_bCanopyDrift = false;
                    leaf.m_fLastTriggerTime = currentTime;
                    SetLeafState(leafIdx, eLeafState::AIRBORNE);
                }
            }
        }
    }
}

void CLeaf::UpdateAirborne(float dt, float currentTime, int index, unsigned int frameCount, const CVector& activeWindVec, float canopyDriftScale, float distToPlayerSq, bool allowRaycast) {
    float scale = GetScale();
    float massFactor = m_Phys.mass;
    float invMassFactor = m_Phys.invMass;

    // Explosion shockwave & strong vertical lift for airborne debris
    for (int e = 0; e < s_ExplosionCount; ++e) {
        const ActiveExplosionWave& exp = s_Explosions[e];
        float dx = m_vecPos.x - exp.pos.x;
        float dy = m_vecPos.y - exp.pos.y;
        float dz = m_vecPos.z - exp.pos.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq < exp.radSq && distSq > 0.001f) {
            float invDist = FastInvSqrt(distSq);
            float dist = distSq * invDist;
            float falloff = 1.0f - (dist / exp.radius);
            float dtBoost = ClampFloat(dt * 24.0f, 0.25f, 1.25f);
            float radialImpulse = exp.power * falloff * invMassFactor * dtBoost;

            m_vecVel.x += dx * invDist * radialImpulse;
            m_vecVel.y += dy * invDist * radialImpulse;
            m_vecVel.z += exp.upLift * (0.5f + 0.7f * falloff) * invMassFactor * dtBoost;
            m_fSleepTimer = 0.0f;
        }
    }

    for (int v = 0; v < CLeafSystem::nActiveVehCount; ++v) {
        const LeafTriggerSource& vehSrc = CLeafSystem::aActiveVehs[v];
        if (!vehSrc.isAircraftRotor) continue;

        float dx = m_vecPos.x - vehSrc.pos.x;
        float dy = m_vecPos.y - vehSrc.pos.y;
        float dz = m_vecPos.z - vehSrc.pos.z;
        if (dz > 2.5f || dz < -26.0f) continue;

        float horizSq = dx * dx + dy * dy;
        float rSq = vehSrc.radius * vehSrc.radius;
        if (horizSq < rSq && horizSq > 0.01f) {
            float invDist = FastInvSqrt(horizSq);
            float dist = horizSq * invDist;
            float t = 1.0f - (dist / vehSrc.radius);
            float heightAtten = 1.0f - (std::fabs(dz) / 26.0f);
            float rx = dx * invDist;
            float ry = dy * invDist;
            float blast = vehSrc.force * t * heightAtten * invMassFactor * dt * 0.45f;

            m_vecVel.x += (rx - ry * 0.55f) * blast;
            m_vecVel.y += (ry + rx * 0.55f) * blast;
            m_vecVel.z += (t - 0.35f) * blast * 0.4f;
            m_fSleepTimer = 0.0f;
        }
    }

    if (m_eType == eDebrisType::TUMBLEWEED) {
        bool impacted = false;
        for (int v = 0; v < CLeafSystem::nActiveVehCount; ++v) {
            const LeafTriggerSource& vehSrc = CLeafSystem::aActiveVehs[v];
            if (vehSrc.isAircraftRotor) continue;
            CVector diffV = m_vecPos - vehSrc.pos;
            float rad = vehSrc.radius * 0.75f;
            if (diffV.MagnitudeSqr() < rad * rad) {
                FastVecNormalize(diffV);
                float hitSpeed = ClampFloat(vehSrc.speed * 0.28f, 3.5f, 16.0f);
                m_vecVel = (diffV + vehSrc.moveDir * 3.0f) * hitSpeed;
                m_vecVel.z = 2.5f + HashToFloat01(m_nRngState) * 3.0f;
                impacted = true;
                break;
            }
        }

        if (!impacted) {
            for (int p = 0; p < CLeafSystem::nActivePedCount; ++p) {
                const LeafTriggerSource& pedSrc = CLeafSystem::aActivePeds[p];
                CVector diffP = m_vecPos - pedSrc.pos;
                float rad = pedSrc.radius * 0.65f;
                if (diffP.MagnitudeSqr() < rad * rad) {
                    FastVecNormalize(diffP);
                    m_vecVel = diffP * ClampFloat(pedSrc.speed * 1.4f, 2.2f, 7.5f);
                    m_vecVel.z = 1.4f + HashToFloat01(m_nRngState) * 1.8f;
                    break;
                }
            }
        }

        float targetVx = activeWindVec.x * 0.16f;
        float targetVy = activeWindVec.y * 0.16f;
        float blendT = ClampFloat(dt * 1.35f, 0.0f, 1.0f);
        m_vecVel.x += (targetVx - m_vecVel.x) * blendT;
        m_vecVel.y += (targetVy - m_vecVel.y) * blendT;
        m_vecVel.z -= CLeafSystem::fGravity * dt;
        m_vecPos += m_vecVel * dt;

        float radiusOffset = scale * 0.7f;
        if (m_vecPos.z <= m_fGroundZ + radiusOffset) {
            m_vecPos.z = m_fGroundZ + radiusOffset;
            if (m_vecVel.z < -2.0f) m_vecVel.z = -m_vecVel.z * 0.42f;
            else m_vecVel.z = 0.45f + HashToFloat01(m_nRngState) * 0.90f;
        }

        // Only compute full 3D rolling basis when within near/mid LOD range
        if (distToPlayerSq < 484.0f) {
            CVector rollAxis = VecCross(CVector(0.0f, 0.0f, 1.0f), m_vecVel);
            float rollSq = rollAxis.MagnitudeSqr();
            if (rollSq > 0.001f) {
                float invRoll = FastInvSqrt(rollSq);
                rollAxis.x *= invRoll;
                rollAxis.y *= invRoll;
                rollAxis.z *= invRoll;
                float speedMag = m_vecVel.Magnitude();
                float rollAngle = (speedMag / (radiusOffset + 0.05f)) * dt;
                m_vecForward += VecCross(rollAxis, m_vecForward) * rollAngle;
                m_vecUp += VecCross(rollAxis, m_vecUp) * rollAngle;
                FastVecNormalize(m_vecForward);
                FastVecNormalize(m_vecUp);
            }
        }
    }
    else if (m_eType == eDebrisType::POLLEN_DUST) {
        float targetVx = activeWindVec.x * 0.065f + FastSin(currentTime * 1.9f + index) * 0.85f;
        float targetVy = activeWindVec.y * 0.065f + FastSin(currentTime * 1.6f + index * 1.3f) * 0.85f;
        float blendT = ClampFloat(dt * 2.2f, 0.0f, 1.0f);
        m_vecVel.x += (targetVx - m_vecVel.x) * blendT;
        m_vecVel.y += (targetVy - m_vecVel.y) * blendT;

        float desiredHeight = m_fGroundZ + 0.22f + 0.16f * FastSin(currentTime * 1.4f + index * 0.7f);
        float heightError = desiredHeight - m_vecPos.z;
        m_vecVel.z = ClampFloat(heightError * 1.8f, -0.45f, 0.45f);
        m_vecPos += m_vecVel * dt;

        if (m_vecPos.z < m_fGroundZ + 0.05f) m_vecPos.z = m_fGroundZ + 0.05f;
        else if (m_vecPos.z > m_fGroundZ + 0.95f) m_vecPos.z = m_fGroundZ + 0.95f;
    }
    else if (m_eType == eDebrisType::INSECT) {
        // Stage 1: Use precomputed m_Phys parameters
        float freqMult = m_Phys.freqMult;
        float ampMult = m_Phys.ampMult;
        float flapSpeed = m_Phys.flapSpeed;
        float baseHoverZ = m_Phys.baseHoverZ;

        // Stage 1: Adaptive simulation LOD for distant insects
        if (distToPlayerSq > 400.0f) {
            float phase = currentTime * 2.5f * freqMult + static_cast<float>(index);
            m_vecVel.x = FastSin(phase) * 1.4f * ampMult + activeWindVec.x * 0.02f;
            m_vecVel.y = FastCos(phase) * 1.4f * ampMult + activeWindVec.y * 0.02f;
            m_vecVel.z = 0.0f;
            m_vecPos.x += m_vecVel.x * dt;
            m_vecPos.y += m_vecVel.y * dt;
            float safeFloorZ = (m_bOverWater || m_fGroundZ <= 0.08f) ? 0.55f : m_fGroundZ;
            m_vecPos.z = safeFloorZ + baseHoverZ;
            return;
        }

        float phase = currentTime * 3.8f * freqMult + static_cast<float>(index) * 1.7f;
        float dartX = (FastSin(phase) * 2.4f + FastCos(phase * 2.3f) * 1.1f) * ampMult + activeWindVec.x * 0.025f;
        float dartY = (FastCos(phase * 0.9f) * 2.4f + FastSin(phase * 2.1f) * 1.1f) * ampMult + activeWindVec.y * 0.025f;

        if (m_bOverWater || m_fGroundZ <= 0.08f) {
            dartX = -m_vecVel.x * 1.4f + FastSin(phase) * 1.5f;
            dartY = -m_vecVel.y * 1.4f + FastCos(phase) * 1.5f;
        }

        float blendT = ClampFloat(dt * 3.5f, 0.0f, 1.0f);
        m_vecVel.x += (dartX - m_vecVel.x) * blendT;
        m_vecVel.y += (dartY - m_vecVel.y) * blendT;

        float safeFloorZ = (m_bOverWater || m_fGroundZ <= 0.08f) ? 0.55f : m_fGroundZ;
        float preferredZ = safeFloorZ + baseHoverZ + 0.50f * FastSin(currentTime * 2.2f * freqMult + index * 0.8f);
        float zErr = preferredZ - m_vecPos.z;
        m_vecVel.z = ClampFloat(zErr * 2.8f, -1.2f, 1.4f);

        m_vecPos += m_vecVel * dt;

        if (m_vecPos.z < 0.45f) {
            m_vecPos.z = 0.45f;
            if (m_vecVel.z < 0.0f) m_vecVel.z = 0.5f;
        }
        if (m_vecPos.z < m_fGroundZ + 0.12f) {
            m_vecPos.z = m_fGroundZ + 0.12f;
        }

        float horizSpdSq = m_vecVel.x * m_vecVel.x + m_vecVel.y * m_vecVel.y;
        if (horizSpdSq > 0.02f) {
            float invH = FastInvSqrt(horizSpdSq);
            m_vecForward = CVector(m_vecVel.x * invH, m_vecVel.y * invH, 0.0f);
            float wingFlutter = FastSin(currentTime * flapSpeed + index) * 0.25f;
            m_vecUp = CVector(-m_vecForward.y * wingFlutter, m_vecForward.x * wingFlutter, 1.0f);
            FastVecNormalize(m_vecUp);
        }

        // Stage 1: Frame-rate independent landing probability (lambda = 0.24/sec ~ 0.004 at 60Hz)
        if (!m_bOverWater && m_fGroundZ > 0.15f && m_vecPos.z <= m_fGroundZ + 0.45f && RandomEventRate(0.24f, dt, m_nRngState)) {
            m_vecPos.z = m_fGroundZ;
            m_vecVel = CVector(0.0f, 0.0f, 0.0f);
            CLeafSystem::SetLeafState(index, eLeafState::DORMANT_GROUND);
            return;
        }
    }
    else {
        if (m_bCanopyDrift && m_vecPos.z > m_fGroundZ + 0.15f) {
            m_vecVel.x += activeWindVec.x * canopyDriftScale * dt;
            m_vecVel.y += activeWindVec.y * canopyDriftScale * dt;
        }

        float effectiveGravity = CLeafSystem::fGravity * massFactor;
        if (m_vecVel.z < 0.0f) {
            effectiveGravity *= m_Phys.gravityFallMult;
            m_vecVel.z *= (1.0f - ClampFloat(dt * m_Phys.dragMult, 0.0f, 0.85f));
        }
        else {
            effectiveGravity *= m_Phys.gravityRiseMult;
        }

        float horizDamp = 1.0f - ClampFloat(dt * 1.15f, 0.0f, 0.85f);
        m_vecVel.z -= effectiveGravity * dt;
        m_vecVel.x *= horizDamp;
        m_vecVel.y *= horizDamp;
        m_vecPos += m_vecVel * dt;
    }

    // Stage 3: Ground sampling with distance-based threshold, temporal cache, and spatial cell cache
    float minMoveSq = (distToPlayerSq < 100.0f) ? 0.35f : ((distToPlayerSq < 400.0f) ? 1.00f : 2.50f);
    if (m_eType == eDebrisType::POLLEN_DUST || m_eType == eDebrisType::INSECT) minMoveSq = 2.25f;

    float moveFromSampleSq = (m_vecPos.x - m_fLastSampleX) * (m_vecPos.x - m_fLastSampleX) +
        (m_vecPos.y - m_fLastSampleY) * (m_vecPos.y - m_fLastSampleY);

    if (moveFromSampleSq > minMoveSq && (currentTime - m_fLastSampleTime) > 0.12f) {
        GroundSampleResult ground;
        bool overWater = false;
        if (CLeafSystem::QueryGroundCached(m_vecPos.x, m_vecPos.y, m_vecPos.z + 2.0f, currentTime, allowRaycast, ground, overWater)) {
            m_fGroundZ = ground.fGroundZ + 0.038f;
            m_nShade = static_cast<unsigned char>(ground.fShade01 * 255.0f);
            SetGroundNormal(ground.vecNormal);
            m_bOverWater = overWater;
            m_fLastSampleX = m_vecPos.x;
            m_fLastSampleY = m_vecPos.y;
            m_fLastSampleTime = currentTime;
        }
        else if (overWater) {
            m_fGroundZ = 0.04f;
            m_bOverWater = true;
            m_fLastSampleX = m_vecPos.x;
            m_fLastSampleY = m_vecPos.y;
            m_fLastSampleTime = currentTime;
        }
    }

    if (m_eType == eDebrisType::TUMBLEWEED || m_eType == eDebrisType::POLLEN_DUST || m_eType == eDebrisType::INSECT) return;

    CVector groundNorm = GetGroundNormal();

    if (m_vecPos.z > m_fGroundZ + 0.02f) {
        m_fSleepTimer = 0.0f;
        // Skip angular rotation math for distant airborne particles
        if (distToPlayerSq < 484.0f) {
            CVector angVel = GetAngVel();
            float speedSq = m_vecVel.MagnitudeSqr();
            float speedMag = (speedSq > 0.0001f) ? (speedSq * FastInvSqrt(speedSq)) : 0.0f;
            float speedFactor = ClampFloat(speedMag * 0.45f, 0.5f, 2.8f);
            float rx = angVel.x * speedFactor * dt;
            float ry = angVel.y * speedFactor * dt;
            float rz = angVel.z * speedFactor * dt;

            CVector right = VecCross(m_vecForward, m_vecUp);
            m_vecUp += right * rx + m_vecForward * ry;
            FastVecNormalize(m_vecUp);

            m_vecForward += right * rz;
            right = VecCross(m_vecForward, m_vecUp);
            FastVecNormalize(right);
            m_vecForward = VecCross(m_vecUp, right);
            FastVecNormalize(m_vecForward);
        }

        if (m_vecPos.z <= m_fGroundZ + 0.45f) {
            AlignToNormal(groundNorm, dt * 6.0f);
        }
    }
    else {
        m_vecPos.z = m_fGroundZ;
        m_bCanopyDrift = false;

        if (m_bOverWater && m_Phys.canFloatOnWater) {
            m_vecUp = CVector(0.0f, 0.0f, 1.0f);
            m_vecVel.z = 0.0f;
            CLeafSystem::SetLeafState(index, eLeafState::WATER_DRIFT);
            return;
        }

        if (massFactor > 1.4f && m_vecVel.z < -2.2f) {
            m_vecVel.z = -m_vecVel.z * 0.28f;
            m_vecVel.x *= 0.65f;
            m_vecVel.y *= 0.65f;
            return;
        }

        if (m_vecVel.z < 0.0f) m_vecVel.z = 0.0f;

        // Stage 1 & 2: Squared speed check + Explicit Velocity Sleep System
        float speedSq = m_vecVel.MagnitudeSqr();
        float frictionDrop = CLeafSystem::fFriction * massFactor * dt * 10.0f;

        if (speedSq <= frictionDrop * frictionDrop || speedSq < 0.0025f) {
            m_vecVel = CVector(0.0f, 0.0f, 0.0f);
            m_fSleepTimer += dt;
            AlignToNormal(groundNorm, 1.0f);
            if (m_fSleepTimer >= 0.18f) {
                CLeafSystem::SetLeafState(index, eLeafState::DORMANT_GROUND);
            }
        }
        else {
            m_fSleepTimer = 0.0f;
            float invVelMag = FastInvSqrt(speedSq);
            float scaleDown = 1.0f - (frictionDrop * invVelMag);
            if (scaleDown < 0.0f) scaleDown = 0.0f;
            m_vecVel *= scaleDown;
            AlignToNormal(groundNorm, dt * 12.0f);
        }
    }
}

void CLeaf::UpdateWaterDrift(float dt, float currentTime, int index, const CVector& activeWindVec, bool allowRaycast) {
    float phase = currentTime * 1.8f + static_cast<float>(index) * 0.9f;
    float targetVx = activeWindVec.x * 0.045f + FastSin(phase * 0.6f) * 0.14f;
    float targetVy = activeWindVec.y * 0.045f + FastCos(phase * 0.55f) * 0.14f;

    float blend = ClampFloat(dt * 1.5f, 0.0f, 1.0f);
    m_vecVel.x += (targetVx - m_vecVel.x) * blend;
    m_vecVel.y += (targetVy - m_vecVel.y) * blend;
    m_vecVel.z = 0.0f;

    m_vecPos.x += m_vecVel.x * dt;
    m_vecPos.y += m_vecVel.y * dt;
    m_vecPos.z = 0.04f + FastSin(phase * 1.7f) * 0.012f;

    float yawRate = 0.25f * dt;
    CVector right = VecCross(m_vecForward, m_vecUp);
    m_vecForward += right * yawRate;
    FastVecNormalize(m_vecForward);
    m_vecUp = CVector(0.0f, 0.0f, 1.0f);

    float moveFromSampleSq = (m_vecPos.x - m_fLastSampleX) * (m_vecPos.x - m_fLastSampleX) +
        (m_vecPos.y - m_fLastSampleY) * (m_vecPos.y - m_fLastSampleY);
    if (moveFromSampleSq > 0.65f && (currentTime - m_fLastSampleTime) > 0.20f) {
        GroundSampleResult ground;
        bool overWater = true;
        if (CLeafSystem::QueryGroundCached(m_vecPos.x, m_vecPos.y, 5.0f, currentTime, allowRaycast, ground, overWater)) {
            if (!overWater && ground.fGroundZ > 0.08f) {
                m_fGroundZ = ground.fGroundZ + 0.038f;
                m_bOverWater = false;
                m_vecPos.z = m_fGroundZ;
                AlignToNormal(ground.vecNormal, 1.0f);
                CLeafSystem::SetLeafState(index, eLeafState::DORMANT_GROUND);
                return;
            }
        }
        m_fLastSampleX = m_vecPos.x;
        m_fLastSampleY = m_vecPos.y;
        m_fLastSampleTime = currentTime;
    }

    for (int e = 0; e < s_ExplosionCount; ++e) {
        const ActiveExplosionWave& exp = s_Explosions[e];
        float dx = m_vecPos.x - exp.pos.x;
        float dy = m_vecPos.y - exp.pos.y;
        float distSq = dx * dx + dy * dy;
        if (distSq < exp.radSq && distSq > 0.001f) {
            float invDist = FastInvSqrt(distSq);
            float falloff = 1.0f - (distSq * invDist / exp.radius);
            m_vecVel.x = dx * invDist * exp.power * falloff;
            m_vecVel.y = dy * invDist * exp.power * falloff;
            m_vecVel.z = exp.upLift * (0.5f + 0.65f * falloff);
            m_vecPos.z = 0.15f;
            m_bOverWater = false;
            CLeafSystem::SetLeafState(index, eLeafState::AIRBORNE);
            return;
        }
    }

    for (int v = 0; v < CLeafSystem::nActiveVehCount; ++v) {
        const LeafTriggerSource& vehSrc = CLeafSystem::aActiveVehs[v];
        float dx = m_vecPos.x - vehSrc.pos.x;
        float dy = m_vecPos.y - vehSrc.pos.y;
        float dz = m_vecPos.z - vehSrc.pos.z;
        float horizSq = dx * dx + dy * dy;
        float rSq = vehSrc.radius * vehSrc.radius;
        if (horizSq < rSq && std::fabs(dz) < (vehSrc.isAircraftRotor ? 24.0f : 3.5f)) {
            float invDist = FastInvSqrt(horizSq + 0.01f);
            m_vecVel.x += dx * invDist * 6.5f;
            m_vecVel.y += dy * invDist * 6.5f;
            m_vecVel.z = 2.8f;
            m_bOverWater = false;
            CLeafSystem::SetLeafState(index, eLeafState::AIRBORNE);
            return;
        }
    }
}

void CLeafSystem::Init(const char* iniPath) {
    if (bInitialized) return;

    LoadConfig(iniPath);
    CreateProceduralAtlasTexture();

    nAirborneCount = 0;
    nWaterCount = 0;
    nDormantCount = 0;
    nRespawnHead = 0;
    nRespawnTail = 0;
    nRespawnCount = 0;
    nRaycastCursor = 0;

    for (int i = 0; i < SPATIAL_GRID_CELLS; ++i) {
        aSpatialCellHeads[i] = -1;
        aGroundCache[i].worldCellX = 0x7FFFFFFF;
        aGroundCache[i].worldCellY = 0x7FFFFFFF;
        aGroundCache[i].timestamp = -100.0f;
    }

    for (int c = 0; c < CLUSTER_COUNT; ++c) {
        float angle = (6.283185f / static_cast<float>(CLUSTER_COUNT)) * c + RandomFloat(-0.3f, 0.3f);
        float dist = RandomFloat(fPositionRange * 0.25f, fPositionRange * 0.75f);
        aClusterOffsets[c] = CVector(FastCos(angle) * dist, FastSin(angle) * dist, 0.0f);
    }

    CPlayerPed* player = FindPlayerPed();
    CVector startPos = player ? player->GetPosition() : CVector(0.0f, 0.0f, 10.0f);
    vecPrevPlayerPos = startPos;
    nGridOriginCellX = WorldToCellCoord(startPos.x) - (SPATIAL_GRID_DIM / 2);
    nGridOriginCellY = WorldToCellCoord(startPos.y) - (SPATIAL_GRID_DIM / 2);

    for (int i = 0; i < MAX_LEAVES_LIMIT; ++i) {
        aInRespawnQueue[i] = false;
        aLeaves[i].m_nState = eLeafState::INACTIVE_RESPAWN;
        aLeaves[i].m_nStateListPos = -1;
        aLeaves[i].m_nSpatialCell = -1;
        aLeaves[i].m_nNextInCell = -1;
        aLeaves[i].m_nPrevInCell = -1;
        aLeaves[i].m_nClusterId = CLUSTER_COUNT;
    }

    float totalLeafSplit = fCanopyLeafPct + fGroundLeafPct;
    float canopyRatio = (totalLeafSplit > 0.001f) ? (fCanopyLeafPct / totalLeafSplit) : 0.12f;
    int canopyLeafCount = static_cast<int>(std::round(static_cast<float>(nInstanceCount) * canopyRatio));
    if (fCanopyLeafPct > 0.0f && canopyLeafCount < 1) canopyLeafCount = 1;
    if (canopyLeafCount > nInstanceCount) canopyLeafCount = nInstanceCount;

    for (int i = 0; i < nInstanceCount; ++i) {
        bool isCanopySpawn = (i < canopyLeafCount);
        aLeaves[i].Init(startPos, i, isCanopySpawn, bSnapToGroundOnStart);
        UpdateLeafSpatialCell(i);
    }

    bInitialized = true;
}

void CLeafSystem::Shutdown() {
    if (!bInitialized) return;

    if (pLeafTexture) {
        RwTextureDestroy(pLeafTexture);
        pLeafTexture = nullptr;
        pProcRaster = nullptr;
    }
    bInitialized = false;
}

void CLeafSystem::Update(float baseDt, float currentTime) {
    if (!bInitialized) return;

    CPlayerPed* player = FindPlayerPed();
    if (!player) return;

    nGroundRaycastBudget = 16;
    int prevQuota = nEffectiveQuota;

    if (bDynamicQuotaScaling) {
        fSmoothedFrameTime = fSmoothedFrameTime * 0.90f + baseDt * 0.10f;
        float targetRatio = (fSmoothedFrameTime > 0.020f) ? 0.80f : 1.00f;

        int targetQuota = static_cast<int>(nInstanceCount * targetRatio);
        if (targetQuota < 16) targetQuota = 16;
        if (targetQuota > MAX_LEAVES_LIMIT) targetQuota = MAX_LEAVES_LIMIT;

        if (nEffectiveQuota > targetQuota) {
            nEffectiveQuota -= 4;
            if (nEffectiveQuota < targetQuota) nEffectiveQuota = targetQuota;
        }
        else if (nEffectiveQuota < targetQuota) {
            nEffectiveQuota += 2;
            if (nEffectiveQuota > targetQuota) nEffectiveQuota = targetQuota;
        }
    }
    else {
        nEffectiveQuota = nInstanceCount;
    }

    CVector playerPos = player->GetPosition();
    s_PlayerPosCache = playerPos;

    // Stage 1: Protect division against near-zero baseDt
    float safeDt = (baseDt > 0.0005f) ? baseDt : 0.0005f;
    float playerSpeed = (playerPos - vecPrevPlayerPos).Magnitude() / safeDt;
    vecPrevPlayerPos = playerPos;

    // Re-center spatial grid window when player moves across cells
    int newOriginX = WorldToCellCoord(playerPos.x) - (SPATIAL_GRID_DIM / 2);
    int newOriginY = WorldToCellCoord(playerPos.y) - (SPATIAL_GRID_DIM / 2);
    if (newOriginX != nGridOriginCellX || newOriginY != nGridOriginCellY) {
        nGridOriginCellX = newOriginX;
        nGridOriginCellY = newOriginY;
        std::memset(aSpatialCellHeads, -1, sizeof(aSpatialCellHeads));
        for (int i = 0; i < nEffectiveQuota; ++i) {
            aLeaves[i].m_nSpatialCell = -1;
            aLeaves[i].m_nNextInCell = -1;
            aLeaves[i].m_nPrevInCell = -1;
            if (aLeaves[i].m_nState != eLeafState::INACTIVE_RESPAWN) {
                UpdateLeafSpatialCell(i);
            }
        }
    }

    // Handle dynamic quota shrinking/expanding cleanly with active lists
    if (nEffectiveQuota < prevQuota) {
        for (int i = nEffectiveQuota; i < prevQuota; ++i) {
            SetLeafState(i, eLeafState::INACTIVE_RESPAWN);
            RemoveLeafFromSpatialCell(i);
        }
    }
    else if (nEffectiveQuota > prevQuota) {
        for (int i = prevQuota; i < nEffectiveQuota; ++i) {
            if (!aInRespawnQueue[i] && aLeaves[i].m_nState == eLeafState::INACTIVE_RESPAWN) {
                EnqueueRespawn(i);
            }
        }
    }

    float hour = static_cast<float>(CClock::ms_nGameClockHours);
    s_IsNightOrDusk = (hour >= 19.0f || hour <= 6.0f);

    // 1. Scan GTA:SA explosion pool at 0xC88950 (16 CExplosion entries)
    s_ExplosionCount = 0;
    CExplosion* explosionPool = reinterpret_cast<CExplosion*>(0xC88950);
    for (int i = 0; i < 16; ++i) {
        CExplosion& exp = explosionPool[i];
        if (exp.m_nType != 0 && exp.m_nActiveCounter > 0) {
            if (exp.m_nActiveCounter < s_PrevExplosionCounter[i] || s_PrevExplosionCounter[i] == 0) {
                if (s_ExplosionCount < 8 && (exp.m_vecPosition - playerPos).MagnitudeSqr() < 4225.0f) {
                    s_Explosions[s_ExplosionCount++] = { exp.m_vecPosition, 24.0f, 576.0f, 19.5f, 15.5f };
                }
            }
            s_PrevExplosionCounter[i] = exp.m_nActiveCounter;
        }
        else {
            s_PrevExplosionCounter[i] = 0;
        }
    }

    // 2. Collect closest moving peds (squared speed early rejection)
    nActivePedCount = 0;
    float maxSearchDistSq = (fPositionRange + fRadius) * (fPositionRange + fRadius);

    if (nMaxTriggerPeds > 0 && CPools::ms_pPedPool != nullptr) {
        struct CandEntry { float distSq; LeafTriggerSource src; };
        CandEntry pedCands[MAX_TRIGGER_PEDS_CAP];
        int candCount = 0;

        int pedPoolSize = CPools::ms_pPedPool->m_nSize;
        for (int i = 0; i < pedPoolSize; ++i) {
            CPed* ped = CPools::ms_pPedPool->GetAt(i);
            if (!ped || !ped->IsAlive() || ped->bInVehicle) continue;

            CVector pPos = ped->GetPosition();
            float distToPlayerSq = (pPos - playerPos).MagnitudeSqr();
            if (distToPlayerSq > maxSearchDistSq) continue;

            float spd = 0.0f;
            if (ped == player) {
                if (playerSpeed < 0.2f) continue;
                spd = playerSpeed;
            }
            else {
                // 0.2 / 50.0 = 0.004 -> sq = 0.000016
                if (ped->m_vecMoveSpeed.MagnitudeSqr() < 0.000016f) continue;
                spd = ped->m_vecMoveSpeed.Magnitude() * 50.0f;
            }

            LeafTriggerSource src = { pPos, ped->m_vecMoveSpeed, spd, fRadius, fForce, fTriggerChance, false };

            int insertIdx = candCount;
            while (insertIdx > 0 && pedCands[insertIdx - 1].distSq > distToPlayerSq) {
                if (insertIdx < nMaxTriggerPeds) pedCands[insertIdx] = pedCands[insertIdx - 1];
                --insertIdx;
            }
            if (insertIdx < nMaxTriggerPeds) {
                pedCands[insertIdx] = { distToPlayerSq, src };
                if (candCount < nMaxTriggerPeds) ++candCount;
            }
        }

        nActivePedCount = candCount;
        for (int k = 0; k < candCount; ++k) aActivePeds[k] = pedCands[k].src;
    }

    // 3. Single-pass vehicle pool scan for physics triggers & spatial night headlights
    nActiveVehCount = 0;
    s_HeadlightCount = 0;
    float maxVehSearchSq = (fPositionRange + 22.0f) * (fPositionRange + 22.0f);

    if ((nMaxTriggerVehicles > 0 || s_IsNightOrDusk) && CPools::ms_pVehiclePool != nullptr) {
        struct CandEntry { float distSq; LeafTriggerSource src; };
        CandEntry vehCands[MAX_TRIGGER_VEHS_CAP];
        int candCount = 0;

        int vehPoolSize = CPools::ms_pVehiclePool->m_nSize;
        for (int i = 0; i < vehPoolSize; ++i) {
            CVehicle* veh = CPools::ms_pVehiclePool->GetAt(i);
            if (!veh) continue;

            CVector vPos = veh->GetPosition();
            float distToPlayerSq = (vPos - playerPos).MagnitudeSqr();

            if (s_IsNightOrDusk && s_HeadlightCount < 8 && veh->m_pDriver && distToPlayerSq < 2500.0f) {
                s_Headlights[s_HeadlightCount].pos = vPos;
                s_Headlights[s_HeadlightCount].fwd = veh->GetForward();
                s_Headlights[s_HeadlightCount].cellX = WorldToCellCoord(vPos.x);
                s_Headlights[s_HeadlightCount].cellY = WorldToCellCoord(vPos.y);
                ++s_HeadlightCount;
            }

            if (nMaxTriggerVehicles > 0 && distToPlayerSq <= maxVehSearchSq) {
                bool isAircraft = (veh->m_nVehicleSubClass == VEHICLE_HELI || veh->m_nVehicleSubClass == VEHICLE_PLANE);
                bool rotorSpinning = isAircraft && (veh->m_pDriver != nullptr || veh->bEngineOn);

                // 1.5 / 50.0 = 0.03 -> sq = 0.0009
                float moveSq = veh->m_vecMoveSpeed.MagnitudeSqr();
                if (!rotorSpinning && moveSq < 0.0009f) continue;

                float spd = (moveSq > 1e-6f) ? (veh->m_vecMoveSpeed.Magnitude() * 50.0f) : 0.0f;

                LeafTriggerSource src;
                if (rotorSpinning) {
                    src = { vPos, veh->m_vecMoveSpeed, ClampFloat(spd + 25.0f, 25.0f, 90.0f), 20.0f, fVehicleForce * 1.25f, 1.0f, true };
                }
                else {
                    src = { vPos, veh->m_vecMoveSpeed, spd, fVehicleRadius, fVehicleForce, fVehicleLiftChance, false };
                }

                int insertIdx = candCount;
                while (insertIdx > 0 && vehCands[insertIdx - 1].distSq > distToPlayerSq) {
                    if (insertIdx < nMaxTriggerVehicles) vehCands[insertIdx] = vehCands[insertIdx - 1];
                    --insertIdx;
                }
                if (insertIdx < nMaxTriggerVehicles) {
                    vehCands[insertIdx] = { distToPlayerSq, src };
                    if (candCount < nMaxTriggerVehicles) ++candCount;
                }
            }
        }

        nActiveVehCount = candCount;
        for (int k = 0; k < candCount; ++k) aActiveVehs[k] = vehCands[k].src;
    }

    ++nFrameCounter;

    const CVector& windVec = CWindSystem::vecActiveWindForce;
    const float canopyScale = CWindSystem::fCanopyDriftScale;

    // 4. Staggered Out-of-Bounds Check -> Enqueues into Deferred Respawn Queue (Stage 2)
    int oobSlice = static_cast<int>(nFrameCounter & 3u);
    for (int i = oobSlice; i < nEffectiveQuota; i += 4) {
        CLeaf& leaf = aLeaves[i];
        if (leaf.m_nState != eLeafState::INACTIVE_RESPAWN && leaf.CheckOutOfBounds(playerPos)) {
            EnqueueRespawn(i);
        }
    }

    // Process up to 8 deferred respawns per frame to eliminate frame spikes
    ProcessRespawnQueue(playerPos, windVec, currentTime, 8);

    // 5. Event-Driven Spatial Activation for Dormant Particles (Stage 2 & 3)
    DispatchEventDrivenActivations(currentTime, windVec);

    // Round-robin raycast eligibility window across active dynamic particles
    int totalDynamic = nAirborneCount + nWaterCount;
    int raycastWindowStart = (totalDynamic > 0) ? (nRaycastCursor % totalDynamic) : 0;
    if (totalDynamic > 0) {
        nRaycastCursor = (nRaycastCursor + 24) % totalDynamic;
    }

    // 6. Update Active Water Drift List with Adaptive Frequency LOD
    for (int i = nWaterCount - 1; i >= 0; --i) {
        int leafIdx = aWaterList[i];
        CLeaf& leaf = aLeaves[leafIdx];

        float dxP = leaf.m_vecPos.x - playerPos.x;
        float dyP = leaf.m_vecPos.y - playerPos.y;
        float distSq = dxP * dxP + dyP * dyP;

        leaf.m_fSimAccumDt += baseDt;
        int stepMask = (distSq < 144.0f) ? 0 : ((distSq < 484.0f) ? 1 : 3);
        if (((leafIdx + static_cast<int>(nFrameCounter)) & stepMask) != 0) {
            continue;
        }

        float stepDt = leaf.m_fSimAccumDt;
        leaf.m_fSimAccumDt = 0.0f;

        int dynSlot = nAirborneCount + i;
        int relSlot = (dynSlot - raycastWindowStart + totalDynamic) % (totalDynamic > 0 ? totalDynamic : 1);
        bool allowRaycast = (relSlot < 24);

        leaf.UpdateWaterDrift(stepDt, currentTime, leafIdx, windVec, allowRaycast);
        UpdateLeafSpatialCell(leafIdx);
    }

    // 7. Update Active Airborne List with Distance-Based Simulation Frequency LOD & Accumulator
    for (int i = nAirborneCount - 1; i >= 0; --i) {
        int leafIdx = aAirborneList[i];
        CLeaf& leaf = aLeaves[leafIdx];

        float dxP = leaf.m_vecPos.x - playerPos.x;
        float dyP = leaf.m_vecPos.y - playerPos.y;
        float distSq = dxP * dxP + dyP * dyP;

        leaf.m_fSimAccumDt += baseDt;

        // Frequency LOD: Near (<12m) = 60Hz, Mid (12-22m) = 30Hz, Far (>22m) = 15Hz
        int stepMask = 0;
        if (distSq > 484.0f) stepMask = 3;
        else if (distSq > 144.0f) stepMask = 1;

        if (((leafIdx + static_cast<int>(nFrameCounter)) & stepMask) != 0) {
            continue;
        }

        float stepDt = leaf.m_fSimAccumDt;
        leaf.m_fSimAccumDt = 0.0f;

        int relSlot = (i - raycastWindowStart + totalDynamic) % (totalDynamic > 0 ? totalDynamic : 1);
        bool allowRaycast = (relSlot < 24);

        leaf.UpdateAirborne(stepDt, currentTime, leafIdx, nFrameCounter, windVec, canopyScale, distSq, allowRaycast);
        UpdateLeafSpatialCell(leafIdx);
    }
}

// =========================================================================
// Stage 3 & 4: Hierarchical Cluster Culling + Two-Pass Visibility & LOD Rendering
// =========================================================================
void CLeafSystem::Render() {
    if (!bInitialized || !pLeafTexture) return;

    CVector camPos = TheCamera.GetPosition();
    CVector camFwd = TheCamera.GetForward();
    CVector camRight = TheCamera.GetRight();
    CVector camUp = TheCamera.GetUp();
    float baseMaxDistSq = fDrawDistance * fDrawDistance;
    float tumbleweedMaxDistSq = (fDrawDistance * 2.0f) * (fDrawDistance * 2.0f);

    // Stage 3: Hierarchical Cluster Bounding Sphere Visibility Pre-Pass
    bool clusterVisible[CLUSTER_COUNT + 1];
    float clusterCullRadius = fClumpRadius + 2.5f;
    float clusterMaxDistSq = (fDrawDistance + clusterCullRadius) * (fDrawDistance + clusterCullRadius);

    for (int c = 0; c < CLUSTER_COUNT; ++c) {
        CVector clusterCenter(
            s_PlayerPosCache.x + aClusterOffsets[c].x,
            s_PlayerPosCache.y + aClusterOffsets[c].y,
            camPos.z
        );
        float cdx = clusterCenter.x - camPos.x;
        float cdy = clusterCenter.y - camPos.y;
        float cDistSq = cdx * cdx + cdy * cdy;
        if (cDistSq > clusterMaxDistSq) {
            clusterVisible[c] = false;
            continue;
        }
        float cFwdDot = cdx * camFwd.x + cdy * camFwd.y;
        if (cFwdDot < -clusterCullRadius) {
            clusterVisible[c] = false;
            continue;
        }
        clusterVisible[c] = TheCamera.IsSphereVisible(clusterCenter, clusterCullRadius + fSpawnHeight);
    }
    clusterVisible[CLUSTER_COUNT] = true; // Unclustered particles always tested individually

    // Stage 4: Tahap A — Visibility & LOD Classification Pass
    if (nLastVisibilityPassFrame != nFrameCounter) {
        nLastVisibilityPassFrame = nFrameCounter;
        nVisibleCutoutCount = 0;
        nVisibleAlphaCount = 0;

        int maxRenderQuota = (nEffectiveQuota > MAX_LEAVES_LIMIT) ? MAX_LEAVES_LIMIT : nEffectiveQuota;
        for (int i = 0; i < maxRenderQuota; ++i) {
            CLeaf& leaf = aLeaves[i];
            if (leaf.m_nState == eLeafState::INACTIVE_RESPAWN) continue;

            // Hierarchical cluster early rejection (skip for tumbleweeds which roam widely)
            unsigned char cid = (leaf.m_nClusterId <= CLUSTER_COUNT) ? leaf.m_nClusterId : CLUSTER_COUNT;
            if (leaf.m_eType != eDebrisType::TUMBLEWEED && !clusterVisible[cid]) {
                leaf.m_bCachedVisible = false;
                leaf.m_nLastVisFrame = nFrameCounter;
                continue;
            }

            float dx = leaf.m_vecPos.x - camPos.x;
            float dy = leaf.m_vecPos.y - camPos.y;
            float dz = leaf.m_vecPos.z - camPos.z;

            float distSq = dx * dx + dy * dy + dz * dz;
            float maxDistSq = (leaf.m_eType == eDebrisType::TUMBLEWEED) ? tumbleweedMaxDistSq : baseMaxDistSq;
            if (distSq > maxDistSq) {
                leaf.m_bCachedVisible = false;
                leaf.m_nLastVisFrame = nFrameCounter;
                continue;
            }

            float forwardDot = dx * camFwd.x + dy * camFwd.y + dz * camFwd.z;
            if (forwardDot < -1.0f) {
                leaf.m_bCachedVisible = false;
                leaf.m_nLastVisFrame = nFrameCounter;
                continue;
            }

            float scale = leaf.GetScale();
            if (!TheCamera.IsSphereVisible(leaf.m_vecPos, scale * 2.0f)) {
                leaf.m_bCachedVisible = false;
                leaf.m_nLastVisFrame = nFrameCounter;
                continue;
            }

            leaf.m_bCachedVisible = true;
            leaf.m_nLastVisFrame = nFrameCounter;

            eRenderLOD lod = eRenderLOD::NEAR_FULL;
            if (distSq > 484.0f) lod = eRenderLOD::FAR_BILLBOARD;      // > 22m
            else if (distSq > 144.0f) lod = eRenderLOD::MID_REDUCED;   // 12m - 22m

            if (leaf.m_Phys.isCutoutAlpha) {
                aVisibleCutout[nVisibleCutoutCount++] = { i, distSq, lod };
            }
            else {
                aVisibleAlpha[nVisibleAlphaCount++] = { i, distSq, lod };
            }
        }
    }

    if (nVisibleCutoutCount == 0 && nVisibleAlphaCount == 0) return;

    CVector ambientColor(
        ClampFloat(CTimeCycle::m_CurrentColours.m_fAmbientRed * 1.15f, 0.12f, 1.0f),
        ClampFloat(CTimeCycle::m_CurrentColours.m_fAmbientGreen * 1.15f, 0.12f, 1.0f),
        ClampFloat(CTimeCycle::m_CurrentColours.m_fAmbientBlue * 1.15f, 0.12f, 1.0f)
    );

    CVector sunColor(
        ClampFloat(CTimeCycle::m_CurrentColours.m_fDirectionalRed * 0.95f, 0.0f, 1.0f),
        ClampFloat(CTimeCycle::m_CurrentColours.m_fDirectionalGreen * 0.95f, 0.0f, 1.0f),
        ClampFloat(CTimeCycle::m_CurrentColours.m_fDirectionalBlue * 0.95f, 0.0f, 1.0f)
    );

    int sunIdx = CTimeCycle::m_CurrentStoredValue;
    CVector sunDir = (sunIdx >= 0 && sunIdx < 16) ? CTimeCycle::m_VectorToSun[sunIdx] : CVector(0.0f, 0.0f, 1.0f);
    FastVecNormalize(sunDir);

    const float cellSize = 0.25f;
    const float halfTexelInset = 0.5f / 256.0f; // Stage 4: Half-texel UV inset

    auto emitVisibleBatch = [&](const VisibleParticleEntry* list, int count) {
        for (int idx = 0; idx < count; ++idx) {
            const VisibleParticleEntry& vis = list[idx];
            CLeaf& leaf = aLeaves[vis.leafIndex];

            // Stage 1: Temporal Dynamic Lighting Color Cache based on LOD distance
            unsigned int colorInterval = (vis.lod == eRenderLOD::NEAR_FULL) ? 1u : ((vis.lod == eRenderLOD::MID_REDUCED) ? 3u : 6u);
            if (leaf.m_nLastColorFrame == 0xFFFFFFFFu || ((nFrameCounter - leaf.m_nLastColorFrame) >= colorInterval)) {
                leaf.m_CachedDynColor = ComputeLeafDynamicColorLOD(leaf, vis.lod, ambientColor, sunDir, sunColor);
                leaf.m_nLastColorFrame = nFrameCounter;
            }
            RwRGBA dynColor = leaf.m_CachedDynColor;

            int col = leaf.m_nAtlasSlot & 3;
            int row = leaf.m_nAtlasSlot >> 2;
            float u0 = col * cellSize + halfTexelInset;
            float v0 = row * cellSize + halfTexelInset;
            float u1 = (col + 1) * cellSize - halfTexelInset;
            float v1 = (row + 1) * cellSize - halfTexelInset;
            float scale = leaf.GetScale();

            // Stage 4: Distance-Based LOD & Billboard Specialization
            if (leaf.m_Phys.useBillboard || (vis.lod == eRenderLOD::FAR_BILLBOARD && leaf.m_eType == eDebrisType::INSECT)) {
                CVector bR = camRight * scale;
                CVector bU = camUp * scale;
                CLeafBatchRenderer::PushQuad(
                    leaf.m_vecPos - bU - bR,
                    leaf.m_vecPos + bU - bR,
                    leaf.m_vecPos + bU + bR,
                    leaf.m_vecPos - bU + bR,
                    dynColor, u0, v0, u1, v1
                );
            }
            else if (leaf.m_eType == eDebrisType::TUMBLEWEED) {
                if (vis.lod == eRenderLOD::FAR_BILLBOARD) {
                    // Far LOD: single camera-facing quad instead of 3 crossed quads
                    CVector bR = camRight * scale;
                    CVector bU = camUp * scale;
                    CLeafBatchRenderer::PushQuad(
                        leaf.m_vecPos - bU - bR,
                        leaf.m_vecPos + bU - bR,
                        leaf.m_vecPos + bU + bR,
                        leaf.m_vecPos - bU + bR,
                        dynColor, u0, v0, u1, v1
                    );
                }
                else if (vis.lod == eRenderLOD::MID_REDUCED) {
                    // Mid LOD: 2 crossed quads instead of 3
                    CVector right = VecCross(leaf.m_vecForward, leaf.m_vecUp);
                    FastVecNormalize(right);
                    CVector vF = leaf.m_vecForward * scale;
                    CVector vR = right * scale;
                    CVector vU = leaf.m_vecUp * scale;
                    CLeafBatchRenderer::PushQuad(leaf.m_vecPos - vF - vR, leaf.m_vecPos + vF - vR, leaf.m_vecPos + vF + vR, leaf.m_vecPos - vF + vR, dynColor, u0, v0, u1, v1);
                    CLeafBatchRenderer::PushQuad(leaf.m_vecPos - vF - vU, leaf.m_vecPos + vF - vU, leaf.m_vecPos + vF + vU, leaf.m_vecPos - vF + vU, dynColor, u0, v0, u1, v1);
                }
                else {
                    // Near LOD: full 3 crossed quads
                    CVector right = VecCross(leaf.m_vecForward, leaf.m_vecUp);
                    FastVecNormalize(right);
                    CVector vF = leaf.m_vecForward * scale;
                    CVector vR = right * scale;
                    CVector vU = leaf.m_vecUp * scale;
                    CLeafBatchRenderer::PushQuad(leaf.m_vecPos - vF - vR, leaf.m_vecPos + vF - vR, leaf.m_vecPos + vF + vR, leaf.m_vecPos - vF + vR, dynColor, u0, v0, u1, v1);
                    CLeafBatchRenderer::PushQuad(leaf.m_vecPos - vF - vU, leaf.m_vecPos + vF - vU, leaf.m_vecPos + vF + vU, leaf.m_vecPos - vF + vU, dynColor, u0, v0, u1, v1);
                    CLeafBatchRenderer::PushQuad(leaf.m_vecPos - vR - vU, leaf.m_vecPos + vR - vU, leaf.m_vecPos + vR + vU, leaf.m_vecPos - vR + vU, dynColor, u0, v0, u1, v1);
                }
            }
            else {
                CVector right = VecCross(leaf.m_vecForward, leaf.m_vecUp);
                FastVecNormalize(right);
                CVector vF = leaf.m_vecForward * scale;
                CVector vR = right * scale;
                CLeafBatchRenderer::PushQuad(
                    leaf.m_vecPos - vF - vR,
                    leaf.m_vecPos + vF - vR,
                    leaf.m_vecPos + vF + vR,
                    leaf.m_vecPos - vF + vR,
                    dynColor, u0, v0, u1, v1
                );
            }
        }
    };

    // Stage 4: Tahap B — Split Cutout Pass (Z-Write ON + Alpha Test) & Translucent Pass (Alpha Blend)
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

    RwRaster* batchRaster = RwTextureGetRaster(pLeafTexture);
    if (!batchRaster) return;
    CLeafBatchRenderer::BeginBatch(batchRaster);

    if (nVisibleCutoutCount > 0) {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)128);

        emitVisibleBatch(aVisibleCutout, nVisibleCutoutCount);
        CLeafBatchRenderer::Flush();

        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)0);
    }

    if (nVisibleAlphaCount > 0) {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

        emitVisibleBatch(aVisibleAlpha, nVisibleAlphaCount);
        CLeafBatchRenderer::Flush();
    }

    CLeafBatchRenderer::EndBatch();

    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
}
