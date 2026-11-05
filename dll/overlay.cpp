#include "stdafx.h"
#include "msdf_text.h"
#include <vector>
#include <string>
#include <time.h>
#include <cstdlib>
#include <format>

namespace overlay {
    struct TimedThrowTimeline {
        ThrowTimeline data;
        double resolved = -1;
    };
    std::array<TimedThrowTimeline, 2> throwTimelines;

    void UpdateThrowTimeline(int side, const ThrowTimeline& timeline) {
        if (side < 0 || side >= 2) return;
        auto& timed = throwTimelines[side];
        if (!timeline.visible || !timed.data.visible || timeline.event_frame != timed.data.event_frame)
            timed.resolved = -1;
        if (timeline.visible && timeline.result != ThrowTimeline::Result::Pending && timed.resolved < 0)
            timed.resolved = ImGui::GetTime();
        timed.data = timeline;
    }

    void ClearThrowTimelines() { throwTimelines = {}; }

    void DrawThrowTimelines(ImGuiIO& io, ImFont* font) {
        if (!menu::frameAdvantageHints.throwTimeline) return;
        auto* draw = ImGui::GetWindowDrawList();
        const auto& settings = menu::frameAdvantageHints;
        const int style = std::clamp(settings.throwCardStyle, 0, 2);
        const float logicalWidth = style == 0 ? 620.0f : style == 1 ? 460.0f : 360.0f;
        const float height = style == 0 ? 136.0f : style == 1 ? 106.0f : 68.0f;
        const float scale = std::min(std::clamp(io.DisplaySize.x / 1920.0f, 0.5f, 2.0f), io.DisplaySize.x / (logicalWidth + 24.0f));
        const float width = logicalWidth * scale;
        for (int side = 0; side < 2; ++side) {
            const auto& timed = throwTimelines[side];
            const auto& data = timed.data;
            const double age = timed.resolved < 0 ? 0 : ImGui::GetTime() - timed.resolved;
            const float duration = std::clamp(settings.throwCardDuration, 1.0f, 15.0f);
            const float fade = std::clamp(settings.throwCardFade, 0.0f, duration);
            if (!data.visible || age >= duration) continue;
            const float alpha = fade > 0 ? static_cast<float>(std::clamp((duration - age) / fade, 0.0, 1.0)) : 1.0f;
            const auto color = [alpha](int r, int g, int b, int a = 255) {
                return IM_COL32(r, g, b, static_cast<int>(a * alpha));
            };
            const float x = (io.DisplaySize.x - width) * 0.5f;
            const float y = std::min(io.DisplaySize.y * 0.70f, io.DisplaySize.y - (2 * height + 22) * scale) + side * (height + 10) * scale;
            const auto accent = data.result == ThrowTimeline::Result::Broken ? color(122, 255, 213) :
                data.result == ThrowTimeline::Result::Failed ? color(255, 82, 122) : color(64, 222, 255);
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height * scale), color(12, 17, 26, 225), 9 * scale);
            draw->AddRectFilled(ImVec2(x, y + 10 * scale), ImVec2(x + 3 * scale, y + (height - 10) * scale), accent, scale);
            const auto text = [&](float dx, float dy, const std::string& value, ImU32 tint, float size = 18.0f) {
                draw->AddText(font, size * scale, ImVec2(x + dx * scale, y + dy * scale), tint, value.c_str());
            };
            const bool reveal = data.result == ThrowTimeline::Result::Broken || data.result == ThrowTimeline::Result::Failed;
            const char* button = !reveal ? "--" : data.buttons == 1 ? "1" : data.buttons == 2 ? "2" :
                data.buttons == 4 ? "1+2" : data.buttons == 3 ? "1 OR 2" : "?";
            const char* result = data.result == ThrowTimeline::Result::Broken ? "BROKEN" :
                data.result == ThrowTimeline::Result::Failed ? "FAILED" :
                data.result == ThrowTimeline::Result::Pending ? "PENDING" : "UNCONFIRMED";
            const float titleSize = style == 0 ? 23.0f : 18.0f;
            const float resultFont = style == 0 ? 20.0f : 16.0f;
            text(16, 12, std::format("P{}  {} [{}]", side + 1, data.ground ? "GROUND" : data.windows.empty() ? "BREAK" : "TACKLE", button), color(245, 248, 255), titleSize);
            const auto resultSize = font->CalcTextSizeA(resultFont * scale, FLT_MAX, 0, result);
            draw->AddText(font, resultFont * scale, ImVec2(x + width - 16 * scale - resultSize.x, y + 14 * scale), accent, result);
            using Origin = ThrowTimeline::InputOrigin;
            const std::string input = data.ground ? (data.input_origin == Origin::Buffered ? "BUFFERED" :
                data.input_origin == Origin::Held ? "HELD" : data.input_origin == Origin::Carried ? "CARRIED" : "--") :
                data.held ? "HELD" : data.input_frame >= 0 ? std::format("+{}f", data.input_frame) : "--";
            std::string window = data.window_frames >= 0 ? std::format("{}f", data.window_frames) : "?";
            if (!data.windows.empty()) {
                window.clear();
                for (const auto& part : data.windows) {
                    if (!window.empty()) window += "/";
                    window += part.start >= 0 && part.end >= 0 ? std::format("{}f", part.end - part.start) : "?";
                }
            }
            if (style == 2) {
                text(16, 40, std::format("WINDOW {}   INPUT {}", window, input), color(175, 185, 203), 15);
                continue;
            }
            if (style == 0) {
            text(16, 44, std::format("STARTUP {}", data.startup > 0 ? std::format("{}f", data.startup) : "?"), color(175, 185, 203));
            text(235, 44, std::format("BREAK WINDOW {}", window), accent);
            } else {
                text(16, 38, std::format("STARTUP {}   WINDOW {}   INPUT {}",
                    data.startup > 0 ? std::format("{}f", data.startup) : "?", window, input), color(175, 185, 203), 15);
            }
            const float left = x + 16 * scale, right = x + width - 16 * scale;
            const float barY = y + (style == 0 ? 77 : 65) * scale;
            const int windowExtent = !data.windows.empty() ? data.windows.back().end : data.window_frames;
            const int span = std::max({ windowExtent > 0 ? windowExtent : data.observed_frames,
                data.break_frame + 1, data.input_frame + 1, 1 });
            const int startup = std::max(data.startup, 0);
            const float unit = (right - left) / static_cast<float>(std::max(startup + span, 1));
            const float boundary = left + startup * unit;
            draw->AddRectFilled(ImVec2(left, barY), ImVec2(boundary, barY + 8 * scale), color(116, 128, 150), 2 * scale);
            if (!data.windows.empty()) {
                draw->AddLine(ImVec2(boundary, barY + 4 * scale), ImVec2(right, barY + 4 * scale), color(116, 128, 150, 90), scale);
                for (const auto& part : data.windows) {
                    if (part.start < 0) continue;
                    const float from = boundary + part.start * unit;
                    const float to = boundary + (part.end >= 0 ? part.end : data.observed_frames) * unit;
                    draw->AddRectFilled(ImVec2(from, barY), ImVec2(to, barY + 8 * scale), color(64, 222, 255, 65), 2 * scale);
                    if (part.end >= 0)
                        draw->AddLine(ImVec2(to, barY - 5 * scale), ImVec2(to, barY + 13 * scale), accent, scale);
                }
            } else if (!data.ground) {
            const float windowEnd = data.window_frames > 0 ? boundary + data.window_frames * unit : right;
            draw->AddRectFilled(ImVec2(boundary, barY), ImVec2(windowEnd, barY + 8 * scale), color(64, 222, 255, 65), 2 * scale);
            if (data.window_frames > 0)
                draw->AddLine(ImVec2(windowEnd, barY - 5 * scale), ImVec2(windowEnd, barY + 13 * scale), accent, scale);
            else for (float tick = boundary; tick < right; tick += 12 * scale)
                draw->AddLine(ImVec2(tick, barY + 4 * scale), ImVec2(std::min(tick + 6 * scale, right), barY + 4 * scale), accent, 2 * scale);
            }
            draw->AddLine(ImVec2(boundary, barY - 5 * scale), ImVec2(boundary, barY + 13 * scale), color(240, 245, 255), scale);
            if (data.input_frame >= 0) {
                const float marker = boundary + data.input_frame * unit;
                draw->AddLine(ImVec2(marker, barY - 4 * scale), ImVec2(marker, barY + 12 * scale), color(255, 228, 92), 2 * scale);
            }
            if (data.break_frame >= 0) {
                const float marker = boundary + data.break_frame * unit;
                draw->AddTriangleFilled(ImVec2(marker, barY - 2 * scale), ImVec2(marker - 5 * scale, barY - 10 * scale),
                    ImVec2(marker + 5 * scale, barY - 10 * scale), accent);
            }
            if (style == 1) {
                text(16, 84, data.break_frame >= 0 ? std::format("ACCEPTED +{}f", data.break_frame) : "ACCEPTED --", accent, 14);
                text(245, 84, data.animation_frame >= 0 ? std::format("ANIMATION +{}f", data.animation_frame) : "ANIMATION --", color(175, 185, 203), 14);
                continue;
            }
            text(16, 100, std::format("INPUT {}", input), color(255, 228, 92), 16);
            text(180, 100, data.break_frame >= 0 ? std::format("BREAK ACCEPTED +{}f", data.break_frame) :
                "BREAK ACCEPTED --", accent, 16);
            text(450, 100, data.animation_frame >= 0 ? std::format("ANIMATION +{}f", data.animation_frame) :
                data.startup > 0 && windowExtent > 0 ? std::format("TOTAL {}f", data.startup + windowExtent) :
                "TOTAL ?", color(175, 185, 203), 13);
        }
    }

    struct FlyingText {
        uint64_t id;
        std::string text;
        ImVec2 position;
        bool centered;
        float alpha;
        float speed;
        float acceleration;
        float fontSize;
        ImColor color;
        float outlineThickness; // ��������� ������� �������
        ImColor outlineColor;   // � ���� �������
        int ringType = -1;
        bool worldLocked = false;
        std::optional<WorldPoint> worldOrigin;
        float worldRise = 0.0f;
    };

    std::vector<FlyingText> flyingTexts;
    std::optional<CameraSnapshot> currentCamera;

    void SetCamera(const std::optional<CameraSnapshot>& camera) {
        currentCamera = camera;
    }
    float fontSize = 60.0f;
    ImColor defaultColor = ImColor(255, 255, 255, 255);
    ImVec2 screenSize;

    ImColor getColor(const std::string& text) {
        try {
            int i = std::stoi(text);
            if (i > 7) {
                return ImColor(
                    menu::frameAdvantageHints.colorLargePlus[0],
                    menu::frameAdvantageHints.colorLargePlus[1],
                    menu::frameAdvantageHints.colorLargePlus[2],
                    255.0
                );
            }
            if (i > 3) {
                return ImColor(
                    menu::frameAdvantageHints.colorMediumPlus[0],
                    menu::frameAdvantageHints.colorMediumPlus[1],
                    menu::frameAdvantageHints.colorMediumPlus[2],
                    255.0
                );
            }
            if (i > 0) {
                return ImColor(
                    menu::frameAdvantageHints.colorSmallPlus[0],
                    menu::frameAdvantageHints.colorSmallPlus[1],
                    menu::frameAdvantageHints.colorSmallPlus[2],
                    255.0
                );
            }
            if (i <= -15) {
                return ImColor(
                    menu::frameAdvantageHints.colorLargeMinus[0],
                    menu::frameAdvantageHints.colorLargeMinus[1],
                    menu::frameAdvantageHints.colorLargeMinus[2],
                    255.0
                );
            }
            if (i <= -10) {
                return ImColor(
                    menu::frameAdvantageHints.colorMediumMinus[0],
                    menu::frameAdvantageHints.colorMediumMinus[1],
                    menu::frameAdvantageHints.colorMediumMinus[2],
                    255.0
                );
            }
            if (i < 0) {
                return ImColor(
                    menu::frameAdvantageHints.colorSmallMinus[0],
                    menu::frameAdvantageHints.colorSmallMinus[1],
                    menu::frameAdvantageHints.colorSmallMinus[2],
                    255.0
                );
            }
        }
        catch (...) {

        }
        return ImColor(
            menu::frameAdvantageHints.color0[0],
            menu::frameAdvantageHints.color0[1],
            menu::frameAdvantageHints.color0[2],
            255.0
        );
    }

    uint64_t AddFlyingText(const std::string& text, const std::optional<ScreenAnchor>& anchor,
        const std::string& hitType, const std::optional<WorldPoint>& worldOrigin) {
        if (!menu::frameAdvantageHints.enabled) {
            return 0;
        }

        const auto display = ImGui::GetIO().DisplaySize;
        if (display.x <= 0 || display.y <= 0) return 0;
        const bool locked = menu::frameAdvantageHints.lockToWorld;
        const bool spatial = locked || menu::frameAdvantageHints.startPositionMode == 1;
        ImVec2 startPosition(display.x * 0.5f, display.y * 0.5f);
        int randomNumX = 0, randomNumY = 0;
        if (!spatial) {
            srand(static_cast<unsigned int>(time(0)));
            const int range = std::max(1, static_cast<int>(display.y * 0.2f));
            randomNumX = static_cast<int>((rand() % range) - range * 0.5f);
            randomNumY = static_cast<int>((rand() % range) - range * 0.5f);
            startPosition.x += randomNumX;
            startPosition.y += randomNumY;
        }
        else if (anchor) {
            const ImVec2 projected(startPosition.x + anchor->horizontal * display.x * 0.5f,
                startPosition.y - anchor->vertical * display.y * 0.5f);
            if (std::isfinite(projected.x) && std::isfinite(projected.y) &&
                projected.x >= 0 && projected.x <= display.x &&
                projected.y >= 0 && projected.y <= display.y) {
                startPosition = projected;
            }
        }
        if (menu::frameAdvantageHints.debugInfo) {
            console::AddLog(std::format("AddFlyingText startPosition {} {} randomNum {} {}", startPosition.x, startPosition.y, randomNumX, randomNumY));
        }
        ImColor color = getColor(text);
        static uint64_t nextId = 0;
        const uint64_t id = ++nextId;
        flyingTexts.push_back({
            id,
            text,
            startPosition,
            spatial,
            1.0f, // ��������� ������������
            menu::frameAdvantageHints.flySpeed, // �������� ��������
            1.05f, // ���������
            menu::frameAdvantageHints.fontSize,
            color,
            menu::frameAdvantageHints.fontShadow ? 1.5f : 0.0f, // ������� �������
            ImColor(0, 0, 0, 255) // ���� �������
            });
        flyingTexts.back().worldLocked = locked;
        if (locked && worldOrigin && worldOrigin->valid()) flyingTexts.back().worldOrigin = worldOrigin;
        for (int i = 0; i < 5; ++i) {
            if (hitType == HitRingSettings::types[i] || (i == 2 && hitType == "MID_UNBLOCKABLE")) {
                flyingTexts.back().ringType = i;
                break;
            }
        }
        return id;
    }

    void UpdateFlyingText(uint64_t id, const std::string& value) {
        if (id == 0) return;
        for (auto& text : flyingTexts) {
            if (text.id == id) {
                text.text = value;
                text.color = getColor(value);
                text.color.Value.w = text.alpha;
                return;
            }
        }
    }

    void DrawTextWithOutline(ImDrawList* drawList, ImFont* font, const FlyingText& text) {
        if (text.text == "JGL" || text.text == "KND") return;
        ImVec4 outline = text.outlineColor.Value;
        outline.w = text.alpha;
        if (msdftext::AddText(text.text, text.position, text.fontSize, text.centered,
            text.color.Value, outline, text.outlineThickness)) return;
        const ImVec2 textSize = font->CalcTextSizeA(text.fontSize, FLT_MAX, 0.0f, text.text.c_str());
        ImVec2 position = text.position;
        if (text.centered) {
            position.x -= textSize.x * 0.5f;
            position.y -= textSize.y * 0.5f;
        }
        if (text.outlineThickness > 0.0f) {
            const float outlineFontSize = text.fontSize + text.outlineThickness * 2.0f;
            ImVec2 outlineTextSize = font->CalcTextSizeA(outlineFontSize, FLT_MAX, 0.0f, text.text.c_str());
            ImVec2 outlinePos = ImVec2(
                position.x - (outlineTextSize.x - textSize.x) * 0.5f,
                position.y - (outlineTextSize.y - textSize.y) * 0.5f
            );

            ImColor outlineColor = text.outlineColor;
            outlineColor.Value.w = text.alpha; // ��������� ������������

            drawList->AddText(font, outlineFontSize, outlinePos, outlineColor, text.text.c_str());
        }

        // �������� �����
        drawList->AddText(font, text.fontSize, position, text.color, text.text.c_str());
    }

    void UpdateAndDraw(ImGuiIO& io, ImFont* font) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // ��������� � ������ ��� �������� ������
        for (auto it = flyingTexts.begin(); it != flyingTexts.end(); ) {
            auto& text = *it;

            // ��������� ��������� � ������������
            bool visible = true;
            text.speed *= text.acceleration;
            if (text.worldLocked && text.worldOrigin) {
                text.worldRise += text.speed;
                WorldPoint point = *text.worldOrigin;
                point.y += text.worldRise; // Tekken world Y is vertical (game units).
                const auto anchor = currentCamera ? currentCamera->project(point) : std::nullopt;
                visible = anchor && std::abs(anchor->horizontal) <= 1.0f && std::abs(anchor->vertical) <= 1.0f;
                if (visible) {
                    text.position = ImVec2((1.0f + anchor->horizontal) * io.DisplaySize.x * 0.5f,
                        (1.0f - anchor->vertical) * io.DisplaySize.y * 0.5f);
                }
            }
            else {
                // TEST/missing world data uses the ordinary screen-space flight.
                text.position.y -= text.speed;
            }
            text.alpha -= menu::frameAdvantageHints.fadeSpeed;
            text.color.Value.w = text.alpha;

            const auto& ring = menu::frameAdvantageHints.hitRing;
            if (visible && ring.enabled && text.ringType >= 0 && text.alpha > 0.0f) {
                const ImVec2 size = font->CalcTextSizeA(text.fontSize, FLT_MAX, 0.0f, text.text.c_str());
                ImVec2 center = text.position;
                if (!text.centered) {
                    center.x += size.x * 0.5f;
                    center.y += size.y * 0.5f;
                }
                const float progress = std::clamp(1.0f - text.alpha, 0.0f, 1.0f);
                const float radius = std::sqrt(size.x * size.x + size.y * size.y) * 0.5f
                    + text.fontSize * (0.1f + progress * 0.75f);
                const auto& rgb = ring.colors[text.ringType];
                drawList->AddCircle(center, radius, ImColor(rgb[0], rgb[1], rgb[2], text.alpha),
                    0, ring.thickness);
            }

            // ������ ����� � ��������
            if (visible && text.alpha > 0.0f) DrawTextWithOutline(drawList, font, text);

            // ������� ����� ���� �� ���� ��������� ����������
            if (text.alpha <= 0.0f) {
                it = flyingTexts.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void Init(ImGuiIO& io, ImFont* font) {
        msdftext::BeginFrame();
        screenSize = io.DisplaySize;
        fontSize = menu::frameAdvantageHints.fontSize;

        // ��������� ���� �������
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Overlay", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBackground);

        // ��������� � ������ ��� ������
        UpdateAndDraw(io, font);
        DrawThrowTimelines(io, font);

        ImGui::End();
    }
}
