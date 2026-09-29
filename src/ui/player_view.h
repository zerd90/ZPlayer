#pragma once

#include "media/media_player.h"
#include "present/video_present.h"

#include <string>

namespace zplayer {

/** ImGui shell: video area + status text. Does not own decode/present lifetime. */
class PlayerView {
public:
    void setPlayer(MediaPlayer *player) { player_ = player; }
    void setPresent(VideoPresent *present) { present_ = present; }

    /** Draw full work-area video rectangle; advances clock/present. */
    void draw();

    void setStatus(const std::string &status) { status_ = status; }

private:
    MediaPlayer *player_ = nullptr;
    VideoPresent *present_ = nullptr;
    VideoFrame lastFrame_;
    bool haveLast_ = false;
    std::string status_ = "拖放媒体文件到窗口以播放";
};

} // namespace zplayer
