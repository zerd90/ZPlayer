#include "media_player.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace zplayer {
namespace {

int64_t steadyUsNow()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

double packetPtsSec(const AVFrame *frame, AVRational timeBase)
{
    int64_t pts = frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
        pts = frame->pts;
    if (pts == AV_NOPTS_VALUE)
        return 0.0;
    return pts * av_q2d(timeBase);
}

AVHWDeviceType preferredHwDeviceType()
{
#if defined(__APPLE__)
    return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#elif defined(_WIN32)
    return AV_HWDEVICE_TYPE_D3D11VA;
#elif defined(__linux__)
    return AV_HWDEVICE_TYPE_VAAPI;
#else
    return AV_HWDEVICE_TYPE_NONE;
#endif
}

const char *hwDeviceTypeName(AVHWDeviceType type)
{
    const char *n = av_hwdevice_get_type_name(type);
    return n ? n : "unknown";
}

struct HwDecodeState {
    AVBufferRef *deviceCtx = nullptr;
    AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
};

enum AVPixelFormat getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    auto *hw = static_cast<HwDecodeState *>(ctx->opaque);
    if (!hw)
        return AV_PIX_FMT_NONE;
    for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == hw->hwPixFmt)
            return *p;
    }
    return AV_PIX_FMT_NONE;
}

bool findHwPixelFormat(const AVCodec *dec, AVHWDeviceType type, AVPixelFormat *outFmt)
{
    for (int i = 0;; ++i) {
        const AVCodecHWConfig *cfg = avcodec_get_hw_config(dec, i);
        if (!cfg)
            break;
        if (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && cfg->device_type == type) {
            *outFmt = cfg->pix_fmt;
            return true;
        }
    }
    return false;
}

/** Try hardware path; on any failure leave ctx ready for soft open (caller opens). */
bool tryInitHardware(AVCodecContext *ctx, const AVCodec *dec, HwDecodeState *hw, std::string *deviceName)
{
    const AVHWDeviceType type = preferredHwDeviceType();
    if (type == AV_HWDEVICE_TYPE_NONE)
        return false;

    AVPixelFormat hwFmt = AV_PIX_FMT_NONE;
    if (!findHwPixelFormat(dec, type, &hwFmt)) {
        std::fprintf(stderr, "[ZPlayer] hw: no hw_config for %s on this codec; soft decode\n",
                     hwDeviceTypeName(type));
        return false;
    }

    AVBufferRef *device = nullptr;
    if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0) {
        std::fprintf(stderr, "[ZPlayer] hw: av_hwdevice_ctx_create(%s) failed; soft decode\n",
                     hwDeviceTypeName(type));
        return false;
    }

    hw->deviceCtx = device;
    hw->hwPixFmt = hwFmt;
    ctx->opaque = hw;
    ctx->get_format = getHwFormat;
    ctx->hw_device_ctx = av_buffer_ref(device);
    if (!ctx->hw_device_ctx) {
        av_buffer_unref(&hw->deviceCtx);
        ctx->opaque = nullptr;
        ctx->get_format = nullptr;
        return false;
    }

    *deviceName = hwDeviceTypeName(type);
    return true;
}

void clearHardware(AVCodecContext *ctx, HwDecodeState *hw)
{
    if (ctx) {
        av_buffer_unref(&ctx->hw_device_ctx);
        ctx->opaque = nullptr;
        ctx->get_format = nullptr;
    }
    if (hw)
        av_buffer_unref(&hw->deviceCtx);
}

