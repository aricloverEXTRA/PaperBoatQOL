#pragma once

#include <libultraship/libultraship.h>

class ShaderDashboardWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override{};
};