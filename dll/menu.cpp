#include "stdafx.h"
#include "VoiceChat/VoiceAudio.h"
#include "VoiceChat/VoicePresentation.h"
#include "VoiceChat/PeerPreferences.h"
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
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
#include <mutex>
#include <limits>

namespace menu {
    bool isOpen = false;
    float test = 0.0f;
    static int currentSection = 1; // Settings first; console is a diagnostic tool.
    static bool bindingMicrophoneKey = false;

    struct Notification {
        std::string title;
        std::string detail;
        double started;
        double refreshed;
        double duration;
        std::shared_ptr<const VoiceChat::Avatar> avatar;
        bool voice = false;
    };
    static std::vector<Notification> notifications;
    static std::mutex notificationMutex;
    static std::vector<Notification> pendingNotifications;

    void QueueNotification(std::string title, std::string detail, double duration) {
        if (!std::isfinite(duration) || duration <= 0.0) duration = 8.0;
        std::lock_guard lock(notificationMutex);
        if (pendingNotifications.size() == 32) pendingNotifications.erase(pendingNotifications.begin());
        pendingNotifications.push_back({std::move(title), std::move(detail), 0, 0, duration});
    }

    static double NotificationTime() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    static void Notify(const char* title, const char* detail, double duration = 4.5,
        std::shared_ptr<const VoiceChat::Avatar> avatar = {}, bool voice = false) {
        if (!ImGui::GetCurrentContext()) return;
        for (auto& item : notifications) {
            if (item.title == title && item.detail == detail) {
                item.refreshed = NotificationTime();
                item.detail = detail;
                item.duration = duration;
                return;
            }
        }
        if (notifications.size() == 3) notifications.erase(notifications.begin());
        const double now = NotificationTime();
        notifications.push_back({ title, detail, now, now, duration, std::move(avatar), voice });
    }

    // Steam small avatars are 32x32. Row runs render the exact RGBA bitmap
    // through ImGui's existing atlas without additional DX12 resources or
    // descriptor lifetimes during resize/unload. CPU images survive notifications.
    static void DrawAvatar(ImDrawList* draw, ImVec2 at, float size,
        const std::shared_ptr<const VoiceChat::Avatar>& avatar, float alpha = 1.0f) {
        if (!avatar) {
            draw->AddRectFilled(at, ImVec2(at.x + size, at.y + size), IM_COL32(65, 65, 80, int(alpha * 255)));
            draw->AddCircleFilled(ImVec2(at.x + size * .5f, at.y + size * .35f), size * .16f, IM_COL32(165, 165, 180, int(alpha * 255)));
            draw->AddRectFilled(ImVec2(at.x + size * .2f, at.y + size * .58f), ImVec2(at.x + size * .8f, at.y + size * .85f), IM_COL32(165, 165, 180, int(alpha * 255)), size * .12f);
            return;
        }
        const int resolution = std::min(avatar->resolution, std::max(1, static_cast<int>(size)));
        const auto pixelAt = [&](int x, int y) {
            return avatar->rgba.data() + ((y * avatar->resolution / resolution) * avatar->resolution + x * avatar->resolution / resolution) * 4;
        };
        const float step = size / resolution;
        for (int y = 0; y < resolution; ++y) for (int x = 0; x < resolution;) {
            const auto* pixel = pixelAt(x, y);
            int end = x + 1;
            while (end < resolution && !memcmp(pixel, pixelAt(end, y), 4)) ++end;
            draw->AddRectFilled(ImVec2(at.x + x * step, at.y + y * step), ImVec2(at.x + end * step, at.y + (y + 1) * step),
                IM_COL32(pixel[0], pixel[1], pixel[2], int(pixel[3] * alpha)));
            x = end;
        }
    }

