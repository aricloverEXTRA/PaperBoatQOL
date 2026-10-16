#include "PaperboatMenu.h"

namespace PaperboatGui {

extern std::shared_ptr<PaperboatMenu> mPaperboatMenu;

using namespace UIWidgets;

static const std::unordered_map<int32_t, const char*> blockWindowOptions = {
    { 0, "Original (3 frames)" },
    { 1, "Forgiving (7 frames)" },
    { 2, "Very Forgiving (10 frames)" },
};

static const std::unordered_map<int32_t, const char*> actionCommandDifficultyOptions = {
    { 0, "Original" },
    { 1, "Forgiving (-1 level)" },
    { 2, "Very Forgiving (-2 levels)" },
    { 3, "Extremely Forgiving (-3 levels)" },
};

static const std::unordered_map<int32_t, const char*> HiddenBlockOptions = {
    { 0, "Original" }, { 1, "When Watt Current Partner" }, { 2, "When Watt Unlocked" }, { 3, "Always" }
};

void PaperboatMenu::AddMenuEnhancements() {
    // Add Enhancements Menu
    AddMenuEntry("Enhancements", CVAR_SETTING("Menu.EnhancementsSidebarSection"));

    // Enhancements > Cheats
    WidgetPath path = { "Enhancements", "Cheats", SECTION_COLUMN_1 };
    AddSidebarEntry("Enhancements", path.sidebarName, 1);
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "Infinite Health", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_CHEAT("InfiniteHealth"))
        .Options(CheckboxOptions().Tooltip("Mario's HP won't decrease during battle."));

    AddWidget(path, "Infinite Flower Points", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_CHEAT("InfiniteFlowerPoints"))
        .Options(CheckboxOptions().Tooltip("Mario's FP won't decrease during battle."));

    AddWidget(path, "No Badge Cost", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_CHEAT("NoBPCost"))
        .Options(CheckboxOptions().Tooltip("Equip any badge regardless of BP cost."));

    AddWidget(path, "Max Star Power", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_CHEAT("MaxStarPower"))
        .Options(CheckboxOptions().Tooltip("Star Power stays full and won't decrease."));

    AddWidget(path, "Max Power Bounce Chance", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_CHEAT("MaxPowerBounceChance"))
        .Options(
            CheckboxOptions().Tooltip(
                "Power Bounce's and Goombario's Multibonk's chance to continue stays full, so a chain only ends "
                "when you miss the timing or reach the bounce limit."
            )
        );

    AddWidget(path, "2x Star Points and Coins", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_CHEAT("DoubleRewards"))
        .Options(CheckboxOptions().Tooltip("Defeated enemies give twice the Star Points and coins."));

