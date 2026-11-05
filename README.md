# T8-FrameOfDestruction

GUI to study frame data while in game.

![example](img/example.gif?raw=true)

This software is intended for educational purposes only.

## Features

- Real-time frame advantage on hit and block, shown as color-coded floating text.
- Hit-type rings and optional world-locked indicators that follow the camera.
- Throw-break timelines showing break windows, input timing and results, including tackles and ground throws. Break buttons are revealed after the result.
- Automatic learning and persistence of actionable frames between sessions.
- In-game customization with `F1`: colors, font size, animation, indicator placement and throw-card style.

## Getting Started

1. Download and install [Visual C++ v14 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe)
2. Download latest [release](https://github.com/nullentrypoint/T8-FrameOfDestruction/releases)
3. Extract the archive into `TEKKEN 8\Polaris\Binaries\Win64\`, next to `Polaris-Win64-Shipping.exe`.
4. Run the game normally; the ASI plugin loads automatically. See the included `readme.txt` for Russian and English instructions.

Press ```F1``` in game to open menu. 

## Build release ZIP

Requires Windows PowerShell 5.1+, Visual Studio / Build Tools with MSVC v143 and the Windows 10 SDK.
Run from the repository root (a developer shell is not required):

```powershell
powershell -ExecutionPolicy Bypass -File .\build-release.ps1
```

Builds Release/x64 and creates `artifacts/T8-FrameOfDestruction-yyyy-MM-dd-HHmmss.zip`.
Downloads the pinned x64 Ultimate ASI Loader automatically. To use a local loader or learned frame data:

```powershell
.\build-release.ps1 -Dinput8Path C:\path\dinput8.dll -ActionableFramesPath C:\path\actionable_frames.json
```

The default frame-data file is `config/actionable_frames.json` (empty). Use `-OutputDirectory` to change the output folder.

Before the first build, create your local configuration (ignored by Git):

```powershell
Copy-Item .\build-release.example.psd1 .\build-release.psd1
```

Build settings are stored in `build-release.psd1`: output directory, input INI/JSON/readme paths,
local loader path (empty means download), loader URL and SHA256. Relative paths in this file
are resolved from its directory. Command-line parameters override the corresponding settings;
relative command-line paths are resolved from the current working directory.
Release/x64 and the archive layout remain fixed.

Set `GameDirectory` in the configuration to your TEKKEN 8 installation root (for example,
`D:\SteamLibrary\steamapps\common\TEKKEN 8`) or directly to `Polaris\Binaries\Win64`.
After creating the ZIP, the script installs its contents there, overwriting matching files,
including `actionable_frames.json`. Close the game before building with installation enabled.
An empty `GameDirectory` disables installation. You can also override it with `-GameDirectory`.

To use a separate configuration:

```powershell
.\build-release.ps1 -ConfigPath C:\path\build-release.psd1
```

## Built With

* [Universal-Dear-ImGui-Hook](https://github.com/Sh0ckFR/Universal-Dear-ImGui-Hook) - An universal Dear ImGui Hook for Directx12, 11, 10, 9 and Vulkan.
* [TekkenBot](https://github.com/dcep93/TekkenBot) - GUI to study frame data while in game.
