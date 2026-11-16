#pragma once
#include "LoungeReader.h"
#include <array>
#include <cmath>
#include <optional>
#include <vector>
#include <atomic>
#include <charconv>
#include <string_view>

namespace VoiceChat {
struct LoungeCameraLayout {
    std::vector<uintptr_t> chain;
    uintptr_t viewOffset = 0, projectionOffset = 0;
};
inline constexpr double LoungeAvatarMaxDistance = 2000.0; // Unreal world units, measured from the camera.
inline std::atomic<int> loungeAvatarDistance{static_cast<int>(LoungeAvatarMaxDistance)};
inline bool ParseLoungeAvatarDistance(std::string_view text, int& value) {
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed < 0 || parsed > 100000) return false;
    value = parsed;
    return true;
}
struct LoungeScreenPoint { float x, y; double cameraDistance; }; // NDC, no fight-coordinate conversion.
inline bool LoungeMarkerInRange(const LoungeScreenPoint& point) {
    const int distance = loungeAvatarDistance.load();
    return distance > 0 && std::isfinite(point.cameraDistance) && point.cameraDistance >= 0 && point.cameraDistance <= distance;
}
struct LoungeCamera {
    std::array<double, 16> view{}, projection{};
    std::optional<LoungeScreenPoint> Project(LoungePosition p) const {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return {};
        for (size_t i = 0; i < 16; ++i)
            if (!std::isfinite(view[i]) || !std::isfinite(projection[i])) return {};
        if (projection[0] <= 0 || projection[5] <= 0 ||
            std::abs(projection[11] - 1) > 1e-6 || std::abs(view[15] - 1) > 1e-6) return {};
        const double world[]{p.x, p.y, p.z, 1};
        double camera[4]{}, clip[4]{};
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) camera[c] += world[r] * view[r * 4 + c];
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) clip[c] += camera[r] * projection[r * 4 + c];
        if (!std::isfinite(clip[3]) || clip[3] <= .01) return {};
        const auto x = clip[0] / clip[3], y = clip[1] / clip[3];
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1 || std::abs(y) > 1) return {};
        return LoungeScreenPoint{static_cast<float>(x), static_cast<float>(y), std::hypot(camera[0], camera[1], camera[2])};
    }
};
inline std::optional<LoungeCamera> ReadLoungeCamera(uintptr_t base, uintptr_t world,
    const LoungeCameraLayout& layout, LoungeMemoryRead read, void* context) {
    if (!read || !world || layout.chain.size() < 2 || layout.chain.size() > 10 ||
        !layout.viewOffset || !layout.projectionOffset) return {};
    uintptr_t p = base;
    for (size_t i = 0; i < layout.chain.size(); ++i) {
        if (p < 0x10000 || p > 0x00007FFFFFFEFFFFULL || layout.chain[i] > 0x10000000 ||
            !read(context, p + layout.chain[i], &p, sizeof(p)) || !p) return {};
        if (i == 0 && p != world) return {};
    }
    LoungeCamera camera;
    if (p > 0x00007FFFFFFEFFFFULL || layout.viewOffset > 0x10000 || layout.projectionOffset > 0x10000 ||
        !read(context, p + layout.viewOffset, camera.view.data(), sizeof(camera.view)) ||
        !read(context, p + layout.projectionOffset, camera.projection.data(), sizeof(camera.projection))) return {};
    uintptr_t again = 0;
    if (!read(context, base + layout.chain.front(), &again, sizeof(again)) || again != world) return {};
    return camera;
}
}
