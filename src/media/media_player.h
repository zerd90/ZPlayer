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
 * Prefers platform hardware decode (VT / D3D11VA / VAAPI) with software fallback.
 * Master clock respects pause / seek; UI drives present timing.
 */
class MediaPlayer {
public:
    static constexpr size_t kMaxVideoQueue = 12;
    /** Default skip step for rewind / fast-forward buttons (seconds). */
    static constexpr double kSkipStepSec = 10.0;

    MediaPlayer();
    ~MediaPlayer();

    MediaPlayer(const MediaPlayer &) = delete;
    MediaPlayer &operator=(const MediaPlayer &) = delete;

    bool open(const std::string &path);
    void close();

    bool isOpen() const { return open_.load(); }
    bool hasAudio() const { return hasAudio_.load(); }
    bool hasVideo() const { return hasVideo_.load(); }
    int videoWidth() const { return videoWidth_.load(); }
    int videoHeight() const { return videoHeight_.load(); }
    const std::string &path() const { return path_; }

    bool usingHardwareDecode() const { return usingHw_.load(); }
    std::string decodeBackendName() const;

    double durationSec() const { return durationSec_.load(); }

    bool takeFrameForTime(double mediaTimeSec, VideoFrame &out);
    double mediaTimeSec() const;
    void markPlaybackStarted();

    bool isPaused() const { return paused_.load(); }
    void setPaused(bool paused);
    void togglePause() { setPaused(!isPaused()); }

    /** Absolute seek (seconds). Decode thread flushes codecs/queues/audio. */
    void seek(double sec);
    /** Relative skip (e.g. ±kSkipStepSec). */
    void skip(double deltaSec);

private:
    void decodeLoop();
    void clearQueues();
    void startClockIfNeeded();
    void setMediaClockSec(double sec);

    std::string path_;
    std::atomic<bool> open_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> hasAudio_{false};
    std::atomic<bool> hasVideo_{false};
    std::atomic<bool> usingHw_{false};
    std::atomic<int> videoWidth_{0};
    std::atomic<int> videoHeight_{0};
    std::atomic<double> durationSec_{0.0};
    std::string hwDeviceName_;

    std::thread decodeThread_;
    AudioOutput audio_;

    mutable std::mutex videoMutex_;
    std::condition_variable videoCv_;
    std::deque<VideoFrame> videoQueue_;

    mutable std::mutex clockMutex_;
    bool clockStarted_ = false;
    int64_t clockAnchorUs_ = 0;
    double mediaBaseSec_ = 0.0;
    double pausedAtSec_ = 0.0;
    std::atomic<bool> paused_{false};

    std::mutex seekMutex_;
    std::atomic<bool> seekPending_{false};
    double seekTargetSec_ = 0.0;
    std::atomic<bool> decodeOneAfterSeek_{false};
    /** Drop decoded frames earlier than this until a frame near the seek lands. */
    std::atomic<double> discardBeforeSec_{-1.0};
};

} // namespace zplayer
