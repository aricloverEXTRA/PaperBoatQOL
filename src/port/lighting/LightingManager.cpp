#include "lighting/LightingManager.h"

#include <libultraship/libultraship.h> // Ship::Context and ResourceManager
#include <yaml-cpp/yaml.h>
#include <cstring>
#include <sstream>
#include <iostream>

// Renderer API (available in PaperBoat): register/unregister post passes and set custom uniforms.
// These symbols are provided by the fast/prism renderer. Include their header if available;
// fallback to extern declarations if not exposed via header in build.
#include "fast/backends/gfx_rendering_api.h"

#ifndef GFX_API_DECLARED
extern "C" {
int gfx_register_post_pass(const char* path);      // returns handle >=0 on success, negative on failure
void gfx_unregister_post_pass(int handle);
void gfx_set_custom_uniform(uint8_t idx, const float values[4]);
}
#endif

using namespace Paperboat::Lighting;

Manager& Manager::Get() {
    static Manager inst;
    return inst;
}

void Manager::Init() {
    std::lock_guard<std::mutex> lock(mtx);
    // Load profiles; safe to call multiple times.
    LoadProfilesFromResource();
}

void Manager::LoadProfilesFromResource() {
    profiles.clear();

    // Try to load "shaders/lighting_profiles.yml" from the ResourceManager (mods and port shaders).
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx) {
        Ship::DebugLog("LightingManager: No Ship::Context available; skipping lighting profiles load.");
        return;
    }

    auto resourceMgr = ctx->GetResourceManager();
    if (!resourceMgr) {
        Ship::DebugLog("LightingManager: No ResourceManager; skipping lighting profiles load.");
        return;
    }

    // Candidate paths (allow mods to override the port file)
    const std::vector<std::string> candidates = {
        "shaders/lighting_profiles.yml",
        "port/shaders/lighting_profiles.yml",
        "lighting_profiles.yml"
    };

    bool anyLoaded = false;
    for (const auto& path : candidates) {
        try {
            auto res = resourceMgr->LoadResource(path, /*loadExact=*/true);
            if (!res) continue;
            const void* ptr = resourceMgr->GetResourceRawPointer(res);
            size_t size = res->GetPointerSize();
            if (!ptr || size == 0) continue;

            std::string yamlText(reinterpret_cast<const char*>(ptr), size);

            YAML::Node root = YAML::Load(yamlText);
            if (!root) continue;

            // Expect top-level key "profiles"
            YAML::Node plist = root["profiles"] ? root["profiles"] : root;
            if (!plist || !plist.IsSequence()) {
                Ship::DebugLog("LightingManager: lighting_profiles.yml did not contain a 'profiles' sequence; skipping %s", path.c_str());
                continue;
            }

            for (const auto& node : plist) {
                Profile p;
                if (node["name"]) p.name = node["name"].as<std::string>();
                if (node["area_id"]) p.areaID = node["area_id"].as<int>();
                if (node["map_id"])  p.mapID  = node["map_id"].as<int>();

                if (node["post_passes"] && node["post_passes"].IsSequence()) {
                    for (const auto& pp : node["post_passes"]) {
                        try {
                            p.postPasses.push_back(pp.as<std::string>());
                        } catch (...) { /* skip invalid entry */ }
                    }
                }

                if (node["uniforms"] && node["uniforms"].IsMap()) {
                    for (auto it = node["uniforms"].begin(); it != node["uniforms"].end(); ++it) {
                        std::string key = it->first.as<std::string>();
                        // Key form: custom_u2 or uCustom2 or u2 -> extract digits
                        uint8_t idx = 0xFF;
                        size_t pos = key.find_first_of("0123456789");
                        if (pos != std::string::npos) {
                            try {
                                int val = std::stoi(key.substr(pos));
                                if (val >= 0 && val <= 15) idx = static_cast<uint8_t>(val);
                            } catch (...) { idx = 0xFF; }
                        }
                        if (idx == 0xFF) continue;

                        std::array<float,4> vec = {0,0,0,0};
                        const YAML::Node& vn = it->second;
                        if (vn.IsSequence()) {
                            for (size_t i = 0; i < 4 && i < vn.size(); ++i) {
                                try { vec[i] = vn[i].as<float>(); } catch(...) { vec[i] = 0.0f; }
                            }
                        } else {
                            // scalar -> broadcast
                            try { vec[0] = vn.as<float>(); } catch(...) { vec[0] = 0.0f; }
                        }
                        p.customUniforms[idx] = vec;
                    }
                }

                if (node["blend"] && node["blend"]["duration"]) {
                    try { p.blendDuration = node["blend"]["duration"].as<float>(); } catch(...) { p.blendDuration = 0.0f; }
                }

                profiles.push_back(std::move(p));
            }

            anyLoaded = true;
            Ship::DebugLog("LightingManager: Loaded lighting profiles from %s", path.c_str());
            break; // stop at first successful candidate
        } catch (const YAML::Exception& e) {
            Ship::DebugLog("LightingManager: YAML parse error for lighting profiles %s: %s", path.c_str(), e.what());
            continue;
        } catch (...) {
            Ship::DebugLog("LightingManager: Unknown error while loading lighting profiles %s", path.c_str());
            continue;
        }
    }

    if (!anyLoaded) {
        Ship::DebugLog("LightingManager: No lighting_profiles.yml found in resources (mods/port).");
    }
}

