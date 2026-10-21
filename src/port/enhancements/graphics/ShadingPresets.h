#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "sprite/sprite_enhanced_shading.h"

// User-authored shading presets, stored one JSON file per preset under the
// application's "presets" directory so they can be shared by copying a file.
// Each preset is a full EnhancedShadingProfile, which the bridge converts into
// per-fragment lighting uniforms. The generated per-area table stays the default;
// an active custom preset overrides it for every area.
namespace ShadingPresets {

struct Entry {
    std::string name;
    std::string file; // file name within the presets directory
    EnhancedShadingProfile profile;
};

// Rescans the presets directory and rebuilds the ordered list.
void Refresh();
const std::vector<Entry>& All();
size_t Count();
const Entry* Get(size_t index);
std::string Directory();

// Currently selected custom preset (empty when none). Persisted in a CVar so the
// choice survives restarts and is visible to the lighting bridge.
std::string ActiveName();
void SetActive(const std::string& name);
void ClearActive();
bool GetActiveProfile(EnhancedShadingProfile* out);

// Captures the current global lighting CVars (the sliders in the menu) into a new
// preset and saves it. Returns the new preset's name.
std::string CaptureFromCvars();

bool SaveEntry(size_t index, std::string* error);
bool ExportEntry(size_t index, const std::filesystem::path& path, std::string* error);
bool ImportFile(const std::filesystem::path& path, std::string* error);
bool DeleteEntry(size_t index, std::string* error);
bool MoveEntry(size_t index, int delta, std::string* error);

} // namespace ShadingPresets
