/* Headless smoke: open a file, wait for frames, print progress. Exit 0 on success. */
#include "media/media_player.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <media>\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    zplayer::MediaPlayer player;
    if (!player.open(path)) {
        std::fprintf(stderr, "open failed: %s\n", path.c_str());
        return 1;
    }
    std::fprintf(stderr, "open ok video=%dx%d audio=%d decode=%s hw=%d duration=%.3f\n", player.videoWidth(),
                 player.videoHeight(), player.hasAudio() ? 1 : 0, player.decodeBackendName().c_str(),
                 player.usingHardwareDecode() ? 1 : 0, player.durationSec());

    int got = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline && got < 30) {
        player.markPlaybackStarted();
        zplayer::VideoFrame frame;
        if (player.takeFrameForTime(player.mediaTimeSec(), frame)) {
            ++got;
            std::fprintf(stderr, "frame#%d %dx%d pts=%.3f\n", got, frame.width, frame.height, frame.ptsSec);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    player.close();
    if (got < 5) {
        std::fprintf(stderr, "FAIL: only got %d frames\n", got);
        return 1;
    }
    std::fprintf(stderr, "PASS: got %d frames\n", got);
    return 0;
}
