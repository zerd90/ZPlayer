#include "audio_output.h"

#include "miniaudio.h"

#include <algorithm>
#include <cstring>

namespace zplayer {

void audioDataCallback(ma_device *device, void *output, const void * /*input*/, unsigned int frameCount)
{
    auto *self = static_cast<AudioOutput *>(device->pUserData);
    if (self)
        self->onCallback(static_cast<float *>(output), frameCount);
}

AudioOutput::AudioOutput() = default;

AudioOutput::~AudioOutput()
{
    stop();
}

bool AudioOutput::prepare(int sampleRate, int channels)
{
    if (sampleRate <= 0 || channels <= 0)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    sampleRate_ = sampleRate;
    channels_ = channels;
    capacityFrames_ = static_cast<size_t>(sampleRate_) * 3;
    ring_.assign(capacityFrames_ * static_cast<size_t>(channels_), 0.f);
    readPos_ = writePos_ = 0;
    paused_ = false;
    return true;
}

bool AudioOutput::startDevice()
{
    if (device_) {
        if (paused_) {
            paused_ = false;
            ma_device_start(device_);
        }
        return true;
    }
    if (sampleRate_ <= 0 || channels_ <= 0)
        return false;

    device_ = new ma_device();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = static_cast<ma_uint32>(channels_);
    config.sampleRate = static_cast<ma_uint32>(sampleRate_);
    config.dataCallback = audioDataCallback;
    config.pUserData = this;
    config.periodSizeInFrames = 256;

    if (ma_device_init(nullptr, &config, device_) != MA_SUCCESS) {
        delete device_;
        device_ = nullptr;
        return false;
    }
    if (ma_device_start(device_) != MA_SUCCESS) {
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
        return false;
    }
    paused_ = false;
    return true;
}

bool AudioOutput::start(int sampleRate, int channels)
{
    stop();
    if (!prepare(sampleRate, channels))
        return false;
    return startDevice();
}

void AudioOutput::setPaused(bool paused)
{
    if (!device_) {
        paused_ = paused;
        return;
    }
    if (paused == paused_)
        return;
    paused_ = paused;
    if (paused)
        ma_device_stop(device_);
    else
        ma_device_start(device_);
}

void AudioOutput::clearBuffer()
{
    std::lock_guard<std::mutex> lock(mutex_);
    readPos_ = writePos_ = 0;
    if (!ring_.empty())
        std::fill(ring_.begin(), ring_.end(), 0.f);
}

void AudioOutput::stop()
{
    if (device_) {
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.clear();
    readPos_ = writePos_ = capacityFrames_ = 0;
    sampleRate_ = 0;
    channels_ = 0;
    paused_ = false;
}

void AudioOutput::write(const float *samples, size_t frameCount)
{
    if (!samples || frameCount == 0 || capacityFrames_ == 0)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    const size_t ch = static_cast<size_t>(channels_);
    for (size_t i = 0; i < frameCount; ++i) {
        size_t next = (writePos_ + 1) % capacityFrames_;
        if (next == readPos_)
            readPos_ = (readPos_ + 1) % capacityFrames_;
        const float *src = samples + i * ch;
        float *dst = ring_.data() + writePos_ * ch;
        std::memcpy(dst, src, ch * sizeof(float));
        writePos_ = next;
    }
}

void AudioOutput::onCallback(float *output, unsigned int frameCount)
{
    const size_t ch = static_cast<size_t>(channels_);
    std::lock_guard<std::mutex> lock(mutex_);
    for (unsigned int i = 0; i < frameCount; ++i) {
        float *dst = output + i * ch;
        if (readPos_ == writePos_) {
            std::memset(dst, 0, ch * sizeof(float));
            continue;
        }
        const float *src = ring_.data() + readPos_ * ch;
        std::memcpy(dst, src, ch * sizeof(float));
        readPos_ = (readPos_ + 1) % capacityFrames_;
    }
}

} // namespace zplayer
