// Offscreen WARP verification of the real atlas, shaders, blending and recreation.
#include "stdafx.h"
#include "msdf_text.h"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <cassert>
#include <algorithm>
#include <fstream>
#include <iostream>
namespace globals { bool enableDebugLog = true; }
namespace console { void AddLogError(const std::string& s) { std::cerr << s << '\n'; } }
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if (FAILED(hr)) { std::cerr << std::hex << hr << '\n'; std::abort(); } }
int main(int argc, char** argv) {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter; Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> info; device.As(&info);
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qd{}; Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    Check(list->Close());
    ComPtr<ID3D12Fence> fence; Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr); assert(event);
    UINT64 value = 0;
    constexpr int W = 512, H = 128;
    constexpr DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = W; td.Height = H;
    td.DepthOrArraySize = td.MipLevels = 1; td.SampleDesc.Count = 1;
    td.Format = Format; td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> target, readback;
    Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)));
    ComPtr<ID3D12DescriptorHeap> rtv;
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 1;
    Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv)));
    auto handle = rtv->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target.Get(), nullptr, handle);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp; UINT64 bytes;
    device->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
    td = {}; td.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; td.Width = bytes;
    td.Height = td.DepthOrArraySize = td.MipLevels = 1; td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; hp.Type = D3D12_HEAP_TYPE_READBACK;
    Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    char windows[MAX_PATH]; GetWindowsDirectoryA(windows, MAX_PATH);
    const std::string font = std::string(windows) + "\\Fonts\\consolab.ttf";
    assert(msdftext::Initialize(device.Get(), Format, font));
    auto draw = [&](float outline, float alpha, const char* text = "+9 -26 0") {
        Check(allocator->Reset()); Check(list->Reset(allocator.Get(), nullptr));
        const float clear[4] = {}; list->ClearRenderTargetView(handle, clear, 0, nullptr);
        list->OMSetRenderTargets(1, &handle, FALSE, nullptr);
        msdftext::BeginFrame();
        assert(msdftext::AddText(text, ImVec2(W / 2.f, H / 2.f), 64, true,
            ImVec4(1, 1, 1, alpha), ImVec4(0, 0, 0, alpha), outline));
        msdftext::Render(list.Get(), ImVec2(W, H));
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {target.Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = target.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter); list->ResourceBarrier(1, &barrier);
        Check(list->Close());
        ID3D12CommandList* lists[] = {list.Get()}; queue->ExecuteCommandLists(1, lists);
        Check(queue->Signal(fence.Get(), ++value)); Check(fence->SetEventOnCompletion(value, event));
        assert(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0);
        unsigned char* mapped; Check(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
        std::vector<unsigned char> image(W * H * 4);
        for (int y = 0; y < H; ++y) memcpy(image.data() + y * W * 4, mapped + fp.Offset + y * fp.Footprint.RowPitch, W * 4);
        D3D12_RANGE noWrite{0, 0}; readback->Unmap(0, &noWrite);
        return image;
    };
    const auto plain = draw(0, 1), outlined = draw(3, 1), hidden = draw(3, 0);
    // Check fade on an isolated glyph: neighboring expanded outlines can overlap.
    const auto single = draw(3, 1, "0"), faded = draw(3, .5f, "0");
    int fillPixels = 0, borderPixels = 0;
    for (size_t i = 0; i < plain.size(); i += 4) {
        if (plain[i] > 128) ++fillPixels;
        if (outlined[i + 3] > plain[i + 3] + 32) ++borderPixels;
        assert(plain[i] == outlined[i]); // Outline must not move or shrink the fill.
        assert(std::abs(int(faded[i + 3]) * 2 - int(single[i + 3])) <= 1);
        assert(hidden[i + 3] == 0);
    }
    assert(fillPixels > 500 && borderPixels > 500);
    msdftext::Shutdown();
    assert(msdftext::Initialize(device.Get(), Format, font));
    assert(draw(3, 1) == outlined);
    msdftext::Shutdown();
    if (argc > 1) {
        std::ofstream out(argv[1], std::ios::binary);
        out << "P6\n" << W << ' ' << H << "\n255\n";
        for (size_t i = 0; i < outlined.size(); i += 4) for (int c = 0; c < 3; ++c)
            out.put(static_cast<char>((std::min)(255, int(outlined[i + c]) + (255 - outlined[i + 3]) / 2)));
    }
    if (info) for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
        SIZE_T size = 0; info->GetMessage(i, nullptr, &size);
        std::vector<unsigned char> data(size); auto* message = reinterpret_cast<D3D12_MESSAGE*>(data.data());
        Check(info->GetMessage(i, message, &size));
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) { std::cerr << message->pDescription << '\n'; return 1; }
    }
    CloseHandle(event);
    std::cout << "MSDF WARP OK: fill=" << fillPixels << ", outline=" << borderPixels << '\n';
}
