#include "stdafx.h"
#include <fstream>
#include <string>
#include <windows.h>
#include <shlobj.h> // для SHGetFolderPath
#include <sstream>
#include "helper.h"
#include <format>

namespace menu {
    bool isOpen = false;
    float test = 0.0f;
    static int currentSection = 0; // 0=Main, 1=Settings, 2=Stats

    // Данные для разных разделов
    FrameAdvantageHints frameAdvantageHints;


    // Дефолтные значения для FrameAdvantageHints
    struct DefaultFrameAdvantageHints {
        bool enabled = true;
        float fontSize = 0.0f;
        bool fontShadow = true;
        float flySpeed = 1.000000f;
        float fadeSpeed = 0.015000f;
        float color0[3] = { 1.000000f, 1.000000f, 1.000000f };
        float colorLargePlus[3] = { 0.078431f, 0.376586f, 1.000000f };
        float colorMediumPlus[3] = { 0.000000f, 0.676471f, 1.000000f };
        float colorSmallPlus[3] = { 0.200000f, 1.000000f, 1.000000f };
        float colorLargeMinus[3] = { 0.647058f, 0.000000f, 1.000000f };
        float colorMediumMinus[3] = { 1.000000f, 0.000000f, 0.000000f };
        float colorSmallMinus[3] = { 1.000000f, 0.446078f, 0.446078f };
        bool debugInfo = false;
    } defaultFrameAdvantageHints;

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
        setKey("FrameAdvantageHints", "fontSize", std::to_string(frameAdvantageHints.fontSize));
        setKey("FrameAdvantageHints", "fontShadow", frameAdvantageHints.fontShadow ? "true" : "false");
        setKey("FrameAdvantageHints", "flySpeed", std::to_string(frameAdvantageHints.flySpeed));
        setKey("FrameAdvantageHints", "fadeSpeed", std::to_string(frameAdvantageHints.fadeSpeed));

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

        kv::userSettings->Save();

