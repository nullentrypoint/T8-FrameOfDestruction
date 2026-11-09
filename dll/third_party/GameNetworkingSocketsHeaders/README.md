# Steam networking ABI declarations

Unmodified public headers from ValveSoftware/GameNetworkingSockets tag `v1.4.1`:
https://github.com/ValveSoftware/GameNetworkingSockets/tree/v1.4.1/include/steam

Only declarations/types are vendored. No standalone networking implementation
is built or shipped. The diagnostic probe obtains SteamNetworkingMessages002
and SteamNetworkingSockets012 through the game's already-initialized Steam API.
See LICENSE for redistribution terms; include it with binary test packages.
