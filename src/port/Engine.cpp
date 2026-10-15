#include "Engine.h"

#include "ShipInit.hpp"
#include "extractor/GameExtractor.h"
#include "importer/PM64TextureFactory.h"
#include "importer/Vec3sFactory.h"
#include "nlohmann/json.hpp"
#include "port/build.h"
#include "port/enhancements/PortEnhancements.h"
#include "port/interpolation/FrameInterpolation.h"
#include "port/ui/cvar_prefixes.h"
#include "port/audio/AudioVolume.h"
#include "port/os/OS.h"
#include "port/DevTools/ThreadWatchdog.h"
#include "port/TextureCache.h"
#include "src/Companion.h"
#include "ui/PaperboatGui.hpp"
#include "ui/PaperboatModMenuWindow.h"
#include "port/lighting/LightingManager.h"
#ifdef __EMSCRIPTEN__
#include "port/web/WebUtils.h"
#endif
#if (defined(__linux__) || defined(__APPLE__)) && !defined(__ANDROID__)
#include <cerrno>
#include <cstring>
#include <unistd.h>
#endif
#include <BS_thread_pool.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <fast/resource/ResourceType.h>
#include <fast/resource/factory/DisplayListFactory.h>
#include <fast/resource/factory/LightFactory.h>
#include <fast/resource/factory/MatrixFactory.h>
#include <fast/resource/factory/TextureFactory.h>
#include <fast/resource/type/Texture.h>
#include <fast/resource/factory/VertexFactory.h>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <chrono>
#include <libultraship.h>
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/mapping/sdl/SDLButtonToAxisDirectionMapping.h>
#include <libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h>
#include <mutex>
#include <optional>
#include <string_view>
#include <ship/resource/factory/BlobFactory.h>
#include <ship/audio/Audio.h>
#include <ship/config/Config.h>
#include <ship/config/ConsoleVariable.h>
#include <ship/debug/Console.h>
#include <ship/debug/CrashHandler.h>
#include <ship/resource/ResourceManager.h>
#include <fast/debug/GfxDebugger.h>
#include <libultraship/bridge/audiobridge.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/controllerbridge.h>
#include <libultraship/bridge/crashhandlerbridge.h>
#include <libultraship/bridge/eventsbridge.h>
#include <libultraship/bridge/gfxbridge.h>
#include <libultraship/bridge/gfxdebuggerbridge.h>
#include <libultraship/bridge/resourcebridge.h>
#include <libultraship/bridge/windowbridge.h>
#include <ship/window/gui/Fonts.h>
#include <ship/window/gui/resource/Font.h>
#include <thread>
#include <unordered_map>

// The decomp EVT script API defines a function-like `Add(VAR, VALUE)` macro that
// swallows ComponentList::Add(). Engine.cpp does not build EVT scripts, so we drop it.
#undef Add

using json = nlohmann::json;

const float imguiScaleOptionToValue[4] = { 0.75f, 1.0f, 1.5f, 2.0f };
std::shared_ptr<Fast::Fast3dWindow> gsFast3dWindow;
Ship::Context* gShipContext = nullptr;
const uint32_t defaultImGuiScale = 1;
int32_t previousImGuiScaleIndex = -1;
float previousImGuiScale = defaultImGuiScale;

namespace fs = std::filesystem;

std::vector<uint8_t*> MemoryPool;
GameEngine* GameEngine::Instance;

// Static audio thread state definition
decltype(GameEngine::mAudio) GameEngine::mAudio;

// Game audio system functions and globals
extern "C" {
void nuScCreateScheduler(uint8_t mode, uint8_t numFields);
void create_audio_system(void);
void nuAuMgr(void* arg);
Acmd* alAudioFrame(Acmd* cmdList, int32_t* cmdLen, int16_t* outBuf, int32_t outLen);
extern int32_t AlFrameSize;
extern int32_t AlMinFrameSize;
}

// Audio constants from game
#define AUDIO_SAMPLES        184
#define HARDWARE_OUTPUT_RATE 32000

// Access to the display list pointer for emitting context markers
// Gfx is already defined in libultraship's gbi.h (included via libultraship.h)
extern "C" {
extern Gfx* gMainGfxPos;
}

// nusys's count of graphics tasks the game has submitted that haven't finished drawing
extern "C" {
extern volatile uint32_t nuGfxTaskSpool;
}

static bool portArchiveExists = false;
static const std::vector<std::string> sRomArchives = { "pm64.o2r" };

typedef enum ExtractSteps {
    ES_PORT_ARCHIVE,
    ES_WINDOWS,
    ES_EXTRACT_ARGS,
    ES_EXTRACT,
    ES_VERIFY,
} ExtractSteps;

typedef enum PromptSteps {
    PS_FILE_CHECK,
    PS_LOCAL,
    PS_FIRST,
    PS_DUPE,
    PS_WAIT,
    // Web only: no folder to scan, so the user is asked outright.
    PS_WEB_CHOICE,
    PS_WEB_UPLOAD,
    PS_NONE,
} PromptSteps;

