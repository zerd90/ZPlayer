#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

struct ma_device;

namespace zplayer {

/** miniaudio device output: float32 interleaved PCM ring → speakers. */
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    AudioOutput(const AudioOutput &) = delete;
    AudioOutput &operator=(const AudioOutput &) = delete;

    /** Prepare ring (sample rate/channels) without starting the device yet. */
    bool prepare(int sampleRate, int channels);
    /** Start device; no-op if already running. Requires prepare() first. */
    bool startDevice();
    /** prepare() + startDevice(). */
    bool start(int sampleRate, int channels);
    void stop();
    bool isRunning() const { return device_ != nullptr; }

    /** Push interleaved float samples. Works before or after startDevice(). */
    void write(const float *samples, size_t frameCount);

    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }

private:
    friend void audioDataCallback(ma_device *device, void *output, const void *input, unsigned int frameCount);

    void onCallback(float *output, unsigned int frameCount);

    ma_device *device_ = nullptr;
    int sampleRate_ = 0;
    int channels_ = 0;

    mutable std::mutex mutex_;
    std::vector<float> ring_;
    size_t readPos_ = 0;
    size_t writePos_ = 0;
    size_t capacityFrames_ = 0; // ring size in frames
};

} // namespace zplayer
