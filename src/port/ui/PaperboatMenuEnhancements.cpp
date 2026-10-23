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

    AddWidget(path, "Mute ISpy Sound", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("MuteISpySound"))
        .Options(CheckboxOptions().Tooltip("Mute the notification sound when ISpy is going off."));

    AddWidget(path, "Always Chuck Quizmo", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("AlwaysChuckQuizmo"))
        .Options(
            CheckboxOptions().Tooltip(
                "Chuck Quizmo normally relies on rng for the location. "
                "Makes Chuck Quizmo Show up in the current map you are in if option is turned on. "
            )
        );

    // TTYD64 Battle Tracks toggle
    path.column = SECTION_COLUMN_2;
    AddWidget(path, "TTYD64 Battle Tracks", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("TTYD64BattleTracks"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Replace vanilla battle tracks with TTYD64 versions (Battle Theme, Battle Won, Level Up). Requires game restart to take effect."));

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

    // Enhancements > Shaders
    path = { "Enhancements", "Shaders", SECTION_COLUMN_1 };
    AddSidebarEntry("Enhancements", "Shaders", 1);

    // Master toggle
    AddWidget(path, "Enhanced Shading (Master)", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("Enabled"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Master switch for all enhanced shading and post-processing effects."));

    // Core Shading (in-shader effects)
    AddWidget(path, "Core Shading", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Engine Lighting", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("EngineLighting"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Per-fragment lighting with sun/hemisphere/rim. Base for modern look."));
    
    AddWidget(path, "Modern Look (Color Grade)", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("ModernLook"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Saturation/Contrast/Brightness color grade. Color Splash style."));
    
    AddWidget(path, "Area Ambient", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("AreaAmbient"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Per-area ambient tint from background palette. Origami King style."));
    
    AddWidget(path, "Fragment Lighting", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("FragmentLighting"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Directional + hemisphere + rim lighting per fragment."));
    
    AddWidget(path, "Relight Mode", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("RelightMode"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Albedo recovery relighting for baked vertex colors."));
    
    AddWidget(path, "Tone Mapping", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("ToneMapping"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Filmic/ACES/Reinhard tonemapping for HDR look."));

    // Post-Processing Passes
    path.column = SECTION_COLUMN_2;
    AddWidget(path, "Post-Processing", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "SSAO", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("SSAO"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Screen-space ambient occlusion. Can cause outlines on UI."));
    
    AddWidget(path, "Depth Sun Shading", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("DepthSunShading"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Depth-based sun shading."));
    
    AddWidget(path, "God Rays", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("GodRays"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Volumetric light shafts from sun."));
    
    AddWidget(path, "Contact Shadows", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("ContactShadows"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Short-range screen-space contact shadows."));
    
    AddWidget(path, "Screen-Space Shadows", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("ScreenSpaceShadows"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Long-range directional shadows."));
    
    AddWidget(path, "Bloom", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("Bloom"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Bloom/glow post-process."));
    
    AddWidget(path, "FXAA", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("FXAA"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Fast approximate anti-aliasing."));
    
    AddWidget(path, "Specular", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("Specular"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Screen-space specular highlights."));

    // Modern Shader Suite (Advanced)
    AddWidget(path, "Modern Suite (Advanced)", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "PBR (Metallic/Roughness)", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("PBR"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Physically Based Rendering with ORM textures. Needs asset support."));
    
    AddWidget(path, "3D LUT Color Grading", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("LUT"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("3D LUT color grading for per-area mood. Origami King style."));
    
    AddWidget(path, "HDR Tonemap (ACES/Filmic)", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("HDR"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("HDR rendering with ACES/Filmic/Reinhard + auto-exposure."));
    
    AddWidget(path, "Volumetric Light Shafts", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("Volumetric"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Ray-marched volumetric light shafts. Heavy."));
    
    AddWidget(path, "TAA (Temporal AA)", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("TAA"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Temporal AA with jitter + history. Can ghost."));
    
    AddWidget(path, "Motion Blur", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("MotionBlur"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Camera/object motion blur via velocity buffer."));
    
    AddWidget(path, "Parallax Occlusion Mapping", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("Parallax"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("POM for depth illusion. Needs height maps."));
    
    AddWidget(path, "VRS (Variable Rate Shading)", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCED_SHADING("VRS"))
        .Options(CheckboxOptions().DefaultValue(false).Tooltip("Simulated Variable Rate Shading (adaptive shading rate)."));

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
