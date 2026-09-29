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

bool transportIconButton(const char *id, float size)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    // Contrast for both light and dark imgui themes.
    const ImU32 bg = active    ? IM_COL32(40, 40, 48, 200)
                     : hovered ? IM_COL32(50, 50, 58, 170)
                               : IM_COL32(60, 60, 68, 140);
    dl->AddCircleFilled(ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), size * 0.5f, bg);
    return pressed;
}

void drawSkipBackIcon(ImVec2 c, float r, ImU32 col)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float x0 = c.x - r * 0.55f;
    const float y0 = c.y - r * 0.45f;
    const float y1 = c.y + r * 0.45f;
    dl->AddTriangleFilled(ImVec2(c.x + r * 0.05f, y0), ImVec2(c.x + r * 0.05f, y1), ImVec2(c.x - r * 0.45f, c.y), col);
    dl->AddTriangleFilled(ImVec2(c.x + r * 0.55f, y0), ImVec2(c.x + r * 0.55f, y1), ImVec2(c.x + r * 0.05f, c.y), col);
    dl->AddRectFilled(ImVec2(x0 - r * 0.12f, y0), ImVec2(x0, y1), col, 1.0f);
}

void drawSkipForwardIcon(ImVec2 c, float r, ImU32 col)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float x1 = c.x + r * 0.55f;
    const float y0 = c.y - r * 0.45f;
    const float y1 = c.y + r * 0.45f;
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.05f, y0), ImVec2(c.x - r * 0.05f, y1), ImVec2(c.x + r * 0.45f, c.y), col);
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.55f, y0), ImVec2(c.x - r * 0.55f, y1), ImVec2(c.x - r * 0.05f, c.y), col);
    dl->AddRectFilled(ImVec2(x1, y0), ImVec2(x1 + r * 0.12f, y1), col, 1.0f);
}

void drawPlayIcon(ImVec2 c, float r, ImU32 col)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.28f, c.y - r * 0.45f), ImVec2(c.x - r * 0.28f, c.y + r * 0.45f),
                          ImVec2(c.x + r * 0.48f, c.y), col);
}

void drawPauseIcon(ImVec2 c, float r, ImU32 col)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float w = r * 0.18f;
    const float h = r * 0.45f;
    const float gap = r * 0.18f;
    dl->AddRectFilled(ImVec2(c.x - gap - w, c.y - h), ImVec2(c.x - gap, c.y + h), col, 1.5f);
    dl->AddRectFilled(ImVec2(c.x + gap, c.y - h), ImVec2(c.x + gap + w, c.y + h), col, 1.5f);
}

} // namespace

void PlayerView::drawTransportControls()
{
    if (!player_)
        return;

    const float btn = 40.0f;
    const float gap = 18.0f;
    const float rowW = btn * 3.f + gap * 2.f;
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (avail - rowW) * 0.5f));

    const ImU32 iconCol = IM_COL32(250, 250, 252, 255);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (transportIconButton("##rew", btn))
        player_->skip(-MediaPlayer::kSkipStepSec);
    drawSkipBackIcon(ImVec2(p.x + btn * 0.5f, p.y + btn * 0.5f), btn * 0.42f, iconCol);

    ImGui::SameLine(0, gap);
    p = ImGui::GetCursorScreenPos();
    if (transportIconButton("##playpause", btn))
        player_->togglePause();
    if (player_->isPaused())
        drawPlayIcon(ImVec2(p.x + btn * 0.5f, p.y + btn * 0.5f), btn * 0.42f, iconCol);
    else
        drawPauseIcon(ImVec2(p.x + btn * 0.5f, p.y + btn * 0.5f), btn * 0.42f, iconCol);

    ImGui::SameLine(0, gap);
    p = ImGui::GetCursorScreenPos();
    if (transportIconButton("##ff", btn))
        player_->skip(MediaPlayer::kSkipStepSec);
    drawSkipForwardIcon(ImVec2(p.x + btn * 0.5f, p.y + btn * 0.5f), btn * 0.42f, iconCol);
}

