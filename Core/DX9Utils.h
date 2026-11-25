#pragma once
#include "RenderWare.h"
#include "CScene.h"
#include "CCamera.h"
#include <d3d9.h>
#include <cstring>

template <typename T>
inline void SafeReleaseDX9(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

inline IDirect3DDevice9* GetD3D9Device() {
    return reinterpret_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice());
}

struct CameraShaderData {
    float viewProjTransposed[16];
    float camRight[3];
    float camUp[3];
    float camPos[3];
    float nearClip;
    float farClip;
};

// Extracts active camera basis vectors and transposed View*Proj directly from GTA: SA's RwCamera
inline bool ExtractRenderWareCameraData(CameraShaderData& outData) {
    RwCamera* cam = Scene.m_pCamera;
    if (!cam) cam = TheCamera.m_pRwCamera;
    if (!cam) return false;

    RwFrame* frame = RwCameraGetFrame(cam);
    if (!frame) return false;
    RwMatrix* ltm = RwFrameGetLTM(frame);
    if (!ltm) return false;

    float rx = ltm->right.x, ry = ltm->right.y, rz = ltm->right.z;
    float ux = ltm->up.x, uy = ltm->up.y, uz = ltm->up.z;
    float ax = ltm->at.x, ay = ltm->at.y, az = ltm->at.z;
    float px = ltm->pos.x, py = ltm->pos.y, pz = ltm->pos.z;

    outData.camRight[0] = rx; outData.camRight[1] = ry; outData.camRight[2] = rz;
    outData.camUp[0] = ux; outData.camUp[1] = uy; outData.camUp[2] = uz;
    outData.camPos[0] = px; outData.camPos[1] = py; outData.camPos[2] = pz;

    float view[4][4] = {
        { rx, ux, ax, 0.0f },
        { ry, uy, ay, 0.0f },
        { rz, uz, az, 0.0f },
        {
            -(px * rx + py * ry + pz * rz),
            -(px * ux + py * uy + pz * uz),
            -(px * ax + py * ay + pz * az),
            1.0f
        }
    };

    const RwV2d* vw = RwCameraGetViewWindow(cam);
    float zn = RwCameraGetNearClipPlane(cam);
    float zf = RwCameraGetFarClipPlane(cam);
    if (!vw || vw->x <= 0.0001f || vw->y <= 0.0001f || zf <= zn + 0.01f) return false;

    outData.nearClip = zn;
    outData.farClip = zf;

    float invW = 1.0f / vw->x;
    float invH = 1.0f / vw->y;
    float q = zf / (zf - zn);

    float proj[4][4] = {
        { invW, 0.0f, 0.0f,     0.0f },
        { 0.0f, invH, 0.0f,     0.0f },
        { 0.0f, 0.0f, q,        1.0f },
        { 0.0f, 0.0f, -zn * q,  0.0f }
    };

    float vp[4][4];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            vp[r][c] =
                view[r][0] * proj[0][c] +
                view[r][1] * proj[1][c] +
                view[r][2] * proj[2][c] +
                view[r][3] * proj[3][c];
        }
    }

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            outData.viewProjTransposed[r * 4 + c] = vp[c][r];
        }
    }

    return true;
}