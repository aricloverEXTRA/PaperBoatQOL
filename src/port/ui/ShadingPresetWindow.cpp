#include "ShadingPresetWindow.h"
#include "port/ui/UIWidgets.hpp"
#include "port/ui/Notification.h"
#include "port/ui/cvar_prefixes.h"
#include "port/FilePicker.h"
#include "port/enhancements/graphics/ShadingPresets.h"

#include <imgui.h>
#include <ship/Context.h>
#include <spdlog/fmt/fmt.h>

#include <filesystem>
#include <optional>
#include <string>

#pragma push_macro("End")
#undef End

#define CVAR_NAME_POPOUT_SHADING_PRESETS CVAR_WINDOW("ShadingPresetWindow")
#define CVAR_SHOW_POPOUT_SHADING_PRESETS CVarGetInteger(CVAR_NAME_POPOUT_SHADING_PRESETS, 0)

static ImGuiWindowFlags sWindowFlags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
static ImVec4 sWindowBG = ImVec4 { 0, 0, 0, 0.5f };
static const ImVec4 sDimColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

static void NotifyResult(const char* prefix, bool ok, const std::string& message) {
    Notification::Emit({
        .prefix = prefix,
        .message = message,
        .messageColor = ok ? ImVec4(0.7f, 0.7f, 0.7f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
    });
}

static int IndexOfName(const std::string& name) {
    const std::vector<ShadingPresets::Entry>& all = ShadingPresets::All();
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].name == name) {
            return (int) i;
        }
    }
    return -1;
}

static void ExportWithPicker(const std::string& name) {
    Ship::FileBrowserRequest request;
    request.Title = "Export shading preset";
    request.StartDir = Ship::Context::GetAppDirectoryPath();
    request.Filters = { { "Shading Preset", { "*.json" } } };
    request.Save = true;
    request.DefaultName = name + ".json";
    Paperboat::PickFile(std::move(request), [name](std::optional<std::filesystem::path> path) {
        if (!path) {
            return;
        }
        int index = IndexOfName(name);
        if (index < 0) {
            NotifyResult("Shading Presets", false, "Preset no longer exists");
            return;
        }
        std::string error;
        bool ok = ShadingPresets::ExportEntry((size_t) index, *path, &error);
        NotifyResult("Shading Presets", ok, ok ? "Exported " + path->filename().string() : error);
    });
}

static void ImportWithPicker() {
    Ship::FileBrowserRequest request;
    request.Title = "Import shading preset";
    request.StartDir = Ship::Context::GetAppDirectoryPath();
    request.Filters = { { "Shading Preset", { "*.json" } }, { "All Files", { "*" } } };
    Paperboat::PickFile(std::move(request), [](std::optional<std::filesystem::path> path) {
        if (!path) {
            return;
        }
        std::string error;
        bool ok = ShadingPresets::ImportFile(*path, &error);
        if (ok) {
            ShadingPresets::Refresh();
        }
        NotifyResult("Shading Presets", ok, ok ? "Imported " + path->filename().string() : error);
    });
}

enum class PendingAction { None, Delete, MoveUp, MoveDown };

static void DrawPresetEditor() {
    const std::string active = ShadingPresets::ActiveName();

    if (active.empty()) {
        ImGui::TextColored(sDimColor, "No custom preset active - areas use their own lighting.");
    } else {
        ImGui::Text("Active: %s", active.c_str());
    }

    if (ImGui::Button("New from Current")) {
        std::string name = ShadingPresets::CaptureFromCvars();
        if (!name.empty()) {
            ShadingPresets::SetActive(name);
            NotifyResult("Shading Presets", true, "Saved and applied \"" + name + "\"");
        } else {
            NotifyResult("Shading Presets", false, "Could not save preset");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        ImportWithPicker();
    }
    if (!active.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Stop Using")) {
            ShadingPresets::ClearActive();
        }
    }

    ImGui::Separator();

    const std::vector<ShadingPresets::Entry>& all = ShadingPresets::All();
    if (all.empty()) {
        ImGui::TextColored(sDimColor, "No presets yet. Tune the lighting sliders, then choose");
        ImGui::TextColored(sDimColor, "\"New from Current\" to save one here.");
        return;
    }

    PendingAction pending = PendingAction::None;
    size_t pendingIndex = 0;

    if (ImGui::BeginChild("ShadingPresetList", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTable("ShadingPresetTable", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Preset", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 250.0f);

            for (size_t i = 0; i < all.size(); i++) {
                const ShadingPresets::Entry& entry = all[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImGui::PushID((int) i);
                const bool selected = entry.name == active;
                if (ImGui::Selectable(entry.name.c_str(), selected, ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(0.0f, 0.0f))) {
                    ShadingPresets::SetActive(entry.name);
                }

                ImGui::TableSetColumnIndex(1);
                if (ImGui::SmallButton("Apply")) {
                    ShadingPresets::SetActive(entry.name);
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::SmallButton("Up")) {
                    pending = PendingAction::MoveUp;
                    pendingIndex = i;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(i + 1 >= all.size());
                if (ImGui::SmallButton("Down")) {
                    pending = PendingAction::MoveDown;
                    pendingIndex = i;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton("Export")) {
                    ExportWithPicker(entry.name);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) {
                    pending = PendingAction::Delete;
                    pendingIndex = i;
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    switch (pending) {
        case PendingAction::Delete: {
            std::string error;
            std::string name = all[pendingIndex].name;
            bool ok = ShadingPresets::DeleteEntry(pendingIndex, &error);
            NotifyResult("Shading Presets", ok, ok ? "Deleted \"" + name + "\"" : error);
            break;
        }
        case PendingAction::MoveUp: {
            std::string error;
            ShadingPresets::MoveEntry(pendingIndex, -1, &error);
            break;
        }
        case PendingAction::MoveDown: {
            std::string error;
            ShadingPresets::MoveEntry(pendingIndex, 1, &error);
            break;
        }
        case PendingAction::None:
            break;
    }
}

void ShadingPresetWindow::InitElement() {
    ShadingPresets::Refresh();
}

void ShadingPresetWindow::DrawElement() {
    if (CVAR_SHOW_POPOUT_SHADING_PRESETS) {
        return;
    }
    DrawPresetEditor();
}

void ShadingPresetWindow::Draw() {
    if (!CVAR_SHOW_POPOUT_SHADING_PRESETS) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, sWindowBG);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, sWindowBG);
    ImGui::SetNextWindowSize(ImVec2(560.0f, 440.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Shading Presets", nullptr, sWindowFlags)) {
        DrawPresetEditor();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
}

#pragma pop_macro("End")
