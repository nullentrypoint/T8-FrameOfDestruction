#include "stdafx.h"
#include "msdf_text.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imgui/imstb_truetype.h"
#define MSDFGEN_PUBLIC
#include "third_party/msdfgen/msdfgen.h"

namespace msdftext {
namespace {
    using Microsoft::WRL::ComPtr;
    constexpr int Cell = 96, Columns = 16, Width = Cell * Columns, Height = Cell * 6;
    constexpr float EmSize = 64, DistanceRange = 24, Padding = 14;
    struct Glyph { float advance = 0, left = 0, top = 0; int width = 0, height = 0, x = 0, y = 0; };
    struct Vertex { float x, y, u, v; ImVec4 color, outline; float range, thickness; };
    std::array<Glyph, 95> glyphs;
    std::vector<unsigned char> pixels;
    std::string cachedFont;
    float ascent = 0;
    std::vector<Vertex> vertices;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12Resource> atlas, upload, vertexBuffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    size_t vertexCapacity = 0;
    bool uploaded = false;

    bool Check(HRESULT hr, const char* operation) {
        if (SUCCEEDED(hr)) return true;
        console::AddLogError(std::string("MSDF: ") + operation);
        DebugLog("[MSDF] %s: 0x%08X\n", operation, hr);
        return false;
    }

    bool BuildAtlas(const std::string& path) {
        if (cachedFont == path && !pixels.empty()) return true;
        std::ifstream file(path, std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), {});
        stbtt_fontinfo font{};
        if (data.empty() || !stbtt_InitFont(&font, data.data(), 0)) return false;
        const float scale = stbtt_ScaleForPixelHeight(&font, EmSize);
        int a, d, gap;
        stbtt_GetFontVMetrics(&font, &a, &d, &gap);
        ascent = a * scale;
        std::vector<unsigned char> result(Width * Height * 4, 0);
        for (int code = 32; code <= 126; ++code) {
            Glyph& g = glyphs[code - 32];
            g = {};
            int advance, bearing;
            stbtt_GetCodepointHMetrics(&font, code, &advance, &bearing);
            g.advance = advance * scale;
            stbtt_vertex* points = nullptr;
            int count = stbtt_GetCodepointShape(&font, code, &points);
            msdfgen::Shape shape;
            msdfgen::Contour* contour = nullptr;
            msdfgen::Point2 previous;
            for (int i = 0; i < count; ++i) {
                const auto& p = points[i];
                msdfgen::Point2 end(p.x * scale, p.y * scale);
                if (p.type == STBTT_vmove) contour = &shape.addContour();
                else if (contour) {
                    if (p.type == STBTT_vline)
                        contour->addEdge(msdfgen::EdgeHolder(previous, end));
                    else if (p.type == STBTT_vcurve)
                        contour->addEdge(msdfgen::EdgeHolder(previous, msdfgen::Point2(p.cx * scale, p.cy * scale), end));
                    else if (p.type == STBTT_vcubic)
                        contour->addEdge(msdfgen::EdgeHolder(previous, msdfgen::Point2(p.cx * scale, p.cy * scale),
                            msdfgen::Point2(p.cx1 * scale, p.cy1 * scale), end));
                }
                previous = end;
            }
            stbtt_FreeShape(&font, points);
            if (!shape.edgeCount()) continue;
            shape.normalize();
            shape.orientContours();
            if (!shape.validate()) return false;
            const auto bounds = shape.getBounds();
            const double left = std::floor(bounds.l) - Padding, bottom = std::floor(bounds.b) - Padding;
            const double top = std::ceil(bounds.t) + Padding;
            g.width = static_cast<int>(std::ceil(bounds.r) + Padding - left);
            g.height = static_cast<int>(top - bottom);
            if (g.width > Cell || g.height > Cell) return false;
            g.left = static_cast<float>(left);
            g.top = ascent - static_cast<float>(top);
            g.x = ((code - 32) % Columns) * Cell;
            g.y = ((code - 32) / Columns) * Cell;
            msdfgen::edgeColoringSimple(shape, 3.0);
            msdfgen::Bitmap<float, 3> bitmap(g.width, g.height);
            msdfgen::generateMSDF(bitmap, shape, msdfgen::Projection(msdfgen::Vector2(1), msdfgen::Vector2(-left, -bottom)), msdfgen::Range(DistanceRange));
            for (int y = 0; y < g.height; ++y) for (int x = 0; x < g.width; ++x) {
                auto* dest = &result[((g.y + y) * Width + g.x + x) * 4];
                const float* src = bitmap(x, g.height - 1 - y);
                for (int c = 0; c < 3; ++c) dest[c] = static_cast<unsigned char>(std::clamp(src[c] * 255.f + .5f, 0.f, 255.f));
                dest[3] = 255;
            }
        }
        pixels = std::move(result);
        cachedFont = path;
        return true;
    }

