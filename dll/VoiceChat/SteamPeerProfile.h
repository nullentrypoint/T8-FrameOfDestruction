#pragma once
#include "VoicePresentation.h"
#include <Windows.h>
#include <cstring>

namespace VoiceChat {
// Flat Steam API entry points avoid depending on the large SteamFriends vtable.
// Interfaces and exports must come from the same already initialized SDK.
class SteamPeerProfile {
    void* friends = nullptr;
    void* utils = nullptr;
    using Name = const char*(__cdecl*)(void*, uint64_t);
    using AvatarHandle = int(__cdecl*)(void*, uint64_t);
    using Request = bool(__cdecl*)(void*, uint64_t, bool);
    using Size = bool(__cdecl*)(void*, int, uint32_t*, uint32_t*);
    using Pixels = bool(__cdecl*)(void*, int, uint8_t*, int);
    Name name = nullptr;
    AvatarHandle avatar = nullptr;
    Request request = nullptr;
    Size size = nullptr;
    Pixels pixels = nullptr;
    using Overlay = void(__cdecl*)(void*, const char*, uint64_t);
    Overlay overlay = nullptr;
public:
    void Bind(HMODULE module, int32_t user) {
        using Factory = void*(__cdecl*)(int32_t, const char*);
        using Utils = void*(__cdecl*)();
        auto factory = reinterpret_cast<Factory>(GetProcAddress(module, "SteamInternal_FindOrCreateUserInterface"));
        auto getUtils = reinterpret_cast<Utils>(GetProcAddress(module, "SteamAPI_SteamUtils_v010"));
        name = reinterpret_cast<Name>(GetProcAddress(module, "SteamAPI_ISteamFriends_GetFriendPersonaName"));
        avatar = reinterpret_cast<AvatarHandle>(GetProcAddress(module, "SteamAPI_ISteamFriends_GetLargeFriendAvatar"));
        overlay = reinterpret_cast<Overlay>(GetProcAddress(module, "SteamAPI_ISteamFriends_ActivateGameOverlayToUser"));
        request = reinterpret_cast<Request>(GetProcAddress(module, "SteamAPI_ISteamFriends_RequestUserInformation"));
        size = reinterpret_cast<Size>(GetProcAddress(module, "SteamAPI_ISteamUtils_GetImageSize"));
        pixels = reinterpret_cast<Pixels>(GetProcAddress(module, "SteamAPI_ISteamUtils_GetImageRGBA"));
        if (!factory || !getUtils || !name || !avatar || !request || !size || !pixels) return;
        friends = factory(user, "SteamFriends017");
        utils = getUtils();
    }
    bool Available() const { return friends && utils; }
    bool OpenProfile(uint64_t id) {
        if (!friends || !overlay) return false;
        overlay(friends, "steamid", id);
        return true;
    }
    void Refresh(PeerView& peer) {
        if (!Available() || !peer.steamId) return;
        // Existing game callback pump fills Steam's persona/avatar cache.
        request(friends, peer.steamId, false);
        if (const char* value = name(friends, peer.steamId); value && *value && strcmp(value, "[unknown]"))
            peer.name.assign(value, strnlen(value, 256));
        if (peer.avatar) return;
        const int image = avatar(friends, peer.steamId);
        uint32_t width = 0, height = 0;
        if (image <= 0 || !size(utils, image, &width, &height) || !width || !height || width > 256 || height > 256) return;
        std::vector<uint8_t> rgba(width * height * 4);
        if (!pixels(utils, image, rgba.data(), static_cast<int>(rgba.size()))) return;
        auto result = std::make_shared<Avatar>();
        result->resolution = static_cast<int>(width);
        result->rgba.resize(width * width * 4);
        for (uint32_t y = 0; y < width; ++y)
            memcpy(result->rgba.data() + y * width * 4, rgba.data() + (y * height / width) * width * 4, width * 4);
        peer.avatar = std::move(result);
    }
};
}