        DebugLog("[menu] Settings saved\n");
    }

    // Загрузка настроек
    void LoadSettings() {
        frameAdvantageHints.enabled = getValue("FrameAdvantageHints", "enabled", defaultFrameAdvantageHints.enabled ? "true" : "false") == "true";
        frameAdvantageHints.fontSize = std::stof(getValue("FrameAdvantageHints", "fontSize", std::to_string(defaultFrameAdvantageHints.fontSize)));
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

        DebugLog("[menu] Settings loaded\n");
    }

    // Функция сброса настроек к дефолтным значениям
    void ResetToDefaults() {
        frameAdvantageHints.enabled = defaultFrameAdvantageHints.enabled;
        frameAdvantageHints.fontSize = defaultFrameAdvantageHints.fontSize;
        frameAdvantageHints.fontShadow = defaultFrameAdvantageHints.fontShadow;
        frameAdvantageHints.flySpeed = defaultFrameAdvantageHints.flySpeed;
        frameAdvantageHints.fadeSpeed = defaultFrameAdvantageHints.fadeSpeed;

        memcpy(frameAdvantageHints.color0, defaultFrameAdvantageHints.color0, sizeof(frameAdvantageHints.color0));
        memcpy(frameAdvantageHints.colorLargePlus, defaultFrameAdvantageHints.colorLargePlus, sizeof(frameAdvantageHints.colorLargePlus));
        memcpy(frameAdvantageHints.colorMediumPlus, defaultFrameAdvantageHints.colorMediumPlus, sizeof(frameAdvantageHints.colorMediumPlus));
        memcpy(frameAdvantageHints.colorSmallPlus, defaultFrameAdvantageHints.colorSmallPlus, sizeof(frameAdvantageHints.colorSmallPlus));
        memcpy(frameAdvantageHints.colorLargeMinus, defaultFrameAdvantageHints.colorLargeMinus, sizeof(frameAdvantageHints.colorLargeMinus));
        memcpy(frameAdvantageHints.colorMediumMinus, defaultFrameAdvantageHints.colorMediumMinus, sizeof(frameAdvantageHints.colorMediumMinus));
        memcpy(frameAdvantageHints.colorSmallMinus, defaultFrameAdvantageHints.colorSmallMinus, sizeof(frameAdvantageHints.colorSmallMinus));

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

    void Init(ImGuiIO& io) {
        DebugLog("[menu] Rendering menu. isOpen=%d, test=%.2f\n", isOpen, test);

        if (defaultFrameAdvantageHints.fontSize == 0.0f) {
            defaultFrameAdvantageHints.fontSize = io.DisplaySize.y / 18;
        }

        if (frameAdvantageHints.fontSize == 0.0f) {
            frameAdvantageHints.fontSize = io.DisplaySize.y / 18;
        }

        io.MouseDrawCursor = isOpen;

        if (!isOpen) {
            return;
        }

        // Style setup (one-time)
        static bool styled = false;
        if (!styled) {
            ImGui::StyleColorsDark();
            ImVec4* colors = ImGui::GetStyle().Colors;

            // Custom color palette
            colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.95f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.95f);
            colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
            colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.7f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.0f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.15f, 0.8f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.9f);

            styled = true;
            DebugLog("[menu] Style applied.\n");
        }

        // Window flags
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        ImGui::SetNextWindowSize(ImVec2(600, 800), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

        ImGui::Begin(globals::appName.c_str(), &isOpen, flags);

        // Левая панель навигации (фиксированная ширина)
        ImGui::BeginChild("Navigation", ImVec2(180, 0), true);

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);

        // Кнопки навигации
        ImVec4 activeColor = ImVec4(0.26f, 0.59f, 0.98f, 0.9f);

        // Кнопка MAIN - исправлено
        bool wasMainActive = (currentSection == 0);
        if (wasMainActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        }
        if (ImGui::Button("CONSOLE", ImVec2(160, 35))) {
            currentSection = 0;
            DebugLog("[menu] Switched to CONSOLE section\n");
        }
        if (wasMainActive) {
            ImGui::PopStyleColor();
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);

        // Кнопка SETTINGS - исправлено
        bool wasSettingsActive = (currentSection == 1);
        if (wasSettingsActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        }
        if (ImGui::Button("SETTINGS", ImVec2(160, 35))) {
            currentSection = 1;
            DebugLog("[menu] Switched to SETTINGS section\n");
        }
        if (wasSettingsActive) {
            ImGui::PopStyleColor();
        }

        // Информация о пути к файлу настроек
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 80);
        ImGui::Separator();
        ImGui::SetCursorPosX(5);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Version:");
        ImGui::SetCursorPosX(5);
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), globals::appVersion.c_str());

        ImGui::EndChild(); // Конец левой панели

        // Разделитель между панелями
        ImGui::SameLine();

        // Правая область содержимого
        ImGui::BeginChild("Content", ImVec2(0, 0), true);

        // Заголовок текущего раздела
        const char* sectionTitles[] = { "CONSOLE", "SETTINGS" };
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
        ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "%s", sectionTitles[currentSection]);
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
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Frame Advantage Hints:");

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            bool tempEnabled = frameAdvantageHints.enabled;
            if (ImGui::Checkbox("Enable Frame Advantage Hints", &tempEnabled)) {
                SetFrameAdvantageEnabled(tempEnabled);
            }

            ImGui::BeginDisabled(!frameAdvantageHints.enabled);

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

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            float tempFadeSpeed = frameAdvantageHints.fadeSpeed;
            if (ImGui::SliderFloat("Fade Speed", &tempFadeSpeed, 0.01f, 0.06f)) {
                SetFrameAdvantageFadeSpeed(tempFadeSpeed);
            }

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
            if (ImGui::ColorEdit3("Meduim Plus Color", tempColorMeduimPlus)) {
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

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);

            if (ImGui::Button("Reset to defaults")) {
                ResetToDefaults();
            }

            break;
        }
        }

        ImGui::EndChild(); // Конец правой области

        ImGui::End(); // Конец главного окна
    }
}