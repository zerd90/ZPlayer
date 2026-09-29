#define IMGUI_DEFINE_MATH_OPERATORS
#include "ImGuiApplication.h"
#include "imgui.h"

#include "media/media_player.h"
#include "present/video_present.h"
#include "ui/player_view.h"

#include <string>
#include <vector>

using namespace ImGui;

class ZPlayerMainWindow : public ImGuiApplication
{
public:
    ZPlayerMainWindow();
    ~ZPlayerMainWindow() override;

    void presetInternal() override;
    bool renderUI() override;
    void transferCmdArgs(std::vector<std::string> &args) override;
    void dropFile(const std::vector<std::string> &files) override;
    void exitInternal() override;

private:
    void openPath(const std::string &path);

    zplayer::MediaPlayer player_;
    zplayer::VideoPresent present_;
    zplayer::PlayerView view_;
};

ZPlayerMainWindow gApp;

ZPlayerMainWindow::ZPlayerMainWindow()
{
    view_.setPlayer(&player_);
    view_.setPresent(&present_);
}

ZPlayerMainWindow::~ZPlayerMainWindow()
{
    player_.close();
    present_.clear();
}

void ZPlayerMainWindow::presetInternal()
{
    mApplicationName = "ZPlayer";
}

void ZPlayerMainWindow::openPath(const std::string &path)
{
    present_.clear();
    view_.setStatus("正在打开: " + path);
    addLog("Open: " + path);
    if (!player_.open(path)) {
        view_.setStatus("打开失败: " + path);
        addLog("Open failed: " + path);
        return;
    }
    const std::string backend = player_.decodeBackendName();
    view_.setStatus(path + "  [" + backend + "]");
    addLog("Open ok decode=" + backend +
           (player_.usingHardwareDecode() ? " (hardware)" : " (software)"));
}

void ZPlayerMainWindow::transferCmdArgs(std::vector<std::string> &args)
{
    // args[0] is the executable; open the first subsequent non-flag path.
    for (size_t i = 1; i < args.size(); ++i) {
        const auto &a = args[i];
        if (!a.empty() && a[0] != '-') {
            openPath(a);
            return;
        }
    }
}

void ZPlayerMainWindow::dropFile(const std::vector<std::string> &files)
{
    if (files.empty())
        return;
    openPath(files.front());
}

bool ZPlayerMainWindow::renderUI()
{
    view_.draw();
    return false;
}

void ZPlayerMainWindow::exitInternal()
{
    player_.close();
    present_.clear();
}
