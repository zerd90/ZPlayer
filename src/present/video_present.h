#pragma once

#include "media/video_frame.h"

#include "ImGuiImageRender.h"

namespace zplayer {

/** Upload VideoFrame to GPU texture; expose ImTextureID (RenderSource*) for ImGui::Image. */
class VideoPresent {
public:
    VideoPresent() = default;
    ~VideoPresent();

    VideoPresent(const VideoPresent &) = delete;
    VideoPresent &operator=(const VideoPresent &) = delete;

    void update(const VideoFrame &frame);
    void clear();

    bool hasFrame() const { return hasFrame_; }
    int width() const { return texture_.width; }
    int height() const { return texture_.height; }

    /** Pointer stays valid while this object lives and hasFrame(). */
    ImTextureID textureId() const { return (ImTextureID)(uintptr_t)&render_; }

private:
    ImGui::TextureSource texture_;
    ImGui::RenderSource render_{ImGui::ImGuiImageSampleType_Linear};
    bool hasFrame_ = false;
};

} // namespace zplayer
