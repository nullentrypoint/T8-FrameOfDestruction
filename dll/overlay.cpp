#include "stdafx.h"
#include "msdf_text.h"
#include <vector>
#include <string>
#include <time.h>
#include <cstdlib>
#include <format>

namespace overlay {
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

        ImGui::End();
    }
}
