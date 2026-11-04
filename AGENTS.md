# Repository notes

## Build
- Use Windows MSVC v143 and the Windows 10 SDK, in an x64 Visual Studio developer shell. The DLL's C++20/include settings are configured only for `Release|x64`; `Debug|x64` incorrectly declares an application. Use Release/x64 for verification.
- From the repository root, build either project independently:
  ```bat
  msbuild dll\dll.vcxproj /p:Configuration=Release /p:Platform=x64 /m
  msbuild injector\injector.vcxproj /p:Configuration=Release /p:Platform=x64 /m
  ```
- Direct DLL project builds produce `dll/x64/Release/dll.dll`. The project currently targets `.dll`, even if older `.asi` artifacts exist locally. Config deployment is manual; there is no post-build copy step.
- Sources are explicitly listed in `.vcxproj`; register new compilation units there. `dll/stdafx.h` links bundled MinHook/Vulkan libraries via pragmas.

## Runtime and code boundaries
- `injector/main.cpp` ignores CLI arguments: it waits for `Polaris-Win64-Shipping.exe` and loads `./dll.dll` relative to the **working directory**. Run elevated from the staged DLL directory.
- Deploy `config/memory_address.ini` to `<DLL directory>/T8FrameOfDestruction/config/memory_address.ini`; a plain `config/` beside the DLL is insufficient. Keep this file in sync with reader changes.
- `dll/dllmain.cpp` initializes hooks on a worker thread. `dll/globals.cpp` selects DX12 explicitly; the presence of other renderer hooks does not mean automatic fallback is enabled.
- Frame-data flow: `dll/d3d12hook.cpp` owns the reader/log, `dll/TekkenBot/GameReader.h` reads configured memory fields, `GameSnapshot.h` models them, and `GameLog.h` computes frame advantage and updates `dll/overlay.cpp` labels. Most game logic is header-defined.
- Learned action frames persist as `actionable_frames.json` beside the runtime memory INI; practice resets retain this learning.
- The compiled ImGui backends are `dll/imgui/imgui_impl_*.cpp`, **not** the duplicate files under `dll/imgui/backends/`. ImGui's examples, workflows, and formatter config belong to the vendored library.

## Focused verification
- The headless replay harness is outside the Visual Studio projects. From `dll/`, in an x64 developer **cmd** prompt:
  ```bat
  cl /nologo /std:c++20 /EHsc /MD /I. tests\replay.cpp TekkenBot\MoveInfoEnums.cpp /Fo%TEMP%\ /Fe%TEMP%\t8_replay.exe /link /OPT:REF
  python tests/compare_replay.py --exe "%TEMP%\t8_replay.exe" --python-root "PATH\TekkenBot420" --out "%TEMP%\cpp_replay"
  ```
- Requires a separate Python checkout with `tools/replay_fields.py` and six captures listed in `dll/tests/README.md`; they are not bundled here. The comparison checks emission/update frames and values with both player orientations, without opening the game.
- After conversion, replay one capture with `%TEMP%\t8_replay.exe %TEMP%\cpp_replay\jin_defs1.txt` (append `swap` to exchange players). This prints events; Python comparison is what checks expected results. Keep assertions enabled: the harness also checks signed decoding and persistence.
- See `dll/tests/README.md` for validated move cases and capture prerequisites. Rendering/hook changes still need in-game verification (`F1` menu, `F11` unload).
