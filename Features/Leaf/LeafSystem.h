#pragma once
#include "RenderWare.h"
#include "CVector.h"
#include "../../Environment/SurfaceScanner.h"
#include <cstdint>

constexpr int MAX_LEAVES_LIMIT = 1500;
constexpr int CLUSTER_COUNT = 12;
constexpr int MAX_TRIGGER_PEDS_CAP = 12;
constexpr int MAX_TRIGGER_VEHS_CAP = 10;

// Uniform Spatial Grid Configuration (16x16 cells covering local player bubble)
constexpr int SPATIAL_GRID_DIM = 16;
constexpr int SPATIAL_GRID_CELLS = SPATIAL_GRID_DIM * SPATIAL_GRID_DIM;
constexpr float SPATIAL_CELL_SIZE = 6.0f;
constexpr float SPATIAL_INV_CELL_SIZE = 1.0f / SPATIAL_CELL_SIZE;

enum class eLeafState : unsigned char {
    DORMANT_GROUND = 0,
    AIRBORNE = 1,
    WATER_DRIFT = 2,
    INACTIVE_RESPAWN = 3
};

enum class eDebrisType : unsigned char {
    LEAF = 0,
    NEWSPAPER = 1,
    PLASTIC_BAG = 2,
    CAN_CAP = 3,
    GRAVEL_BRICK = 4,
    CIGARETTE_NAIL = 5,
    GLASS_SHARD = 6,
    FEATHER = 7,
    TUMBLEWEED = 8,
    INSECT = 9,
    POLLEN_DUST = 10
};

enum class eInsectVariant : unsigned char {
    MOTH_BUTTERFLY = 0, // Slot 11
    FIREFLY = 1,        // Slot 13
    DRAGONFLY = 2,      // Slot 14
    BEETLE_GNAT = 3     // Slot 15
};

enum class eRenderLOD : unsigned char {
    NEAR_FULL = 0,      // 0 - 12m: full 3D geometry & dynamic lighting
    MID_REDUCED = 1,    // 12 - 22m: reduced geometry & ambient+sun lighting
    FAR_BILLBOARD = 2   // 22m+: single billboard quad & cached ambient lighting
};

struct LeafTriggerSource {
    CVector pos;
    CVector moveDir;
    float speed;
    float radius;
    float force;
    float chance;
    bool isAircraftRotor;
};

// Precomputed physics & visual traits per particle (Stage 1: Precompute physics parameters)
struct LeafPhysicsProps {
    float mass;
    float invMass;
    float gravityFallMult;
    float gravityRiseMult;
    float dragMult;
    float windLiftMult;
    float freqMult;
    float ampMult;
    float flapSpeed;
    float baseHoverZ;
    bool canFloatOnWater;
    bool isCutoutAlpha;
    bool useBillboard;
};

// Spatial Ground Sample Cache per cell (Stage 3: Ground sample cache)
struct GroundSpatialCacheEntry {
    float groundZ;
    CVector normal;
    unsigned char shade;
    unsigned char surfaceType;
    bool overWater;
    float timestamp;
    int worldCellX;
    int worldCellY;
};

// Visible particle entry generated during Visibility Pass (Stage 4: Visible particle list)
struct VisibleParticleEntry {
    int leafIndex;
    float distSq;
    float scale;
    float submergedDepth;
    eRenderLOD lod;
    bool isSubmerged;
};

// Customizable percentage weights for all 11 debris types per location
struct DebrisSpawnWeights {
    float weights[11]; // Indexed by eDebrisType (0..10)
};

class CLeaf {
public:
    CVector m_vecPos;
    CVector m_vecVel;
    CVector m_vecForward;
    CVector m_vecUp;

    float m_fGroundZ;
    float m_fLastSampleX;
    float m_fLastSampleY;
    float m_fLastSampleTime;
    float m_fLastTriggerTime;

    // Stage 1 & 2: Sleep timer, simulation accumulator & cached visibility/lighting
    float m_fSleepTimer;
    float m_fSimAccumDt;
    unsigned int m_nLastVisFrame;
    bool m_bCachedVisible;

    RwRGBA m_CachedDynColor;
    unsigned int m_nLastColorFrame;

    // Stage 2: Active list & Spatial grid intrusive/index tracking
    int m_nStateListPos;
    int m_nSpatialCell;
    int m_nNextInCell;
    int m_nPrevInCell;
    unsigned char m_nClusterId;
    uint32_t m_nRngState;

    // Precomputed per-instance physics & properties
    LeafPhysicsProps m_Phys;

    float m_fScale;
    CVector m_vecAngVel;
    CVector m_vecGroundNormal;

    eLeafState m_nState;
    eDebrisType m_eType;
    unsigned char m_nAtlasSlot;
    unsigned char m_nShade;
    unsigned char m_nAlpha;
    float m_fFadeAlpha;      // 0.0f (invisible) -> 1.0f (fully visible)
    float m_fFadeSpeed;      // Opacity units per second
    bool m_bFadingOut;       // True when despawning / out of buffered range
    bool m_bIsTreeLeaf;
    bool m_bCanopyDrift;
    bool m_bOverWater;

    float GetScale() const { return m_fScale; }
    void SetScale(float s) { m_fScale = s; }

    CVector GetAngVel() const { return m_vecAngVel; }
    void SetAngVel(const CVector& v) { m_vecAngVel = v; }