typedef enum WindowsSteps {
    WS_TEMP,
    WS_PERMS,
    WS_ONEDRIVE,
    WS_DONE,
} WindowsSteps;

static bool IsSubpath(const std::filesystem::path& path, const std::filesystem::path& base) {
    auto rel = std::filesystem::relative(path, base);
    return !rel.empty() && rel.native()[0] != '.';
}

static bool PathTestCleanup(FILE* tfile) {
    try {
        if (std::filesystem::exists("./text.txt")) {
            std::filesystem::remove("./text.txt");
        }
        if (std::filesystem::exists("./test/")) {
            std::filesystem::remove("./test/");
        }
    } catch (std::filesystem::filesystem_error const& ex) {
        return false;
    }
    return true;
}

static void CheckAndCreateModFolder() {
    try {
        std::string modsPath = Ship::Context::LocateFileAcrossAppDirs("mods", "boat");
        if (!std::filesystem::exists(modsPath)) {
            modsPath = Ship::Context::GetPathRelativeToAppDirectory("mods", "boat");
            std::string filePath = modsPath + "/custom_mod_files_go_here.txt";
            if (std::filesystem::create_directories(modsPath)) {
                std::ofstream(filePath).close();
            }
        }
    } catch (std::filesystem::filesystem_error const& ex) {
        return;
    }
}

static bool AnyRomArchiveExists() {
    for (const auto& archive : sRomArchives) {
        if (std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs(archive))) {
            return true;
        }
    }
    return false;
}