const Manager::Profile* Manager::FindBestProfile(int areaID, int mapID) const {
    // Priority: exact (area+map) -> area-only -> default(profile with areaID=-1/mapID=-1)
    const Profile* fallback = nullptr;
    for (const auto& p : profiles) {
        if (p.areaID == areaID && p.mapID == mapID) return &p;
    }
    for (const auto& p : profiles) {
        if (p.areaID == areaID && p.mapID == -1) return &p;
    }
    for (const auto& p : profiles) {
        if (p.areaID == -1 && p.mapID == -1) { fallback = &p; break; }
    }
    return fallback;
}

void Manager::OnMapChange(int areaID, int mapID) {
    std::lock_guard<std::mutex> lock(mtx);
    // Reload profiles if needed (developer convenience; cheap enough)
    // Note: loading every change is okay given YAML parsing overhead is low for small files.
    LoadProfilesFromResource();

    const Profile* chosen = FindBestProfile(areaID, mapID);
    if (!chosen) {
        // no profile found: clear active
        ClearActiveProfile();
        return;
    }

    if (activeProfileName == chosen->name) {
        // already applied
        return;
    }

    ApplyProfile(*chosen);
}

void Manager::ApplyProfile(const Profile& p) {
    // Clear existing passes/uniforms
    ClearActiveProfile();

    // Register post passes (order matters)
    for (const auto& passPath : p.postPasses) {
        // defensive: path must be non-empty
        if (passPath.empty()) continue;

        int handle = -1;
        try {
            handle = gfx_register_post_pass(passPath.c_str());
        } catch (...) {
            handle = -1;
        }
        if (handle >= 0) {
            activePostPassHandles.push_back(handle);
        } else {
            // registration failed: log and continue
            Ship::DebugLog("LightingManager: gfx_register_post_pass failed for %s", passPath.c_str());
        }
    }

    // Apply uniforms (immediate set)
    for (const auto& kv : p.customUniforms) {
        uint8_t idx = kv.first;
        const auto& v = kv.second;
        // Defensive: engine reserves 0-15; 0-1 may be built-ins — allow but warn.
        if (idx > 15) continue;
        float arr[4] = { v[0], v[1], v[2], v[3] };
        try {
            gfx_set_custom_uniform(idx, arr);
        } catch (...) {
            Ship::DebugLog("LightingManager: gfx_set_custom_uniform failed for idx %u", idx);
        }
    }

    activeProfileName = p.name;
    Ship::DebugLog("LightingManager: Applied profile '%s' (area=%d map=%d)", p.name.c_str(), p.areaID, p.mapID);
}

void Manager::ClearActiveProfile() {
    // Unregister post passes in reverse order
    for (int h : activePostPassHandles) {
        if (h >= 0) {
            try { gfx_unregister_post_pass(h); } catch(...) {}
        }
    }
    activePostPassHandles.clear();
    activeProfileName.clear();
}

bool Manager::ApplyProfileByName(const std::string& name) {
    std::lock_guard<std::mutex> lock(mtx);
    for (const auto& p : profiles) {
        if (p.name == name) {
            ApplyProfile(p);
            return true;
        }
    }
    return false;
}

/////////////////////////
// C API wrappers
extern "C" {
void LightingManager_Init(void) {
    Manager::Get().Init();
}
void LightingManager_HandleAreaChange(int areaID, int mapID) {
    Manager::Get().OnMapChange(areaID, mapID);
}
int LightingManager_ApplyProfileByName(const char* name) {
    if (!name) return 0;
    return Manager::Get().ApplyProfileByName(name) ? 1 : 0;
}
} // extern "C"
