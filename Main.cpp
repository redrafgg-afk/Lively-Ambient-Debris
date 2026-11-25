#include "plugin.h"
#include "CTimer.h"
#include "Environment/WindSystem.h"
#include "Features/Leaf/LeafSystem.h"

#include <windows.h>
#include <string>
#include <cstdio>

using namespace plugin;

namespace {
    std::string g_IniPath;

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
    }

    void UpdateAllSystems() {
        if (!CLeafSystem::bInitialized) {
            InitAllSystems();
        }

        float baseDt = CTimer::ms_fTimeStep / 50.0f;
        if (baseDt <= 0.0f || baseDt > 0.2f) baseDt = 0.016f;

        float currentTime = static_cast<float>(CTimer::m_snTimeInMilliseconds) * 0.001f;

        CWindSystem::Update(currentTime);
        CLeafSystem::Update(baseDt, currentTime);
    }

    void RenderAllSystems() {
        CLeafSystem::Render();
    }
}

class LeafPhysicsPlugin {
public:
    LeafPhysicsPlugin() {
        Events::initGameEvent += []() {
            InitAllSystems();
            };

        Events::gameProcessEvent += []() {
            UpdateAllSystems();
            };

        Events::shutdownRwEvent += []() {
            CLeafSystem::Shutdown();
            };

        patch::RedirectCall(0x53E175, RenderAllSystems);
    }
} g_LeafPhysicsPlugin;