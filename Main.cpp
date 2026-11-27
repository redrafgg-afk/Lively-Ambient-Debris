#include "plugin.h"
#include "CTimer.h"
#include "CMenuManager.h"
#include "RenderWare.h"
#include "Environment/WindSystem.h"
#include "Features/Leaf/LeafSystem.h"

#include <windows.h>
#include <string>
#include <cstdio>
#include <cstdint>

using namespace plugin;

namespace {
    std::string g_IniPath;

    // 0x53E175 = call CSkidmarks::Render (0x720640) di akhir RenderScene
    // Dipanggil setiap frame di Single Player maupun SA-MP, dan tidak aktif di Pause Menu
    constexpr uintptr_t kRenderHookAddr = 0x53E175;
    using RenderFn = void(__cdecl*)();

    RenderFn g_OrigCallTarget = nullptr;
    void* g_OrigJmpTarget = nullptr;
    bool g_WasNopped = false;
    uint32_t g_LastUpdatedFrame = 0xFFFFFFFFu;

    std::string GetModuleDirectory() {
        char path[MAX_PATH] = { 0 };
        HMODULE hMod = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetModuleDirectory),
            &hMod
        );
        if (hMod && GetModuleFileNameA(hMod, path, MAX_PATH)) {
            std::string fullPath(path);
            size_t pos = fullPath.find_last_of("\\/");
            if (pos != std::string::npos) {
                return fullPath.substr(0, pos + 1);
            }
        }
        return "";
    }

    void EnsureConfigExists() {
        std::string dir = GetModuleDirectory();
        g_IniPath = dir + "LivelyAmbientDebris.ini";

        if (GetFileAttributesA(g_IniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (GetFileAttributesA("LivelyAmbientDebris.ini") != INVALID_FILE_ATTRIBUTES) {
                g_IniPath = "LivelyAmbientDebris.ini";
            }
            else {
                FILE* f = nullptr;
                if (fopen_s(&f, g_IniPath.c_str(), "w") == 0 && f) {
                    CLeafSystem::SaveDefaultConfig(f);
                    CWindSystem::SaveDefaultConfig(f);
                    std::fclose(f);
                }
            }
        }
    }

    void InitAllSystems() {
        EnsureConfigExists();
        CWindSystem::LoadConfig(g_IniPath.c_str());
        CLeafSystem::Init(g_IniPath.c_str());
        g_LastUpdatedFrame = 0xFFFFFFFFu;
    }

    void UpdateAllSystems() {
        if (!CLeafSystem::bInitialized) {
            InitAllSystems();
        }

        if (FrontEndMenuManager.m_bMenuActive) {
            return;
        }

        // Pastikan simulasi hanya berjalan 1 kali per frame (baik dipicu gameProcessEvent maupun render hook SA-MP)
        uint32_t currentFrame = CTimer::m_FrameCounter;
        if (currentFrame == g_LastUpdatedFrame) {
            return;
        }
        g_LastUpdatedFrame = currentFrame;

        float baseDt = CTimer::ms_fTimeStep / 50.0f;
        if (baseDt <= 0.0f || baseDt > 0.2f) baseDt = 0.016f;

        float currentTime = static_cast<float>(CTimer::m_snTimeInMilliseconds) * 0.001f;

        CWindSystem::Update(currentTime);
        CLeafSystem::Update(baseDt, currentTime);
    }

    void __cdecl UpdateAndRenderAllSystems() {
        if (!CLeafSystem::bInitialized || FrontEndMenuManager.m_bMenuActive) {
            return;
        }

        UpdateAllSystems();
        CLeafSystem::Render();

        // Reset RenderWare state agar tekstur partikel tidak bocor ke nametag / chat SA-MP
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    }

    void __cdecl HookedRenderCall() {
        static bool s_InHook = false;
        if (s_InHook) {
            if (g_OrigCallTarget) g_OrigCallTarget();
            return;
        }

        s_InHook = true;
        UpdateAndRenderAllSystems();
        if (g_OrigCallTarget) {
            g_OrigCallTarget();
        }
        s_InHook = false;
    }

    __declspec(naked) void HookedRenderJmp() {
        __asm {
            pushad
            pushfd
            call UpdateAndRenderAllSystems
            popfd
            popad
            jmp dword ptr[g_OrigJmpTarget]
        }
    }

    bool Is5ByteNop(uintptr_t addr) {
        for (uintptr_t i = 0; i < 5; ++i) {
            if (patch::GetUChar(addr + i) != 0x90) {
                return false;
            }
        }
        return true;
    }

    void EnsureRenderHook() {
        uint8_t opcode = patch::GetUChar(kRenderHookAddr);

        if (opcode == 0xE8) {
            int32_t relOffset = patch::GetInt(kRenderHookAddr + 1);
            uintptr_t currentTarget = kRenderHookAddr + 5 + static_cast<uintptr_t>(relOffset);
            uintptr_t ourCall = reinterpret_cast<uintptr_t>(&HookedRenderCall);

            if (currentTarget != ourCall) {
                g_OrigJmpTarget = nullptr;
                g_WasNopped = false;
                g_OrigCallTarget = reinterpret_cast<RenderFn>(currentTarget);
                patch::RedirectCall(kRenderHookAddr, HookedRenderCall);
            }
        }
        else if (opcode == 0x90 && Is5ByteNop(kRenderHookAddr)) {
            g_OrigJmpTarget = nullptr;
            g_OrigCallTarget = nullptr;
            g_WasNopped = true;
            patch::RedirectCall(kRenderHookAddr, HookedRenderCall);
        }
        else if (opcode == 0xE9) {
            int32_t relOffset = patch::GetInt(kRenderHookAddr + 1);
            uintptr_t currentTarget = kRenderHookAddr + 5 + static_cast<uintptr_t>(relOffset);
            uintptr_t ourJmp = reinterpret_cast<uintptr_t>(&HookedRenderJmp);

            if (currentTarget != ourJmp) {
                g_OrigCallTarget = nullptr;
                g_WasNopped = false;
                g_OrigJmpTarget = reinterpret_cast<void*>(currentTarget);
                patch::RedirectJump(kRenderHookAddr, HookedRenderJmp);
            }
        }
    }

    void RemoveRenderHook() {
        uint8_t opcode = patch::GetUChar(kRenderHookAddr);
        int32_t relOffset = patch::GetInt(kRenderHookAddr + 1);
        uintptr_t currentTarget = kRenderHookAddr + 5 + static_cast<uintptr_t>(relOffset);

        if (opcode == 0xE8 && currentTarget == reinterpret_cast<uintptr_t>(&HookedRenderCall)) {
            if (g_WasNopped) {
                for (uintptr_t i = 0; i < 5; ++i) {
                    patch::SetUChar(kRenderHookAddr + i, 0x90);
                }
            }
            else if (g_OrigCallTarget) {
                patch::RedirectCall(kRenderHookAddr, g_OrigCallTarget);
            }
        }
        else if (opcode == 0xE9 && g_OrigJmpTarget && currentTarget == reinterpret_cast<uintptr_t>(&HookedRenderJmp)) {
            patch::RedirectJump(kRenderHookAddr, g_OrigJmpTarget);
        }

        g_OrigCallTarget = nullptr;
        g_OrigJmpTarget = nullptr;
        g_WasNopped = false;
    }
}

class LeafPhysicsPlugin {
public:
    LeafPhysicsPlugin() {
        Events::initGameEvent += []() {
            InitAllSystems();
            EnsureRenderHook();
            };

        Events::gameProcessEvent += []() {
            UpdateAllSystems();
            EnsureRenderHook();
            };

        Events::shutdownRwEvent += []() {
            RemoveRenderHook();
            CLeafSystem::Shutdown();
            g_LastUpdatedFrame = 0xFFFFFFFFu;
            };

        EnsureRenderHook();
    }
} g_LeafPhysicsPlugin;