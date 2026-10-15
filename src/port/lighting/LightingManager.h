#pragma once

#ifdef __cplusplus
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <mutex>

namespace Paperboat {
namespace Lighting {

struct Profile {
    std::string name;
    int areaID = -1;          // -1 = wildcard
    int mapID  = -1;          // -1 = wildcard
    std::vector<std::string> postPasses; // paths inside o2r / mods (e.g. "shaders/post/my.shader")
    std::unordered_map<uint8_t, std::array<float,4>> customUniforms; // register index -> vec4
    float blendDuration = 0.0f; // seconds to blend from previous profile (not implemented as smooth lerp here, placeholder)
};

class Manager {
  public:
    static Manager& Get();

    // Load lighting_profiles.yml from resource manager (mods/port override possible).
    // Safe to call multiple times (reloads).
    void Init();

    // Called when a map/area is loaded. areaID and mapID are engine ints.
    void OnMapChange(int areaID, int mapID);

    // For tests / CLI: force-apply a profile by name (returns true if applied)
    bool ApplyProfileByName(const std::string& name);

  private:
    Manager() = default;
    void LoadProfilesFromResource();    // internal loader
    const Profile* FindBestProfile(int areaID, int mapID) const;
    void ApplyProfile(const Profile& p);
    void ClearActiveProfile();

    // defensive state
    std::mutex mtx;
    std::vector<Profile> profiles;
    std::vector<int> activePostPassHandles;
    std::string activeProfileName;
};

} // namespace Lighting
} // namespace Paperboat

extern "C" {
#endif

// C API (can be used from C files like state_map_transitions.c)
void LightingManager_Init(void);
void LightingManager_HandleAreaChange(int areaID, int mapID);
int LightingManager_ApplyProfileByName(const char* name); // returns 1 on success, 0 otherwise

#ifdef __cplusplus
} // extern "C"
#endif
