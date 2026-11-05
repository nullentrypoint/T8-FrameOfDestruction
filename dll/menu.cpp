#include "stdafx.h"
#include <fstream>
#include <string>
#include <windows.h>
#include <shlobj.h> // для SHGetFolderPath
#include <sstream>
#include "helper.h"
#include <format>
#include <vector>
#include <cstring>
#include <chrono>

namespace menu {
    bool isOpen = false;
    float test = 0.0f;
    static int currentSection = 1; // Settings first; console is a diagnostic tool.

    struct Notification {
        const char* title;
        const char* detail;
        double started;
        double refreshed;
    };
    static std::vector<Notification> notifications;

    static double NotificationTime() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    static void Notify(const char* title, const char* detail) {
        if (!ImGui::GetCurrentContext()) return;
        for (auto& item : notifications) {
            if (std::strcmp(item.title, title) == 0) {
                item.refreshed = NotificationTime();
                item.detail = detail;
                return;
            }
        }
        if (notifications.size() == 3) notifications.erase(notifications.begin());
        const double now = NotificationTime();
        notifications.push_back({ title, detail, now, now });
    }

    static void DrawNotifications(const ImGuiIO& io, ImFont* font) {
        if (!font) font = ImGui::GetFont();
        const double now = NotificationTime();
        std::erase_if(notifications, [now](const Notification& item) { return now - item.refreshed >= 4.5; });
        auto* draw = ImGui::GetForegroundDrawList();
        const float scale = std::clamp(io.DisplaySize.y / 1080.0f, 0.75f, 1.5f);
        const float width = std::min(360.0f * scale, io.DisplaySize.x - 24.0f);
        float y = 24.0f * scale;
        for (const auto& item : notifications) {
            const float age = static_cast<float>(now - item.refreshed);
            const float alpha = std::min(std::clamp(static_cast<float>(now - item.started) / 0.18f, 0.0f, 1.0f), std::clamp((4.5f - age) / 0.5f, 0.0f, 1.0f));
            const float x = io.DisplaySize.x - width - 24.0f * scale + (1.0f - alpha) * 20.0f * scale;
            const auto color = [alpha](int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, static_cast<int>(a * alpha)); };
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + 86 * scale), color(15, 16, 23, 245), 2 * scale);
            draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + 86 * scale), color(74, 49, 65), 2 * scale);
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + 4 * scale, y + 86 * scale), color(240, 24, 85));
            draw->PushClipRect(ImVec2(x + 10 * scale, y), ImVec2(x + width - 10 * scale, y + 86 * scale), true);
            draw->AddText(font, 17 * scale, ImVec2(std::round(x + 20 * scale), std::round(y + 18 * scale)), color(255, 245, 249), item.title);
            draw->AddText(font, 14 * scale, ImVec2(std::round(x + 20 * scale), std::round(y + 46 * scale)), color(173, 176, 190), item.detail);
            draw->PopClipRect();
            draw->AddRectFilled(ImVec2(x, y + 84 * scale), ImVec2(x + width * (1.0f - age / 4.5f), y + 86 * scale), color(240, 24, 85));
            y += 98 * scale;
        }
    }

    // Данные для разных разделов
    FrameAdvantageHints frameAdvantageHints;


    // Дефолтные значения для FrameAdvantageHints
    const FrameAdvantageHints defaultFrameAdvantageHints{};

    float DefaultFontSize() {
        if (!ImGui::GetCurrentContext()) return 0.0f;
        const float height = ImGui::GetIO().DisplaySize.y;
        return std::isfinite(height) && height > 0.0f ? height / 18.0f : 0.0f;
    }

    std::string getValue(const std::string& section, const std::string& key, const std::string& defaultVal) {
        if (kv::userSettings->HasKey(key, section)) {
            return kv::userSettings->GetValue(key, section);
        }

        return defaultVal;
    }

    void setKey(const std::string& section, const std::string& key, const std::string& value) {
        kv::userSettings->SetKey(key, value, section);
    }

    // Сохранение всех настроек в INI файл
    void SaveAllSettings() {
        // Секция [FrameAdvantageHints]
        setKey("FrameAdvantageHints", "enabled", frameAdvantageHints.enabled ? "true" : "false");
        setKey("FrameAdvantageHints", "throwTimeline", frameAdvantageHints.throwTimeline ? "true" : "false");
        setKey("FrameAdvantageHints", "throwCardStyle", std::to_string(frameAdvantageHints.throwCardStyle));
        setKey("FrameAdvantageHints", "throwCardDuration", std::to_string(frameAdvantageHints.throwCardDuration));
        setKey("FrameAdvantageHints", "throwCardFade", std::to_string(frameAdvantageHints.throwCardFade));
        setKey("FrameAdvantageHints", "startPositionMode", std::to_string(frameAdvantageHints.startPositionMode));
        setKey("FrameAdvantageHints", "lockToWorld", frameAdvantageHints.lockToWorld ? "true" : "false");
        setKey("FrameAdvantageHints", "fontSize", std::to_string(frameAdvantageHints.fontSize));
        setKey("FrameAdvantageHints", "fontShadow", frameAdvantageHints.fontShadow ? "true" : "false");
        setKey("FrameAdvantageHints", "flySpeed", std::to_string(frameAdvantageHints.flySpeed));
        setKey("FrameAdvantageHints", "fadeSpeed", std::to_string(frameAdvantageHints.fadeSpeed));
        setKey("FrameAdvantageHints", "debugInfo", frameAdvantageHints.debugInfo ? "true" : "false");

        // Сохраняем цвета
        setKey("FrameAdvantageHints", "color0_r", std::to_string(frameAdvantageHints.color0[0]));
        setKey("FrameAdvantageHints", "color0_g", std::to_string(frameAdvantageHints.color0[1]));
        setKey("FrameAdvantageHints", "color0_b", std::to_string(frameAdvantageHints.color0[2]));

        setKey("FrameAdvantageHints", "colorLargePlus_r", std::to_string(frameAdvantageHints.colorLargePlus[0]));
        setKey("FrameAdvantageHints", "colorLargePlus_g", std::to_string(frameAdvantageHints.colorLargePlus[1]));
        setKey("FrameAdvantageHints", "colorLargePlus_b", std::to_string(frameAdvantageHints.colorLargePlus[2]));

        setKey("FrameAdvantageHints", "colorMedimPlus_r", std::to_string(frameAdvantageHints.colorMediumPlus[0]));
        setKey("FrameAdvantageHints", "colorMedimPlus_g", std::to_string(frameAdvantageHints.colorMediumPlus[1]));
        setKey("FrameAdvantageHints", "colorMedimPlus_b", std::to_string(frameAdvantageHints.colorMediumPlus[2]));

        setKey("FrameAdvantageHints", "colorSmallPlus_r", std::to_string(frameAdvantageHints.colorSmallPlus[0]));
        setKey("FrameAdvantageHints", "colorSmallPlus_g", std::to_string(frameAdvantageHints.colorSmallPlus[1]));
        setKey("FrameAdvantageHints", "colorSmallPlus_b", std::to_string(frameAdvantageHints.colorSmallPlus[2]));

        setKey("FrameAdvantageHints", "colorLargeMinus_r", std::to_string(frameAdvantageHints.colorLargeMinus[0]));
        setKey("FrameAdvantageHints", "colorLargeMinus_g", std::to_string(frameAdvantageHints.colorLargeMinus[1]));
        setKey("FrameAdvantageHints", "colorLargeMinus_b", std::to_string(frameAdvantageHints.colorLargeMinus[2]));

        setKey("FrameAdvantageHints", "colorMediumMinus_r", std::to_string(frameAdvantageHints.colorMediumMinus[0]));
        setKey("FrameAdvantageHints", "colorMediumMinus_g", std::to_string(frameAdvantageHints.colorMediumMinus[1]));
        setKey("FrameAdvantageHints", "colorMediumMinus_b", std::to_string(frameAdvantageHints.colorMediumMinus[2]));

        setKey("FrameAdvantageHints", "colorSmallMinus_r", std::to_string(frameAdvantageHints.colorSmallMinus[0]));
        setKey("FrameAdvantageHints", "colorSmallMinus_g", std::to_string(frameAdvantageHints.colorSmallMinus[1]));
        setKey("FrameAdvantageHints", "colorSmallMinus_b", std::to_string(frameAdvantageHints.colorSmallMinus[2]));

        const auto& ring = frameAdvantageHints.hitRing;
        setKey("HitRings", "enabled", ring.enabled ? "true" : "false");
        setKey("HitRings", "thickness", std::to_string(ring.thickness));
        const char* channels[] = { "r", "g", "b" };
        for (int i = 0; i < 5; ++i) for (int c = 0; c < 3; ++c) {
            setKey("HitRings", std::string(HitRingSettings::types[i]) + "_" + channels[c],
                std::to_string(ring.colors[i][c]));
        }

        if (!kv::userSettings->Save()) {
            Notify("SAVE FAILED", "Could not write the settings file.");
            DebugLog("[menu] Failed to save settings\n");
            return;
        }
        Notify("SETTINGS SAVED", "Your preferences have been updated.");

        DebugLog("[menu] Settings saved\n");
    }

    // Загрузка настроек
    void LoadSettings() {
        frameAdvantageHints.enabled = getValue("FrameAdvantageHints", "enabled", defaultFrameAdvantageHints.enabled ? "true" : "false") == "true";
        frameAdvantageHints.throwTimeline = getValue("FrameAdvantageHints", "throwTimeline", "true") == "true";
        const auto throwSetting = [&](const char* key, float fallback, float low, float high) {
            try {
                const float value = std::stof(getValue("FrameAdvantageHints", key, std::to_string(fallback)));
                return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
            } catch (...) { return fallback; }
        };
        frameAdvantageHints.throwCardStyle = static_cast<int>(throwSetting("throwCardStyle", static_cast<float>(defaultFrameAdvantageHints.throwCardStyle), 0, 2));
        frameAdvantageHints.throwCardDuration = throwSetting("throwCardDuration", 5, 1, 15);
        frameAdvantageHints.throwCardFade = throwSetting("throwCardFade", 1, 0, 3);
        frameAdvantageHints.startPositionMode = getValue("FrameAdvantageHints", "startPositionMode",
            std::to_string(defaultFrameAdvantageHints.startPositionMode)) == "0" ? 0 : 1;
        frameAdvantageHints.lockToWorld = getValue("FrameAdvantageHints", "lockToWorld",
            defaultFrameAdvantageHints.lockToWorld ? "true" : "false") == "true";
        frameAdvantageHints.fontSize = std::stof(getValue("FrameAdvantageHints", "fontSize", std::to_string(DefaultFontSize())));
        frameAdvantageHints.fontShadow = getValue("FrameAdvantageHints", "fontShadow", defaultFrameAdvantageHints.fontShadow ? "true" : "false") == "true";
        frameAdvantageHints.flySpeed = std::stof(getValue("FrameAdvantageHints", "flySpeed", std::to_string(defaultFrameAdvantageHints.flySpeed)));
        frameAdvantageHints.fadeSpeed = std::stof(getValue("FrameAdvantageHints", "fadeSpeed", std::to_string(defaultFrameAdvantageHints.fadeSpeed)));

        // Загружаем цвета
        frameAdvantageHints.color0[0] = std::stof(getValue("FrameAdvantageHints", "color0_r", std::to_string(defaultFrameAdvantageHints.color0[0])));
        frameAdvantageHints.color0[1] = std::stof(getValue("FrameAdvantageHints", "color0_g", std::to_string(defaultFrameAdvantageHints.color0[1])));
        frameAdvantageHints.color0[2] = std::stof(getValue("FrameAdvantageHints", "color0_b", std::to_string(defaultFrameAdvantageHints.color0[2])));

        frameAdvantageHints.colorLargePlus[0] = std::stof(getValue("FrameAdvantageHints", "colorLargePlus_r", std::to_string(defaultFrameAdvantageHints.colorLargePlus[0])));
        frameAdvantageHints.colorLargePlus[1] = std::stof(getValue("FrameAdvantageHints", "colorLargePlus_g", std::to_string(defaultFrameAdvantageHints.colorLargePlus[1])));
        frameAdvantageHints.colorLargePlus[2] = std::stof(getValue("FrameAdvantageHints", "colorLargePlus_b", std::to_string(defaultFrameAdvantageHints.colorLargePlus[2])));

        frameAdvantageHints.colorMediumPlus[0] = std::stof(getValue("FrameAdvantageHints", "colorMedimPlus_r", std::to_string(defaultFrameAdvantageHints.colorMediumPlus[0])));
        frameAdvantageHints.colorMediumPlus[1] = std::stof(getValue("FrameAdvantageHints", "colorMedimPlus_g", std::to_string(defaultFrameAdvantageHints.colorMediumPlus[1])));
        frameAdvantageHints.colorMediumPlus[2] = std::stof(getValue("FrameAdvantageHints", "colorMedimPlus_b", std::to_string(defaultFrameAdvantageHints.colorMediumPlus[2])));

        frameAdvantageHints.colorSmallPlus[0] = std::stof(getValue("FrameAdvantageHints", "colorSmallPlus_r", std::to_string(defaultFrameAdvantageHints.colorSmallPlus[0])));
        frameAdvantageHints.colorSmallPlus[1] = std::stof(getValue("FrameAdvantageHints", "colorSmallPlus_g", std::to_string(defaultFrameAdvantageHints.colorSmallPlus[1])));
        frameAdvantageHints.colorSmallPlus[2] = std::stof(getValue("FrameAdvantageHints", "colorSmallPlus_b", std::to_string(defaultFrameAdvantageHints.colorSmallPlus[2])));

        frameAdvantageHints.colorLargeMinus[0] = std::stof(getValue("FrameAdvantageHints", "colorLargeMinus_r", std::to_string(defaultFrameAdvantageHints.colorLargeMinus[0])));
        frameAdvantageHints.colorLargeMinus[1] = std::stof(getValue("FrameAdvantageHints", "colorLargeMinus_g", std::to_string(defaultFrameAdvantageHints.colorLargeMinus[1])));
        frameAdvantageHints.colorLargeMinus[2] = std::stof(getValue("FrameAdvantageHints", "colorLargeMinus_b", std::to_string(defaultFrameAdvantageHints.colorLargeMinus[2])));

        frameAdvantageHints.colorMediumMinus[0] = std::stof(getValue("FrameAdvantageHints", "colorMediumMinus_r", std::to_string(defaultFrameAdvantageHints.colorMediumMinus[0])));
        frameAdvantageHints.colorMediumMinus[1] = std::stof(getValue("FrameAdvantageHints", "colorMediumMinus_g", std::to_string(defaultFrameAdvantageHints.colorMediumMinus[1])));
        frameAdvantageHints.colorMediumMinus[2] = std::stof(getValue("FrameAdvantageHints", "colorMediumMinus_b", std::to_string(defaultFrameAdvantageHints.colorMediumMinus[2])));

        frameAdvantageHints.colorSmallMinus[0] = std::stof(getValue("FrameAdvantageHints", "colorSmallMinus_r", std::to_string(defaultFrameAdvantageHints.colorSmallMinus[0])));
        frameAdvantageHints.colorSmallMinus[1] = std::stof(getValue("FrameAdvantageHints", "colorSmallMinus_g", std::to_string(defaultFrameAdvantageHints.colorSmallMinus[1])));
        frameAdvantageHints.colorSmallMinus[2] = std::stof(getValue("FrameAdvantageHints", "colorSmallMinus_b", std::to_string(defaultFrameAdvantageHints.colorSmallMinus[2])));

        frameAdvantageHints.debugInfo = getValue("FrameAdvantageHints", "debugInfo", defaultFrameAdvantageHints.debugInfo ? "true" : "false") == "true";

        auto& ring = frameAdvantageHints.hitRing;
        const auto& ringDefaults = defaultFrameAdvantageHints.hitRing;
        ring.enabled = getValue("HitRings", "enabled", ringDefaults.enabled ? "true" : "false") == "true";
        auto readRingFloat = [&](const std::string& key, float fallback, float low, float high) {
            try {
                const float value = std::stof(getValue("HitRings", key, std::to_string(fallback)));
                return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
            }
            catch (const std::exception&) { return fallback; }
        };
        ring.thickness = readRingFloat("thickness", ringDefaults.thickness, 1.0f, 12.0f);
        const char* channels[] = { "r", "g", "b" };
        for (int i = 0; i < 5; ++i) for (int c = 0; c < 3; ++c) {
            ring.colors[i][c] = readRingFloat(std::string(HitRingSettings::types[i]) + "_" + channels[c],
                ringDefaults.colors[i][c], 0.0f, 1.0f);
        }

        DebugLog("[menu] Settings loaded\n");
    }

    // Функция сброса настроек к дефолтным значениям
    void ResetToDefaults() {
        frameAdvantageHints = defaultFrameAdvantageHints;
        frameAdvantageHints.fontSize = DefaultFontSize();

        SaveAllSettings(); // Автоматически сохраняем после сброса

        DebugLog("[menu] Settings reset to default values\n");
    }

    // Обертки для автоматического сохранения при изменениях
    void SetFrameAdvantageEnabled(bool value) {
        if (frameAdvantageHints.enabled != value) {
            frameAdvantageHints.enabled = value;
            SaveAllSettings();
        }
    }

    void SetFrameAdvantageFontSize(float value) {
        if (frameAdvantageHints.fontSize != value) {
            frameAdvantageHints.fontSize = value;
            SaveAllSettings();
        }
    }

    void SetFrameAdvantageFontShadow(bool value) {
        if (frameAdvantageHints.fontShadow != value) {
            frameAdvantageHints.fontShadow = value;
            SaveAllSettings();
        }
    }

    void SetFrameAdvantageFlySpeed(float value) {
        if (frameAdvantageHints.flySpeed != value) {
            frameAdvantageHints.flySpeed = value;
            SaveAllSettings();
        }
    }

    void SetFrameAdvantageFadeSpeed(float value) {
        if (frameAdvantageHints.fadeSpeed != value) {
            frameAdvantageHints.fadeSpeed = value;
            SaveAllSettings();
        }
    }

    void SetColor(float oldColor[3], const float newColor[3]) {
        if (oldColor[0] != newColor[0] || oldColor[1] != newColor[1] || oldColor[2] != newColor[2]) {
            oldColor[0] = newColor[0];
            oldColor[1] = newColor[1];
            oldColor[2] = newColor[2];
            SaveAllSettings();
        }
    }

    void Init(ImGuiIO& io, ImFont* notificationFont) {
        static bool announced = false;
        if (!announced) {
            Notify(globals::appName.c_str(), "Module loaded. Press F1 to open menu.");
            announced = true;
        }
        DrawNotifications(io, notificationFont);

        if (frameAdvantageHints.fontSize == 0.0f) {
            frameAdvantageHints.fontSize = DefaultFontSize();
        }

        io.MouseDrawCursor = isOpen;

        if (!isOpen) {
            return;
        }

        // Reapply the base theme: resize/fullscreen transitions recreate ImGui.
        {
            ImGui::StyleColorsDark();
            ImVec4* colors = ImGui::GetStyle().Colors;

            // Custom color palette
            colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.035f, 0.055f, 0.98f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.065f, 0.065f, 0.09f, 1.0f);
            colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
            colors[ImGuiCol_Button] = ImVec4(0.24f, 0.075f, 0.13f, 1.0f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.72f, 0.06f, 0.24f, 1.0f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.94f, 0.09f, 0.33f, 1.0f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.15f, 0.8f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.9f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.10f, 0.18f, 1.0f);
            colors[ImGuiCol_CheckMark] = colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.15f, 0.39f, 1.0f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.45f, 0.60f, 1.0f);
            colors[ImGuiCol_ResizeGrip] = ImVec4(0.85f, 0.06f, 0.28f, 0.35f);
            colors[ImGuiCol_ResizeGripHovered] = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_ResizeGripActive] = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_Header] = ImVec4(0.23f, 0.07f, 0.13f, 1.0f);
            colors[ImGuiCol_HeaderHovered] = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_HeaderActive] = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_Border] = ImVec4(0.27f, 0.18f, 0.24f, 1.0f);
            colors[ImGuiCol_Text] = ImVec4(0.96f, 0.94f, 0.97f, 1.0f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.57f, 0.57f, 0.65f, 1.0f);
            auto& style = ImGui::GetStyle();
            style.WindowPadding = ImVec2(16, 16);
            style.FramePadding = ImVec2(10, 7);
            style.ItemSpacing = ImVec2(10, 10);
            style.WindowRounding = style.ChildRounding = 2.0f;
            style.FrameRounding = style.GrabRounding = 2.0f;

        }

        // Scale from a fresh copy each frame, and restore the shared overlay font/style.
        struct MenuStyleScope {
            ImGuiStyle style = ImGui::GetStyle();
            ImFont* font;
            float fontScale;
            MenuStyleScope(ImFont* value, float scale) : font(value), fontScale(value->Scale) {
                ImGui::GetStyle().ScaleAllSizes(scale);
                font->Scale = 16.0f * scale / font->FontSize;
                ImGui::PushFont(font);
            }
            ~MenuStyleScope() {
                ImGui::PopFont();
                font->Scale = fontScale;
                ImGui::GetStyle() = style;
            }
        };
        if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
        const float scale = std::min(io.DisplaySize.x / 1920.0f, io.DisplaySize.y / 1080.0f);
        MenuStyleScope menuStyle(notificationFont ? notificationFont : ImGui::GetFont(), scale);

        // Refit after resolution changes, including changes while the menu was closed.
        static float previousScale = 0.0f;
        static ImVec2 previousSize;
        const float margin = 16.0f * scale;
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
        const ImVec2 maxSize(io.DisplaySize.x - 2 * margin, io.DisplaySize.y - 2 * margin);
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(960.0f * scale, maxSize.x), std::min(450.0f * scale, maxSize.y)), maxSize);
        if (previousScale != scale) {
            const ImVec2 size = previousScale > 0 ? ImVec2(previousSize.x * scale / previousScale, previousSize.y * scale / previousScale) : ImVec2(1100 * scale, 800 * scale);
            ImGui::SetNextWindowSize(ImVec2(std::min(size.x, maxSize.x), std::min(size.y, maxSize.y)));
        }
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        if (!ImGui::Begin(globals::appName.c_str(), &isOpen, flags)) { ImGui::End(); return; }
        previousScale = scale;
        previousSize = ImGui::GetWindowSize();
        const ImVec2 position = ImGui::GetWindowPos();
        ImGui::SetWindowPos(ImVec2(
            std::clamp(position.x, margin, std::max(margin, io.DisplaySize.x - previousSize.x - margin)),
            std::clamp(position.y, margin, std::max(margin, io.DisplaySize.y - previousSize.y - margin))));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.0f, 0.12f, 0.35f, 1.0f), "%s", globals::appName.c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - 110 * scale);
        if (ImGui::Button("CLOSE", ImVec2(110 * scale, 36 * scale))) isOpen = false;
        ImGui::TextDisabled("TRAINING OVERLAY  /  TEKKEN 8");
        ImGui::Separator();

        // Левая панель навигации (фиксированная ширина)
        ImGui::BeginChild("Navigation", ImVec2(200 * scale, 0), true);

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);

        // Кнопки навигации
        ImVec4 activeColor = ImVec4(0.85f, 0.06f, 0.28f, 1.0f);

        // Кнопка MAIN - исправлено
        bool wasMainActive = (currentSection == 1);
        if (wasMainActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        }
        if (ImGui::Button("SETTINGS", ImVec2(-1, 40 * scale))) {
            currentSection = 1;
        }
        if (wasMainActive) {
            ImGui::PopStyleColor();
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);

        // Кнопка SETTINGS - исправлено
        bool wasSettingsActive = (currentSection == 0);
        if (wasSettingsActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        }
        if (ImGui::Button("CONSOLE", ImVec2(-1, 40 * scale))) {
            currentSection = 0;
        }
        if (wasSettingsActive) {
            ImGui::PopStyleColor();
        }

        // Информация о пути к файлу настроек
        ImGui::Spacing();
        ImGui::TextDisabled("F1   Menu");
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + 20 * scale, ImGui::GetWindowHeight() - 100 * scale));
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Version:");
        ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
        ImGui::TextDisabled("%s", globals::appVersion.c_str());

        ImGui::EndChild(); // Конец левой панели

        // Разделитель между панелями
        ImGui::SameLine();

        // Правая область содержимого
        ImGui::BeginChild("Content", ImVec2(0, 0), true);

        // Заголовок текущего раздела
        const char* sectionTitles[] = { "CONSOLE", "SETTINGS" };
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
        ImGui::TextColored(activeColor, "%s", sectionTitles[currentSection]);
        ImGui::Separator();

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);

        // Отображение содержимого в зависимости от выбранного раздела
        switch (currentSection) {
        case 0: { // CONSOLE
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);

            console::DrawConsoleContent();

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);

            break;
        }

        case 1: { // SETTINGS SECTION
            // Frame Advantage Hints Settings
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            ImGui::TextDisabled("Changes are saved automatically.");
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.48f);

            ImGui::Indent(10.0f * scale);
            bool tempEnabled = frameAdvantageHints.enabled;
            if (ImGui::CollapsingHeader("THROW BREAK / Timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Show Throw Break Timeline", &frameAdvantageHints.throwTimeline)) SaveAllSettings();
            ImGui::BeginDisabled(!frameAdvantageHints.throwTimeline);
            const char* throwStyles[] = { "Detailed", "Compact", "Minimal" };
            if (ImGui::Combo("Throw Card Style", &frameAdvantageHints.throwCardStyle, throwStyles, IM_ARRAYSIZE(throwStyles))) SaveAllSettings();
            if (ImGui::SliderFloat("Throw Card Duration", &frameAdvantageHints.throwCardDuration, 1.0f, 15.0f, "%.1f s")) SaveAllSettings();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Time after the result, including fade-out. Later animation updates do not restart it.");
            if (ImGui::SliderFloat("Throw Card Fade-out", &frameAdvantageHints.throwCardFade, 0.0f, 3.0f, "%.1f s")) SaveAllSettings();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fade during the final seconds of the duration. Zero disables fading. Limited to the card duration.");
            ImGui::TextWrapped("Break buttons are revealed only after BROKEN or FAILED.");
            ImGui::EndDisabled();
            }
            if (ImGui::CollapsingHeader("FRAME ADVANTAGE / Text & motion", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Frame Advantage Hints", &tempEnabled)) {
                SetFrameAdvantageEnabled(tempEnabled);
            }

            ImGui::BeginDisabled(!frameAdvantageHints.enabled);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            if (ImGui::Checkbox("Lock text and ring to world position", &frameAdvantageHints.lockToWorld))
                SaveAllSettings();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("New hints rise from the original impact / attack point in world space.\nCamera motion changes their screen position. Without world data (TEST), uses screen-space flight.");
            }

            ImGui::BeginDisabled(frameAdvantageHints.lockToWorld);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            int tempStartPosition = frameAdvantageHints.startPositionMode;
            const char* startPositions[] = { "1 - Screen center (random)", "2 - Impact / attack hitbox" };
            if (ImGui::Combo("Start Position", &tempStartPosition, startPositions, IM_ARRAYSIZE(startPositions))) {
                frameAdvantageHints.startPositionMode = tempStartPosition;
                SaveAllSettings();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Impact point on hit/block; attack hitbox on whiff.\nWithout position data (including TEST), uses screen center.");
            }
            ImGui::EndDisabled();

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            float tempFontSize = frameAdvantageHints.fontSize;
            if (ImGui::SliderFloat("Font Size", &tempFontSize, 20.0f, 200.0f)) {
                SetFrameAdvantageFontSize(tempFontSize);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            bool tempFontShadow = frameAdvantageHints.fontShadow;
            if (ImGui::Checkbox("Font Shadow", &tempFontShadow)) {
                SetFrameAdvantageFontShadow(tempFontShadow);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            float tempFlySpeed = frameAdvantageHints.flySpeed;
            if (ImGui::SliderFloat("Fly Speed", &tempFlySpeed, 0.1f, 25.0f)) {
                SetFrameAdvantageFlySpeed(tempFlySpeed);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Initial upward speed: game world units per frame when world-locked, pixels per frame otherwise.");
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            float tempFadeSpeed = frameAdvantageHints.fadeSpeed;
            if (ImGui::SliderFloat("Fade Speed", &tempFadeSpeed, 0.01f, 0.06f)) {
                SetFrameAdvantageFadeSpeed(tempFadeSpeed);
            }

            ImGui::EndDisabled();
            }
            if (ImGui::CollapsingHeader("HIT RINGS / Impact indicators")) {
            ImGui::TextWrapped("Rings use the frame advantage hint position and require hints to be enabled.");
            ImGui::BeginDisabled(!frameAdvantageHints.enabled);
            auto& ring = frameAdvantageHints.hitRing;
            if (ImGui::Checkbox("Enable hit rings", &ring.enabled)) SaveAllSettings();
            ImGui::BeginDisabled(!ring.enabled);
            if (ImGui::SliderFloat("Ring line thickness", &ring.thickness, 1.0f, 12.0f, "%.1f px"))
                SaveAllSettings();
            for (int i = 0; i < 5; ++i) {
                ImGui::PushID(i);
                if (ImGui::ColorEdit3(HitRingSettings::types[i], ring.colors[i])) SaveAllSettings();
                if (ImGui::Button("TEST ring")) {
                    overlay::AddFlyingText("+4", std::nullopt, HitRingSettings::types[i]);
                }
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            }
            if (ImGui::CollapsingHeader("COLORS / Frame advantage")) {
            ImGui::BeginDisabled(!frameAdvantageHints.enabled);

            // Цвета для подсказок
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            ImGui::Text("Colors:");

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            float tempColor0[3] = { frameAdvantageHints.color0[0], frameAdvantageHints.color0[1], frameAdvantageHints.color0[2] };
            if (ImGui::ColorEdit3("Color 0", tempColor0)) {
                SetColor(frameAdvantageHints.color0, tempColor0);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            if (ImGui::Button("TEST##1")) {
                overlay::AddFlyingText("0");
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            float tempColorLargePlus[3] = { frameAdvantageHints.colorLargePlus[0], frameAdvantageHints.colorLargePlus[1], frameAdvantageHints.colorLargePlus[2] };
            if (ImGui::ColorEdit3("Large Plus Color", tempColorLargePlus)) {
                SetColor(frameAdvantageHints.colorLargePlus, tempColorLargePlus);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            if (ImGui::Button("TEST##2")) {
                overlay::AddFlyingText("+9");
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            float tempColorMeduimPlus[3] = { frameAdvantageHints.colorMediumPlus[0], frameAdvantageHints.colorMediumPlus[1], frameAdvantageHints.colorMediumPlus[2] };
            if (ImGui::ColorEdit3("Medium Plus Color", tempColorMeduimPlus)) {
                SetColor(frameAdvantageHints.colorMediumPlus, tempColorMeduimPlus);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            if (ImGui::Button("TEST##3")) {
                overlay::AddFlyingText("+4");
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            float tempColorSmallPlus[3] = { frameAdvantageHints.colorSmallPlus[0], frameAdvantageHints.colorSmallPlus[1], frameAdvantageHints.colorSmallPlus[2] };
            if (ImGui::ColorEdit3("Small Plus Color", tempColorSmallPlus)) {
                SetColor(frameAdvantageHints.colorSmallPlus, tempColorSmallPlus);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            if (ImGui::Button("TEST##4")) {
                overlay::AddFlyingText("+1");
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            float tempColorSmallMinus[3] = { frameAdvantageHints.colorSmallMinus[0], frameAdvantageHints.colorSmallMinus[1], frameAdvantageHints.colorSmallMinus[2] };
            if (ImGui::ColorEdit3("Small Minus Color", tempColorSmallMinus)) {
                SetColor(frameAdvantageHints.colorSmallMinus, tempColorSmallMinus);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            if (ImGui::Button("TEST##5")) {
                overlay::AddFlyingText("-1");
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            float tempColorMediumMinus[3] = { frameAdvantageHints.colorMediumMinus[0], frameAdvantageHints.colorMediumMinus[1], frameAdvantageHints.colorMediumMinus[2] };
            if (ImGui::ColorEdit3("Medium Minus Color", tempColorMediumMinus)) {
                SetColor(frameAdvantageHints.colorMediumMinus, tempColorMediumMinus);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            if (ImGui::Button("TEST##6")) {
                overlay::AddFlyingText("-10");
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            float tempColorLargeMinus[3] = { frameAdvantageHints.colorLargeMinus[0], frameAdvantageHints.colorLargeMinus[1], frameAdvantageHints.colorLargeMinus[2] };
            if (ImGui::ColorEdit3("Large Minus Color", tempColorLargeMinus)) {
                SetColor(frameAdvantageHints.colorLargeMinus, tempColorLargeMinus);
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            if (ImGui::Button("TEST##7")) {
                overlay::AddFlyingText("-26");
            }

            ImGui::EndDisabled();

            }
            if (ImGui::CollapsingHeader("SYSTEM / Diagnostics & defaults")) {
            if (ImGui::Checkbox("Show diagnostic information", &frameAdvantageHints.debugInfo)) SaveAllSettings();
            ImGui::TextWrapped("Restore all overlay preferences to their default values. The reset is saved immediately.");

            if (ImGui::Button("Reset to defaults")) {
                ResetToDefaults();
            }
            }
            ImGui::Unindent(10.0f * scale);
            ImGui::PopItemWidth();

            break;
        }
        }

        ImGui::EndChild(); // Конец правой области

        ImGui::End(); // Конец главного окна
    }
}