    static void DrawNotifications(const ImGuiIO& io, ImFont* font) {
        std::vector<Notification> pending;
        {
            std::lock_guard lock(notificationMutex);
            pending.swap(pendingNotifications);
        }
        for (const auto& item : pending) Notify(item.title.c_str(), item.detail.c_str(), item.duration);
        std::vector<VoiceChat::VoiceNotice> voiceEvents;
        {
            std::lock_guard lock(VoiceChat::presentationMutex);
            voiceEvents.swap(VoiceChat::voiceNotices);
        }
        for (const auto& event : voiceEvents) {
            const std::string detail = event.peer.name + (event.connected ? "\nOpponent joined voice chat." : "\nOpponent left voice chat.");
            Notify("VOICE CHAT", detail.c_str(), 6, event.peer.avatar, true);
        }
        if (!font) font = ImGui::GetFont();
        const double now = NotificationTime();
        std::erase_if(notifications, [now](const Notification& item) { return now - item.refreshed >= item.duration; });
        auto* draw = ImGui::GetForegroundDrawList();
        const float scale = std::clamp(io.DisplaySize.y / 1080.0f, 0.75f, 1.5f);
        const float width = std::min(360.0f * scale, io.DisplaySize.x - 24.0f);
        float y = 24.0f * scale;
        for (const auto& item : notifications) {
            const float age = static_cast<float>(now - item.refreshed);
            const float alpha = std::min(std::clamp(static_cast<float>(now - item.started) / 0.18f, 0.0f, 1.0f), std::clamp((static_cast<float>(item.duration) - age) / 0.5f, 0.0f, 1.0f));
            const float x = io.DisplaySize.x - width - 24.0f * scale + (1.0f - alpha) * 20.0f * scale;
            const float inset = (item.voice ? 78.0f : 20.0f) * scale;
            const float wrapWidth = std::max(1.0f, width - inset - 20 * scale);
            const float detailHeight = font->CalcTextSizeA(14 * scale, (std::numeric_limits<float>::max)(), wrapWidth, item.detail.c_str()).y;
            const float height = std::max(86 * scale, 46 * scale + detailHeight + 18 * scale);
            const auto color = [alpha](int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, static_cast<int>(a * alpha)); };
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), color(15, 16, 23, 245), 2 * scale);
            draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), color(74, 49, 65), 2 * scale);
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + 4 * scale, y + height), color(240, 24, 85));
            draw->PushClipRect(ImVec2(x + 10 * scale, y), ImVec2(x + width - 10 * scale, y + height), true);
            if (item.voice) DrawAvatar(draw, ImVec2(x + 16 * scale, y + 20 * scale), 48 * scale, item.avatar, alpha);
            draw->AddText(font, 17 * scale, ImVec2(std::round(x + inset), std::round(y + 18 * scale)), color(255, 245, 249), item.title.c_str());
            draw->AddText(font, 14 * scale, ImVec2(std::round(x + inset), std::round(y + 46 * scale)), color(173, 176, 190), item.detail.c_str(), nullptr, wrapWidth);
            draw->PopClipRect();
            draw->AddRectFilled(ImVec2(x, y + height - 2 * scale), ImVec2(x + width * (1.0f - age / static_cast<float>(item.duration)), y + height), color(240, 24, 85));
            y += height + 12 * scale;
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
        if (kv::userSettings->HasKey("microphone_test", "VoiceChat"))
            kv::userSettings->RemoveKey("microphone_test", "VoiceChat");
        setKey("VoiceChat", "microphone_hotkey", std::to_string(VoiceChat::microphoneHotkey.load()));
        setKey("VoiceChat", "enabled", VoiceChat::voiceEnabled ? "true" : "false");
        setKey("VoiceChat", "microphone_muted", VoiceChat::microphoneMuted ? "true" : "false");
        setKey("VoiceChat", "microphone_gain", std::to_string(VoiceChat::microphoneGain.load()));
        {
            std::lock_guard lock(VoiceChat::voiceMutex);
            setKey("VoiceChat", "microphone_name", VoiceChat::preferredMicrophone);
        }
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
        VoiceChat::voiceEnabled = getValue("VoiceChat", "enabled", "true") == "true";
        VoiceChat::microphoneMuted = getValue("VoiceChat", "microphone_muted", "false") == "true";
        VoiceChat::microphoneTest = false;
        try {
            VoiceChat::microphoneGain = std::clamp(std::stoi(getValue("VoiceChat", "microphone_gain", "100")), 0, 400);
        } catch (...) { VoiceChat::microphoneGain = 100; }
        {
            std::lock_guard lock(VoiceChat::voiceMutex);
            VoiceChat::preferredMicrophone = getValue("VoiceChat", "microphone_name", "");
            if (!VoiceChat::microphones.empty()) VoiceChat::ResolveMicrophone();
        }
        try {
            const int key = std::stoi(getValue("VoiceChat", "microphone_hotkey", "0"));
            VoiceChat::microphoneHotkey = key >= 8 && key <= 254 && key != VK_F1 && key != VK_F11 ? key : 0;
        } catch (...) { VoiceChat::microphoneHotkey = 0; }
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

    static std::string HotkeyName(int key) {
        if (!key) return "Not assigned";
        wchar_t wide[64]{}; char text[256]{};
        UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
        if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN || key == VK_INSERT || key == VK_DELETE || key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT || key == VK_DIVIDE || key == VK_NUMLOCK) scan |= 0x100;
        if (GetKeyNameTextW(static_cast<LONG>(scan << 16), wide, 64)) {
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, sizeof(text), nullptr, nullptr);
            return text;
        }
        return "Key " + std::to_string(key);
    }
    static void PollMicrophoneKey(const ImGuiIO& io) {
        static std::array<bool, 256> previous{};
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        const bool focused = process == GetCurrentProcessId();
        for (int key = 8; key < 255; ++key) {
            const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
            const bool pressed = down && !previous[key];
            previous[key] = down;
            if (!pressed || !focused) continue;
            if (bindingMicrophoneKey && isOpen) {
                if (key == VK_ESCAPE) { bindingMicrophoneKey = false; continue; }
                if (key == VK_F1 || key == VK_F11 || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU ||
                    (key >= VK_LSHIFT && key <= VK_RMENU) || key == VK_LWIN || key == VK_RWIN) continue;
                VoiceChat::microphoneHotkey = key == VK_BACK ? 0 : key;
                bindingMicrophoneKey = false;
                SaveAllSettings();
            } else if (!io.WantTextInput && key == VoiceChat::microphoneHotkey.load()) {
                VoiceChat::microphoneMuted = !VoiceChat::microphoneMuted.load();
                SaveAllSettings();
            }
        }
        if (!isOpen || !focused) bindingMicrophoneKey = false;
    }
    static bool ProfileIconButton(const char* id, bool copy, float scale) {
        const bool clicked = ImGui::Button(id, ImVec2(30 * scale, 28 * scale));
        const ImVec2 at = ImGui::GetItemRectMin();
        auto* draw = ImGui::GetWindowDrawList();
        const auto p = [&](float x, float y) { return ImVec2(at.x + x * scale, at.y + y * scale); };
        const auto color = ImGui::GetColorU32(ImGuiCol_Text);
        draw->AddRect(p(7, 10), p(19, 22), color, 1, 0, scale);
        if (copy) draw->AddRect(p(11, 6), p(23, 18), color, 1, 0, scale);
        else {
            draw->AddLine(p(13, 16), p(23, 6), color, 2 * scale);
            draw->AddLine(p(16, 6), p(23, 6), color, 2 * scale);
            draw->AddLine(p(23, 6), p(23, 13), color, 2 * scale);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", copy ? "Copy profile link" : "Open Steam profile in overlay");
        return clicked;
    }
    static void DrawVoiceHUD(const ImGuiIO& io, ImFont* font) {
        VoiceChat::PeerView peer, local;
        { std::lock_guard lock(VoiceChat::presentationMutex); peer = VoiceChat::peerView; local = VoiceChat::localView; }
        const auto now = GetTickCount64();
        const auto previewUntil = VoiceChat::avatarPreviewUntil.load();
        const bool preview = now < previewUntil;
        static bool wasPreview = false;
        static int dragging = -1;
        static ImVec2 dragOffset{}, reference(16.50f, 162.50f);
        if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
        const float hudScale = std::min(io.DisplaySize.x / 1920.0f, io.DisplaySize.y / 1080.0f);
        const float scale = std::clamp(io.DisplaySize.y / 1080.0f, .75f, 1.5f);
        const float size = 42 * scale;
        const float origin = (io.DisplaySize.x - 1920 * hudScale) * .5f;
        float left = origin + reference.x * hudScale, top = reference.y * hudScale;
        if (wasPreview && !preview) {
            console::AddLog(std::format("[Voice preview] Avatar top-left coordinates: P1=({:.1f}, {:.1f}), P2=({:.1f}, {:.1f}), size={:.1f}px, viewport={:.0f}x{:.0f}",
                left, top, io.DisplaySize.x - left - size, top, size, io.DisplaySize.x, io.DisplaySize.y));
            console::AddLog(std::format("[Voice preview] Reference 1920x1080: x={:.2f}, y={:.2f}; P2 mirrored. Position retained for this session.", reference.x, reference.y));
            dragging = -1;
        }
        wasPreview = preview;
        if (preview) {
            ImGui::GetIO().MouseDrawCursor = true;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                for (int side = 0; side < 2; ++side) {
                    const ImVec2 at(side == 0 ? left : io.DisplaySize.x - left - size, top);
                    if (io.MousePos.x >= at.x && io.MousePos.x <= at.x + size && io.MousePos.y >= at.y && io.MousePos.y <= at.y + size) {
                        dragging = side;
                        dragOffset = ImVec2(io.MousePos.x - at.x, io.MousePos.y - at.y);
                        break;
                    }
                }
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = -1;
            if (dragging >= 0 && ImGui::IsMousePosValid()) {
                const float x = io.MousePos.x - dragOffset.x;
                left = std::clamp(dragging == 0 ? x : io.DisplaySize.x - x - size, 0.0f, std::max(0.0f, io.DisplaySize.x * .5f - size));
                top = std::clamp(io.MousePos.y - dragOffset.y, 0.0f, std::max(0.0f, io.DisplaySize.y - size));
                reference = ImVec2((left - origin) / hudScale, top / hudScale);
            }
        }
        const int phase = preview ? static_cast<int>((10000 - (previewUntil - now)) / 2000) : -1;
        if ((!peer.connected && !preview) || io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
        const int localSlot = preview ? 0 : VoiceChat::localPlayerSlot.load();
        if (localSlot < 0) return; // Wait for a known game side rather than mislabel players.
        if (!font) font = ImGui::GetFont();
        auto* draw = ImGui::GetForegroundDrawList();
        const auto preference = VoiceChat::peerPreferences.Get(peer.steamId);
        for (int side = 0; side < 2; ++side) {
            const bool self = VoiceChat::IsLocalHudSlot(side, localSlot);
            const ImVec2 at(side == 0 ? left : io.DisplaySize.x - left - size, top);
            const bool known = preview ? phase != 4 : self || (VoiceChat::remoteMicState.load() >= 0 && now - VoiceChat::remoteMicStateAt.load() < 2500);
            const bool muted = preview ? phase == 2 : self ? (!VoiceChat::voiceEnabled || VoiceChat::microphoneMuted || VoiceChat::microphoneTest || !VoiceChat::captureActive) : known && VoiceChat::remoteMicState.load() == 1;
            const bool blocked = preview ? phase == 3 : !self && (preference.is_mute || preference.volume == 0 || !VoiceChat::voiceEnabled);
            const auto spoke = self ? VoiceChat::localSpokeAt.load() : VoiceChat::remoteSpokeAt.load();
            const bool speaking = preview ? phase == 1 : !muted && !blocked && spoke && now - spoke < 180;
            if (speaking) for (int glow = 4; glow >= 1; --glow)
                draw->AddRect(ImVec2(at.x - glow * scale, at.y - glow * scale), ImVec2(at.x + size + glow * scale, at.y + size + glow * scale), IM_COL32(70, 255, 130, 150 / glow), 3 * scale, 0, 2 * scale);
            DrawAvatar(draw, at, size, self ? local.avatar : peer.avatar);
            if (known && !muted && !blocked) continue;
            const ImVec2 badge(at.x + size - 7 * scale, at.y + size - 6 * scale);
            draw->AddCircleFilled(badge, 10 * scale, IM_COL32(22, 23, 30, 240));
            const ImU32 color = (muted || blocked) ? IM_COL32(255, 75, 85, 255) : IM_COL32(190, 195, 205, 255);
            if (!known && !blocked) draw->AddText(font, 14 * scale, ImVec2(badge.x - 4 * scale, badge.y - 8 * scale), color, "?");
            else if (blocked) {
                draw->AddRectFilled(ImVec2(badge.x - 6 * scale, badge.y - 2 * scale), ImVec2(badge.x - 3 * scale, badge.y + 2 * scale), color);
                draw->AddTriangleFilled(ImVec2(badge.x - 4 * scale, badge.y), ImVec2(badge.x + 1 * scale, badge.y - 5 * scale), ImVec2(badge.x + 1 * scale, badge.y + 5 * scale), color);
                draw->AddLine(ImVec2(badge.x - 6 * scale, badge.y + 6 * scale), ImVec2(badge.x + 6 * scale, badge.y - 6 * scale), color, 2 * scale);
            } else {
                draw->AddRectFilled(ImVec2(badge.x - 2 * scale, badge.y - 6 * scale), ImVec2(badge.x + 2 * scale, badge.y + 1 * scale), color, 2 * scale);
                draw->AddLine(ImVec2(badge.x, badge.y), ImVec2(badge.x, badge.y + 5 * scale), color, scale);
                draw->AddLine(ImVec2(badge.x - 3 * scale, badge.y + 5 * scale), ImVec2(badge.x + 3 * scale, badge.y + 5 * scale), color, scale);
                if (muted || blocked) draw->AddLine(ImVec2(badge.x - 6 * scale, badge.y + 6 * scale), ImVec2(badge.x + 6 * scale, badge.y - 6 * scale), color, 2 * scale);
            }
        }
    }

    void Init(ImGuiIO& io, ImFont* notificationFont) {
        if (!isOpen) VoiceChat::microphoneTest = false;
        if (kv::userSettings && VoiceChat::voiceSettingsDirty.exchange(false)) SaveAllSettings();
        PollMicrophoneKey(io);
        DrawVoiceHUD(io, notificationFont);
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

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
        const bool wasVoiceActive = currentSection == 2;
        if (wasVoiceActive) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        if (ImGui::Button("VOICE CHAT", ImVec2(-1, 40 * scale))) currentSection = 2;
        if (wasVoiceActive) ImGui::PopStyleColor();

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
        const char* sectionTitles[] = { "CONSOLE", "SETTINGS", "VOICE CHAT" };
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
        ImGui::TextColored(activeColor, "%s", sectionTitles[currentSection]);
        ImGui::Separator();

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);

        // Отображение содержимого в зависимости от выбранного раздела
        switch (currentSection) {
        case 2: {
            VoiceChat::PeerView peer;
            { std::lock_guard lock(VoiceChat::presentationMutex); peer = VoiceChat::peerView; }
            std::string status, error;
            std::vector<VoiceChat::Microphone> microphones;
            {
                std::lock_guard lock(VoiceChat::voiceMutex);
                status = VoiceChat::voiceStatus; error = VoiceChat::audioError; microphones = VoiceChat::microphones;
            }
            ImGui::TextWrapped("Connection: %s", status.c_str());
            if (peer.steamId) {
                ImGui::Spacing();
                DrawAvatar(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), 80 * scale, peer.avatar);
                ImGui::Dummy(ImVec2(80 * scale, 80 * scale));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(peer.name.c_str());
                ImGui::Text("SteamID: %llu", static_cast<unsigned long long>(peer.steamId));
                ImGui::TextDisabled("%s", peer.connected ? "In voice chat" : "Not connected");
                ImGui::EndGroup();
                const std::string url = "https://steamcommunity.com/profiles/" + std::to_string(peer.steamId);
                ImGui::TextWrapped("%s", url.c_str());
                if (ProfileIconButton("##open_profile", false, scale)) VoiceChat::openProfileRequest = peer.steamId;
                ImGui::SameLine();
                if (ProfileIconButton("##copy_profile", true, scale)) ImGui::SetClipboardText(url.c_str());
                const bool micKnown = peer.connected && VoiceChat::remoteMicState >= 0 && GetTickCount64() - VoiceChat::remoteMicStateAt.load() < 2500;
                ImGui::Text("Opponent microphone: %s", !micKnown ? "Unknown" : VoiceChat::remoteMicState == 1 ? "Muted" : "Enabled");
                auto preference = VoiceChat::peerPreferences.Get(peer.steamId);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .60f);
                if (ImGui::SliderInt("Opponent volume", &preference.volume, 0, 100, "%d%%")) VoiceChat::peerPreferences.Set(peer.steamId, preference);
                if (ImGui::Checkbox("Mute this opponent", &preference.is_mute)) VoiceChat::peerPreferences.Set(peer.steamId, preference);
                const auto saveError = VoiceChat::peerPreferences.Error();
                if (!saveError.empty()) ImGui::TextWrapped("%s", saveError.c_str());
                ImGui::TextUnformatted("Opponent signal level");
                const float remoteLevel = peer.connected && GetTickCount64() - VoiceChat::remoteLevelAt.load() < 250 ? VoiceChat::opponentLevel.load() : 0;
                ImGui::ProgressBar(remoteLevel, ImVec2(-1, 14 * scale));
            }
            ImGui::Separator();
            ImGui::TextUnformatted("AUDIO");
            bool enabled = VoiceChat::voiceEnabled.load();
            if (ImGui::Checkbox("Enable voice chat", &enabled)) { VoiceChat::voiceEnabled = enabled; SaveAllSettings(); }
            bool muted = VoiceChat::microphoneMuted.load();
            if (ImGui::Checkbox("Mute microphone", &muted)) { VoiceChat::microphoneMuted = muted; SaveAllSettings(); }
            ImGui::Text("Microphone hotkey: %s", HotkeyName(VoiceChat::microphoneHotkey.load()).c_str());
            if (ImGui::Button(bindingMicrophoneKey ? "Press a key (Esc cancels, Backspace clears)" : "Assign microphone hotkey")) bindingMicrophoneKey = true;
            bool test = VoiceChat::microphoneTest.load();
            if (ImGui::Button(test ? "Stop microphone test" : "Test microphone / hear myself")) { test = !test; VoiceChat::microphoneTest = test; }
            if (test) ImGui::TextWrapped("Local monitoring only: your microphone is not sent to the opponent during this test.");
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .60f);
            std::string deviceName = "Unavailable device";
            const UINT device = VoiceChat::microphoneDevice.load();
            for (const auto& mic : microphones) if (mic.id == device) deviceName = mic.name;
            if (ImGui::BeginCombo("Microphone", deviceName.c_str())) {
                for (const auto& mic : microphones) {
                    ImGui::PushID(static_cast<int>(mic.id));
                    if (ImGui::Selectable(mic.name.c_str(), mic.id == device)) {
                        {
                            std::lock_guard lock(VoiceChat::voiceMutex);
                            VoiceChat::preferredMicrophone = mic.id == WAVE_MAPPER ? "" : mic.name;
                            VoiceChat::microphoneDevice = mic.id;
                        }
                        SaveAllSettings();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            int gain = VoiceChat::microphoneGain.load();
            if (ImGui::SliderInt("Microphone gain", &gain, 0, 400, "%d%%")) VoiceChat::microphoneGain = gain;
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveAllSettings();
            ImGui::TextDisabled("100%% = original level; higher gain can clip loud speech.");
            ImGui::PopItemWidth();
            ImGui::TextUnformatted("Microphone level (after gain)");
            const float localLevel = GetTickCount64() - VoiceChat::microphoneLevelAt.load() < 250 ? VoiceChat::microphoneLevel.load() : 0;
            ImGui::ProgressBar(localLevel, ImVec2(-1, 14 * scale));
            ImGui::TextUnformatted(!enabled ? "Audio disabled" : muted ? "Microphone muted" : "Microphone activates when connected");
            if (!error.empty()) ImGui::TextWrapped("Audio: %s", error.c_str());
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Advanced connection details")) {
            if (peer.connected && peer.ping >= 0) ImGui::Text("Ping: %d ms", peer.ping);
            else ImGui::TextUnformatted("Ping: unavailable");
            if (peer.connected && peer.qualityLocal >= 0 && peer.qualityRemote >= 0)
                ImGui::Text("Quality: receive %.1f%% / send %.1f%%", peer.qualityLocal * 100, peer.qualityRemote * 100);
            else ImGui::TextUnformatted("Quality: unavailable");
            ImGui::Text("Traffic: down %.1f KB/s / up %.1f KB/s", peer.incoming / 1024, peer.outgoing / 1024);
            ImGui::Text("Queued: %d bytes", peer.pendingBytes);
            ImGui::Text("Audio packets: sent %llu / received %llu / send errors %llu",
                static_cast<unsigned long long>(peer.sent), static_cast<unsigned long long>(peer.received), static_cast<unsigned long long>(peer.failed));
            ImGui::Text("Connected: %llu s", static_cast<unsigned long long>(peer.connectedSeconds));
            ImGui::TextDisabled("Steam session statistics; PCM16 / 16 kHz / mono / 20 ms");
            }
            ImGui::TextWrapped("Use headphones for microphone testing. Voice settings are saved automatically. Missing microphones fall back to the system default.");
            break;
        }
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
