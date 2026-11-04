#pragma once
#include <d3d12.h>
#include <string>
#include "imgui/imgui.h"

namespace msdftext {
    bool Initialize(ID3D12Device* device, DXGI_FORMAT targetFormat, const std::string& fontPath);
    void Shutdown(); // Caller must wait for the overlay GPU fence first.
    void BeginFrame();
    bool AddText(const std::string& text, ImVec2 position, float size, bool centered,
        ImVec4 color, ImVec4 outlineColor, float outlinePixels);
    // Called after the previous overlay submission has completed, with the RTV bound.
    void Render(ID3D12GraphicsCommandList* commands, ImVec2 displaySize);
}
