#include "player_view.h"

#include "imgui.h"

#include <algorithm>

namespace zplayer {

void PlayerView::draw()
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##ZPlayerVideo", nullptr, flags);

    if (player_ && player_->isOpen() && present_) {
        player_->markPlaybackStarted();
        const double t = player_->mediaTimeSec();
        VideoFrame frame;
        if (player_->takeFrameForTime(t, frame)) {
            lastFrame_ = std::move(frame);
            haveLast_ = true;
            present_->update(lastFrame_);
        }

        if (present_->hasFrame()) {
            const float availW = ImGui::GetContentRegionAvail().x;
            const float availH = ImGui::GetContentRegionAvail().y;
            const float vw = static_cast<float>(present_->width());
            const float vh = static_cast<float>(present_->height());
            float drawW = availW;
            float drawH = availH;
            if (vw > 0.f && vh > 0.f && availW > 0.f && availH > 0.f) {
                const float scale = std::min(availW / vw, availH / vh);
                drawW = vw * scale;
                drawH = vh * scale;
            }
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cursor.x + (availW - drawW) * 0.5f, cursor.y + (availH - drawH) * 0.5f));
            ImGui::Image(present_->textureId(), ImVec2(drawW, drawH));
        } else {
            ImGui::TextUnformatted(status_.c_str());
            if (!status_.empty() && player_->isOpen())
                ImGui::TextUnformatted("解码中…");
        }
    } else {
        ImGui::TextUnformatted(status_.c_str());
    }

    ImGui::End();
}

} // namespace zplayer
