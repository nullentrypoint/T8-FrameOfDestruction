// Real settings load/save/reset roundtrip, isolated from the user's INI.
#include "stdafx.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
namespace globals {
    bool enableDebugLog = false;
    const std::string appName = "settings test", appVersion = "test";
}
namespace console {
    void AddLog(const std::string&) {}
    void DrawConsoleContent() {}
    void AddLogError(const std::string& s) { throw std::runtime_error(s); }
}
namespace kv { ini::Config* userSettings = nullptr; }
namespace overlay {
    uint64_t AddFlyingText(const std::string&, const std::optional<ScreenAnchor>&,
        const std::string&, const std::optional<WorldPoint>&) { return 0; }
}
namespace menu { void SaveAllSettings(); void ResetToDefaults(); }

static std::vector<float*> colors(FrameAdvantageHints& s) {
    std::vector<float*> result{s.color0, s.colorSmallPlus, s.colorMediumPlus, s.colorLargePlus,
        s.colorSmallMinus, s.colorMediumMinus, s.colorLargeMinus};
    for (auto& rgb : s.hitRing.colors) result.push_back(rgb);
    return result;
}

static void checkDefaults(float fontSize = 60) {
    const unsigned expected[] = {0xFFFFFF, 0x7AFFD5, 0x40DEFF, 0x8495FF,
        0xFFE0AD, 0xFF792E, 0xFF527A, 0x66FF99, 0x39E9FF, 0xFFE45C, 0xFF4976, 0x6685FF};
    auto& s = menu::frameAdvantageHints;
    assert(s.enabled && s.startPositionMode == 1 && s.lockToWorld && s.fontShadow);
    assert(s.fontSize == fontSize && s.flySpeed == 1 && std::abs(s.fadeSpeed - .015f) < 1e-6f);
    assert(!s.debugInfo && s.hitRing.enabled && s.hitRing.thickness == 3);
    auto rgb = colors(s);
    for (size_t i = 0; i < rgb.size(); ++i) for (int c = 0; c < 3; ++c)
        assert(std::abs(rgb[i][c] - ((expected[i] >> (16 - c * 8)) & 255) / 255.f) < 1e-6f);
}

static void customize() {
    auto& s = menu::frameAdvantageHints;
    s.enabled = false; s.startPositionMode = 0; s.lockToWorld = false;
    s.fontSize = 123; s.fontShadow = false; s.flySpeed = 7; s.fadeSpeed = .04f;
    s.debugInfo = true; s.hitRing.enabled = false; s.hitRing.thickness = 9;
    for (auto rgb : colors(s)) for (int c = 0; c < 3; ++c) rgb[c] = .25f;
}

int main() {
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
    const auto path = std::filesystem::temp_directory_path() /
        ("t8_settings_test_" + std::to_string(GetCurrentProcessId()) + ".ini");
    ini::Config config(path.string());
    kv::userSettings = &config;
    menu::LoadSettings(); // No keys: defaults must match reset defaults.
    checkDefaults();
    customize(); menu::SaveAllSettings();
    ini::Config loaded(path.string()); loaded.Read(); kv::userSettings = &loaded;
    menu::frameAdvantageHints = FrameAdvantageHints{};
    menu::LoadSettings();
    assert(!menu::frameAdvantageHints.enabled && menu::frameAdvantageHints.startPositionMode == 0);
    assert(menu::frameAdvantageHints.debugInfo && !menu::frameAdvantageHints.lockToWorld);
    for (auto rgb : colors(menu::frameAdvantageHints)) for (int c = 0; c < 3; ++c) assert(rgb[c] == .25f);
    menu::ResetToDefaults(); checkDefaults();
    // Reopen the file, corrupt the in-memory state and load: reset must persist.
    ini::Config reset(path.string()); reset.Read(); kv::userSettings = &reset;
    customize(); menu::LoadSettings(); checkDefaults();
    ImGui::GetIO().DisplaySize = ImVec2(2560, 1440);
    menu::ResetToDefaults(); checkDefaults(80);
    ImGui::GetIO().DisplaySize = ImVec2(3840, 2160);
    menu::ResetToDefaults(); checkDefaults(120);
    reset.Read(); customize(); menu::LoadSettings(); checkDefaults(120);
    std::filesystem::remove(path);
    kv::userSettings = nullptr;
    ImGui::DestroyContext();
    std::cout << "Neon defaults and settings reset/save/reload checks passed\n";
}
