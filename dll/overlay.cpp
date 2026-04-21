#include "stdafx.h"
#include <vector>
#include <string>
#include <time.h>
#include <cstdlib>
#include <format>

namespace overlay {
    struct FlyingText {
        std::string text;
        ImVec2 position;
        float alpha;
        float speed;
        float acceleration;
        float fontSize;
        ImColor color;
        float outlineThickness; // Добавляем толщину обводки
        ImColor outlineColor;   // И цвет обводки
    };

    std::vector<FlyingText> flyingTexts;
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

    void AddFlyingText(const std::string& text) {
        if (!menu::frameAdvantageHints.enabled) {
            return;
        }

        srand(time(0));
        int y = screenSize.y * 0.2f;
        int randomNumX = (rand() % y) - (y * 0.5f);
        int randomNumY = (rand() % y) - (y * 0.5f);
        

        ImVec2 startPosition = ImVec2(screenSize.x * 0.5f + randomNumX, screenSize.y * 0.5f + randomNumY);
        if (menu::frameAdvantageHints.debugInfo) {
            console::AddLog(std::format("AddFlyingText startPosition {} {} randomNum {} {}", startPosition.x, startPosition.y, randomNumX, randomNumY));
        }
        ImColor color = getColor(text);
        flyingTexts.push_back({
            text,
            startPosition,
            1.0f, // начальная прозрачность
            menu::frameAdvantageHints.flySpeed, // скорость движения
            1.05f, // ускорение
            fontSize,
            color,
            menu::frameAdvantageHints.fontShadow ? 1.5f : 0.0f, // толщина обводки
            ImColor(0, 0, 0, 255) // цвет обводки
            });
    }

    void DrawTextWithOutline(ImDrawList* drawList, ImFont* font, const FlyingText& text) {
        if (text.outlineThickness > 0.0f) {
            const float outlineFontSize = text.fontSize + text.outlineThickness * 2.0f;
            ImVec2 textSize = font->CalcTextSizeA(text.fontSize, FLT_MAX, 0.0f, text.text.c_str());
            ImVec2 outlineTextSize = font->CalcTextSizeA(outlineFontSize, FLT_MAX, 0.0f, text.text.c_str());
            ImVec2 outlinePos = ImVec2(
                text.position.x - (outlineTextSize.x - textSize.x) * 0.5f,
                text.position.y - (outlineTextSize.y - textSize.y) * 0.5f
            );

            ImColor outlineColor = text.outlineColor;
            outlineColor.Value.w = text.alpha; // Применяем прозрачность

            drawList->AddText(font, outlineFontSize, outlinePos, outlineColor, text.text.c_str());
        }

        // Основной текст
        drawList->AddText(font, text.fontSize, text.position, text.color, text.text.c_str());
    }

    void UpdateAndDraw(ImGuiIO& io, ImFont* font) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Обновляем и рисуем все летающие тексты
        for (auto it = flyingTexts.begin(); it != flyingTexts.end(); ) {
            auto& text = *it;

            // Обновляем положение и прозрачность
            text.speed *= text.acceleration;
            text.position.y -= text.speed;
            text.alpha -= menu::frameAdvantageHints.fadeSpeed;
            text.color.Value.w = text.alpha;

            // Рисуем текст с обводкой
            DrawTextWithOutline(drawList, font, text);

            // Удаляем текст если он стал полностью прозрачным
            if (text.alpha <= 0.0f) {
                it = flyingTexts.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void Init(ImGuiIO& io, ImFont* font) {
        screenSize = io.DisplaySize;
        fontSize = menu::frameAdvantageHints.fontSize;

        // Настройки окна оверлея
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

        // Обновляем и рисуем все тексты
        UpdateAndDraw(io, font);

        ImGui::End();
    }
}