#include "SurfaceScanner.h"
#include "../Core/MathUtils.h"
#include "CWorld.h"
#include "CColPoint.h"
#include "CClock.h"

eBiomeZone CSurfaceScanner::DetectBiome(const CVector& pos) {
    if (pos.y > 550.0f && pos.x > -1400.0f && pos.x < 850.0f) {
        return eBiomeZone::DESERT;
    }
    if (pos.x > 150.0f && pos.y < -400.0f) return eBiomeZone::URBAN;
    if (pos.x < -1350.0f && pos.y > -1100.0f && pos.y < 1500.0f) return eBiomeZone::URBAN;
    if (pos.x > 850.0f && pos.y > 550.0f) return eBiomeZone::URBAN;

    return eBiomeZone::FOREST_RURAL;
}

bool CSurfaceScanner::IsNaturalGroundSurface(unsigned char surf) {
    if (surf >= 9 && surf <= 35) return true;
    if (surf >= 80 && surf <= 130) return true;
    return false;
}

bool CSurfaceScanner::IsGrassGrowableSurface(unsigned char surf, eBiomeZone biome, float& outDensityWeight) {
    outDensityWeight = 0.0f;

    // GTA:SA Surface IDs (surfinfo.dat):
    // 9..17  : Grass (short, meadow, lush, dry, weeds, flowers, lawn)
    // 18..23 : Dirt, mud, forest floor, bush bed
    // 24..27 : Sand, beach, desert scrub
    // 80..125: Countryside / Mount Chiliad / Flint County natural terrain materials
    if (surf >= 9 && surf <= 17) {
        outDensityWeight = (biome == eBiomeZone::DESERT) ? 0.35f : 1.0f;
        return true;
    }
    if (surf >= 18 && surf <= 23) {
        outDensityWeight = (biome == eBiomeZone::DESERT) ? 0.25f : 0.72f;
        return true;
    }
    if (surf >= 80 && surf <= 125) {
        // Exclude rock cliff IDs (typically 28..33 or specific steep rock IDs)
        if (surf == 84 || surf == 85 || surf == 96) return false;
        outDensityWeight = (biome == eBiomeZone::DESERT) ? 0.20f : 0.85f;
        return true;
    }
    if (biome == eBiomeZone::DESERT && (surf >= 24 && surf <= 27)) {
        // Sparse desert scrub tufts
        outDensityWeight = 0.14f;
        return true;
    }

    return false;
}

bool CSurfaceScanner::SampleGround(
    float x,
    float y,
    float refZ,
    float upOffset,
    float downOffset,
    GroundSampleResult& outResult,
    bool checkBuildings,
    bool checkObjects
) {
    outResult.bValid = false;
    CColPoint colPoint;
    CEntity* hitEntity = nullptr;

    if (CWorld::ProcessVerticalLine(
        CVector(x, y, refZ + upOffset),
        refZ - downOffset,
        colPoint,
        hitEntity,
        checkBuildings,
        false, // vehicles
        false, // peds
        checkObjects,
        false,
        false,
        nullptr
    )) {
        outResult.bValid = true;
        outResult.fGroundZ = colPoint.m_vecPoint.z;
        outResult.vecNormal = colPoint.m_vecNormal;
        outResult.nSurfaceType = colPoint.m_nSurfaceTypeB;

        float dayLight = static_cast<float>(colPoint.m_nLightingB.day) / 15.0f;
        float nightLight = static_cast<float>(colPoint.m_nLightingB.night) / 15.0f;

        float hour = static_cast<float>(CClock::ms_nGameClockHours) + static_cast<float>(CClock::ms_nGameClockMinutes) / 60.0f;
        float dayFactor = 1.0f;
        if (hour < 5.0f || hour >= 21.0f) dayFactor = 0.0f;
        else if (hour < 7.0f) dayFactor = (hour - 5.0f) * 0.5f;
        else if (hour > 19.0f) dayFactor = (21.0f - hour) * 0.5f;

        // Keep the daytime minimum shade at 0.62f so that it doesn't get too dark in shadowed areas
        float rawShade = dayLight * dayFactor + nightLight * (1.0f - dayFactor * 0.55f);
        float minShade = 0.32f + 0.30f * dayFactor;
        outResult.fShade01 = ClampFloat(rawShade, minShade, 1.0f);
        return true;
    }

    return false;
}