    bool CreateBuffer(UINT64 bytes, ComPtr<ID3D12Resource>& resource) {
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return Check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)), "create upload buffer");
    }

    const char* Shader = R"(
Texture2D atlas : register(t0);
SamplerState linearSampler : register(s0);
struct V { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0;
    float4 outline : COLOR1; float2 params : TEXCOORD1; };
struct P { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0;
    float4 outline : COLOR1; float2 params : TEXCOORD1; };
P VS(V v) { P p; p.pos=float4(v.pos,0,1); p.uv=v.uv; p.color=v.color;
    p.outline=v.outline; p.params=v.params; return p; }
float4 PS(P p) : SV_TARGET {
    float3 s = atlas.Sample(linearSampler, p.uv).rgb;
    float median = max(min(s.r,s.g), min(max(s.r,s.g),s.b));
    float distance = (median-.5)*p.params.x;
    float fill = saturate(distance+.5);
    float outer = saturate(distance+p.params.y+.5);
    float fa = fill*p.color.a;
    float oa = max(outer-fill,0)*p.outline.a;
    return float4(p.color.rgb*fa+p.outline.rgb*oa, fa+oa);
})";
}

void Shutdown() {
    vertices.clear();
    vertexBuffer.Reset(); upload.Reset(); atlas.Reset(); heap.Reset();
    pipeline.Reset(); root.Reset(); device.Reset();
    vertexCapacity = 0; uploaded = false;
}

bool Initialize(ID3D12Device* newDevice, DXGI_FORMAT format, const std::string& fontPath) {
    Shutdown();
    if (!newDevice) {
        console::AddLogError("MSDF: missing DX12 device");
        return false;
    }
    device = newDevice;
    // Build into local/cache CPU memory; GPU state is committed only on success.
    auto initialize = [&]() -> bool {
        if (!BuildAtlas(fontPath)) {
            console::AddLogError("MSDF: unable to generate font atlas");
            return false;
        }
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable = {1, &range}; parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{1, &parameter, 1, &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ComPtr<ID3DBlob> blob, errors, vs, ps;
        if (!Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "serialize root signature") ||
            !Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "create root signature")) return false;
        auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& output) {
            HRESULT hr = D3DCompile(Shader, strlen(Shader), "msdf_text", nullptr, nullptr, entry, target,
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &output, &errors);
            if (FAILED(hr) && errors) DebugLog("[MSDF] %s\n", static_cast<char*>(errors->GetBufferPointer()));
            return Check(hr, "compile shader");
        };
        if (!compile("VS", "vs_5_0", vs) || !compile("PS", "ps_5_0", ps)) return false;
        D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
        };
        static_assert(sizeof(Vertex) == 56);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = root.Get(); pso.InputLayout = {layout, _countof(layout)};
        pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        auto& blend = pso.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1; pso.RTVFormats[0] = format; pso.SampleDesc.Count = 1;
        if (!Check(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline)), "create pipeline")) return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 1;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "create descriptor heap")) return false;
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = Width; td.Height = Height;
        td.DepthOrArraySize = td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (!Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&atlas)), "create atlas")) return false;
        UINT64 bytes;
        device->GetCopyableFootprints(&td, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        if (!CreateBuffer(bytes, upload)) return false;
        unsigned char* mapped;
        D3D12_RANGE noRead{0, 0};
        if (!Check(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "map atlas upload")) return false;
        for (int y = 0; y < Height; ++y)
            memcpy(mapped + footprint.Offset + y * footprint.Footprint.RowPitch, pixels.data() + y * Width * 4, Width * 4);
        upload->Unmap(0, nullptr);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = td.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(atlas.Get(), &srv, heap->GetCPUDescriptorHandleForHeapStart());
        return true;
    };
    try { if (initialize()) return true; }
    catch (const std::exception& e) { console::AddLogError(std::string("MSDF: ") + e.what()); }
    Shutdown();
    return false;
}