    CVector GetGroundNormal() const { return m_vecGroundNormal; }
    void SetGroundNormal(const CVector& n) { m_vecGroundNormal = n; }

    void RebuildPhysicsProps();
    void AssignTypeForLocation(eBiomeZone biome, unsigned char surfaceType, bool isTreeLeaf);
    void Init(const CVector& centerPos, int index, bool isTreeLeaf, bool snapToGround);
    void AlignToNormal(const CVector& targetNormal, float lerpSpeed);
    void BeginFadeOut(float durationSec);
    void UpdateFade(float dt, int index);
    bool CheckOutOfBounds(const CVector& camPos) const;
    void ExecuteRespawn(const CVector& playerPos, int index, const CVector& activeWindVec, float currentTime);
    void UpdateAirborne(float dt, float currentTime, int index, unsigned int frameCount, const CVector& activeWindVec, float canopyDriftScale, float distToPlayerSq, bool allowRaycast);
    void UpdateWaterDrift(float dt, float currentTime, int index, const CVector& activeWindVec, bool allowRaycast);
};

class CLeafSystem {
public:
    static CLeaf aLeaves[MAX_LEAVES_LIMIT];
    static CVector aClusterOffsets[CLUSTER_COUNT];
    static RwTexture* pLeafTexture;
    static RwRaster* pProcRaster;
    static bool bInitialized;
    static unsigned int nFrameCounter;
    static CVector vecPrevPlayerPos;

    static bool bDynamicQuotaScaling;
    static int nEffectiveQuota;
    static float fSmoothedFrameTime;
    static int nGroundRaycastBudget;

    // Stage 2: Segregated Active Lists (O(1) swap-remove)
    static int aAirborneList[MAX_LEAVES_LIMIT];
    static int nAirborneCount;
    static int aWaterList[MAX_LEAVES_LIMIT];
    static int nWaterCount;
    static int aDormantList[MAX_LEAVES_LIMIT];
    static int nDormantCount;

    // Stage 2: Deferred Respawn Ring Queue
    static int aRespawnQueue[MAX_LEAVES_LIMIT];
    static bool aInRespawnQueue[MAX_LEAVES_LIMIT];
    static int nRespawnHead;
    static int nRespawnTail;
    static int nRespawnCount;

    // Stage 2: Round-Robin Raycast Cursor
    static int nRaycastCursor;

    // Stage 3: Uniform Spatial Grid & Ground Cache
    static int aSpatialCellHeads[SPATIAL_GRID_CELLS];
    static GroundSpatialCacheEntry aGroundCache[SPATIAL_GRID_CELLS];
    static int nGridOriginCellX;
    static int nGridOriginCellY;

    // Stage 4: Pre-filtered Visible Particle Lists (Cutout vs Alpha-Blended)
    static VisibleParticleEntry aVisibleCutout[MAX_LEAVES_LIMIT];
    static int nVisibleCutoutCount;
    static VisibleParticleEntry aVisibleAlpha[MAX_LEAVES_LIMIT];
    static int nVisibleAlphaCount;
    static unsigned int nLastVisibilityPassFrame;

    static int nInstanceCount;
    static float fForce;
    static float fGravity;
    static float fUpForce;
    static int nMaxTriggerPeds;
    static int nMaxTriggerVehicles;
    static LeafTriggerSource aActivePeds[MAX_TRIGGER_PEDS_CAP];
    static int nActivePedCount;
    static LeafTriggerSource aActiveVehs[MAX_TRIGGER_VEHS_CAP];
    static int nActiveVehCount;

    static float fTriggerChance;
    static float fVehicleLiftChance;
    static float fVehicleForce;
    static float fVehicleRadius;
    static float fFriction;
    static float fRadius;

    static float fSpawnHeight;
    static float fPositionRange;
    static float fScaleMin;
    static float fScaleMax;
    static float fClumpRatio;
    static float fClumpRadius;
    static float fDrawDistance;
    static float fSpawnBufferDistance;
    static float fMinCameraSpawnDist;
    static float fFadeInDuration;
    static float fFadeOutDuration;
    static bool bSnapToGroundOnStart;

    static CVector vecCamVelocity;
    static float fCamHorizSpeed;

    // Leaf Distribution & Biome/Surface Spawn Percentages
    static float fCanopyLeafPct;
    static float fGroundLeafPct;
    static DebrisSpawnWeights sWeightsDesert;
    static DebrisSpawnWeights sWeightsForest;
    static DebrisSpawnWeights sWeightsCityNatural;
    static DebrisSpawnWeights sWeightsCityUrban;

    static void SaveDefaultConfig(void* filePtr);
    static void LoadConfig(const char* iniPath);
    static void CreateProceduralAtlasTexture();
    static void Init(const char* iniPath);
    static void Shutdown();
    static void Update(float baseDt, float currentTime);
    static void Render();

    // State & Spatial Management Helpers
    static void SetLeafState(int leafIdx, eLeafState newState);
    static void UpdateLeafSpatialCell(int leafIdx);
    static void RemoveLeafFromSpatialCell(int leafIdx);
    static void EnqueueRespawn(int leafIdx);
    static void ProcessRespawnQueue(const CVector& playerPos, const CVector& windVec, float currentTime, int maxPerFrame);
    static void DispatchEventDrivenActivations(float currentTime, const CVector& windVec);
    static bool QueryGroundCached(float x, float y, float refZ, float currentTime, bool allowRaycast, GroundSampleResult& outGround, bool& outOverWater, float upOffset = 1.25f);
};