bool frameToRgba(AVFrame *src, AVFrame *rgbaFrame, SwsContext **sws, VideoFrame &out, AVRational videoTb)
{
    // Hardware surfaces must be transferred to a software frame first.
    AVFrame *swFrame = nullptr;
    AVFrame *use = src;
    bool transferred = false;

    if (src->format == AV_PIX_FMT_VIDEOTOOLBOX || src->format == AV_PIX_FMT_D3D11 ||
        src->format == AV_PIX_FMT_D3D11VA_VLD || src->format == AV_PIX_FMT_VAAPI ||
        (src->hw_frames_ctx != nullptr)) {
        swFrame = av_frame_alloc();
        if (!swFrame)
            return false;
        if (av_hwframe_transfer_data(swFrame, src, 0) < 0) {
            av_frame_free(&swFrame);
            return false;
        }
        swFrame->best_effort_timestamp = src->best_effort_timestamp;
        swFrame->pts = src->pts;
        use = swFrame;
        transferred = true;
    }

    *sws = sws_getCachedContext(*sws, use->width, use->height, static_cast<AVPixelFormat>(use->format),
                                rgbaFrame->width, rgbaFrame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                nullptr, nullptr);
    if (!*sws) {
        if (transferred)
            av_frame_free(&swFrame);
        return false;
    }
    sws_scale(*sws, use->data, use->linesize, 0, use->height, rgbaFrame->data, rgbaFrame->linesize);

    out.width = rgbaFrame->width;
    out.height = rgbaFrame->height;
    out.ptsSec = packetPtsSec(src, videoTb);
    const size_t rowBytes = static_cast<size_t>(out.width) * 4;
    out.rgba.resize(rowBytes * static_cast<size_t>(out.height));
    for (int y = 0; y < out.height; ++y) {
        std::memcpy(out.rgba.data() + static_cast<size_t>(y) * rowBytes,
                    rgbaFrame->data[0] + y * rgbaFrame->linesize[0], rowBytes);
    }

    if (transferred)
        av_frame_free(&swFrame);
    return true;
}

} // namespace

MediaPlayer::MediaPlayer() = default;

MediaPlayer::~MediaPlayer()
{
    close();
}

std::string MediaPlayer::decodeBackendName() const
{
    if (!usingHw_.load())
        return "sw";
    if (!hwDeviceName_.empty())
        return std::string("hw:") + hwDeviceName_;
    return "hw";
}

void MediaPlayer::clearQueues()
{
    {
        std::lock_guard<std::mutex> lock(videoMutex_);
        videoQueue_.clear();
    }
    videoCv_.notify_all();
}