void BeginFrame() { vertices.clear(); }

bool AddText(const std::string& text, ImVec2 position, float size, bool centered,
    ImVec4 color, ImVec4 outlineColor, float outlinePixels) {
    if (!pipeline) return false;
    if (size <= 0 || color.w <= 0) return true;
    const float scale = size / EmSize;
    // One replacement glyph per unsupported UTF-8 code point, rather than per byte.
    std::vector<unsigned char> codes;
    for (unsigned char c : text) {
        if ((c & 0xc0) == 0x80) continue;
        codes.push_back(c >= 32 && c <= 126 ? c : '?');
    }
    float advance = 0;
    for (auto c : codes) advance += glyphs[c - 32].advance * scale;
    if (centered) { position.x -= advance * .5f; position.y -= size * .5f; }
    const float range = DistanceRange * scale;
    const float thickness = std::clamp(outlinePixels, 0.f, (std::max)(0.f, range * .5f - 1.f));
    for (auto c : codes) {
        const auto& g = glyphs[c - 32];
        if (g.width) {
            const float l = position.x + g.left * scale, t = position.y + g.top * scale;
            const float r = l + g.width * scale, b = t + g.height * scale;
            const float u0 = float(g.x) / Width, v0 = float(g.y) / Height;
            const float u1 = float(g.x + g.width) / Width, v1 = float(g.y + g.height) / Height;
            Vertex quad[] = {
                {l,t,u0,v0,color,outlineColor,range,thickness}, {r,t,u1,v0,color,outlineColor,range,thickness},
                {r,b,u1,v1,color,outlineColor,range,thickness}, {l,t,u0,v0,color,outlineColor,range,thickness},
                {r,b,u1,v1,color,outlineColor,range,thickness}, {l,b,u0,v1,color,outlineColor,range,thickness}
            };
            vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
        }
        position.x += g.advance * scale;
    }
    return true;
}

void Render(ID3D12GraphicsCommandList* commands, ImVec2 displaySize) {
    if (!pipeline || vertices.empty() || displaySize.x <= 0 || displaySize.y <= 0) return;
    if (vertices.size() > vertexCapacity) {
        vertexBuffer.Reset(); vertexCapacity = 0;
        const size_t capacity = vertices.size() + 1024;
        if (!CreateBuffer(capacity * sizeof(Vertex), vertexBuffer)) return;
        vertexCapacity = capacity;
    }
    Vertex* mapped;
    D3D12_RANGE noRead{0, 0};
    if (!Check(vertexBuffer->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "map vertices")) return;
    for (size_t i = 0; i < vertices.size(); ++i) {
        mapped[i] = vertices[i];
        mapped[i].x = vertices[i].x * 2 / displaySize.x - 1;
        mapped[i].y = 1 - vertices[i].y * 2 / displaySize.y;
    }
    vertexBuffer->Unmap(0, nullptr);
    if (!uploaded) {
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = atlas.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;
        commands->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {atlas.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        commands->ResourceBarrier(1, &barrier);
        uploaded = true;
    }
    commands->SetPipelineState(pipeline.Get()); commands->SetGraphicsRootSignature(root.Get());
    ID3D12DescriptorHeap* heaps[] = {heap.Get()}; commands->SetDescriptorHeaps(1, heaps);
    commands->SetGraphicsRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
    D3D12_VIEWPORT viewport{0, 0, displaySize.x, displaySize.y, 0, 1};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(displaySize.x), static_cast<LONG>(displaySize.y)};
    commands->RSSetViewports(1, &viewport); commands->RSSetScissorRects(1, &scissor);
    D3D12_VERTEX_BUFFER_VIEW view{vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(vertices.size() * sizeof(Vertex)), sizeof(Vertex)};
    commands->IASetVertexBuffers(0, 1, &view); commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
}
}
