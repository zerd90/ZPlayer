#include "player_view.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace zplayer {
namespace {

void formatTime(char *buf, size_t n, double sec)
{
    if (sec < 0.0)
        sec = 0.0;
    const int total = static_cast<int>(sec + 0.5);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0)
        std::snprintf(buf, n, "%d:%02d:%02d", h, m, s);
    else
        std::snprintf(buf, n, "%d:%02d", m, s);
}

} // namespace

void PlayerView::drawProgressBar(float barWidth, double mediaTime, double duration)
{
    const float barH = 6.0f;
    const float trackR = 3.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 trackMin = cursor;
    const ImVec2 trackMax = ImVec2(cursor.x + barWidth, cursor.y + barH);

    const ImU32 colTrack = IM_COL32(255, 255, 255, 40);
    const ImU32 colFill = IM_COL32(230, 230, 235, 220);
    const ImU32 colKnob = IM_COL32(255, 255, 255, 255);

    dl->AddRectFilled(trackMin, trackMax, colTrack, trackR);

    float ratio = 0.f;
    if (duration > 0.05)
        ratio = static_cast<float>(std::clamp(mediaTime / duration, 0.0, 1.0));
    if (ratio > 0.f) {
        const float fillW = std::max(barH, barWidth * ratio);
        dl->AddRectFilled(trackMin, ImVec2(trackMin.x + fillW, trackMax.y), colFill, trackR);
        const float knobX = trackMin.x + fillW;
        dl->AddCircleFilled(ImVec2(knobX, trackMin.y + barH * 0.5f), 5.0f, colKnob);
    }

    // Reserve layout space (non-interactive in step 2 — no seek).
    ImGui::InvisibleButton("##progress", ImVec2(barWidth, barH + 4.0f));

    char curBuf[32], durBuf[32];
    formatTime(curBuf, sizeof(curBuf), mediaTime);
    if (duration > 0.05)
        formatTime(durBuf, sizeof(durBuf), duration);
    else
        std::snprintf(durBuf, sizeof(durBuf), "--:--");

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.88f, 1.f));
    ImGui::Text("%s / %s", curBuf, durBuf);
    ImGui::PopStyleColor();
}

void PlayerView::draw()
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##ZPlayerVideo", nullptr, flags);

    const float chromeH = 48.0f;
    const float pad = 12.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y;
    const float videoAreaH = std::max(0.f, availH - chromeH - pad);

    if (player_ && player_->isOpen() && present_) {
        player_->markPlaybackStarted();
        const double t = player_->mediaTimeSec();
        VideoFrame frame;
        if (player_->takeFrameForTime(t, frame)) {
            lastFrame_ = std::move(frame);
            haveLast_ = true;
            present_->update(lastFrame_);
        }

        ImGui::BeginChild("##video", ImVec2(availW, videoAreaH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (present_->hasFrame()) {
            const float vw = static_cast<float>(present_->width());
            const float vh = static_cast<float>(present_->height());
            const float regionW = ImGui::GetContentRegionAvail().x;
            const float regionH = ImGui::GetContentRegionAvail().y;
            float drawW = regionW;
            float drawH = regionH;
            if (vw > 0.f && vh > 0.f && regionW > 0.f && regionH > 0.f) {
                const float scale = std::min(regionW / vw, regionH / vh);
                drawW = vw * scale;
                drawH = vh * scale;
            }
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cursor.x + (regionW - drawW) * 0.5f, cursor.y + (regionH - drawH) * 0.5f));
            ImGui::Image(present_->textureId(), ImVec2(drawW, drawH));
        } else {
            ImGui::TextUnformatted(status_.c_str());
            if (!status_.empty())
                ImGui::TextUnformatted("解码中…");
        }
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::Indent(pad);
        drawProgressBar(std::max(0.f, availW - pad * 2.f), t, player_->durationSec());
        ImGui::Unindent(pad);

        if (!status_.empty()) {
            ImGui::Indent(pad);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.f));
            ImGui::TextUnformatted(status_.c_str());
            ImGui::PopStyleColor();
            ImGui::Unindent(pad);
        }
    } else {
        ImGui::TextUnformatted(status_.c_str());
    }

    ImGui::End();
}

} // namespace zplayer
