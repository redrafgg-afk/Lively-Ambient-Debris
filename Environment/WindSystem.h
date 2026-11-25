#pragma once
#include "CVector.h"

class CWindSystem {
public:
    // Base parameters loaded from INI
    static CVector vecBaseWindForce;
    static float fWindScale;
    static float fWindFrequency;
    static float fWindThreshold;

    // Live per-frame state shared across LeafSystem (CPU) and GrassRenderer (GPU)
    static float fCurrentTime;
    static float fWindRotationAngle;
    static CVector vecActiveWindForce;
    static CVector vecWindDirection2D;
    static float fWindStrength;
    static float fCanopyDriftScale;

    static void LoadConfig(const char* iniPath);
    static void SaveDefaultConfig(void* filePtr);
    static void Update(float currentTimeSec);

    // Evaluates spatial gust intensity [0..1] at world (x, y)
    static float SampleSpatialGust(float x, float y);
};