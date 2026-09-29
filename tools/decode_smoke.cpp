/* Headless smoke: open, decode frames, pause/seek/skip, print backend. Exit 0 on success. */
#include "media/media_player.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

static int waitFrames(zplayer::MediaPlayer &player, int need, int timeoutMs)
{
    int got = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline && got < need) {
        if (!player.isPaused())
            player.markPlaybackStarted();
        zplayer::VideoFrame frame;
        if (player.takeFrameForTime(player.mediaTimeSec(), frame)) {
            ++got;
            std::fprintf(stderr, "frame#%d %dx%d pts=%.3f t=%.3f\n", got, frame.width, frame.height, frame.ptsSec,
                         player.mediaTimeSec());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return got;
}

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

    int got = waitFrames(player, 15, 8000);
    if (got < 5) {
        std::fprintf(stderr, "FAIL: only got %d frames before controls\n", got);
        return 1;
    }

    // Pause should freeze media clock.
    const double tPause = player.mediaTimeSec();
    player.setPaused(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const double tPaused = player.mediaTimeSec();
    if (std::fabs(tPaused - tPause) > 0.05) {
        std::fprintf(stderr, "FAIL: pause clock drifted %.3f -> %.3f\n", tPause, tPaused);
        return 1;
    }
    std::fprintf(stderr, "pause ok at %.3f\n", tPaused);

    // Seek while paused.
    const double seekTo = std::min(1.0, std::max(0.2, player.durationSec() * 0.3));
    player.seek(seekTo);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const double tSeek = player.mediaTimeSec();
    if (std::fabs(tSeek - seekTo) > 0.15) {
        std::fprintf(stderr, "FAIL: seek clock %.3f expected ~%.3f\n", tSeek, seekTo);
        return 1;
    }
    std::fprintf(stderr, "seek ok -> %.3f (target %.3f)\n", tSeek, seekTo);

    player.setPaused(false);
    got = waitFrames(player, 10, 5000);
    if (got < 3) {
        std::fprintf(stderr, "FAIL: only got %d frames after resume\n", got);
        return 1;
    }

    const double beforeSkip = player.mediaTimeSec();
    player.skip(zplayer::MediaPlayer::kSkipStepSec);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const double afterSkip = player.mediaTimeSec();
    std::fprintf(stderr, "skip %+g: %.3f -> %.3f\n", zplayer::MediaPlayer::kSkipStepSec, beforeSkip, afterSkip);

    player.close();
    std::fprintf(stderr, "PASS: got frames, pause/seek/skip exercised\n");
    return 0;
}
