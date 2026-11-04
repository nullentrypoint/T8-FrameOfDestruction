// Exercise the actual overlay update path with a moving camera, without a GPU/game.
#include "stdafx.h"
#include "msdf_text.h"
#include <cassert>
#include <iostream>

namespace menu { FrameAdvantageHints frameAdvantageHints{}; }
namespace console { void AddLog(const std::string&) {} }
namespace msdftext {
    struct Draw { std::string text; ImVec2 position; float alpha; };
    std::vector<Draw> draws;
    void BeginFrame() { draws.clear(); }
    bool AddText(const std::string& text, ImVec2 position, float, bool, ImVec4 color, ImVec4, float) {
        draws.push_back({text, position, color.w});
        return true;
    }
}

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 600);
    io.DeltaTime = 1.f / 60;
    auto* font = io.Fonts->AddFontDefault();
    assert(io.Fonts->Build());
    auto& settings = menu::frameAdvantageHints;
    assert(settings.lockToWorld);
    settings.enabled = true; settings.fontSize = 64; settings.fadeSpeed = .01f; settings.flySpeed = 0;
    CameraSnapshot camera;
    camera.view[2] = camera.view[4] = camera.view[9] = camera.view[15] = 1;
    camera.projection[0] = camera.projection[11] = 1;
    camera.projection[5] = 16.0 / 9.0; camera.projection[14] = 10;
    auto frame = [&]() {
        ImGui::NewFrame(); overlay::Init(io, font); ImGui::Render();
    };
    auto closeEnough = [](float a, float b) { return std::abs(a - b) < .01f; };
    WorldPoint impact{0, 0, 1000};
    overlay::SetCamera(camera);
    const auto id = overlay::AddFlyingText("+4", std::nullopt, "MID", impact);
    impact.z = 2000; // Moving the source after publication must not move the saved anchor.
    frame();
    assert(msdftext::draws.size() == 1);
    assert(closeEnough(msdftext::draws[0].position.x, 500) && closeEnough(msdftext::draws[0].position.y, 300));
    const float firstAlpha = msdftext::draws[0].alpha;
    camera.view[12] = -25; overlay::SetCamera(camera);
    overlay::UpdateFlyingText(id, "+9");
    frame();
    assert(msdftext::draws[0].text == "+9");
    assert(closeEnough(msdftext::draws[0].position.x, 375) && closeEnough(msdftext::draws[0].position.y, 300));
    assert(msdftext::draws[0].alpha < firstAlpha);
    io.DisplaySize = ImVec2(2000, 1200);
    frame();
    assert(closeEnough(msdftext::draws[0].position.x, 750) && closeEnough(msdftext::draws[0].position.y, 600));
    overlay::SetCamera(std::nullopt); frame(); assert(msdftext::draws.empty());
    camera.view[14] = -200; overlay::SetCamera(camera); frame(); assert(msdftext::draws.empty());
    camera.view[14] = 0; overlay::SetCamera(camera); frame(); assert(msdftext::draws.size() == 1);
    // No-world TEST labels use screen-space fallback, unaffected by camera motion.
    overlay::AddFlyingText("test"); frame();
    assert(closeEnough(msdftext::draws.back().position.x, 1000) && closeEnough(msdftext::draws.back().position.y, 600));
    camera.view[12] = 25; overlay::SetCamera(camera); frame();
    assert(closeEnough(msdftext::draws.back().position.x, 1000) && closeEnough(msdftext::draws.back().position.y, 600));
    // Under a rolled camera, world-up maps to screen-right rather than screen-up.
    settings.flySpeed = 10;
    camera.view[4] = camera.view[9] = 0;
    camera.view[8] = 1; camera.view[5] = -1; camera.view[12] = 0;
    overlay::SetCamera(camera);
    overlay::AddFlyingText("world flight", std::nullopt, "MID", WorldPoint{0, 0, 1000}); frame();
    assert(msdftext::draws.back().position.x > 1000);
    assert(closeEnough(msdftext::draws.back().position.y, 600));
    settings.lockToWorld = false; settings.startPositionMode = 1;
    overlay::AddFlyingText("flight"); frame();
    assert(msdftext::draws.back().position.y < 600);
    // Hidden world anchors still expire rather than reappearing after their lifetime.
    overlay::SetCamera(std::nullopt);
    for (int i = 0; i < 110; ++i) frame();
    overlay::SetCamera(camera); frame(); assert(msdftext::draws.empty());
    ImGui::DestroyContext();
    std::cout << "World lock checks passed\n";
}
