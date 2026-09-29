#pragma once

#include <cstdint>
#include <vector>

namespace zplayer {

/** Presentable video frame (packed RGBA8888). Owned by media → present. */
struct VideoFrame {
    int width = 0;
    int height = 0;
    double ptsSec = 0.0;
    std::vector<uint8_t> rgba; // width * height * 4
};

} // namespace zplayer
