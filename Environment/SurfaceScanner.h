#pragma once
#include "CVector.h"

enum class eBiomeZone : unsigned char {
    URBAN = 0,
    FOREST_RURAL = 1,
    DESERT = 2
};

struct GroundSampleResult {
    bool bValid;
    float fGroundZ;
    CVector vecNormal;
    unsigned char nSurfaceType;
    float fShade01;
};

class CSurfaceScanner {
public:
    static eBiomeZone DetectBiome(const CVector& pos);
    static bool IsNaturalGroundSurface(unsigned char surf);
    static bool IsGrassGrowableSurface(unsigned char surf, eBiomeZone biome, float& outDensityWeight);

    static bool SampleGround(
        float x,
        float y,
        float refZ,
        float upOffset,
        float downOffset,
        GroundSampleResult& outResult,
        bool checkBuildings = true,
        bool checkObjects = false
    );
};