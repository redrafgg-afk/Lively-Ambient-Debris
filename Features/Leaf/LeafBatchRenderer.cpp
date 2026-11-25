// LeafBatchRenderer.cpp
#include "LeafBatchRenderer.h"

alignas(64) RwIm3DVertex CLeafBatchRenderer::s_Vertices[MAX_BATCH_VERTICES];
alignas(64) RwImVertexIndex CLeafBatchRenderer::s_Indices[MAX_BATCH_INDICES];
int CLeafBatchRenderer::s_QuadCount = 0;
RwRaster* CLeafBatchRenderer::s_CurrentRaster = nullptr;
bool CLeafBatchRenderer::s_IndicesInitialized = false;

void CLeafBatchRenderer::Init() {
    if (s_IndicesInitialized) return;

    for (int i = 0; i < MAX_BATCH_QUADS; ++i) {
        RwImVertexIndex baseVert = static_cast<RwImVertexIndex>(i * 4);
        int baseIdx = i * 6;

        s_Indices[baseIdx + 0] = baseVert + 0;
        s_Indices[baseIdx + 1] = baseVert + 1;
        s_Indices[baseIdx + 2] = baseVert + 2;
        s_Indices[baseIdx + 3] = baseVert + 0;
        s_Indices[baseIdx + 4] = baseVert + 2;
        s_Indices[baseIdx + 5] = baseVert + 3;
    }

    s_IndicesInitialized = true;
}

void CLeafBatchRenderer::BeginBatch(RwRaster* raster) {
    if (!s_IndicesInitialized) Init();

    if (s_CurrentRaster != raster) {
        Flush();
        s_CurrentRaster = raster;
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, reinterpret_cast<void*>(raster));
    }
}

void CLeafBatchRenderer::PushQuad(
    const CVector& v0,
    const CVector& v1,
    const CVector& v2,
    const CVector& v3,
    const RwRGBA& color,
    float uMin,
    float vMin,
    float uMax,
    float vMax
) {
    if (s_QuadCount >= MAX_BATCH_QUADS) {
        Flush();
    }

    RwIm3DVertex* v = &s_Vertices[s_QuadCount * 4];

    RwIm3DVertexSetPos(&v[0], v0.x, v0.y, v0.z);
    RwIm3DVertexSetRGBA(&v[0], color.red, color.green, color.blue, color.alpha);
    RwIm3DVertexSetU(&v[0], uMin);
    RwIm3DVertexSetV(&v[0], vMin);

    RwIm3DVertexSetPos(&v[1], v1.x, v1.y, v1.z);
    RwIm3DVertexSetRGBA(&v[1], color.red, color.green, color.blue, color.alpha);
    RwIm3DVertexSetU(&v[1], uMax);
    RwIm3DVertexSetV(&v[1], vMin);

    RwIm3DVertexSetPos(&v[2], v2.x, v2.y, v2.z);
    RwIm3DVertexSetRGBA(&v[2], color.red, color.green, color.blue, color.alpha);
    RwIm3DVertexSetU(&v[2], uMax);
    RwIm3DVertexSetV(&v[2], vMax);

    RwIm3DVertexSetPos(&v[3], v3.x, v3.y, v3.z);
    RwIm3DVertexSetRGBA(&v[3], color.red, color.green, color.blue, color.alpha);
    RwIm3DVertexSetU(&v[3], uMin);
    RwIm3DVertexSetV(&v[3], vMax);

    ++s_QuadCount;
}

void CLeafBatchRenderer::Flush() {
    if (s_QuadCount <= 0) return;

    int vertCount = s_QuadCount * 4;
    int idxCount = s_QuadCount * 6;

    if (RwIm3DTransform(s_Vertices, vertCount, nullptr, rwIM3D_VERTEXUV | rwIM3D_VERTEXRGBA)) {
        RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_Indices, idxCount);
        RwIm3DEnd();
    }

    s_QuadCount = 0;
}

void CLeafBatchRenderer::EndBatch() {
    Flush();
    s_CurrentRaster = nullptr;
}
