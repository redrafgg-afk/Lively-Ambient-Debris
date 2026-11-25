// LeafBatchRenderer.h
#pragma once
#include "RenderWare.h"
#include "CVector.h"
#include <cstdint>

class CLeafBatchRenderer {
public:
    static constexpr int MAX_BATCH_QUADS = 1024;
    static constexpr int MAX_BATCH_VERTICES = MAX_BATCH_QUADS * 4;
    static constexpr int MAX_BATCH_INDICES = MAX_BATCH_QUADS * 6;

    static void Init();
    static void BeginBatch(RwRaster* raster);
    static void PushQuad(
        const CVector& v0,
        const CVector& v1,
        const CVector& v2,
        const CVector& v3,
        const RwRGBA& color,
        float uMin = 0.0f,
        float vMin = 0.0f,
        float uMax = 1.0f,
        float vMax = 1.0f
    );
    static void Flush();
    static void EndBatch();

private:
    alignas(64) static RwIm3DVertex s_Vertices[MAX_BATCH_VERTICES];
    alignas(64) static RwImVertexIndex s_Indices[MAX_BATCH_INDICES];
    static int s_QuadCount;
    static RwRaster* s_CurrentRaster;
    static bool s_IndicesInitialized;
};
