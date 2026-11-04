#pragma once

#include <cmath>
#include <optional>

struct WorldPoint {
    float x = 0, y = 0, z = 0;

    bool valid() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
            std::abs(x) < 1e8f && std::abs(y) < 1e8f && std::abs(z) < 1e8f &&
            (x != 0 || y != 0 || z != 0);
    }
    bool operator==(const WorldPoint&) const = default;
};

// Normalized device coordinates, independent of the swap-chain resolution.
struct ScreenAnchor {
    float horizontal = 0, vertical = 0;
};

struct CameraSnapshot {
    double view[16]{};
    double projection[16]{};

    std::optional<ScreenAnchor> project(const WorldPoint& point) const {
        if (!point.valid()) return std::nullopt;
        for (int i = 0; i < 16; ++i) {
            if (!std::isfinite(view[i]) || !std::isfinite(projection[i])) return std::nullopt;
        }
        // Reject missing/invalid perspective matrices during loading.
        if (projection[0] <= 0 || projection[5] <= 0 ||
            std::abs(projection[11] - 1) > 1e-6 || std::abs(view[15] - 1) > 1e-6) return std::nullopt;
        // Tekken simulation (Y up, millimetres) -> Unreal (Z up, centimetres).
        const double world[4]{point.z * 0.1, -point.x * 0.1, point.y * 0.1, 1};
        double camera[4]{}, clip[4]{};
        // Unreal FMatrix uses row vectors. The projection without TAA jitter
        // matches the rendered FOV (PlayerCameraManager's cached FOV does not).
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) camera[column] += world[row] * view[row * 4 + column];
        }
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) clip[column] += camera[row] * projection[row * 4 + column];
        }
        if (!std::isfinite(clip[3]) || clip[3] <= 0.01) return std::nullopt;
        const double h = clip[0] / clip[3], v = clip[1] / clip[3];
        if (!std::isfinite(h) || !std::isfinite(v) || std::abs(h) > 1e4 || std::abs(v) > 1e4) {
            return std::nullopt;
        }
        return ScreenAnchor{ static_cast<float>(h), static_cast<float>(v) };
    }
};