GameEngine::GameEngine() {
    const std::string assets_path = Ship::Context::LocateFileAcrossAppDirs("paperboat.o2r");
    portArchiveExists = std::filesystem::exists(assets_path);

#if defined(_WIN32) && defined(_DEBUG)
    AllocConsole();
#endif

    this->context = Ship::Context::CreateUninitializedInstance("Paperboat", "boat", "paperboat.cfg.json");
    gShipContext = this->context;

    this->context->InitLogging();
    this->context->InitConfiguration();
    this->context->InitConsoleVariables();

    this->context->InitControlDeck(std::make_shared<LUS::ControlDeck>());
    this->context->InitResourceManager(
        portArchiveExists ? std::vector<std::string> { assets_path } : std::vector<std::string> {}, {}, 3
    );
    this->context->InitConsole();
    this->context->InitCrashHandler();
    this->context->InitEventSystem();

    // Initialize lighting manager (loads lighting_profiles.yml from resources/mods if present).
    // Safe: the manager guards against missing context/resourcemanager.
    LightingManager_Init();

    gsFast3dWindow = std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({}));
    this->context->InitWindow(gsFast3dWindow);
    if (auto interpreter = gsFast3dWindow->GetInterpreterWeak().lock()) {
        // Prefetch a replacement's folder on its first draw. Sprites, map textures and
        // backgrounds are prefetched by whatever loads them instead.
        interpreter->SetReplacementGroupResolver([](const std::string& name) -> std::string {
            if (name.starts_with("sprites/") || name.starts_with("textures/") || name.starts_with("backgrounds/")) {
                return {};
            }
            return name.substr(0, name.find_last_of('/') + 1);
        });
    }
    this->context->InitFileDropMgr();

    PaperboatGui::SetupMenu();

    if (portArchiveExists) {
        fontMono = CreateFontWithSize(16.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLarger = CreateFontWithSize(20.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLargest = CreateFontWithSize(24.0f, "fonts/Inconsolata-Regular.ttf");
        fontStandard = CreateFontWithSize(16.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLarger = CreateFontWithSize(20.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLargest = CreateFontWithSize(24.0f, "fonts/Montserrat-Regular.ttf");
        ImGui::GetIO().FontDefault = fontStandardLarger;
    }

    previousImGuiScaleIndex = -1;
    previousImGuiScale = defaultImGuiScale;
    ScaleImGui();
}

void GameEngine::FinishInit() {
    spdlog::set_pattern("[%H:%M:%S.%e] [%s:%#] [%l] %v");
    SPDLOG_INFO(
        "Starting PaperBoat version {} (Branch: {} | Commit: {})", std::string_view(gBuildVersion),
        std::string_view(gGitBranch), std::string_view(gGitCommitHash)
    );

    auto archiveManager = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();

    for (const auto& archive : sRomArchives) {
        const auto romPath = Ship::Context::LocateFileAcrossAppDirs(archive);
        if (std::filesystem::exists(romPath)) {
            archiveManager->AddArchive(romPath);
        }
    }

    const std::string hd_path = Ship::Context::GetPathRelativeToAppDirectory("paperboat-hd.o2r");
    if (std::filesystem::exists(hd_path)) {
        SPDLOG_INFO("Loading HD asset archive: paperboat-hd.o2r");
        archiveManager->AddArchive(hd_path);
    }
    UpdateModFiles(true);

#ifdef _DEBUG
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);
#else
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::warn);
#endif

    Ship::Context::GetRawInstance()->InitAudio({ .SampleRate = 32000, .SampleLength = 1024, .DesiredBuffered = 1680 });

    // Opt in to texture path memoization.
    if (gsFast3dWindow != nullptr) {
        if (auto interpreter = gsFast3dWindow->GetInterpreterWeak().lock()) {
            interpreter->SetResolvedResourceCacheEnabled(true);
        }
    }
    TextureCache_Configure();

    auto loader = Ship::Context::GetRawInstance()->GetResourceManager()->GetResourceLoader();
    loader->RegisterResourceFactory(
        std::make_shared<Ship::ResourceFactoryBinaryBlobV0>(), RESOURCE_FORMAT_BINARY, "Blob",
        static_cast<uint32_t>(Ship::ResourceType::Blob), 0
    );
    loader->RegisterResourceFactory(
        std::make_shared<PM64::ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY, "Texture",
        static_cast<uint32_t>(Fast::ResourceType::Texture), 0
    );
    loader->RegisterResourceFactory(
        std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY, "Texture",
        static_cast<uint32_t>(Fast::ResourceType::Texture), 1
    );
    loader->RegisterResourceFactory(
        std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(), RESOURCE_FORMAT_BINARY, "DisplayList",
        static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0
    );
    loader->RegisterResourceFactory(
        std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY, "Vertex",
        static_cast<uint32_t>(Fast::ResourceType::Vertex), 0
    );
    loader->RegisterResourceFactory(
        std::make_shared<Fast::ResourceFactoryBinaryLightV0>(), RESOURCE_FORMAT_BINARY, "Light",
        static_cast<uint32_t>(Fast::ResourceType::Light), 0
    );
    loader->RegisterResourceFactory(
        std::make_shared<Fast::ResourceFactoryBinaryMatrixV0>(), RESOURCE_FORMAT_BINARY, "Matrix",
        static_cast<uint32_t>(Fast::ResourceType::Matrix), 0
    );
    loader->RegisterResourceFactory(
        std::make_shared<PM64::ResourceFactoryBinaryVec3sV0>(), RESOURCE_FORMAT_BINARY, "Vec3s",
        static_cast<uint32_t>(0x56433353), 0
    );
}

bool GameEngine::GenAssetFile(bool exitOnFail) {
    return false;
}

ImFont* GameEngine::CreateFontWithSize(float size, std::string fontPath) {
    auto mImGuiIo = &ImGui::GetIO();
    ImFont* font;
    if (fontPath == "") {
        ImFontConfig fontCfg = ImFontConfig();
        fontCfg.OversampleH = fontCfg.OversampleV = 1;
        fontCfg.PixelSnapH = true;
        fontCfg.SizePixels = size;
        font = mImGuiIo->Fonts->AddFontDefault(&fontCfg);
    } else {
        auto initData = std::make_shared<Ship::ResourceInitData>();
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;

        initData->Format = RESOURCE_FORMAT_BINARY;
        initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_FONT);
        initData->ResourceVersion = 0;
        initData->Path = fontPath;
        std::shared_ptr<Ship::Font> fontData = std::static_pointer_cast<Ship::Font>(
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(fontPath, false, initData)
        );
        font = mImGuiIo->Fonts->AddFontFromMemoryTTF(fontData->Data, fontData->DataSize, size, &config);
    }
    // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to
    // align correctly
    float iconFontSize = size * 2.0f / 3.0f;
    static const ImWchar sIconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig iconsConfig;
    iconsConfig.MergeMode = true;
    iconsConfig.PixelSnapH = true;
    iconsConfig.GlyphMinAdvanceX = iconFontSize;
    mImGuiIo->Fonts->AddFontFromMemoryCompressedBase85TTF(
        fontawesome_compressed_data_base85, iconFontSize, &iconsConfig, sIconsRanges
    );

    return font;
}

void GameEngine::ScaleImGui() {
    int32_t imGuiScaleIndex = CVarGetInteger("gSettings.ImGuiScale", defaultImGuiScale);
    if (imGuiScaleIndex == previousImGuiScaleIndex) {
        return;
    }

    float scale = imguiScaleOptionToValue[imGuiScaleIndex];
    float newScale = scale / previousImGuiScale;
    ImGui::GetStyle().ScaleAllSizes(newScale);
    ImGui::GetIO().FontGlobalScale = scale;
    previousImGuiScale = scale;
    previousImGuiScaleIndex = imGuiScaleIndex;
}

void GameEngine::RunExtract(int argc, char* argv[]) {
    bool extractDone = false;
    ExtractSteps extractStep = ES_PORT_ARCHIVE;
    WindowsSteps windowsStep = WS_TEMP;
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    auto gui = wnd->GetGui();
    bool menuWasVisible = false;

    ... (file truncated for brevity) ...
