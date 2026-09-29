#pragma once

#include "audio_output.h"
#include "video_frame.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace zplayer {

/**
 * Demux + decode worker. Video frames → bounded queue; audio → AudioOutput.
 * Master clock: wall time from first presented frame (UI drives present).
 */
class MediaPlayer {
public:
    static constexpr size_t kMaxVideoQueue = 12;

    MediaPlayer();
    ~MediaPlayer();

    MediaPlayer(const MediaPlayer &) = delete;
    MediaPlayer &operator=(const MediaPlayer &) = delete;

    /** Open path and start decode thread. Stops any previous session. */
    bool open(const std::string &path);
    void close();

    bool isOpen() const { return open_.load(); }
    bool hasAudio() const { return hasAudio_.load(); }
    bool hasVideo() const { return hasVideo_.load(); }
    int videoWidth() const { return videoWidth_.load(); }
    int videoHeight() const { return videoHeight_.load(); }
    const std::string &path() const { return path_; }

    /**
     * Pop/update the frame that should show at mediaTimeSec.
     * Drops late frames; keeps last good frame if waiting for next.
     * Returns true if out was filled (possibly same as previous).
     */
    bool takeFrameForTime(double mediaTimeSec, VideoFrame &out);

    /** Seconds since playback start (wall clock). Starts on first takeFrameForTime. */
    double mediaTimeSec() const;

    void markPlaybackStarted();

private:
    void decodeLoop();
    void clearQueues();
    void startClockIfNeeded();

    std::string path_;
    std::atomic<bool> open_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> hasAudio_{false};
    std::atomic<bool> hasVideo_{false};
    std::atomic<int> videoWidth_{0};
    std::atomic<int> videoHeight_{0};

    std::thread decodeThread_;
    AudioOutput audio_;

    mutable std::mutex videoMutex_;
    std::condition_variable videoCv_;
    std::deque<VideoFrame> videoQueue_;

    mutable std::mutex clockMutex_;
    bool clockStarted_ = false;
    int64_t clockStartUs_ = 0; // steady_clock microseconds at media t=0
};

} // namespace zplayer