    // Enhancements > Gameplay
    path = { "Enhancements", "Gameplay", SECTION_COLUMN_1 };
    AddSidebarEntry("Enhancements", path.sidebarName, 2);
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "DX: Prevent Loading Zone Storage", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("PreventLoadingZoneStorage"))
        .Options(
            CheckboxOptions().Tooltip(
                "Locks out player input the moment a loading zone is triggered, which patches "
                "out the Loading Zone Storage glitch. Off by default to match the original "
                "game. Note that most loading zones also trigger while you are airborne above "
                "them, so enabling this can freeze Mario in midair until he lands."
            )
        );

    AddWidget(path, "Sprint Button", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("SprintButton"))
        .Options(CheckboxOptions().Tooltip("Hold R to move at double speed in the overworld."));

    AddWidget(path, "Free QoL Badges", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("FreeQoLBadges"))
        .Options(CheckboxOptions().Tooltip("Speedy Spin, I Spy and Peekaboo cost 0 BP."));

    AddWidget(path, "Action Command Difficulty", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_ENHANCEMENT("ActionCommandDifficulty"))
        .Options(
            ComboboxOptions()
                .Tooltip(
                    "Lowers the difficulty of attack action commands, which widens their input "
                    "windows. Stacks with the Dodge Master badge."
                )
                .ComboMap(actionCommandDifficultyOptions)
        );

    AddWidget(path, "Block Window", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_ENHANCEMENT("BlockWindowMode"))
        .Options(
            ComboboxOptions()
                .Tooltip(
                    "Sets the window for timed defensive blocks. The default is 3 frames. Anything "
                    "wider overrides the Dodge Master badge."
                )
                .ComboMap(blockWindowOptions)
        );

    AddWidget(path, "Show Hidden Blocks", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_ENHANCEMENT("ShowHiddenBlocks"))
        .Options(
            ComboboxOptions()
                .Tooltip(
                    "Makes hidden blocks show based on the selected option. "
                    "Default requires using Watt's ability."
                )
                .ComboMap(HiddenBlockOptions)
        );
    // Enhancements > Graphics
    path = { "Enhancements", "Graphics", SECTION_COLUMN_1 };
    AddSidebarEntry("Enhancements", "Graphics", 1);

    AddWidget(path, "Full Height View", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.FullHeightView"))
        .Options(
            CheckboxOptions().Tooltip("Removes the letterboxing bars on the top and bottom of the screen in gameplay.")
        );

    AddWidget(path, "Rounded Projector Reel", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.RoundedReel"))
        .Options(
            CheckboxOptions().Tooltip("Flips the battle projector reel to appear rounded for widescreen resolutions.")
        );

    AddWidget(path, "Disable Mipmaps", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.DisableMipmaps"))
        .Options(CheckboxOptions().Tooltip("Just like emulator!"));

    AddWidget(path, "Enhanced Shading", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Enable Enhanced Shading", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("Enabled"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Enables the enhanced shading system with area-based lighting presets, "
            "rim lighting, hemisphere lighting, and more."
        ));

    static const std::unordered_map<int32_t, const char*> enhancedShadingModeOptions = {
        { 0, "Vanilla (Original)" },
        { 1, "Area-Based Presets (Color Splash Style)" },
        { 2, "Custom Lighting" },
    };

    AddWidget(path, "Enhanced Shading Mode", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_ENHANCED_SHADING("Mode"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            ComboboxOptions()
                .Tooltip(
                    "Selects the enhanced shading mode. 'Area-Based Presets' uses "
                    "pre-configured lighting for each area inspired by Paper Mario: Color Splash. "
                    "'Custom Lighting' allows manual configuration. 'Vanilla' uses original game lighting."
                )
                .ComboMap(enhancedShadingModeOptions)
        );

    AddWidget(path, "Fallback to Vanilla for Unknown Areas", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("FallbackToVanilla"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(true).Tooltip(
                "When enabled, areas without custom lighting presets will use vanilla lighting. "
                "When disabled, they will use a generic default preset. Useful for modded areas."
            )
        );

    AddWidget(path, "Hemisphere Lighting", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("HemisphereLighting"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(true).Tooltip(
                "Enables hemisphere lighting (sky/ground colors) for more natural outdoor lighting. "
                "Inspired by Color Splash's ambient lighting."
            )
        );

    AddWidget(path, "Rim Lighting", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("RimLighting"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(false).Tooltip(
                "Adds rim/fresnel lighting on character edges for better silhouette definition. "
                "Gives a more 3D appearance similar to modern rendering."
            )
        );

    AddWidget(path, "Subsurface Scattering", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("SubsurfaceScattering"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(false).Tooltip(
                "Simulates light passing through thin materials (ears, paper edges). "
                "Adds warmth to backlit characters. Subtle but effective."
            )
        );

    AddWidget(path, "Ambient Occlusion", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("AmbientOcclusion"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(false).Tooltip(
                "Darkens crevices and contact points for better depth perception. "
                "May impact performance on lower-end hardware."
            )
        );

    AddWidget(path, "Smooth Falloff", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("SmoothFalloff"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(true).Tooltip(
                "Uses smoother quadratic falloff for point/spot lights instead of linear. "
                "More realistic but slightly more expensive."
            )
        );

    AddWidget(path, "Temporal Stability", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("TemporalStability"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(true).Tooltip(
                "Blends lighting between frames to reduce flickering. "
                "May cause slight lag in lighting changes."
            )
        );

    AddWidget(path, "Dynamic Lights (Torches/Lanterns)", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Enable Dynamic Lights", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("DynamicLights"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(true).Tooltip(
                "Enables dynamic lights from torches, lanterns, candles, and other light sources. "
                "These are attached to entities in the world."
            )
        );

    AddWidget(path, "Dynamic Light Flicker", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("DynamicLightFlicker"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("DynamicLights"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(true).Tooltip(
                "Enables flickering animation for torch and candle lights."
            )
        );

    AddWidget(path, "Max Dynamic Lights", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_ENHANCED_SHADING("MaxDynamicLights"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("DynamicLights"), 1);
        })
        .Options(
            IntSliderOptions().Min(4).Max(32).DefaultValue(16).Tooltip(
                "Maximum number of dynamic lights that can be active at once. "
                "Higher values may impact performance."
            )
        );

    AddWidget(path, "Rim Light Intensity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("RimIntensity"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("RimLighting"), 0);
        })
        .Options(
            FloatSliderOptions().Min(0.0f).Max(2.0f).DefaultValue(0.5f).Step(0.05f).Format("%.2f").Tooltip(
                "Strength of the rim lighting effect."
            )
        );

    AddWidget(path, "Subsurface Strength", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("SubsurfaceStrength"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1)
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("SubsurfaceScattering"), 0);
        })
        .Options(
            FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.3f).Step(0.05f).Format("%.2f").Tooltip(
                "Strength of subsurface scattering effect."
            )
        );

    AddWidget(path, "AO Intensity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("AOIntensity"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1)
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("AmbientOcclusion"), 0);
        })
        .Options(
            FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.5f).Step(0.05f).Format("%.2f").Tooltip(
                "Strength of ambient occlusion darkening."
            )
        );

    // Graphics Presets
    static const std::unordered_map<int32_t, const char*> graphicsPresetOptions = {
        { 0, "Low (Performance)" },
        { 1, "Medium (Balanced)" },
        { 2, "High (Quality)" },
        { 3, "Ultra (Maximum)" },
        { 4, "Custom" },
    };

    AddWidget(path, "Graphics Preset", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_ENHANCED_SHADING("GraphicsPreset"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            ComboboxOptions()
                .Tooltip(
                    "Selects a graphics preset that automatically configures multiple effects. "
                    "'Custom' allows individual effect toggles below. "
                    "Mobile, console, and web builds are capped (Web = Low, Android/iOS/Switch = Medium) "
                    "to prevent overheating and frame drops."
                )
                .ComboMap(graphicsPresetOptions)
        );

    // Advanced Effects
    AddWidget(path, "Advanced Effects", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Depth of Field", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("DepthOfField"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(false).Tooltip(
                "Simulates camera focus blur. Objects outside the focal range appear blurred. "
                "Skyboxes are exempt by default to preserve atmosphere."
            )
        );

    AddWidget(path, "DOF Focal Distance", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("DOFFocalDistance"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("DepthOfField"), 0);
        })
        .Options(
            FloatSliderOptions().Min(50.0f).Max(2000.0f).DefaultValue(500.0f).Step(10.0f).Format("%.0f").Tooltip(
                "Distance from camera where objects are in sharp focus."
            )
        );

    AddWidget(path, "DOF Focal Range", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("DOFFocalRange"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("DepthOfField"), 0);
        })
        .Options(
            FloatSliderOptions().Min(10.0f).Max(500.0f).DefaultValue(100.0f).Step(5.0f).Format("%.0f").Tooltip(
                "Range around focal distance where objects remain sharp."
            )
        );

    AddWidget(path, "DOF Max Blur", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("DOFMaxBlur"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("DepthOfField"), 0);
        })
        .Options(
            FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.5f).Step(0.05f).Format("%.2f").Tooltip(
                "Maximum blur strength for out-of-focus areas."
            )
        );

    AddWidget(path, "DOF Skybox Exempt", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("SkyboxDOFExempt"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("DepthOfField"), 0);
        })
        .Options(
            CheckboxOptions().DefaultValue(true).Tooltip(
                "Prevents skyboxes from being blurred by Depth of Field. "
                "Preserves sky atmosphere and distant vistas."
            )
        );

    AddWidget(path, "Shadows", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("Shadows"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(false).Tooltip(
                "Enables real-time shadows for dynamic lights. "
                "Significant performance cost on lower-end hardware."
            )
        );

    AddWidget(path, "Shadow Distance", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("ShadowDistance"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("Shadows"), 0);
        })
        .Options(
            FloatSliderOptions().Min(200.0f).Max(5000.0f).DefaultValue(1000.0f).Step(50.0f).Format("%.0f").Tooltip(
                "Maximum distance from camera where shadows are rendered."
            )
        );

    AddWidget(path, "Shadow PCF", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("ShadowPCF"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("Shadows"), 0);
        })
        .Options(
            CheckboxOptions().DefaultValue(true).Tooltip(
                "Percentage-Closer Filtering for softer shadow edges."
            )
        );

    auto hideUnlessPostFx = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1)
            || !CVarGetInteger(CVAR_ENHANCED_SHADING("PostProcessing"), 0);
    };

    AddWidget(path, "Post-Processing", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("PostProcessing"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
        })
        .Options(
            CheckboxOptions().DefaultValue(false).Tooltip(
                "Enables real screen-space post-processing passes (glow and colour grading). "
                "Only supported on the OpenGL and Vulkan renderers."
            )
        );

    AddWidget(path, "Bloom", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("Bloom"))
        .PreFunc(hideUnlessPostFx)
        .Options(
            CheckboxOptions().DefaultValue(false).Tooltip(
                "Adds glow effect to bright areas (lava, fire, magic). Creates a dreamy, modern look."
            )
        );

    AddWidget(path, "Bloom Threshold", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("BloomThreshold"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("PostProcessing"), 0)
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("Bloom"), 0);
        })
        .Options(
            FloatSliderOptions().Min(0.0f).Max(2.0f).DefaultValue(1.0f).Step(0.05f).Format("%.2f").Tooltip(
                "Minimum brightness for bloom to activate."
            )
        );

    AddWidget(path, "Bloom Intensity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("BloomIntensity"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("PostProcessing"), 0)
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("Bloom"), 0);
        })
        .Options(
            FloatSliderOptions().Min(0.0f).Max(2.0f).DefaultValue(0.3f).Step(0.05f).Format("%.2f").Tooltip(
                "Strength of the bloom glow effect."
            )
        );

    AddWidget(path, "Tone Mapping", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("ToneMapping"))
        .PreFunc(hideUnlessPostFx)
        .Options(
            CheckboxOptions().DefaultValue(false).Tooltip(
                "Applies ACES filmic tone mapping for HDR-like color grading. "
                "Prevents blown-out highlights and improves color balance."
            )
        );

    AddWidget(path, "Exposure", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("Exposure"))
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1) 
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("PostProcessing"), 0)
                || !CVarGetInteger(CVAR_ENHANCED_SHADING("ToneMapping"), 0);
        })
        .Options(
            FloatSliderOptions().Min(0.1f).Max(4.0f).DefaultValue(1.0f).Step(0.05f).Format("%.2f").Tooltip(
                "Overall scene brightness multiplier for tone mapping."
            )
        );

    AddWidget(path, "Contrast", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("PostContrast"))
        .PreFunc(hideUnlessPostFx)
        .Options(
            FloatSliderOptions().Min(0.5f).Max(2.0f).DefaultValue(1.0f).Step(0.05f).Format("%.2f").Tooltip(
                "Post-processing contrast. 1.00 leaves the image unchanged."
            )
        );

    AddWidget(path, "Saturation", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("PostSaturation"))
        .PreFunc(hideUnlessPostFx)
        .Options(
            FloatSliderOptions().Min(0.0f).Max(2.0f).DefaultValue(1.0f).Step(0.05f).Format("%.2f").Tooltip(
                "Post-processing colour saturation. 1.00 leaves the image unchanged, 0.00 is greyscale."
            )
        );

    AddWidget(path, "Colour Tint Strength", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCED_SHADING("PostTintMix"))
        .PreFunc(hideUnlessPostFx)
        .Options(
            FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.0f).Step(0.05f).Format("%.2f").Tooltip(
                "How strongly the colour tint below is blended over the scene."
            )
        );

    AddWidget(path, "Colour Tint", WIDGET_CVAR_COLOR_PICKER)
        .CVar(CVAR_ENHANCED_SHADING("PostTintColor"))
        .PreFunc(hideUnlessPostFx)
        .Options(ColorPickerOptions().DefaultValue({ 255, 255, 255, 255 }).ShowReset());

    // Enhancements > Status Bar
    path = { "Enhancements", "Status Bar", SECTION_COLUMN_1 };
    AddSidebarEntry("Enhancements", path.sidebarName, 2);

    auto hideWithoutBackground = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("StatusBar.ShowBackground"), 1);
    };
    auto hideUnlessShaded = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("StatusBar.ShowBackground"), 1)
            || CVarGetInteger(CVAR_ENHANCEMENT("StatusBar.FlatColor"), 0);
    };
    auto hideUnlessFlat = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("StatusBar.ShowBackground"), 1)
            || !CVarGetInteger(CVAR_ENHANCEMENT("StatusBar.FlatColor"), 0);
    };

    AddWidget(path, "Layout", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Always Show Status Bar", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.AlwaysShow"))
        .Options(
            CheckboxOptions().Tooltip(
                "Keeps the status bar on screen in the overworld instead of hiding it a few seconds after your stats "
                "change. It still hides during cutscenes and conversations."
            )
        );

    AddWidget(path, "Status Bar Scale", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_ENHANCEMENT("StatusBar.Scale"))
        .Options(
            IntSliderOptions().Min(50).Max(200).Step(5).DefaultValue(100).Format("%d%%").Tooltip(
                "Scales the whole status bar. HP, FP and Star Power grow from the left edge; the counters on the "
                "right grow from the right edge."
            )
        );

    AddWidget(path, "Background", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Show Background", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.ShowBackground"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Draws the box behind the status bar."));

    AddWidget(path, "Flat Bottom", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.FlatBottom"))
        .PreFunc(hideWithoutBackground)
        .Options(
            CheckboxOptions().Tooltip(
                "Makes the box one height all the way across, instead of stepping up after the Star Power gauge."
            )
        );

    AddWidget(path, "Flat Color", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.FlatColor"))
        .PreFunc(hideWithoutBackground)
        .Options(
            CheckboxOptions().Tooltip("Fills the box with one solid color, without its highlight, shadow and outline.")
        );

    AddWidget(path, "Highlight Color", WIDGET_CVAR_COLOR_PICKER)
        .CVar(CVAR_ENHANCEMENT("StatusBar.HighlightColor"))
        .PreFunc(hideUnlessShaded)
        .Options(ColorPickerOptions().DefaultValue({ 235, 230, 119, 255 }).UseAlpha().ShowReset());

    AddWidget(path, "Shadow Color", WIDGET_CVAR_COLOR_PICKER)
        .CVar(CVAR_ENHANCEMENT("StatusBar.ShadowColor"))
        .PreFunc(hideUnlessShaded)
        .Options(ColorPickerOptions().DefaultValue({ 142, 90, 37, 255 }).ShowReset());

    AddWidget(path, "Fill Color", WIDGET_CVAR_COLOR_PICKER)
        .CVar(CVAR_ENHANCEMENT("StatusBar.FillColor"))
        .PreFunc(hideUnlessFlat)
        .Options(ColorPickerOptions().DefaultValue({ 185, 155, 75, 255 }).UseAlpha().ShowReset());

    AddWidget(path, "Box Width", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_ENHANCEMENT("StatusBar.BoxWidth"))
        .PreFunc(hideWithoutBackground)
        .Options(
            IntSliderOptions().Min(20).Max(100).Step(5).DefaultValue(100).Format("%d%%").Tooltip(
                "How far the box reaches across the screen. The icons and numbers stay where they are."
            )
        );

    AddWidget(path, "Box Height", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_ENHANCEMENT("StatusBar.BoxHeight"))
        .PreFunc(hideWithoutBackground)
        .Options(
            IntSliderOptions().Min(50).Max(200).Step(5).DefaultValue(100).Format("%d%%").Tooltip(
                "How tall the box is. The icons and numbers stay where they are."
            )
        );

    path.column = SECTION_COLUMN_2;
    AddWidget(path, "Elements", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Show HP", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.ShowHP"))
        .Options(CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Show FP", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.ShowFP"))
        .Options(CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Show Star Power", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.ShowStarPower"))
        .Options(CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Show Star Points", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.ShowStarPoints"))
        .Options(CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Show Coins", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.ShowCoins"))
        .Options(CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Show Star Pieces", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.ShowStarPieces"))
        .Options(CheckboxOptions());

    AddWidget(path, "Show Badges", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("StatusBar.ShowBadges"))
        .Options(CheckboxOptions());

    AddWidget(path, "Reset", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Reset Status Bar", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            // Clearing a block reloads every CVar from disk, so flush pending changes elsewhere first
            CVarSave();
            CVarClearBlock(CVAR_ENHANCEMENT("StatusBar"));
        })
        .Options(ButtonOptions().Tooltip("Puts every option on this page back to its default."));
}

} // namespace PaperboatGui
