#pragma once

#include "media/media_player.h"
#include "present/video_present.h"

#include <string>

namespace zplayer {

/** ImGui shell: video area + status + transport chrome. Does not own decode/present. */
class PlayerView {
public:
    void setPlayer(MediaPlayer *player) { player_ = player; }
    void setPresent(VideoPresent *present) { present_ = present; }

    /** Draw full work-area video rectangle; advances clock/present. */
    void draw();

    void setStatus(const std::string &status) { status_ = status; }

private:
    void drawProgressBar(float barWidth, double mediaTime, double duration);
    void drawTransportControls();

    // Step-3 UI stubs (wired to MediaPlayer in step 4).
    void onTogglePauseStub();
    void onSkipBackStub();
    void onSkipForwardStub();

    MediaPlayer *player_ = nullptr;
    VideoPresent *present_ = nullptr;
    VideoFrame lastFrame_;
    bool haveLast_ = false;
    std::string status_ = "拖放媒体文件到窗口以播放";

    /** Local chrome state until real pause is connected. */
    bool uiPaused_ = false;
};

} // namespace zplayer
