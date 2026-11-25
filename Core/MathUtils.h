#pragma once
#include "CVector.h"
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>

inline float RandomFloat(float minVal, float maxVal) {
    return minVal + (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * (maxVal - minVal);
}

inline int RandomInt(int minVal, int maxVal) {
    return minVal + (std::rand() % (maxVal - minVal + 1));
}

inline float ClampFloat(float val, float minVal, float maxVal) {
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

inline CVector LerpVector(const CVector& a, const CVector& b, float t) {
    return CVector(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    );
}

inline CVector VecCross(const CVector& a, const CVector& b) {
    return CVector(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

inline void VecNormalize(CVector& v) {
    float magSq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (magSq > 0.000001f) {
        float invMag = 1.0f / std::sqrt(magSq);
        v.x *= invMag;
        v.y *= invMag;
        v.z *= invMag;
    }
    else {
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = 1.0f;
    }
}

// 1024-entry Sin LUT with linear interpolation for high-frequency particle waves
class FastTrigLUT {
public:
    static constexpr int TABLE_SIZE = 1024;
    static constexpr int TABLE_MASK = TABLE_SIZE - 1;
    static constexpr float TWO_PI = 6.28318530718f;
    static constexpr float HALF_PI = 1.57079632679f;
    static constexpr float RAD_TO_INDEX = static_cast<float>(TABLE_SIZE) / TWO_PI;

    static const float* GetTable() {
        static float table[TABLE_SIZE + 1];
        static bool initialized = false;
        if (!initialized) {
            for (int i = 0; i <= TABLE_SIZE; ++i) {
                table[i] = std::sin((static_cast<float>(i) / static_cast<float>(TABLE_SIZE)) * TWO_PI);
            }
            initialized = true;
        }
        return table;
    }
};

inline float FastSin(float x) {
    const float* tbl = FastTrigLUT::GetTable();
    float fIdx = x * FastTrigLUT::RAD_TO_INDEX;
    int iIdx = static_cast<int>(std::floor(fIdx));
    float frac = fIdx - static_cast<float>(iIdx);
    int idx0 = iIdx & FastTrigLUT::TABLE_MASK;
    return tbl[idx0] + (tbl[idx0 + 1] - tbl[idx0]) * frac;
}

inline float FastCos(float x) {
    return FastSin(x + FastTrigLUT::HALF_PI);
}

inline float FastInvSqrt(float number) {
    if (number <= 1e-6f) return 0.0f;
    float x2 = number * 0.5f;
    float y = number;
    std::uint32_t i;
    std::memcpy(&i, &y, sizeof(i));
    i = 0x5f3759df - (i >> 1);
    std::memcpy(&y, &i, sizeof(y));
    y = y * (1.5f - (x2 * y * y));
    return y;
}

inline void FastVecNormalize(CVector& v) {
    float magSq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (magSq > 1e-6f) {
        float invMag = FastInvSqrt(magSq);
        v.x *= invMag;
        v.y *= invMag;
        v.z *= invMag;
    }
    else {
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = 1.0f;
    }
}

inline uint32_t HashCoords2D(int x, int y, uint32_t seed = 1337u) {
    uint32_t h = seed ^ (static_cast<uint32_t>(x) * 374761393u) ^ (static_cast<uint32_t>(y) * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

inline float HashToFloat01(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}
