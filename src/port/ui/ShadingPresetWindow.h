#pragma once
#include <libultraship/libultraship.h>

// Menu window for managing user shading presets: create from the current sliders,
// import/export JSON, apply, reorder and delete. Registered in PaperboatGui.cpp
// and opened from the Graphic Enhancements section.
class ShadingPresetWindow : public Ship::GuiWindow {
public:
    using Ship::GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void Draw() override;
    void UpdateElement() override { };
};
