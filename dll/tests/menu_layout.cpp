// Headless resolution-switch regression check using the real menu and ImGui.
#include "stdafx.h"
#include "imgui/imgui_internal.h"
#include "VoiceChat/VoiceAudio.h"
#include "VoiceChat/VoicePresentation.h"
#include <cassert>
#include <iostream>

namespace globals {
    bool enableDebugLog = false;
    const std::string appName = "T8-FrameOfDestruction", appVersion = "test";
}
namespace console {
    void DrawConsoleContent() {}
    void AddLog(const std::string&) {}
    void AddLogError(const std::string&) {}
}
namespace kv { ini::Config* userSettings = nullptr; }
#ifdef T8_MENU_LAYOUT_TEST
namespace menu { void ShowVoiceSectionForTest(); }
#endif
namespace overlay {
    uint64_t AddFlyingText(const std::string&, const std::optional<ScreenAnchor>&,
        const std::string&, const std::optional<WorldPoint>&) { return 0; }
}

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    auto* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolab.ttf", 100);
    assert(font);
    io.Fonts->Build();
    menu::isOpen = true;
    const auto frame = [&](float width, float height) {
        io.DisplaySize = ImVec2(width, height);
        ImGui::NewFrame();
        menu::Init(io, font);
        ImGui::Render();
        assert(font->Scale == 1.0f);
        if (!menu::isOpen) return;
        auto* window = ImGui::FindWindowByName(globals::appName.c_str());
        assert(window);
        assert(window->Pos.x >= 0 && window->Pos.y >= 0);
        assert(window->Pos.x + window->Size.x <= width + 1);
        assert(window->Pos.y + window->Size.y <= height + 1);
        assert(std::abs(window->FontRefSize - 16 * std::min(width / 1920, height / 1080)) < 0.1f);
    };
    frame(2560, 1440);
    auto* window = ImGui::FindWindowByName(globals::appName.c_str());
    const float width2k = window->Size.x;
    ImGui::SetWindowPos(window, ImVec2(2300, 1300));
    frame(1280, 720);
    assert(window->Size.x < width2k);
    frame(3840, 2160);
    ImGui::SetWindowPos(window, ImVec2(3500, 1900));
    menu::isOpen = false;
    VoiceChat::microphoneTest = true;
    frame(800, 600);
    assert(!VoiceChat::microphoneTest);
    for (int phase = 0; phase < 5; ++phase) {
        VoiceChat::avatarPreviewUntil = GetTickCount64() + 10000 - phase * 2000;
        frame(1920, 1080);
        assert(ImGui::GetDrawData()->TotalVtxCount > 0);
        assert(!VoiceChat::peerView.connected);
    }
    VoiceChat::avatarPreviewUntil = 0;
    menu::isOpen = true;
    frame(800, 600);
    frame(2560, 1080);
    frame(1280, 1080); // Width-only change.
    frame(1920, 1080);
    const auto padding = ImGui::GetStyle().WindowPadding;
    for (int i = 0; i < 10; ++i) frame(1920, 1080);
    assert(ImGui::GetStyle().WindowPadding.x == padding.x);
    const auto accent = ImGui::GetStyle().Colors[ImGuiCol_ResizeGripActive];
    assert(accent.x > accent.z);
#ifdef T8_MENU_LAYOUT_TEST
    menu::ShowVoiceSectionForTest();
    VoiceChat::loungeView.status = VoiceChat::LoungeReadStatus::Ok;
    VoiceChat::loungeView.roomKey = "SYNTHETIC_LOUNGE";
    for (int i = 0; i < 64; ++i) {
        VoiceChat::LoungeMemberView row;
        row.peer.steamId = 76561198000000001ULL + i;
        row.peer.gameName = "Long game nickname to exercise cell clipping " + std::to_string(i);
        row.peer.name = "Different Steam nickname " + std::to_string(i);
        row.peer.connected = i % 2 == 0;
        row.peer.ping = row.peer.connected ? 42 : -1;
        VoiceChat::loungeView.members.push_back(row);
    }
    for (const auto resolution : {ImVec2(1920, 1080), ImVec2(800, 600), ImVec2(3840, 2160)}) {
        VoiceChat::loungeView.capturedAt = GetTickCount64();
        frame(resolution.x, resolution.y);
        frame(resolution.x, resolution.y);
        auto& tables = ImGui::GetCurrentContext()->Tables;
        bool found = false;
        for (int i = 0; i < tables.GetMapSize(); ++i) {
            auto* table = tables.TryGetMapData(i);
            if (!table || table->ColumnsCount != 6) continue;
            found = true;
            assert(table->InnerWindow->ScrollMax.y > 0); // Many participants scroll instead of expanding the menu.
            assert(table->OuterRect.Min.x >= 0 && table->OuterRect.Max.x <= resolution.x + 1);
            for (int column = 0; column < 6; ++column)
                assert(table->Columns[column].WidthGiven > 0);
        }
        assert(found);
    }
    VoiceChat::loungeView = {};
#endif
    ImGui::DestroyContext();
    // ResizeBuffers destroys and recreates the entire context in the DX12 hook.
    ImGui::CreateContext();
    auto& resizedIO = ImGui::GetIO();
    resizedIO.IniFilename = nullptr;
    resizedIO.DisplaySize = ImVec2(1280, 720);
    resizedIO.Fonts->AddFontDefault();
    auto* resizedFont = resizedIO.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolab.ttf", 100);
    assert(resizedFont);
    resizedIO.Fonts->Build();
    ImGui::NewFrame();
    menu::Init(resizedIO, resizedFont);
    ImGui::Render();
    const auto restoredAccent = ImGui::GetStyle().Colors[ImGuiCol_ResizeGripActive];
    assert(restoredAccent.x == accent.x && restoredAccent.z == accent.z);
    auto* resizedWindow = ImGui::FindWindowByName(globals::appName.c_str());
    assert(resizedWindow->Pos.x >= 0 && resizedWindow->Pos.y >= 0);
    assert(resizedWindow->Pos.x + resizedWindow->Size.x <= 1281);
    assert(resizedWindow->Pos.y + resizedWindow->Size.y <= 721);
    ImGui::DestroyContext();
    std::cout << "Menu resolution, bounds and font/style restoration checks passed\n";
}
