#include "ImGuiApplication.h"
using namespace ImGui;

class ZPlayerMainWindow : public ImGuiApplication
{
public:
    ZPlayerMainWindow() {}
    bool renderUI() override;
};

ZPlayerMainWindow gApp;

bool ZPlayerMainWindow::renderUI()
{
    return false;
}