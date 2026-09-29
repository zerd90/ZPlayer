#include "media_player.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
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

} // namespace

MediaPlayer::MediaPlayer() = default;

MediaPlayer::~MediaPlayer()
{
    close();
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
    videoWidth_ = 0;
    videoHeight_ = 0;
    {
        std::lock_guard<std::mutex> lock(clockMutex_);
        clockStarted_ = false;
        clockStartUs_ = 0;
    }
    clearQueues();

    decodeThread_ = std::thread(&MediaPlayer::decodeLoop, this);
    // Wait briefly for open success/fail via open_ flag set in decodeLoop after format open.
    // Decode loop sets open_ true once streams are ready; false + stop if fail.
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

    // Drop frames that are clearly late (>1 frame worth behind).
    while (videoQueue_.size() > 1 && videoQueue_.front().ptsSec < mediaTimeSec - 0.05)
        videoQueue_.pop_front();

    const VideoFrame &front = videoQueue_.front();
    if (front.ptsSec > mediaTimeSec + 0.001) {
        // Too early — keep waiting; still provide last shown if we already have out.
        return false;
    }

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
        if (avcodec_open2(videoCtx, dec, nullptr) < 0) {
            fail("avcodec_open2 video failed");
            goto cleanup;
        }
        videoTb = st->time_base;
        videoWidth_ = videoCtx->width;
        videoHeight_ = videoCtx->height;
        hasVideo_ = true;

        rgbaFrame = av_frame_alloc();
        if (!rgbaFrame) {
            fail("av_frame_alloc rgba failed");
            goto cleanup;
        }
        rgbaFrame->format = AV_PIX_FMT_RGBA;
        rgbaFrame->width = videoCtx->width;
        rgbaFrame->height = videoCtx->height;
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
    std::fprintf(stderr, "[ZPlayer] opened %s video=%dx%d audio=%s\n", path_.c_str(), videoWidth_.load(),
                 videoHeight_.load(), hasAudio_.load() ? "yes" : "no");

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

                sws = sws_getCachedContext(sws, frame->width, frame->height, (AVPixelFormat)frame->format,
                                           rgbaFrame->width, rgbaFrame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                           nullptr, nullptr);
                if (!sws) {
                    av_frame_unref(frame);
                    continue;
                }
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, rgbaFrame->data, rgbaFrame->linesize);

                VideoFrame vf;
                vf.width = rgbaFrame->width;
                vf.height = rgbaFrame->height;
                vf.ptsSec = packetPtsSec(frame, videoTb);
                const size_t rowBytes = static_cast<size_t>(vf.width) * 4;
                vf.rgba.resize(rowBytes * static_cast<size_t>(vf.height));
                for (int y = 0; y < vf.height; ++y) {
                    std::memcpy(vf.rgba.data() + static_cast<size_t>(y) * rowBytes, rgbaFrame->data[0] + y * rgbaFrame->linesize[0],
                                rowBytes);
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

    // Flush video decoder
    if (videoCtx && !stop_.load()) {
        avcodec_send_packet(videoCtx, nullptr);
        while (!stop_.load()) {
            int ret = avcodec_receive_frame(videoCtx, frame);
            if (ret < 0)
                break;
            sws = sws_getCachedContext(sws, frame->width, frame->height, (AVPixelFormat)frame->format, rgbaFrame->width,
                                       rgbaFrame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (sws) {
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, rgbaFrame->data, rgbaFrame->linesize);
                VideoFrame vf;
                vf.width = rgbaFrame->width;
                vf.height = rgbaFrame->height;
                vf.ptsSec = packetPtsSec(frame, videoTb);
                const size_t rowBytes = static_cast<size_t>(vf.width) * 4;
                vf.rgba.resize(rowBytes * static_cast<size_t>(vf.height));
                for (int y = 0; y < vf.height; ++y) {
                    std::memcpy(vf.rgba.data() + static_cast<size_t>(y) * rowBytes, rgbaFrame->data[0] + y * rgbaFrame->linesize[0],
                                rowBytes);
                }
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
    avcodec_free_context(&audioCtx);
    avcodec_free_context(&videoCtx);
    if (fmt)
        avformat_close_input(&fmt);
    // Keep open_=true until close() so UI can drain remaining frames after EOF.
    if (!hasVideo_.load())
        open_ = false;
}

} // namespace zplayer
