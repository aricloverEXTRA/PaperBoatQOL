#include "ShaderDashboard.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include "port/ui/cvar_prefixes.h"
#include <spdlog/spdlog.h>
#include <sstream>

extern "C" {
    // These are called from the graphics backend when shaders compile
    void GameEngine_OnShaderCompileStart(const char* shaderName);
    void GameEngine_OnShaderCompileEnd(const char* shaderName, bool success, float timeMs);
    int GameEngine_GetShaderCompileCount();
    int GameEngine_GetShaderTotalCount();
    const char* GameEngine_GetShaderCompileStatus(int index);
    float GameEngine_GetShaderCompileTime(int index);
    void GameEngine_RequestShaderRecompile(const char* shaderName);
}

namespace {

struct ShaderInfo {
    std::string name;
    std::string status; // "pending", "compiling", "compiled", "failed"
    float compileTimeMs = 0.0f;
    int passId = -1;
};

std::vector<ShaderInfo> gShaderList;
bool gShadersDirty = true;
float gLastUpdateTime = 0.0f;

void RefreshShaderList() {
    gShaderList.clear();
    
    // Get the current post-fx passes from PostFx.cpp
    // We'll query the interpreter for registered passes
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp) {
        // Try to get registered post passes
        // For now, manually list known shaders
        static const char* knownShaders[] = {
            "shaders/post/pb_depthdebug.shader",
            "shaders/post/pb_ssao.shader",
            "shaders/post/pb_sunshade.shader",
            "shaders/post/pb_godrays.shader",
            "shaders/post/pb_contactshadow.shader",
            "shaders/post/pb_ssshadow.shader",
            "shaders/post/pb_bloom.shader",
            "shaders/post/pb_fxaa.shader",
            "shaders/post/pb_specular.shader",
            "shaders/post/pb_pbr.shader",
            "shaders/post/pb_lut.shader",
            "shaders/post/pb_hdr_tonemap.shader",
            "shaders/post/pb_volumetric.shader",
            "shaders/post/pb_taa.shader",
            "shaders/post/pb_motionblur.shader",
            "shaders/post/pb_parallax.shader",
            "shaders/post/pb_vrs.shader",
        };
        
        for (const char* shader : knownShaders) {
            ShaderInfo info;
            info.name = shader;
            info.status = "pending";
            gShaderList.push_back(info);
        }
    }
    
    // Update counts from engine
    int compiled = GameEngine_GetShaderCompileCount();
    int total = GameEngine_GetShaderTotalCount();
    
    // Update statuses from engine
    for (int i = 0; i < total && i < (int)gShaderList.size(); ++i) {
        const char* status = GameEngine_GetShaderCompileStatus(i);
        float time = GameEngine_GetShaderCompileTime(i);
        if (status) gShaderList[i].status = status;
        if (time > 0) gShaderList[i].compileTimeMs = time;
    }
    
    gShadersDirty = false;
}

} // namespace

void ShaderDashboardWindow::InitElement() {
    RefreshShaderList();
}

void ShaderDashboardWindow::DrawElement() {
    // Update shader list periodically
    float currentTime = ImGui::GetTime();
    if (gShadersDirty || (currentTime - gLastUpdateTime > 1.0f)) {
        RefreshShaderList();
        gLastUpdateTime = currentTime;
    }

    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    
    if (!ImGui::Begin(mWindowName.c_str(), &mIsVisible, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Actions")) {
            if (ImGui::MenuItem("Refresh")) {
                gShadersDirty = true;
            }
            if (ImGui::MenuItem("Recompile All")) {
                for (auto& shader : gShaderList) {
                    GameEngine_RequestShaderRecompile(shader.name.c_str());
                    shader.status = "compiling";
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show Pending", nullptr, nullptr, false);
            ImGui::MenuItem("Show Compiled", nullptr, nullptr, false);
            ImGui::MenuItem("Show Failed", nullptr, nullptr, false);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Summary bar
    int total = gShaderList.size();
    int compiled = 0, failed = 0, compiling = 0, pending = 0;
    float totalTime = 0.0f;
    
    for (const auto& s : gShaderList) {
        if (s.status == "compiled") { compiled++; totalTime += s.compileTimeMs; }
        else if (s.status == "failed") failed++;
        else if (s.status == "compiling") compiling++;
        else pending++;
    }

    ImGui::Text("Shader Compilation Dashboard");
    ImGui::Separator();
    
    // Summary stats
    ImGui::Text("Status: %d/%d compiled  |  %d compiling  |  %d pending  |  %d failed", 
                compiled, total, compiling, pending, failed);
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Total time: %.1f ms", totalTime);
    
    // Progress bar
    float progress = total > 0 ? (float)compiled / (float)total : 1.0f;
    ImGui::ProgressBar(progress, ImVec2(-1, 0), 
                       (std::to_string(compiled) + "/" + std::to_string(total) + " shaders compiled").c_str());
    
    ImGui::Separator();

    // Shader list table
    if (ImGui::BeginTable("ShaderTable", 5, 
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | 
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("Shader", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Pass ID", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)gShaderList.size(); ++i) {
            auto& s = gShaderList[i];
            ImGui::TableNextRow();
            
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", s.name.c_str());
            
            ImGui::TableSetColumnIndex(1);
            ImVec4 color = ImVec4(1,1,1,1);
            if (s.status == "compiled") color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
            else if (s.status == "failed") color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            else if (s.status == "compiling") color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            else color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            ImGui::TextColored(color, "%s", s.status.c_str());
            
            ImGui::TableSetColumnIndex(2);
            if (s.compileTimeMs > 0) {
                ImGui::Text("%.1f", s.compileTimeMs);
            } else {
                ImGui::Text("-");
            }
            
            ImGui::TableSetColumnIndex(3);
            if (s.passId >= 0) {
                ImGui::Text("%d", s.passId);
            } else {
                ImGui::Text("-");
            }
            
            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(i);
            if (ImGui::SmallButton("Recompile")) {
                GameEngine_RequestShaderRecompile(s.name.c_str());
                s.status = "compiling";
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(s.status == "compiled" ? "Unload" : "Load")) {
                // TODO: toggle pass registration
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    
    // Hot-reload section
    ImGui::Text("Hot Reload Controls");
    ImGui::Separator();
    
    static char shaderPath[256] = "shaders/post/pb_custom.shader";
    ImGui::InputText("Shader Path", shaderPath, sizeof(shaderPath));
    if (ImGui::Button("Load & Compile")) {
        GameEngine_RequestShaderRecompile(shaderPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Watch for Changes")) {
        // TODO: file watcher
    }

    ImGui::End();
}