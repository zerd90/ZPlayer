#include "video_present.h"

namespace zplayer {

VideoPresent::~VideoPresent()
{
    clear();
}

void VideoPresent::update(const VideoFrame &frame)
{
    if (frame.width <= 0 || frame.height <= 0 || frame.rgba.empty())
        return;

    ImGui::ImageData image{};
    image.format = ImGui::ImGuiImageFormat_RGBA;
    image.colorRange = ImGui::ImGuiImageColorRange_0_255;
    image.width = static_cast<unsigned int>(frame.width);
    image.height = static_cast<unsigned int>(frame.height);
    image.plane[0] = const_cast<uint8_t *>(frame.rgba.data());
    image.stride[0] = static_cast<unsigned int>(frame.width) * 4;

    if (!ImGui::updateImageTexture(image, texture_))
        return;

    render_ = ImGui::RenderSource(texture_, ImGui::ImGuiImageSampleType_Linear);
    hasFrame_ = true;
}

void VideoPresent::clear()
{
    // freeTexture zeros ids; TextureSource dtor also frees — safe if ids already 0.
    ImGui::freeTexture(texture_);
    for (int i = 0; i < IMGUI_IMAGE_MAX_PLANES; ++i)
        texture_.textureID[i] = 0;
    texture_.width = texture_.height = 0;
    render_ = ImGui::RenderSource(ImGui::ImGuiImageSampleType_Linear);
    hasFrame_ = false;
}

} // namespace zplayer