double PlayerView::drawProgressBar(float barWidth, double mediaTime, double duration)
{
    const float barH = 10.0f;
    const float trackR = 3.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 trackMin = cursor;
    const ImVec2 trackMax = ImVec2(cursor.x + barWidth, cursor.y + barH);

    ImGui::InvisibleButton("##progress", ImVec2(barWidth, barH + 8.0f));
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();

    float ratio = 0.f;
    if (duration > 0.05)
        ratio = static_cast<float>(std::clamp(mediaTime / duration, 0.0, 1.0));

    if (active && duration > 0.05) {
        const float mouseX = ImGui::GetIO().MousePos.x;
        scrubRatio_ = std::clamp((mouseX - trackMin.x) / barWidth, 0.f, 1.f);
        scrubbing_ = true;
        ratio = scrubRatio_;
    } else if (scrubbing_ && !active) {
        if (player_ && duration > 0.05)
            player_->seek(static_cast<double>(scrubRatio_) * duration);
        scrubbing_ = false;
    } else if (!active) {
        scrubbing_ = false;
    }

    const ImU32 colTrack = IM_COL32(255, 255, 255, 40);
    const ImU32 colFill = hovered || scrubbing_ ? IM_COL32(245, 245, 250, 235) : IM_COL32(230, 230, 235, 220);
    const ImU32 colKnob = IM_COL32(255, 255, 255, 255);

    dl->AddRectFilled(trackMin, trackMax, colTrack, trackR);
    if (ratio > 0.f) {
        const float fillW = std::max(barH * 0.5f, barWidth * ratio);
        dl->AddRectFilled(trackMin, ImVec2(trackMin.x + fillW, trackMax.y), colFill, trackR);
        dl->AddCircleFilled(ImVec2(trackMin.x + fillW, trackMin.y + barH * 0.5f), scrubbing_ ? 7.0f : 5.0f, colKnob);
    }

    const double displayTime = scrubbing_ && duration > 0.05 ? static_cast<double>(scrubRatio_) * duration : mediaTime;

    char curBuf[32], durBuf[32];
    formatTime(curBuf, sizeof(curBuf), displayTime);
    if (duration > 0.05)
        formatTime(durBuf, sizeof(durBuf), duration);
    else
        std::snprintf(durBuf, sizeof(durBuf), "--:--");

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.88f, 1.f));
    ImGui::Text("%s / %s%s", curBuf, durBuf, scrubbing_ ? "  (预览)" : "");
    ImGui::PopStyleColor();

    return displayTime;
}

void PlayerView::draw()
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##ZPlayerVideo", nullptr, flags);

    const float chromeH = 110.0f;
    const float pad = 14.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y;
    const float videoAreaH = std::max(80.f, availH - chromeH);

    if (player_ && player_->isOpen() && present_) {
        if (!player_->isPaused())
            player_->markPlaybackStarted();

        const double clockT = player_->mediaTimeSec();
        const double duration = player_->durationSec();

        ImGui::BeginChild("##video", ImVec2(availW, videoAreaH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const double presentT = scrubbing_ && duration > 0.05 ? static_cast<double>(scrubRatio_) * duration : clockT;
        VideoFrame frame;
        if (player_->takeFrameForTime(presentT, frame)) {
            lastFrame_ = std::move(frame);
            haveLast_ = true;
            present_->update(lastFrame_);
        }

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

        // Bottom chrome: progress + transport (always reserved via videoAreaH).
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Indent(pad);
        drawProgressBar(std::max(0.f, availW - pad * 2.f), clockT, duration);
        ImGui::Dummy(ImVec2(0, 8));
        drawTransportControls();
        ImGui::Unindent(pad);

        if (!status_.empty()) {
            ImGui::Indent(pad);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.f));
            ImGui::TextUnformatted(status_.c_str());
            ImGui::PopStyleColor();
            ImGui::Unindent(pad);
        }
    } else {
        scrubbing_ = false;
        ImGui::TextUnformatted(status_.c_str());
    }

    ImGui::End();
}

} // namespace zplayer