bool MediaPlayer::open(const std::string &path)
{
    close();
    path_ = path;
    stop_ = false;
    open_ = false;
    hasAudio_ = false;
    hasVideo_ = false;
    usingHw_ = false;
    hwDeviceName_.clear();
    videoWidth_ = 0;
    videoHeight_ = 0;
    durationSec_ = 0.0;
    {
        std::lock_guard<std::mutex> lock(clockMutex_);
        clockStarted_ = false;
        clockStartUs_ = 0;
    }
    clearQueues();

    decodeThread_ = std::thread(&MediaPlayer::decodeLoop, this);
    for (int i = 0; i < 200; ++i) {
        if (open_.load() || stop_.load())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return open_.load();
}

void MediaPlayer::close()
{
    stop_ = true;
    videoCv_.notify_all();
    if (decodeThread_.joinable())
        decodeThread_.join();
    audio_.stop();
    clearQueues();
    open_ = false;
    hasAudio_ = false;
    hasVideo_ = false;
    usingHw_ = false;
    hwDeviceName_.clear();
    {
        std::lock_guard<std::mutex> lock(clockMutex_);
        clockStarted_ = false;
        clockStartUs_ = 0;
    }
}

void MediaPlayer::markPlaybackStarted()
{
    startClockIfNeeded();
}

void MediaPlayer::startClockIfNeeded()
{
    bool shouldStartAudio = false;
    {
        std::lock_guard<std::mutex> lock(clockMutex_);
        if (!clockStarted_) {
            clockStarted_ = true;
            clockStartUs_ = steadyUsNow();
            shouldStartAudio = hasAudio_.load();
        }
    }
    if (shouldStartAudio)
        audio_.startDevice();
}

double MediaPlayer::mediaTimeSec() const
{
    std::lock_guard<std::mutex> lock(clockMutex_);
    if (!clockStarted_)
        return 0.0;
    return (steadyUsNow() - clockStartUs_) / 1'000'000.0;
}

bool MediaPlayer::takeFrameForTime(double mediaTimeSec, VideoFrame &out)
{
    std::lock_guard<std::mutex> lock(videoMutex_);
    if (videoQueue_.empty())
        return false;

    while (videoQueue_.size() > 1 && videoQueue_.front().ptsSec < mediaTimeSec - 0.05)
        videoQueue_.pop_front();

    const VideoFrame &front = videoQueue_.front();
    if (front.ptsSec > mediaTimeSec + 0.001)
        return false;

    out = std::move(videoQueue_.front());
    videoQueue_.pop_front();
    videoCv_.notify_one();
    return true;
}

void MediaPlayer::decodeLoop()
{
    AVFormatContext *fmt = nullptr;
    AVCodecContext *videoCtx = nullptr;
    AVCodecContext *audioCtx = nullptr;
    SwsContext *sws = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *frame = nullptr;
    AVFrame *rgbaFrame = nullptr;
    HwDecodeState hw{};

    int videoStream = -1;
    int audioStream = -1;
    AVRational videoTb{};
    AVChannelLayout outLayout{};
    int outSampleRate = 48000;
    const int outChannels = 2;

    auto fail = [&](const char *msg) {
        std::fprintf(stderr, "[ZPlayer] %s\n", msg);
        stop_ = true;
        open_ = false;
    };

    if (avformat_open_input(&fmt, path_.c_str(), nullptr, nullptr) < 0) {
        fail("avformat_open_input failed");
        goto cleanup;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        fail("avformat_find_stream_info failed");
        goto cleanup;
    }

    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        durationSec_ = static_cast<double>(fmt->duration) / static_cast<double>(AV_TIME_BASE);

    videoStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audioStream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (videoStream >= 0) {
        AVStream *st = fmt->streams[videoStream];
        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            fail("no video decoder");
            goto cleanup;
        }
        videoCtx = avcodec_alloc_context3(dec);
        if (!videoCtx || avcodec_parameters_to_context(videoCtx, st->codecpar) < 0) {
            fail("video codec context failed");
            goto cleanup;
        }

        bool hwOk = tryInitHardware(videoCtx, dec, &hw, &hwDeviceName_);
        if (hwOk) {
            if (avcodec_open2(videoCtx, dec, nullptr) < 0) {
                std::fprintf(stderr, "[ZPlayer] hw: avcodec_open2 failed; falling back to soft\n");
                clearHardware(videoCtx, &hw);
                hw = {};
                hwDeviceName_.clear();
                // Re-apply codecpar after clearing hw fields.
                avcodec_free_context(&videoCtx);
                videoCtx = avcodec_alloc_context3(dec);
                if (!videoCtx || avcodec_parameters_to_context(videoCtx, st->codecpar) < 0 ||
                    avcodec_open2(videoCtx, dec, nullptr) < 0) {
                    fail("avcodec_open2 video failed (soft fallback)");
                    goto cleanup;
                }
                hwOk = false;
            }
        } else {
            if (avcodec_open2(videoCtx, dec, nullptr) < 0) {
                fail("avcodec_open2 video failed");
                goto cleanup;
            }
        }

        usingHw_ = hwOk;
        videoTb = st->time_base;
        videoWidth_ = videoCtx->width > 0 ? videoCtx->width : st->codecpar->width;
        videoHeight_ = videoCtx->height > 0 ? videoCtx->height : st->codecpar->height;
        hasVideo_ = true;

        if (durationSec_.load() <= 0.0 && st->duration != AV_NOPTS_VALUE && st->duration > 0)
            durationSec_ = st->duration * av_q2d(st->time_base);

        rgbaFrame = av_frame_alloc();
        if (!rgbaFrame) {
            fail("av_frame_alloc rgba failed");
            goto cleanup;
        }
        rgbaFrame->format = AV_PIX_FMT_RGBA;
        rgbaFrame->width = videoWidth_.load();
        rgbaFrame->height = videoHeight_.load();
        if (av_frame_get_buffer(rgbaFrame, 32) < 0) {
            fail("rgba buffer alloc failed");
            goto cleanup;
        }
    }

    if (audioStream >= 0) {
        AVStream *st = fmt->streams[audioStream];
        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (dec) {
            audioCtx = avcodec_alloc_context3(dec);
            if (audioCtx && avcodec_parameters_to_context(audioCtx, st->codecpar) >= 0 &&
                avcodec_open2(audioCtx, dec, nullptr) >= 0) {
                av_channel_layout_default(&outLayout, outChannels);
                outSampleRate = audioCtx->sample_rate > 0 ? audioCtx->sample_rate : 48000;
                if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, outSampleRate, &audioCtx->ch_layout,
                                        audioCtx->sample_fmt, audioCtx->sample_rate, 0, nullptr) >= 0 &&
                    swr_init(swr) >= 0) {
                    if (audio_.prepare(outSampleRate, outChannels))
                        hasAudio_ = true;
                    else
                        std::fprintf(stderr, "[ZPlayer] audio prepare failed; continuing video-only\n");
                } else {
                    std::fprintf(stderr, "[ZPlayer] swr init failed; continuing video-only\n");
                    swr_free(&swr);
                }
            } else {
                std::fprintf(stderr, "[ZPlayer] audio codec open failed; continuing video-only\n");
                avcodec_free_context(&audioCtx);
            }
        }
    }

    if (!hasVideo_.load()) {
        fail("no video stream");
        goto cleanup;
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        fail("packet/frame alloc failed");
        goto cleanup;
    }

    open_ = true;
    std::fprintf(stderr, "[ZPlayer] opened %s video=%dx%d audio=%s decode=%s duration=%.3fs\n", path_.c_str(),
                 videoWidth_.load(), videoHeight_.load(), hasAudio_.load() ? "yes" : "no",
                 decodeBackendName().c_str(), durationSec_.load());

    while (!stop_.load()) {
        int ret = av_read_frame(fmt, pkt);
        if (ret < 0)
            break;

        if (pkt->stream_index == videoStream && videoCtx) {
            if (avcodec_send_packet(videoCtx, pkt) < 0) {
                av_packet_unref(pkt);
                continue;
            }
            while (!stop_.load()) {
                ret = avcodec_receive_frame(videoCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                if (ret < 0)
                    break;

                // Late size discovery after first hw frame.
                if (frame->width > 0 && frame->height > 0 &&
                    (rgbaFrame->width != frame->width || rgbaFrame->height != frame->height)) {
                    av_frame_unref(rgbaFrame);
                    rgbaFrame->format = AV_PIX_FMT_RGBA;
                    rgbaFrame->width = frame->width;
                    rgbaFrame->height = frame->height;
                    if (av_frame_get_buffer(rgbaFrame, 32) < 0) {
                        av_frame_unref(frame);
                        break;
                    }
                    videoWidth_ = frame->width;
                    videoHeight_ = frame->height;
                }

                VideoFrame vf;
                if (!frameToRgba(frame, rgbaFrame, &sws, vf, videoTb)) {
                    av_frame_unref(frame);
                    continue;
                }

                {
                    std::unique_lock<std::mutex> lock(videoMutex_);
                    videoCv_.wait(lock, [&] { return stop_.load() || videoQueue_.size() < kMaxVideoQueue; });
                    if (stop_.load()) {
                        av_frame_unref(frame);
                        break;
                    }
                    videoQueue_.push_back(std::move(vf));
                }
                startClockIfNeeded();
                av_frame_unref(frame);
            }
        } else if (pkt->stream_index == audioStream && audioCtx && swr && hasAudio_.load()) {
            if (avcodec_send_packet(audioCtx, pkt) < 0) {
                av_packet_unref(pkt);
                continue;
            }
            while (!stop_.load()) {
                ret = avcodec_receive_frame(audioCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                if (ret < 0)
                    break;

                const int outSamples = swr_get_out_samples(swr, frame->nb_samples);
                std::vector<float> interleaved(static_cast<size_t>(std::max(outSamples, 1)) * static_cast<size_t>(outChannels));
                uint8_t *outPlanes[1] = {reinterpret_cast<uint8_t *>(interleaved.data())};
                const int converted =
                    swr_convert(swr, outPlanes, outSamples, (const uint8_t **)frame->extended_data, frame->nb_samples);
                if (converted > 0)
                    audio_.write(interleaved.data(), static_cast<size_t>(converted));
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    if (videoCtx && !stop_.load()) {
        avcodec_send_packet(videoCtx, nullptr);
        while (!stop_.load()) {
            int ret = avcodec_receive_frame(videoCtx, frame);
            if (ret < 0)
                break;
            VideoFrame vf;
            if (frameToRgba(frame, rgbaFrame, &sws, vf, videoTb)) {
                std::unique_lock<std::mutex> lock(videoMutex_);
                videoCv_.wait(lock, [&] { return stop_.load() || videoQueue_.size() < kMaxVideoQueue; });
                if (!stop_.load())
                    videoQueue_.push_back(std::move(vf));
            }
            av_frame_unref(frame);
        }
    }

    std::fprintf(stderr, "[ZPlayer] decode thread finished\n");

cleanup:
    av_channel_layout_uninit(&outLayout);
    swr_free(&swr);
    sws_freeContext(sws);
    av_frame_free(&rgbaFrame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (videoCtx)
        clearHardware(videoCtx, &hw);
    avcodec_free_context(&audioCtx);
    avcodec_free_context(&videoCtx);
    if (fmt)
        avformat_close_input(&fmt);
    if (!hasVideo_.load())
        open_ = false;
}

} // namespace zplayer
