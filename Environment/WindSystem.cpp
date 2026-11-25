#include "WindSystem.h"
#include "../Core/MathUtils.h"
#include "CWeather.h"
#include "CClock.h"
#include <windows.h>
#include <cstdio>
#include <cmath>

CVector CWindSystem::vecBaseWindForce = CVector(20.0f, 20.0f, 15.0f);
float CWindSystem::fWindScale = 0.05f;
float CWindSystem::fWindFrequency = 0.25f;
float CWindSystem::fWindThreshold = 0.65f;

float CWindSystem::fCurrentTime = 0.0f;
float CWindSystem::fWindRotationAngle = 0.0f;
CVector CWindSystem::vecActiveWindForce = CVector(20.0f, 20.0f, 15.0f);
CVector CWindSystem::vecWindDirection2D = CVector(0.7071f, 0.7071f, 0.0f);
float CWindSystem::fWindStrength = 1.0f;
float CWindSystem::fCanopyDriftScale = 0.03f;

static float ReadIniFloatLocal(const char* section, const char* key, float defaultVal, const char* filePath) {
    char buf[64] = { 0 };
    char defStr[64] = { 0 };
    std::snprintf(defStr, sizeof(defStr), "%.4f", defaultVal);
    GetPrivateProfileStringA(section, key, defStr, buf, sizeof(buf), filePath);
    return static_cast<float>(std::atof(buf));
}

void CWindSystem::SaveDefaultConfig(void* filePtr) {
    FILE* f = static_cast<FILE*>(filePtr);
    if (!f) return;
    std::fprintf(f, "[WindSimulation]\n");
    std::fprintf(f, "WindForceX=20.0\n");
    std::fprintf(f, "WindForceY=20.0\n");
    std::fprintf(f, "WindForceZ=15.0\n");
    std::fprintf(f, "WindScale=0.05\n");
    std::fprintf(f, "WindFrequency=0.25\n");
    std::fprintf(f, "WindThreshold=0.65\n\n");
}

void CWindSystem::LoadConfig(const char* iniPath) {
    vecBaseWindForce.x = ReadIniFloatLocal("WindSimulation", "WindForceX", 20.0f, iniPath);
    vecBaseWindForce.y = ReadIniFloatLocal("WindSimulation", "WindForceY", 20.0f, iniPath);
    vecBaseWindForce.z = ReadIniFloatLocal("WindSimulation", "WindForceZ", 15.0f, iniPath);
    fWindScale = ReadIniFloatLocal("WindSimulation", "WindScale", 0.05f, iniPath);
    fWindFrequency = ReadIniFloatLocal("WindSimulation", "WindFrequency", 0.25f, iniPath);
    fWindThreshold = ClampFloat(ReadIniFloatLocal("WindSimulation", "WindThreshold", 0.65f, iniPath), 0.0f, 0.99f);
}

static void ComputeWeatherAndTimeMultipliers(float& outHorizMult, float& outVertMult, float& outCanopyDriftScale) {
    short weatherA = CWeather::NewWeatherType;
    short weatherB = CWeather::OldWeatherType;
    float interp = CWeather::InterpolationValue;

    auto evalWeather = [](short w, float& hM, float& vM, float& drift) {
        switch (w) {
        case 8:  // WEATHER_RAINY_COUNTRYSIDE
        case 16: // WEATHER_RAINY_SF
            hM = 1.65f; vM = 1.15f; drift = 0.075f; break;
        case 19: // WEATHER_SANDSTORM_DESERT
            hM = 2.10f; vM = 1.30f; drift = 0.095f; break;
        case 4:  // WEATHER_CLOUDY_LA
        case 7:  // WEATHER_CLOUDY_COUNTRYSIDE
        case 9:  // WEATHER_FOGGY_SF
        case 12: // WEATHER_CLOUDY_SF
        case 18: // WEATHER_CLOUDY_VEGAS
            hM = 1.25f; vM = 1.02f; drift = 0.045f; break;
        case 0:  // WEATHER_EXTRASUNNY_LA
        case 5:  // WEATHER_EXTRASUNNY_SF
        case 10: // WEATHER_EXTRASUNNY_COUNTRYSIDE
        case 17: // WEATHER_EXTRASUNNY_VEGAS
            hM = 0.65f; vM = 0.80f; drift = 0.018f; break;
        default:
            hM = 1.00f; vM = 0.95f; drift = 0.030f; break;
        }
        };

    float hA = 1.0f, vA = 1.0f, dA = 0.03f;
    float hB = 1.0f, vB = 1.0f, dB = 0.03f;
    evalWeather(weatherA, hA, vA, dA);
    evalWeather(weatherB, hB, vB, dB);

    outHorizMult = hB + (hA - hB) * interp;
    outVertMult = vB + (vA - vB) * interp;
    outCanopyDriftScale = dB + (dA - dB) * interp;

    int hour = CClock::ms_nGameClockHours;
    if (hour >= 13 && hour <= 17) {
        outHorizMult *= 1.18f;
        outCanopyDriftScale *= 1.15f;
    }
    else if (hour >= 1 && hour <= 6) {
        outHorizMult *= 0.72f;
        outVertMult *= 0.78f;
        outCanopyDriftScale *= 0.70f;
    }
}

void CWindSystem::Update(float currentTimeSec) {
    fCurrentTime = currentTimeSec;

    float windHorizMult = 1.0f, windVertMult = 1.0f;
    ComputeWeatherAndTimeMultipliers(windHorizMult, windVertMult, fCanopyDriftScale);

    // Dynamically rotate horizontal wind direction over time so gusts swirl naturally
    fWindRotationAngle = FastSin(currentTimeSec * 0.07f) * 1.45f + FastSin(currentTimeSec * 0.023f) * 2.10f;
    float cosW = FastSin(fWindRotationAngle + 1.5708f);
    float sinW = FastSin(fWindRotationAngle);

    float baseWx = vecBaseWindForce.x * windHorizMult;
    float baseWy = vecBaseWindForce.y * windHorizMult;

    vecActiveWindForce.x = baseWx * cosW - baseWy * sinW;
    vecActiveWindForce.y = baseWx * sinW + baseWy * cosW;
    vecActiveWindForce.z = vecBaseWindForce.z * windVertMult;

    float horizMagSq = vecActiveWindForce.x * vecActiveWindForce.x + vecActiveWindForce.y * vecActiveWindForce.y;
    if (horizMagSq > 0.0001f) {
        float horizMag = std::sqrt(horizMagSq);
        vecWindDirection2D.x = vecActiveWindForce.x / horizMag;
        vecWindDirection2D.y = vecActiveWindForce.y / horizMag;
        vecWindDirection2D.z = 0.0f;
        // Normalized wind strength relative to default ~28.28 baseline
        fWindStrength = ClampFloat(horizMag / 28.28f, 0.25f, 3.0f);
    }
    else {
        vecWindDirection2D = CVector(1.0f, 0.0f, 0.0f);
        fWindStrength = 0.25f;
    }
}

float CWindSystem::SampleSpatialGust(float x, float y) {
    float nx = x * fWindScale + fCurrentTime * fWindFrequency;
    float ny = y * fWindScale + fCurrentTime * fWindFrequency * 0.5f;
    return (FastSin(nx) * FastSin(ny + 1.5708f) + FastSin(nx * 0.6f + ny * 0.8f)) * 0.25f + 0.5f;
}